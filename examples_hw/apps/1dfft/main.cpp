#include <iostream>
#include <string>
#include <malloc.h>
#include <time.h> 
#include <sys/time.h>  
#include <chrono>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <iomanip>
#include <signal.h>
#ifdef EN_AVX
#include <x86intrin.h>
#endif
#include <boost/program_options.hpp>
#include <any>
#include <cmath>
#include <random>
#include <vector>
#include "cBench.hpp"
#include "cThread.hpp"

using namespace std;
using namespace std::chrono;
using namespace fpga;

std::atomic<bool> stalled(false); 
void gotInt(int) {
    stalled.store(true);
}

constexpr auto const defDevice = 0;
constexpr auto const targetVfid = 0;  
constexpr auto const defReps = 1;
constexpr auto const defSize = 16384;
constexpr auto const defDW = 4;
constexpr auto const accumulateSize = 512;  
constexpr auto const outputSize = defSize / accumulateSize;

// Generate sine wave value
float generateSineValue(int index, int totalPoints) {
    const float amplitude = 1000.0f;  // Large amplitude for better visibility
    const float frequency = 256.0f;   // Using a power of 2 frequency
    const float phase = 0.0f;
    
    float t = (float)index / totalPoints * 2.0f * M_PI;
    return amplitude * sin(frequency * t + phase);
}

// Structure to hold complex numbers
struct Complex {
    float real;
    float imag;
    
    Complex() : real(0.0f), imag(0.0f) {}
    Complex(float r, float i) : real(r), imag(i) {}
    
    float magnitude() const {
        return std::sqrt(real * real + imag * imag);
    }
    
    Complex& operator+=(const Complex& other) {
        real += other.real;
        imag += other.imag;
        return *this;
    }
    
    Complex& operator/=(float div) {
        real /= div;
        imag /= div;
        return *this;
    }
};

void accumulateFFTOutput(float* input, float* output, int size, int accumulateSize) {
    std::vector<float> maxMagnitudes(size / accumulateSize, 0.0f);
    
    // Zero the output array first
    memset(output, 0, (size/accumulateSize) * sizeof(float));
    
    // For each input point
    for(int i = 0; i < size; i++) {
        int accIndex = i / accumulateSize;
        float real = input[2*i];
        float imag = input[2*i+1];
        float mag = std::sqrt(real * real + imag * imag);
        
        // If this magnitude is larger than what we've seen for this bin
        if (mag > maxMagnitudes[accIndex]) {
            maxMagnitudes[accIndex] = mag;
            output[accIndex] = std::abs(real) * 1000000.0f;  // Scale up to match speech data range
        }
    }
}

// Function to calculate frequency for a bin
float getFrequencyForBin(int bin, int fftSize, float sampleRate) {
    return (float)bin * sampleRate / fftSize;
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = gotInt;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    boost::program_options::options_description programDescription("Options:");
    programDescription.add_options()
        ("reps,r", boost::program_options::value<uint32_t>(), "Number of reps");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    uint32_t size = defSize;
    uint32_t n_reps = defReps;
    
    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();

    uint32_t buffer_size = 2 * size * sizeof(float);
    uint32_t n_pages = (buffer_size + hugePageSize - 1) / hugePageSize;

    PR_HEADER("PARAMS");
    std::cout << "vFPGA ID: " << targetVfid << std::endl;
    std::cout << "Number of allocated pages per run: " << n_pages << std::endl;
    std::cout << "FFT size: " << size << std::endl;
    std::cout << "Number of reps: " << n_reps << std::endl;

    try {
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
        cthread->start();
        
        std::vector<float*> input_buffers(n_reps, nullptr);
        std::vector<float*> output_buffers(n_reps, nullptr);

        std::vector<float> test_data(size);

        // Generate sine wave test data
        std::cout << "\nFirst 32 input values:\n";
        for (int i = 0; i < size; ++i) {
            test_data[i] = generateSineValue(i, size);
            if (i < 32) {
                std::cout << std::fixed << std::setprecision(6) << test_data[i] << " ";
                if ((i + 1) % 8 == 0) std::cout << "\n";
            }
        }
        std::cout << "\n";

        // Allocate and initialize memory
        for(int i = 0; i < n_reps; i++) {
            input_buffers[i] = (float*) cthread->getMem({CoyoteAlloc::HPF, n_pages});
            output_buffers[i] = (float*) cthread->getMem({CoyoteAlloc::HPF, n_pages});
            
            if (!input_buffers[i] || !output_buffers[i]) {
                throw std::runtime_error("Memory allocation failed");
            }

            memcpy(input_buffers[i], test_data.data(), size * sizeof(float));
            memset(output_buffers[i], 0, buffer_size);
        }

        sgEntry sg;
        sgFlags sg_flags = { true, true, false };

        cBench bench(n_reps);
        PR_HEADER("FFT PROCESSING");

        cthread->clearCompleted();

        auto benchmark_thr = [&]() {
            for(int i = 0; i < n_reps; i++) {
                memset(&sg, 0, sizeof(localSg));
                
                sg.local.src_addr = input_buffers[i];
                sg.local.src_len = buffer_size;
                sg.local.src_stream = strmHost;
                sg.local.src_dest = targetVfid;

                sg.local.dst_addr = output_buffers[i];
                sg.local.dst_len = buffer_size;
                sg.local.dst_stream = strmHost;
                sg.local.dst_dest = targetVfid;

                sg_flags.last = (i == n_reps-1);
                
                cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, sg_flags);
            }

            while(cthread->checkCompleted(CoyoteOper::LOCAL_WRITE) != 1) {
                if(stalled.load()) throw std::runtime_error("Stalled");
            }
        };

        bench.runtime(benchmark_thr);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Size: " << std::setw(8) << size << ", throughput: " 
                  << std::setw(8) << (1000 * buffer_size) / (bench.getAvg() / n_reps) 
                  << " MB/s" << std::endl;

        // Process and print FFT results
        for(int i = 0; i < n_reps; i++) {
            float* output_ptr = static_cast<float*>(output_buffers[i]);
            
            // Print raw FFT around interesting region
            std::cout << "\nRaw FFT values around peak region (indices 15-25):\n";
            for(int j = 15; j < 25; j++) {
                std::cout << "Bin " << std::setw(2) << j << ": (" 
                         << std::setw(10) << std::fixed << std::setprecision(6) << output_ptr[2*j]
                         << ", " << std::setw(10) << output_ptr[2*j+1] << "i) "
                         << "mag: " << std::sqrt(output_ptr[2*j]*output_ptr[2*j] + 
                                               output_ptr[2*j+1]*output_ptr[2*j+1]) << "\n";
            }
            
            // Create temporary buffer for 32 real values
            float accumulated[32] = {0};  // Will hold the 32 real values
            
            // Accumulate and store only real values
            accumulateFFTOutput(output_ptr, accumulated, size, accumulateSize);
            
            // Copy the 32 real values back to the start of the output buffer
            memcpy(output_ptr, accumulated, 32 * sizeof(float));
            
            // Print the accumulated real values
            std::cout << "\n32 Real Values (from output buffer):\n";
            for(int j = 0; j < 32; j++) {
                std::cout << std::setw(12) << std::fixed << std::setprecision(6) 
                         << output_ptr[j] << " ";
                if((j + 1) % 8 == 0) std::cout << "\n";
            }
            std::cout << std::endl;
        }

        cthread->printDebug();

        // Cleanup
        for(int i = 0; i < n_reps; i++) {
            if(input_buffers[i]) {
                cthread->freeMem(input_buffers[i]);
                input_buffers[i] = nullptr;
            }
            if(output_buffers[i]) {
                cthread->freeMem(output_buffers[i]);
                output_buffers[i] = nullptr;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
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
constexpr auto const defSize = 32;       
constexpr auto const defDW = 4;
constexpr float const sampleRate = 44100.0f;

// Generate sine wave value
float generateSineValue(int index) {
    const float amplitude = 1000.0f;
    const float frequency = 1378.125f;
    const float phase = 0.0f;
    
    float t = (float)index / sampleRate;
    return amplitude * sin(2.0f * M_PI * frequency * t + phase);
}

// Function to print raw FFT results - adjusted for 32-point
void printRawFFT(float* output_ptr, int size) {
    const int nyquistBins = size/2;  // For 32-point: 16 bins
    
    std::cout << "\nRaw FFT Output (32-point FFT):\n";
    std::cout << "Bin\tFrequency (Hz)\tReal\t\tImaginary\tMagnitude\n";
    std::cout << "----------------------------------------------------------------\n";
    
    float freqResolution = sampleRate / 32;  // Always 32-point FFT: 44100/32 = 1378.125 Hz/bin
    
    for(int i = 0; i < 16; i++) {  // Always show 16 bins for 32-point FFT
        float frequency = i * freqResolution;
        float real = output_ptr[2*i];
        float imag = output_ptr[2*i+1];
        float magnitude = std::sqrt(real*real + imag*imag);
        
        std::cout << i << "\t" 
                  << std::fixed << std::setprecision(2) << frequency << "\t\t"
                  << std::setprecision(6) << real << "\t" 
                  << imag << "\t"
                  << magnitude << "\n";
    }
}

// Helper function to print latency statistics
void printLatencyStats(double latency_ns) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Processing started at: 0 ns" << std::endl;
    std::cout << "Processing completed at: " << latency_ns << " ns" << std::endl;
    std::cout << "Total latency: " << latency_ns << " ns (" << (latency_ns / 1000) << " us)" << std::endl;
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = gotInt;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    boost::program_options::options_description programDescription("Options:");
    programDescription.add_options()
        ("size,s", boost::program_options::value<uint32_t>(), "FFT size (must be multiple of 32)")
        ("reps,r", boost::program_options::value<uint32_t>(), "Number of reps")
        ("freq,f", boost::program_options::value<float>(), "Input signal frequency (Hz)");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    uint32_t n_reps = defReps;
    uint32_t size = defSize; 
    
    if(commandLineArgs.count("size") > 0) size = commandLineArgs["size"].as<uint32_t>();
    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();

    // Ensure size is multiple of 32 (FFT hardware constraint)
    if(size % 32 != 0) {
        std::cout << "Warning: Size must be multiple of 32. Adjusting " << size 
                  << " to " << ((size + 31) / 32) * 32 << std::endl;
        size = ((size + 31) / 32) * 32;
    }

    uint32_t input_size = size;                    // Total complex samples
    uint32_t complex_size = 2 * input_size;       // Total floats (real + imag)
    uint32_t buffer_size = complex_size * sizeof(float);  // Total bytes
    uint32_t n_pages = (buffer_size + hugePageSize - 1) / hugePageSize;
    uint32_t num_ffts = size / 32;                 // Number of 32-point FFTs

    PR_HEADER("PARAMS");
    std::cout << "vFPGA ID: " << targetVfid << std::endl;
    std::cout << "Number of allocated pages per run: " << n_pages << std::endl;
    std::cout << "Total size: " << size << " points (" << num_ffts << " x 32-point FFTs)" << std::endl;
    std::cout << "Buffer size: " << buffer_size << " bytes" << std::endl;
    std::cout << "Number of reps: " << n_reps << std::endl;
    std::cout << "Sample rate: " << sampleRate << " Hz" << std::endl;
    std::cout << "Frequency resolution: " << (sampleRate/32) << " Hz/bin (per FFT)" << std::endl;

    try {
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
        cthread->start();
        
        std::vector<float*> input_buffers(n_reps, nullptr);
        std::vector<float*> output_buffers(n_reps, nullptr);

        std::vector<float> test_data(complex_size, 0.0f);

        std::cout << "\nFirst 32 input values:\n";
        for (uint32_t i = 0; i < min(32U, input_size); ++i) {
            test_data[2*i] = generateSineValue(i);      // Real part
            test_data[2*i + 1] = 0.0f;                  // Imaginary part                 
            std::cout << std::fixed << std::setprecision(6) 
                     << test_data[2*i] << " " << test_data[2*i + 1] << " ";
            if ((i + 1) % 4 == 0) std::cout << "\n";
        }
        
        // Generate remaining data for larger sizes
        for (uint32_t i = 32; i < input_size; ++i) {
            test_data[2*i] = generateSineValue(i);      // Real part
            test_data[2*i + 1] = 0.0f;                  // Imaginary part
        }
        std::cout << "\n";

        // Allocate and initialize memory
        for(int i = 0; i < n_reps; i++) {
            input_buffers[i] = (float*) cthread->getMem({CoyoteAlloc::HPF, n_pages});
            output_buffers[i] = (float*) cthread->getMem({CoyoteAlloc::HPF, n_pages});
            
            if (!input_buffers[i] || !output_buffers[i]) {
                throw std::runtime_error("Memory allocation failed");
            }

            memcpy(input_buffers[i], test_data.data(), buffer_size);
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

        // Print basic results
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Size: " << std::setw(8) << size << ", thr: " 
                  << std::setw(8) << (1000 * buffer_size) / (bench.getAvg() / n_reps) 
                  << " MB/s" << std::endl << std::endl;

        // Print latency statistics
        PR_HEADER("LATENCY MEASUREMENTS");
        printLatencyStats(bench.getAvg() / n_reps);

        // Print FFT results - show first few FFT outputs
        PR_HEADER("RESULTS");
        int max_ffts_to_show = min(3, (int)num_ffts);  // Show max 3 FFT results
        for(int i = 0; i < n_reps; i++) {
            for(int fft_idx = 0; fft_idx < max_ffts_to_show; fft_idx++) {
                std::cout << "\n--- FFT #" << (fft_idx + 1) << " of " << num_ffts << " ---";
                printRawFFT(output_buffers[i] + (fft_idx * 64), 32);  // Each FFT is 64 floats
            }
            if(num_ffts > max_ffts_to_show) {
                std::cout << "\n... (remaining " << (num_ffts - max_ffts_to_show) 
                         << " FFT outputs omitted for brevity)\n";
            }
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
#include <iostream>
#include <string>
#include <malloc.h>
#include <signal.h>
#include <boost/program_options.hpp>
#include <any>
#include <cmath>
#include <vector>
#include <iomanip>
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
constexpr auto const defSize = 128;  // Changed to match FFT size
constexpr auto const defDW = 4;

// Updated structures for floating point
struct InputPair {
    float a_real;  // Maps to [63:32] real part
    float a_imag;  // Maps to [31:0]  imaginary part
} __attribute__((aligned(8)));  // 8-byte alignment for float pair

struct Complex {
    float real;
    float imag;
};

void generateInput(InputPair* buffer, int size) {
    for(int i = 0; i < size; i++) {
        int row = i / size;
        int col = i % size;
        
        // Calculate distance from center (64,64) to determine frequency position
        int row_diff = (row >= 64) ? (row - 64) : (64 - row);
        int col_diff = (col >= 64) ? (col - 64) : (64 - col);
        int dist = row_diff * row_diff + col_diff * col_diff;

        if (dist <= 50) {
            // DC/low frequency test
            buffer[i].a_real = 1.0f;  // Will be multiplied by 0.5
            buffer[i].a_imag = 1.0f;
        } 
        else if (dist <= 2000) {
            // Mid frequency test
            buffer[i].a_real = 1.0f;  // Will be multiplied by 1.0
            buffer[i].a_imag = 1.0f;
        }
        else {
            // High frequency test
            buffer[i].a_real = 1.0f;  // Will be multiplied by 2.0
            buffer[i].a_imag = -1.0f;
        }
        
        // Add memory layout debug info
        std::cout << "Input pair " << i << " @ " << &buffer[i] << ":\n"
                 << "  Position [" << row << "," << col << "] dist=" << dist << "\n"
                 << "  a_real @ " << &buffer[i].a_real << ": " << buffer[i].a_real << "\n"
                 << "  a_imag @ " << &buffer[i].a_imag << ": " << buffer[i].a_imag << std::endl;
    }
}

void printBuffer(Complex* buffer, int size, const char* label) {
    std::cout << "\nFirst " << std::min(16, size) << " values of " << label << ":\n";
    for(int i = 0; i < std::min(16, size); i++) {
        int row = i / size;
        int col = i % size;
        std::cout << "[" << row << "," << col << "]: " << std::fixed << std::setprecision(3) 
                 << buffer[i].real << " + " << buffer[i].imag << "i" << std::endl;
    }
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

    // Calculate buffer sizes 
    uint32_t buffer_size = size * sizeof(InputPair);  // Size for one buffer (2 floats per input)
    uint32_t n_pages = (buffer_size + hugePageSize - 1) / hugePageSize;

    PR_HEADER("PARAMS");
    std::cout << "vFPGA ID: " << targetVfid << std::endl;
    std::cout << "Number of allocated pages per run: " << n_pages << std::endl;
    std::cout << "Complex multiply size: " << size << std::endl;
    std::cout << "Number of reps: " << n_reps << std::endl;
    std::cout << "Buffer size: " << buffer_size << " bytes" << std::endl;

    try {
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
        cthread->start();
        
        std::vector<InputPair*> input_buffers(n_reps, nullptr);
        std::vector<Complex*> output_buffers(n_reps, nullptr);

        // Allocate and initialize memory
        for(int i = 0; i < n_reps; i++) {
            input_buffers[i] = (InputPair*) cthread->getMem({CoyoteAlloc::HPF, n_pages});
            output_buffers[i] = (Complex*) cthread->getMem({CoyoteAlloc::HPF, n_pages});
            
            if (!input_buffers[i] || !output_buffers[i]) {
                throw std::runtime_error("Memory allocation failed");
            }

            // Initialize input data
            generateInput(input_buffers[i], size);
            memset(output_buffers[i], 0, buffer_size);
        }

        PR_HEADER("COMPLEX MULTIPLICATION");

        sgEntry sg;
        sgFlags sg_flags = { true, true, false };
        cBench bench(n_reps);
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

                if(i == n_reps-1) sg_flags.last = true;
                
                cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, sg_flags);
            }

            while(cthread->checkCompleted(CoyoteOper::LOCAL_WRITE) != 1) {
                if(stalled.load()) throw std::runtime_error("Stalled");
            }
        };

        bench.runtime(benchmark_thr);

        // Print results with frequency position information
        printBuffer(output_buffers[0], size, "Final Output Buffer");

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Size: " << std::setw(8) << size << ", throughput: " 
                  << std::setw(8) << (1000 * buffer_size * 2) / (bench.getAvg() / n_reps) 
                  << " MB/s" << std::endl;

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
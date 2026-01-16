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
#include "cBench.hpp"
#include "cThread.hpp"

using namespace std;
using namespace std::chrono;
using namespace fpga;

/* Signal handler */
std::atomic<bool> stalled(false); 
void gotInt(int) {
    stalled.store(true);
}

/* Def params */
constexpr auto const defDevice = 0;
constexpr auto const targetVfid = 0;  
constexpr auto const defReps = 1;
constexpr auto const defSize = 32;  // Default: single set of 32 floats

// Helper function to print latency statistics
void printLatencyStats(double latency_ns) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Processing started at: 0 ns" << std::endl;
    std::cout << "Processing completed at: " << latency_ns << " ns" << std::endl;
    std::cout << "Total latency: " << latency_ns << " ns (" << (latency_ns / 1000) << " us)" << std::endl;
}

int main(int argc, char *argv[])  
{
    // Signal handler setup
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = gotInt;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    // Read arguments
    boost::program_options::options_description programDescription("Options:");
    programDescription.add_options()
        ("size,s", boost::program_options::value<uint32_t>(), "Total number of floats (must be multiple of 32)")
        ("reps,r", boost::program_options::value<uint32_t>(), "Number of reps");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), 
                                commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    uint32_t size = defSize;
    uint32_t n_reps = defReps;

    if(commandLineArgs.count("size") > 0) size = commandLineArgs["size"].as<uint32_t>();
    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();

    // Ensure size is multiple of 32 (SVM requirement)
    if(size % 32 != 0) {
        std::cout << "Warning: Size must be multiple of 32. Adjusting " << size 
                  << " to " << ((size + 31) / 32) * 32 << std::endl;
        size = ((size + 31) / 32) * 32;
    }

    uint32_t num_classifications = size / 32;  // Number of expected classification results
    uint32_t buffer_size = size * sizeof(float);
    uint32_t output_buffer_size = num_classifications * sizeof(float);  // One float per classification
    uint32_t n_pages_input = (buffer_size + hugePageSize - 1) / hugePageSize;
    uint32_t n_pages_output = (output_buffer_size + hugePageSize - 1) / hugePageSize;

    // Test data - known good SVM input that should produce a classification
    float test_data[32] = {
        36257662.0f, 70308074.0f, 162763557.0f, 109956489.0f, 86125933.0f,
        35535698.0f, 5473712.0f, 2191429.0f, 1655529.0f, 1427210.0f,
        1819290.0f, 2594503.0f, 2649694.0f, 849597.0f, 765175.0f,
        890647.0f, 1405332.0f, 1132804.0f, 5776375.0f, 18372118.0f,
        6724989.0f, 7313762.0f, 1469345.0f, 1744257.0f, 1631652.0f,
        823229.0f, 1018674.0f, 639273.0f, 601445.0f, 630083.0f,
        678854.0f, 636897.0f
    };

    PR_HEADER("PARAMS");
    std::cout << "vFPGA ID: " << targetVfid << std::endl;
    std::cout << "Input pages per run: " << n_pages_input << std::endl;
    std::cout << "Output pages per run: " << n_pages_output << std::endl;
    std::cout << "Total input size: " << size << " floats (" << num_classifications << " sets of 32)" << std::endl;
    std::cout << "Expected classifications: " << num_classifications << std::endl;
    std::cout << "Number of reps: " << n_reps << std::endl;

    try {
        // Initialize thread
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
        cthread->start();

        // Memory allocation
        std::vector<float*> input_data(n_reps, nullptr);
        std::vector<float*> output_data(n_reps, nullptr);

        // Allocate and fill memory
        for(int i = 0; i < n_reps; i++) {
            input_data[i] = (float*) cthread->getMem({CoyoteAlloc::HPF, n_pages_input});
            output_data[i] = (float*) cthread->getMem({CoyoteAlloc::HPF, n_pages_output});
            
            if (!input_data[i] || !output_data[i]) {
                throw std::runtime_error("Memory allocation failed");
            }

            // Fill input buffer with DIFFERENT test data for each packet
            for(int set = 0; set < num_classifications; set++) {
                for(int j = 0; j < 32; j++) {
                    // Send different data per packet by adding offset
                    input_data[i][set * 32 + j] = test_data[j] + (set * 1000.0f);
                }
            }

            // Clear output buffer
            memset(output_data[i], 0, output_buffer_size);
        }

        // Show input data for each packet
        std::cout << "\nInput data for each packet:\n";
        for(int set = 0; set < num_classifications; set++) {
            std::cout << "Packet " << (set + 1) << " (first 8 values): ";
            for(int j = 0; j < 8; j++) {
                std::cout << std::fixed << std::setprecision(1) << (test_data[j] + (set * 1000.0f));
                if(j < 7) std::cout << " ";
            }
            std::cout << " ... (+" << (set * 1000.0f) << " offset)\n";
        }
        std::cout << "\n";

        // Setup scatter-gather entries for transfer
        sgEntry sg;
        sgFlags sg_flags = { true, true, false };

        // Benchmark setup
        cBench bench(n_reps);
        PR_HEADER("SVM CLASSIFICATION");

        // Clear any previous completions
        cthread->clearCompleted();
        
        auto benchmark_thr = [&]() {
            // Queue all transfers
            for(int i = 0; i < n_reps; i++) {
                memset(&sg, 0, sizeof(localSg));
                sg.local.src_addr = input_data[i];
                sg.local.src_len = buffer_size; 
                sg.local.src_stream = strmHost;
                sg.local.src_dest = targetVfid;

                sg.local.dst_addr = output_data[i];
                sg.local.dst_len = output_buffer_size;  
                sg.local.dst_stream = strmHost;
                sg.local.dst_dest = targetVfid;
                
                sg_flags.last = (i == n_reps-1);

                cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, sg_flags);
            }

            // Wait for completion
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

        // SIMPLE DEBUG: Just dump the raw output buffer values
        PR_HEADER("RAW OUTPUT BUFFER DEBUG");
        for(int i = 0; i < n_reps; i++) {
            std::cout << "Rep " << i << " output buffer contents:\n";
            std::cout << "Buffer size: " << output_buffer_size << " bytes (" << (output_buffer_size/4) << " floats)\n";
            
            for(int j = 0; j < (output_buffer_size/4); j++) {
                uint32_t bits = *(uint32_t*)&output_data[i][j];
                std::cout << "  output[" << j << "] = " << output_data[i][j] 
                          << " (hex: 0x" << std::hex << bits << std::dec << ")\n";
            }
            std::cout << std::endl;
        }

        PR_HEADER("RESULTS");
        // Print all classification results
        for(int i = 0; i < n_reps; i++) {
            std::cout << "Rep " << i << " results:\n";
            for(int j = 0; j < num_classifications; j++) {
                std::cout << "  Classification " << (j+1) << "/" << num_classifications 
                         << ": " << output_data[i][j];
                
                // Map result to class label if it matches known values
                uint32_t result_bits = *(uint32_t*)&output_data[i][j];
                switch(result_bits) {
                    case 0x40800000: std::cout << " (Class 4)"; break;
                    case 0x3F800000: std::cout << " (Class 1)"; break;
                    case 0x40000000: std::cout << " (Class 2)"; break;
                    case 0x40400000: std::cout << " (Class 3)"; break;
                    case 0x40A00000: std::cout << " (Class 5)"; break;
                    default: std::cout << " (Unknown)"; break;
                }
                std::cout << std::endl;
            }
            std::cout << std::endl;
        }
        
        // Print debug info
        cthread->printDebug();

        // Cleanup
        for(int i = 0; i < n_reps; i++) {
            if(input_data[i]) {
                cthread->freeMem(input_data[i]);
                input_data[i] = nullptr;
            }
            if(output_data[i]) {
                cthread->freeMem(output_data[i]);
                output_data[i] = nullptr;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
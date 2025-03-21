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
constexpr auto const INPUT_SIZE = 64;     // 512-bit input for SHA-256
constexpr auto const OUTPUT_SIZE = 64;    // 512-bit final output after width converter

// Helper function to print hex values
void printHexBuffer(uint32_t* buffer, size_t bytes, const char* label) {
    std::cout << label << ": 0x";
    for(int i = (bytes/4)-1; i >= 0; i--) {
        std::cout << std::hex << std::setw(8) << std::setfill('0') << buffer[i];
    }
    std::cout << std::dec << std::endl;
}

int main(int argc, char *argv[]) {
    // Signal handler setup
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = gotInt;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    // Read arguments
    boost::program_options::options_description programDescription("Options:");
    programDescription.add_options()
        ("message,m", boost::program_options::value<std::string>(), "Input message to sign");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), 
                                commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    uint32_t n_pages_host = (INPUT_SIZE + hugePageSize - 1) / hugePageSize;
    uint32_t n_pages_rslt = (OUTPUT_SIZE + pageSize - 1) / pageSize;

    PR_HEADER("DIGITAL SIGNATURE TEST");
    std::cout << "Input size: " << INPUT_SIZE << " bytes (512-bit)" << std::endl;
    std::cout << "Output size: " << OUTPUT_SIZE << " bytes (512-bit)" << std::endl;

    try {
        // Initialize FPGA
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
        cthread->start();

        // Memory allocation
        uint32_t* inputData = (uint32_t*) cthread->getMem({CoyoteAlloc::HPF, n_pages_host});
        uint32_t* outputData = (uint32_t*) cthread->getMem({CoyoteAlloc::HPF, n_pages_rslt});
            
        if (!inputData || !outputData) {
            throw std::runtime_error("Memory allocation failed");
        }

        // Clear buffers
        memset(outputData, 0, OUTPUT_SIZE);
        
        // Fill input with test pattern
        for(int i = 0; i < INPUT_SIZE/4; i++) {
            inputData[i] = i + 1;  // Simple test pattern
        }

        sgEntry sg;
        sgFlags sg_flags = { true, true, false };
        
        cBench bench(1);
        cthread->clearCompleted();

        auto benchmark_thr = [&]() {
            memset(&sg, 0, sizeof(localSg));
                
            // Configure transfers
            sg.local.src_addr = inputData;
            sg.local.src_len = INPUT_SIZE;
            sg.local.src_stream = strmHost;
            sg.local.src_dest = targetVfid;

            sg.local.dst_addr = outputData;
            sg.local.dst_len = OUTPUT_SIZE;
            sg.local.dst_stream = strmHost;
            sg.local.dst_dest = targetVfid;

            cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, sg_flags);

            // Wait for completion
            auto start_time = std::chrono::high_resolution_clock::now();
            while(cthread->checkCompleted(CoyoteOper::LOCAL_TRANSFER) != 1) {
                if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");
                
                auto current_time = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
                
                if (elapsed > 30) {
                    throw std::runtime_error("Transfer timeout after 30 seconds");
                }
            }
        };

        bench.runtime(benchmark_thr);

        // Print results
        PR_HEADER("RESULTS");
        std::cout << "\nInput Data (512 bits):" << std::endl;
        printHexBuffer(inputData, INPUT_SIZE, "Input ");
        
        std::cout << "\nDigital Signature (512 bits):" << std::endl;
        printHexBuffer(outputData, OUTPUT_SIZE, "Output");
        
        std::cout << "\nLatency: " << bench.getAvg() << " us" << std::endl;

        // Print debug information
        PR_HEADER("DEBUG INFORMATION");
        cthread->printDebug();

        // Cleanup
        cthread->freeMem(inputData);
        cthread->freeMem(outputData);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
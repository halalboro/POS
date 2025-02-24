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
constexpr auto const defSize = 32;     // 256-bit input size in bytes
constexpr auto const RSA_OUTPUT_SIZE = 32;  // 256-bit RSA output (in bytes)

// Structure to hold 256-bit value as 8 32-bit words
struct BigInt256 {
    uint32_t words[8];
    
    // Constructor for hex string
    BigInt256(const std::string& hexStr) {
        std::string paddedHex = hexStr;
        // Remove 0x if present
        if(paddedHex.substr(0,2) == "0x") {
            paddedHex = paddedHex.substr(2);
        }
        // Pad to 64 hex chars (256 bits)
        paddedHex.insert(0, 64 - paddedHex.length(), '0');
        
        // Parse into words
        for(int i = 0; i < 8; i++) {
            words[7-i] = std::stoul(paddedHex.substr(i*8, 8), nullptr, 16);
        }
    }
    
    // Constructor for default value
    BigInt256() {
        memset(words, 0, sizeof(words));
    }
};

// Helper function to print hex values
void printHexBuffer(uint32_t* buffer, size_t words, const char* label) {
    std::cout << label << ": 0x";
    for(int i = words-1; i >= 0; i--) {
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
        ("input,i", boost::program_options::value<std::string>(), "Input value (256-bit hex)");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), 
                                commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    // Test values
    BigInt256 inputValue;
    if(commandLineArgs.count("input") > 0) {
        inputValue = BigInt256(commandLineArgs["input"].as<std::string>());
    } else {
        // Default test pattern - using a meaningful 256-bit test value
        inputValue = BigInt256("0xA5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5");
    }

    uint32_t n_pages_host = (defSize + hugePageSize - 1) / hugePageSize;
    uint32_t n_pages_rslt = (RSA_OUTPUT_SIZE + pageSize - 1) / pageSize;

    PR_HEADER("RSA TEST");
    std::cout << "Input size: " << defSize << " bytes (256-bit)" << std::endl;
    std::cout << "Output size: " << RSA_OUTPUT_SIZE << " bytes (256-bit)" << std::endl;

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

        // Clear output buffer and set input
        memset(outputData, 0, RSA_OUTPUT_SIZE);
        memcpy(inputData, inputValue.words, defSize);

        sgEntry sg;
        sgFlags sg_flags = { true, true, false };
        
        cBench bench(1);
        cthread->clearCompleted();

        auto benchmark_thr = [&]() {
            memset(&sg, 0, sizeof(localSg));
                
            // Configure transfers
            sg.local.src_addr = inputData;
            sg.local.src_len = defSize;
            sg.local.src_stream = strmHost;
            sg.local.src_dest = targetVfid;

            sg.local.dst_addr = outputData;
            sg.local.dst_len = RSA_OUTPUT_SIZE;
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
        printHexBuffer(inputData, defSize/4, "Input ");
        printHexBuffer(outputData, RSA_OUTPUT_SIZE/4, "Output");
        
        std::cout << "\nLatency: " << bench.getAvg() << " us" << std::endl;

        // Cleanup
        cthread->freeMem(inputData);
        cthread->freeMem(outputData);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

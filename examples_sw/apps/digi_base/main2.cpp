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
#include <vector>
#include <map>
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
constexpr auto const defInputSize = 64;  // Default is 64 bytes (512 bits)
constexpr auto const OUTPUT_SIZE = 32;   // 32 bytes (256 bits)

// Define expected signatures for different data sizes from simulation
const std::map<uint32_t, std::string> expectedSignatures = {
    {32 * 1024, "0df7cd1be029d306e6659b55f528662920ad44045ee08d2f711c7f9eeffcf7fc"},
    {64 * 1024, "0e76911d8183866e508357435109d8348da11959407f8fe42849fe554b00c89a"},
    {128 * 1024, "05f847595f9a76013962e9bf322cf7aa0531af248c898cafd42e7248839b3bd2"},
    {256 * 1024, "0d80761a70dbf3027ef2dfb7b50a7f01615f96367aaf71403bc993baa8f2b99c"},
    {512 * 1024, "09f219ce7f89d489446567fa85e2849a06258a9f9529bf3c4692b11da41578cf"}
};

// Helper function to print hex values
void printHexBuffer(uint32_t* buffer, size_t bytes, const char* label) {
    std::cout << label << ": 0x";
    for(int i = (bytes/4)-1; i >= 0; i--) {
        std::cout << std::hex << std::setw(8) << std::setfill('0') << buffer[i];
    }
    std::cout << std::dec << std::endl;
}

// Helper function to convert buffer to hex string
std::string bufferToHexString(uint32_t* buffer, size_t bytes) {
    std::stringstream ss;
    for(int i = (bytes/4)-1; i >= 0; i--) {
        ss << std::hex << std::setw(8) << std::setfill('0') << buffer[i];
    }
    return ss.str();
}

// Helper function to print latency statistics
void printLatencyStats(double avg_latency_ns, uint32_t data_size_bytes, uint32_t n_reps) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nLatency Measurements:" << std::endl;
    std::cout << "Processing started at: 0 ns" << std::endl;
    std::cout << "Processing completed at: " << avg_latency_ns << " ns" << std::endl;
    std::cout << "Total latency: " << avg_latency_ns << " ns (" << (avg_latency_ns / 1000) << " us)" << std::endl;
    std::cout << "Average latency per KB: " << (avg_latency_ns * 1024 / data_size_bytes) << " ns" << std::endl;
    std::cout << "Throughput: " << std::setw(8) 
              << (1000000000 * data_size_bytes * n_reps) / (avg_latency_ns * n_reps) 
              << " MB/s" << std::endl;
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
        ("message,m", boost::program_options::value<std::string>(), "Input message to sign")
        ("size,s", boost::program_options::value<uint32_t>(), "Input size in bytes (default: 64)")
        ("reps,r", boost::program_options::value<uint32_t>(), "Number of repetitions");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), 
                                commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    uint32_t n_reps = defReps;
    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();

    uint32_t input_size = defInputSize;
    if(commandLineArgs.count("size") > 0) {
        input_size = commandLineArgs["size"].as<uint32_t>();
        // Ensure input size is a multiple of 64 (512 bits)
        if(input_size % 64 != 0) {
            uint32_t old_size = input_size;
            input_size = ((input_size + 63) / 64) * 64;
            std::cout << "Adjusting input size from " << old_size << " to " << input_size 
                      << " bytes (multiple of 64 bytes)" << std::endl;
        }
    }

    uint32_t n_pages_host = (input_size + hugePageSize - 1) / hugePageSize;
    uint32_t n_pages_rslt = (OUTPUT_SIZE + pageSize - 1) / pageSize;

    PR_HEADER("DIGITAL SIGNATURE TEST");
    std::cout << "Input size: " << input_size << " bytes (" << (input_size * 8) << "-bit)" << std::endl;
    std::cout << "Output size: " << OUTPUT_SIZE << " bytes (256-bit)" << std::endl;
    std::cout << "Number of repetitions: " << n_reps << std::endl;

    try {
        // Initialize FPGA
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
        cthread->start();

        // Memory allocation - vectors for multiple buffers
        std::vector<uint32_t*> input_buffers(n_reps, nullptr);
        std::vector<uint32_t*> output_buffers(n_reps, nullptr);
        
        // Allocate and initialize memory for each repetition
        for(int i = 0; i < n_reps; i++) {
            input_buffers[i] = (uint32_t*) cthread->getMem({CoyoteAlloc::HPF, n_pages_host});
            output_buffers[i] = (uint32_t*) cthread->getMem({CoyoteAlloc::HPF, n_pages_rslt});
            
            if (!input_buffers[i] || !output_buffers[i]) {
                throw std::runtime_error("Memory allocation failed");
            }

            // Clear output buffer
            memset(output_buffers[i], 0, OUTPUT_SIZE);
            
            // Fill input with constant chunk pattern (matching the testbench)
            // This matches CONSTANT_CHUNK = 512'hFEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210;
            for (int j = 0; j < input_size/4; j += 2) {
                // Fill two 32-bit words at a time to match the 64-bit pattern
                input_buffers[i][j] = 0x76543210;
                if (j+1 < input_size/4) {
                    input_buffers[i][j+1] = 0xFEDCBA98;
                }
            }
        }
        std::cout << "\nUsing constant chunk pattern for all inputs." << std::endl;

        sgEntry sg;
        sgFlags sg_flags = { true, true, false };
        
        cBench bench(n_reps);
        cthread->clearCompleted();

        PR_HEADER("PROCESSING");
        
        auto benchmark_thr = [&]() {
            for(int i = 0; i < n_reps; i++) {
                memset(&sg, 0, sizeof(localSg));
                    
                // Configure transfers
                sg.local.src_addr = input_buffers[i];
                sg.local.src_len = input_size;
                sg.local.src_stream = strmHost;
                sg.local.src_dest = targetVfid;

                sg.local.dst_addr = output_buffers[i];
                sg.local.dst_len = OUTPUT_SIZE;
                sg.local.dst_stream = strmHost;
                sg.local.dst_dest = targetVfid;

                sg_flags.last = (i == n_reps-1);
                cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, sg_flags);
            }

            // Wait for completion of all transfers
            while(cthread->checkCompleted(CoyoteOper::LOCAL_WRITE) != 1) {
                if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");
            }
        };

        bench.runtime(benchmark_thr);

        // Print results
        PR_HEADER("RESULTS");
        
        // Only print the first signature result as a sample
        std::cout << "\nInput Data (first 512 bits):" << std::endl;
        printHexBuffer(input_buffers[0], 64, "Input ");
        
        std::cout << "\nDigital Signature (256 bits):" << std::endl;
        printHexBuffer(output_buffers[0], OUTPUT_SIZE, "Output");
    
        // Print detailed latency statistics
        PR_HEADER("LATENCY MEASUREMENTS");
        printLatencyStats(bench.getAvg() / n_reps, input_size, n_reps);

        // Verify the signature against expected value
        PR_HEADER("VERIFICATION");
        std::string actual_sig = bufferToHexString(output_buffers[0], OUTPUT_SIZE);
        
        // Check if we have an expected signature for this data size
        if (expectedSignatures.find(input_size) != expectedSignatures.end()) {
            std::string expected_sig = expectedSignatures.at(input_size);
            std::cout << "Expected: 0x" << expected_sig << std::endl;
            std::cout << "Actual  : 0x" << actual_sig << std::endl;
            
            if (expected_sig == actual_sig) {
                std::cout << "\033[32mSIGNATURE MATCH: The output matches expected simulation result!\033[0m" << std::endl;
            } else {
                std::cout << "\033[31mSIGNATURE MISMATCH: The output does not match expected simulation!\033[0m" << std::endl;
            }
        } else {
            std::cout << "No expected signature available for comparison at size " << input_size << " bytes." << std::endl;
            std::cout << "Actual signature: 0x" << actual_sig << std::endl;
        }

        // Print debug information
        PR_HEADER("DEBUG INFORMATION");
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
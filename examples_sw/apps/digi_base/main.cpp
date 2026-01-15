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
#ifdef EN_AVX
#include <x86intrin.h>
#endif
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

constexpr auto const nRegions = 1;
constexpr auto const targetVfid = 0;  
constexpr auto const defReps = 1;
constexpr auto const defMinSize = 8 * 1024;
constexpr auto const defMaxSize = 128 * 1024 ;
constexpr auto const defDW = 4;            // 32-bit for SHA
constexpr auto const SHA256_DIGEST_LENGTH = 32;  // SHA256 produces 256-bit (32-byte) hash
constexpr auto const nBenchRuns = 1;

constexpr auto const outputSize2 = 32;  // 256-bit RSA output (in bytes)

/**
 * @brief Benchmark API
 * 
 */
enum class BenchRegs : uint32_t {
    CTRL_REG = 0,
    DONE_REG = 1,
    TIMER_REG = 2,
    VADDR_REG = 3,
    LEN_REG = 4,
    PID_REG = 5,
    N_REPS_REG = 6,
    N_BEATS_REG = 7,
    DEST_REG = 8
};

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
        ("size,s", boost::program_options::value<uint32_t>(), "Data size")
        ("reps,r", boost::program_options::value<uint32_t>(), "Number of reps");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    uint32_t size = defMinSize;
    uint32_t n_reps = defReps;
    uint32_t cs_dev = defDevice; 
    uint32_t n_regions = nRegions;
    uint32_t curr_size = defMinSize;
    uint32_t max_size = defMaxSize;

    if(commandLineArgs.count("size") > 0) size = commandLineArgs["size"].as<uint32_t>();
    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();

    uint32_t n_pages_host = (max_size + hugePageSize - 1) / hugePageSize;
    uint32_t n_pages_rslt = (outputSize2 + pageSize - 1) / pageSize;

    PR_HEADER("PARAMS");
    std::cout << "vFPGA ID: " << targetVfid << std::endl;
    std::cout << "Number of allocated pages per run: " << n_pages_host << std::endl;
    std::cout << "Starting transfer size: " << size << std::endl;
    std::cout << "Ending transfer size: " << max_size << std::endl << std::endl;
    std::cout << "Number of reps: " << n_reps << std::endl;

    std::vector<std::unique_ptr<cThread<std::any>>> cthread; // Coyote threads

    void* hMem[n_regions];
    void* hMem_out[n_regions];

    // Obtain resources
    cthread.emplace_back(new cThread<std::any>(0, getpid(), cs_dev));
    hMem[0] = cthread[0]->getMem({CoyoteAlloc::HPF, n_pages_host});
    hMem_out[0] = cthread[0]->getMem({CoyoteAlloc::HPF, n_pages_rslt});

    // Fill input with constant chunk pattern (matching the testbench)
    // This matches CONSTANT_CHUNK = 512'hFEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210;
    for (int j = 0; j < max_size/4; j += 2) {
        // Fill two 32-bit words at a time to match the 64-bit pattern
        ((uint32_t *)hMem[0])[j] = 0x76543210;
        if (j+1 < max_size/4) {
            ((uint32_t *)hMem[0])[j+1] = 0xFEDCBA98;
        }
    }
    memset(hMem_out[0], 0, outputSize2);

    sgEntry sg[n_regions];

    // SG entries
    memset(&sg[0], 0, sizeof(localSg));
    sg[0].local.src_addr = hMem[0]; sg[0].local.src_stream = strmHost;
    sg[0].local.dst_addr = hMem_out[0]; sg[0].local.dst_stream = strmHost;
    // sg[0].local.src_dest = targetVfid;
    // sg[0].local.dst_dest = targetVfid;

    sgFlags sg_flags = { true, true, false };
    sg_flags.last = true;    

    cBench bench(nBenchRuns);
    uint32_t n_runs;

    uint64_t timer_value;
    uint32_t timer_value_sha2;
    uint32_t timer_value_rsa;

    // ---------------------------------------------------------------
    // Runs 
    // ---------------------------------------------------------------
    PR_HEADER("Digi sig base");

    while(curr_size <= max_size) { 
        cthread[0]->clearCompleted();

        auto benchmark_thr = [&]() {
            // Queue all transfers
            sg[0].local.src_len = curr_size;
            sg[0].local.dst_len = SHA256_DIGEST_LENGTH;
            
            cthread[0]->invoke(CoyoteOper::LOCAL_TRANSFER, &sg[0], sg_flags);

            while(cthread[0]->checkCompleted(CoyoteOper::LOCAL_TRANSFER) != n_reps) {
                if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");
            }
            timer_value = (uint64_t) cthread[0]->getCSR(static_cast<uint64_t>(BenchRegs::TIMER_REG));

        };

        bench.runtime(benchmark_thr);

        // Print detailed latency statistics
        PR_HEADER("LATENCY MEASUREMENTS");
        printLatencyStats(bench.getAvg() / n_reps, curr_size, n_reps);

        timer_value_sha2 = static_cast<uint32_t>(timer_value);
        timer_value_rsa = static_cast<uint32_t>(timer_value  >> 32);

        // Print results
        std::cout << "size: " << curr_size << ", lat: " << std::setw(8) << bench.getAvg() / (n_reps) << " ns" << std::endl;
        std::cout << "clock cycle sha2: " << timer_value_sha2 << std::endl;
        std::cout << "clock cycle rsa: " << timer_value_rsa << std::endl;

        curr_size *= 2;
    }

    // Verify the signature against expected value
    PR_HEADER("VERIFICATION");
    std::string actual_sig = bufferToHexString((uint32_t*)hMem_out[0], outputSize2);
    
    // Check if we have an expected signature for this data size
    if (expectedSignatures.find(max_size) != expectedSignatures.end()) {
        std::string expected_sig = expectedSignatures.at(max_size);
        std::cout << "Expected: 0x" << expected_sig << std::endl;
        std::cout << "Actual  : 0x" << actual_sig << std::endl;
        
        if (expected_sig == actual_sig) {
            std::cout << "\033[32mSIGNATURE MATCH: The output matches expected simulation result!\033[0m" << std::endl;
        } else {
            std::cout << "\033[31mSIGNATURE MISMATCH: The output does not match expected simulation!\033[0m" << std::endl;
        }
    } else {
        std::cout << "No expected signature available for comparison at size " << max_size << " bytes." << std::endl;
        std::cout << "Actual signature: 0x" << actual_sig << std::endl;
    }


    // Print results


    // // Print hash results
    // for(int i = 0; i < n_reps; i++) {
    //     std::cout << "Hash " << i << ": ";
    //     for(int j = 0; j < SHA256_DIGEST_LENGTH; j++) {
    //         std::cout << std::hex << std::setw(2) << std::setfill('0') 
    //                     << ((int *)hMem_out[0])[i * SHA256_DIGEST_LENGTH + j];
    //     }
    //     std::cout << std::dec << std::endl;
    // }
    
    // Print debug info
    

    // Cleanup
    for(int i = 0; i < n_regions; i++) {
        if(hMem[i]) {
            cthread[i]->freeMem(hMem[i]);
            hMem[i] = nullptr;
        }
        if(hMem_out[i]) {
            cthread[i]->freeMem(hMem_out[i]);
            hMem_out[i] = nullptr;
        }
        cthread[i]->printDebug();
    }
    
    
    return EXIT_SUCCESS;
}
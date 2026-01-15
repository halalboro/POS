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

constexpr auto const nRegions = 2;
constexpr auto const targetVfid = 0;  
constexpr auto const defReps = 1;
constexpr auto const defSize = 64 * 1024;  // 64KB default
constexpr auto const defDW = 4;            // 32-bit for SHA
constexpr auto const SHA256_DIGEST_LENGTH = 32;  // SHA256 produces 256-bit (32-byte) hash
constexpr auto const nBenchRuns = 1;

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

    uint32_t size = defSize;
    uint32_t n_reps = defReps;
    uint32_t cs_dev = defDevice; 
    uint32_t n_regions = nRegions;

    if(commandLineArgs.count("size") > 0) size = commandLineArgs["size"].as<uint32_t>();
    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();
    BigInt256 inputValue;
    // Default test pattern - using a meaningful 256-bit test value
    inputValue = BigInt256("0xA5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5");

    uint32_t n_pages_host = (size + hugePageSize - 1) / hugePageSize;
    uint32_t n_pages_rslt = (n_reps * SHA256_DIGEST_LENGTH + pageSize - 1) / pageSize;

    uint32_t n_pages_host_2 = (SHA256_DIGEST_LENGTH + hugePageSize - 1) / hugePageSize;
    uint32_t n_pages_rslt_2 = (RSA_OUTPUT_SIZE + pageSize - 1) / pageSize;

    PR_HEADER("PARAMS");
    std::cout << "vFPGA ID: " << targetVfid << std::endl;
    std::cout << "Number of allocated pages per run: " << n_pages_host << std::endl;
    std::cout << "Data size: " << size << std::endl;
    std::cout << "Number of reps: " << n_reps << std::endl;

    
    std::vector<std::unique_ptr<cThread<std::any>>> cthread; // Coyote threads

    void* hMem[n_regions];
    void* hMem_out[n_regions];

    // Obtain resources
    cthread.emplace_back(new cThread<std::any>(0, getpid(), cs_dev));
    hMem[0] = cthread[0]->getMem({CoyoteAlloc::HPF, n_pages_host});
    hMem_out[0] = cthread[0]->getMem({CoyoteAlloc::HPF, n_pages_rslt});
                        
    for(int j = 0; j < size/defDW; j++) {
        ((uint32_t *)hMem[0])[j] = j;  // Simple pattern for testing
    }

    cthread.emplace_back(new cThread<std::any>(1, getpid(), cs_dev));
    hMem[1] = cthread[1]->getMem({CoyoteAlloc::HPF, n_pages_host_2});
    hMem_out[1] = cthread[1]->getMem({CoyoteAlloc::HPF, n_pages_rslt_2});
                        
    // memcpy(hMem[1], inputValue.words, SHA256_DIGEST_LENGTH);
    memset(hMem_out[1], 0, RSA_OUTPUT_SIZE);

    sgEntry sg[n_regions];

    // SG entries
    memset(&sg[0], 0, sizeof(localSg));
    sg[0].local.src_addr = hMem[0]; sg[0].local.src_len = size; sg[0].local.src_stream = strmHost;
    sg[0].local.dst_addr = hMem_out[0]; sg[0].local.dst_len = SHA256_DIGEST_LENGTH; sg[0].local.dst_stream = strmHost;

    memset(&sg[1], 0, sizeof(localSg));
    sg[1].local.src_addr = hMem[1]; sg[1].local.src_len = SHA256_DIGEST_LENGTH; sg[1].local.src_stream = strmHost;
    sg[1].local.dst_addr = hMem_out[1]; sg[1].local.dst_len = RSA_OUTPUT_SIZE; sg[1].local.dst_stream = strmHost;


    // sg[0].local.offset_r = 0;
    // sg[0].local.offset_w = 0;
    // cthread[0]->ioSwitch(IODevs::Inter_2_TO_HOST_0);
    // cthread[0]->ioSwDbg();
    // sg[1].local.offset_r = 0;
    // sg[1].local.offset_w = 0;
    // cthread[1]->ioSwitch(IODevs::Inter_2_TO_HOST_1);
    // cthread[1]->ioSwDbg();

    // from vFPGA 0 to vFPGA 1
    sg[0].local.offset_r = 0;
    sg[0].local.offset_w = 6;
    cthread[0]->ioSwitch(IODevs::Inter_2_TO_DTU_1);
    cthread[0]->ioSwDbg();
    sg[1].local.offset_r = 6;
    sg[1].local.offset_w = 0;
    cthread[1]->ioSwitch(IODevs::Inter_2_TO_HOST_1);
    cthread[1]->ioSwDbg();

    cBench bench(nBenchRuns);
    uint32_t n_runs;


    // ---------------------------------------------------------------
    // Runs 
    // ---------------------------------------------------------------
    PR_HEADER("Digi sig pipeline");

    auto benchmark_thr = [&]() {

        cthread[0]->invoke(CoyoteOper::LOCAL_TRANSFER, &sg[0], {true, true, false});
        cthread[1]->invoke(CoyoteOper::LOCAL_TRANSFER, &sg[1], {true, true, false});
        // Wait for all transfers with timeout
        auto start_time = std::chrono::high_resolution_clock::now();
        // while(cthread[0]->checkCompleted(CoyoteOper::LOCAL_TRANSFER) != n_reps) {
        //     if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");
            
        //     auto current_time = std::chrono::high_resolution_clock::now();
        //     auto elapsed = std::chrono::duration_cast<std::chrono::seconds>
        //                     (current_time - start_time).count();
            
        //     if (elapsed > 30) {
        //         throw std::runtime_error("Transfers timeout");
        //     }
        // }

        while(cthread[1]->checkCompleted(CoyoteOper::LOCAL_TRANSFER) != n_reps) {
            if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");
            
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>
                            (current_time - start_time).count();
        }
    };

    try {
        bench.runtime(benchmark_thr);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }    

    // Print results
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Size: " << std::setw(8) << size << ", thr: " 
                << std::setw(8) << (1000 * size) / (bench.getAvg() / n_reps) 
                << " MB/s" << std::endl << std::endl;

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
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
constexpr auto const targetVfid = 0;  
constexpr auto const defReps = 1;
constexpr auto const defMinSize = 8 * 1024;  // 64KB default
constexpr auto const defMaxSize = 128 * 1024;  // 64KB default
constexpr auto const defDW = 4;            // 32-bit for SHA
constexpr auto const SHA256_DIGEST_LENGTH = 32;  // SHA256 produces 256-bit (32-byte) hash

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

    uint32_t curr_size = defMinSize;
    uint32_t max_size = defMaxSize;
    uint32_t n_reps = defReps;

    if(commandLineArgs.count("size") > 0) max_size = commandLineArgs["size"].as<uint32_t>();
    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();

    uint32_t n_pages_host = (max_size + hugePageSize - 1) / hugePageSize;
    uint32_t n_pages_rslt = (n_reps * SHA256_DIGEST_LENGTH + pageSize - 1) / pageSize;

    PR_HEADER("PARAMS");
    std::cout << "vFPGA ID: " << targetVfid << std::endl;
    std::cout << "Number of allocated pages per run: " << n_pages_host << std::endl;
    std::cout << "Min data size: " << curr_size << std::endl;
    std::cout << "Max data size: " << max_size << std::endl;
    std::cout << "Number of reps: " << n_reps << std::endl;

    try {
        // Initialize thread
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
        cthread->start();

        // Memory allocation
        std::vector<uint32_t*> inputData(n_reps, nullptr);
        unsigned char* hashResults = nullptr;

        // Allocate and fill memory
        for(int i = 0; i < n_reps; i++) {
            inputData[i] = (uint32_t*) cthread->getMem({CoyoteAlloc::HPF, n_pages_host});
            if (!inputData[i]) {
                throw std::runtime_error("Input memory allocation failed");
            }

            // Fill with test pattern
            for(int j = 0; j < max_size/defDW; j++) {
                inputData[i][j] = j;  // Simple pattern for testing
            }
        }

        // Allocate result memory
        hashResults = (unsigned char*) cthread->getMem({CoyoteAlloc::HPF, n_pages_rslt});
        if (!hashResults) {
            throw std::runtime_error("Result memory allocation failed");
        }

        // Setup scatter-gather entries for transfer
        sgEntry sg;
        sgFlags sg_flags = { true, true, false };

        // Benchmark setup
        cBench bench(1);
        PR_HEADER("SHA256 HASHING");

        // Clear any previous completions
        cthread->clearCompleted();
        cthread->ioSwitch(IODevs::Inter_2_TO_HOST_0);
        cthread->ioSwDbg();

        // std::vector<double> time_bench;
        uint32_t timer_value;

        while(curr_size <= max_size) { 
            auto benchmark_thr = [&]() {
                // Queue all transfers
                for(int i = 0; i < n_reps; i++) {
                    memset(&sg, 0, sizeof(localSg));
                    sg.local.src_addr = inputData[i];
                    sg.local.src_len = curr_size;
                    sg.local.src_stream = strmHost;
                    // sg.local.src_dest = targetVfid;

                    sg.local.dst_addr = &hashResults[i * SHA256_DIGEST_LENGTH];
                    sg.local.dst_len = SHA256_DIGEST_LENGTH;
                    sg.local.dst_stream = strmHost;
                    // sg.local.dst_dest = targetVfid;

                    // if(i == n_reps-1) sg_flags.last = true;
                    
                    cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, sg_flags);
                }

                while(cthread->checkCompleted(CoyoteOper::LOCAL_TRANSFER) != n_reps) {
                    if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");
                }

                timer_value = (uint32_t) cthread->getCSR(static_cast<uint32_t>(BenchRegs::TIMER_REG));
                // time_bench.emplace_back(cthread.getCSR(static_cast<uint32_t>(BenchRegs::TIMER_REG)));
            };

            bench.runtime(benchmark_thr);

            // Print results
            std::cout << "size: " << curr_size << ", lat: " << std::setw(8) << bench.getAvg() / (n_reps) << " ns" << std::endl;
            std::cout << "clock cycle: " << timer_value << std::endl;

            curr_size *= 2;
        }

        // Print hash results
        for(int i = 0; i < n_reps; i++) {
            std::cout << "Hash " << i << ": ";
            for(int j = 0; j < SHA256_DIGEST_LENGTH; j++) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') 
                         << static_cast<int>(hashResults[i * SHA256_DIGEST_LENGTH + j]);
            }
            std::cout << std::dec << std::endl;
        }
        
        // Print debug info
        cthread->printDebug();

        // Cleanup
        for(int i = 0; i < n_reps; i++) {
            if(inputData[i]) {
                cthread->freeMem(inputData[i]);
                inputData[i] = nullptr;
            }
        }
        if(hashResults) {
            cthread->freeMem(hashResults);
            hashResults = nullptr;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
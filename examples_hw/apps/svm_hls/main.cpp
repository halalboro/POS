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
constexpr auto const defSize = 32;  // 32 floats for SVM input

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
        ("reps,r", boost::program_options::value<uint32_t>(), "Number of reps");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    uint32_t size = defSize;
    uint32_t n_reps = defReps;

    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();

    uint32_t n_pages_host = (size + hugePageSize - 1) / hugePageSize;

    PR_HEADER("PARAMS");
    std::cout << "vFPGA ID: " << targetVfid << std::endl;
    std::cout << "Number of allocated pages per run: " << n_pages_host << std::endl;
    std::cout << "Data size: " << size << std::endl;
    std::cout << "Number of reps: " << n_reps << std::endl;

    try {
        // Initialize thread
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
        cthread->start();

        // Memory allocation
        std::vector<float*> input_data(n_reps, nullptr);
        std::vector<float*> output_data(n_reps, nullptr);

	float test_data[32] = {
	    36257662.0f, 70308074.0f, 162763557.0f, 109956489.0f, 86125933.0f,
	    35535698.0f, 5473712.0f, 2191429.0f, 1655529.0f, 1427210.0f,
	    1819290.0f, 2594503.0f, 2649694.0f, 849597.0f, 765175.0f,
	    890647.0f, 1405332.0f, 1132804.0f, 5776375.0f, 18372118.0f,
	    6724989.0f, 7313762.0f, 1469345.0f, 1744257.0f, 1631652.0f,
	    823229.0f, 1018674.0f, 639273.0f, 601445.0f, 630083.0f,
	    678854.0f, 636897.0f
	};

        // Allocate and fill memory
        for(int i = 0; i < n_reps; i++) {
            input_data[i] = (float*) cthread->getMem({CoyoteAlloc::HPF, n_pages_host});
            output_data[i] = (float*) cthread->getMem({CoyoteAlloc::HPF, n_pages_host});
            
            if (!input_data[i] || !output_data[i]) {
                throw std::runtime_error("Memory allocation failed");
            }

            // Fill with test data
            for(int j = 0; j < 32; j++) {
            memcpy(&input_data[i][j], &test_data[j], sizeof(float));
            std::cout << input_data[i][j] << std::endl;
            }
        }
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
                sg.local.src_len = 32 * sizeof(float); 
                sg.local.src_stream = strmHost;
                sg.local.src_dest = targetVfid;

                sg.local.dst_addr = output_data[i];
                sg.local.dst_len = sizeof(float);  
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

        // Print results
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Size: " << std::setw(8) << size << ", thr: " 
                  << std::setw(8) << (1000 * size) / (bench.getAvg() / n_reps) 
                  << " MB/s" << std::endl << std::endl;

        // Print all classification results
        for(int i = 0; i < n_reps; i++) {
            std::cout << "Rep " << i << " classification result: " << output_data[i][0] << std::endl;
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

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
constexpr auto const targetVfid = 1;  
constexpr auto const defReps = 1;
constexpr auto const FFT_SIZE = 16384;
constexpr auto const inputSize = ((FFT_SIZE * sizeof(uint64_t) + pageSize - 1) / pageSize) * pageSize;
constexpr auto const outputSize = ((32 * sizeof(float) + pageSize - 1) / pageSize) * pageSize;

void generateFftData(uint64_t* buffer) {
    float base_magnitude = 20000000.0f;
    
    for(size_t i = 0; i < FFT_SIZE; i++) {
        float value = base_magnitude * (1.0f - (float)i/FFT_SIZE);
        value += base_magnitude * 0.5f * sin(2.0f * M_PI * i / FFT_SIZE);
        uint64_t packed_data = 0;
        memcpy(&packed_data, &value, sizeof(float));
        buffer[i] = packed_data;
    }

    std::cout << "\nFirst few FFT samples:" << std::endl;
    for(int i = 0; i < 5; i++) {
        float value;
        memcpy(&value, &buffer[i], sizeof(float));
        std::cout << "Sample " << i << ": " << value 
                  << " (hex: 0x" << std::hex << buffer[i] << std::dec << ")" << std::endl;
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
        ("reps,r", boost::program_options::value<uint32_t>(), "Number of repetitions")
        ("size,s", boost::program_options::value<uint32_t>(), "Override FFT size");

    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    uint32_t n_reps = defReps;
    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();

    uint32_t input_pages = (inputSize + hugePageSize - 1) / hugePageSize;
    uint32_t total_output_size = n_reps * outputSize;
    uint32_t output_pages = (total_output_size + hugePageSize - 1) / hugePageSize;

    PR_HEADER("PARAMS");
    std::cout << "vFPGA ID: " << targetVfid << std::endl;
    std::cout << "Number of repetitions: " << n_reps << std::endl;
    std::cout << "Input pages: " << input_pages << std::endl;
    std::cout << "Output pages: " << output_pages << std::endl;
    std::cout << "Input size: " << inputSize << " bytes" << std::endl;
    std::cout << "Total output size: " << total_output_size << " bytes" << std::endl;
    std::cout << "input size: " << FFT_SIZE * sizeof(uint64_t) << " bytes" << std::endl;
    std::cout << "output size: " << 32 * sizeof(float) << " bytes" << std::endl;


    try {
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
        cthread->start();

        std::vector<void*> input_buffers(n_reps, nullptr);
        std::vector<void*> output_buffers(n_reps, nullptr);
        
        for(uint32_t i = 0; i < n_reps; i++) {
            input_buffers[i] = cthread->getMem({CoyoteAlloc::HPF, input_pages});
            output_buffers[i] = cthread->getMem({CoyoteAlloc::HPF, output_pages});
            if(!input_buffers[i]) throw std::runtime_error("Input memory allocation failed");
            generateFftData(static_cast<uint64_t*>(input_buffers[i]));
            if(!output_buffers[i]) throw std::runtime_error("Output memory allocation failed");
            memset(output_buffers[i], 0, total_output_size);
        }

        sgEntry sg;
	    sgFlags sg_flags = {true, true, false};

        cBench bench(n_reps);
        PR_HEADER("FFT2SVM PROCESSING");

        cthread->clearCompleted();
        cthread->ioSwitch(IODevs::Inter_3_TO_HOST_1);
        cthread->ioSwDbg();

	auto benchmark_thr = [&]() {
	    for(int i = 0; i < n_reps; i++) {
		memset(&sg, 0, sizeof(localSg));
		
		sg.local.src_addr = input_buffers[i];
		sg.local.src_len = FFT_SIZE * sizeof(uint64_t);
		sg.local.src_stream = strmHost;
		// sg.local.src_dest = targetVfid;

		sg.local.dst_addr = reinterpret_cast<char*>(output_buffers[i]) + i * outputSize;
		sg.local.dst_len = 32 * sizeof(float);
		sg.local.dst_stream = strmHost;
		// sg.local.dst_dest = targetVfid;

        sg.local.offset_r = 0;
        sg.local.offset_w = 0;

		sg_flags.last = (i == n_reps-1);
		
        cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, sg_flags);
        }

        // Wait for completion
        while(cthread->checkCompleted(CoyoteOper::LOCAL_WRITE) != 1) {
            if(stalled.load()) throw std::runtime_error("Stalled");
        }
        
	};

        bench.runtime(benchmark_thr);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Processing throughput: " 
                  << (1000.0 * inputSize * n_reps) / bench.getAvg() << " MB/s" 
                  << std::endl << std::endl;

        for(uint32_t rep = 0; rep < n_reps; rep++) {
            std::cout << "Repetition " << rep << " results:" << std::endl;
	uint32_t* output_ptr = reinterpret_cast<uint32_t*>(
		        reinterpret_cast<char*>(output_buffers[rep]) + rep * outputSize
		    );
            
            for(int i = 0; i < 32; i++) {
                uint32_t raw_value = output_ptr[i];
                float scaled_value = static_cast<float>(raw_value);
                std::cout << "Feature " << i << ": " 
                          << scaled_value 
                          << " (hex: 0x" << std::hex << raw_value << std::dec << ")" 
                          << std::endl;
            }
            
            uint64_t* output_512 = reinterpret_cast<uint64_t*>(output_ptr);
            std::cout << "\nRaw output buffer in 512-bit chunks:" << std::endl;
            for(int i = 0; i < 4; i++) {
                std::cout << "512-bit chunk " << i << ": ";
                for(int j = 0; j < 8; j++) {
                    std::cout << "0x" << std::hex << output_512[i*8 + j] << " ";
                }
                std::cout << std::dec << std::endl;
            }
            std::cout << "\n";
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
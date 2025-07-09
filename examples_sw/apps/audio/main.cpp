/**
 * Copyright (c) 2021, Systems Group, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
#include <random>

#include "cBench.hpp"
#include "cThread.hpp"

// #define EN_THR_TESTS
#define EN_DIRECT_TESTS
// #define EN_INTER_2_TESTS
// #define EN_INTER_3_TESTS

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
constexpr auto const defHuge = true;
constexpr auto const defMappped = true;
constexpr auto const defStream = 1;
constexpr auto const nRepsThr = 1;
constexpr auto const nRepsLat = 1;
constexpr auto const defMinSize = 4 * 1024;
constexpr auto const defMaxSize = 4 * 1024;
constexpr auto const nBenchRuns = 1;
constexpr float const sampleRate = 44100.0f;

// Generate sine wave value
float generateSineValue(int index) {
    const float amplitude = 1000.0f;
    const float frequency = 1378.125f;
    const float phase = 0.0f;
    
    float t = (float)index / sampleRate;
    return amplitude * sin(2.0f * M_PI * frequency * t + phase);
}

void generateCompressibleAudio(float* audio_data, uint32_t input_size) {
    std::cout << "\nGenerating random audio for quantization testing..." << std::endl;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Generate random amplitudes that will hit all quantization levels
    // Based on quantization thresholds: <1.0, <100.0, <1000.0, <10000.0, >=10000.0
    std::uniform_real_distribution<float> dist(-15000.0f, 15000.0f);
    
    for (uint32_t sample_idx = 0; sample_idx < input_size; ++sample_idx) {
        // Generate random amplitude
        float real = dist(gen);
        
        audio_data[2*sample_idx] = real;     // Real part
        audio_data[2*sample_idx + 1] = 0.00f;   // Imaginary part
    }
}

/**
 * @brief Validation tests
 * 
 */
int main(int argc, char *argv[])  
{
    // ---------------------------------------------------------------
    // Args 
    // ---------------------------------------------------------------

    // Sig handler
    struct sigaction sa;
    memset( &sa, 0, sizeof(sa) );
    sa.sa_handler = gotInt;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL);

    // Read arguments
    boost::program_options::options_description programDescription("Options:");
    programDescription.add_options()
        ("bitstream,b", boost::program_options::value<string>(), "Shell bitstream")
        ("device,d", boost::program_options::value<uint32_t>(), "Target device")
        ("regions,g", boost::program_options::value<uint32_t>(), "Number of vFPGAs")
        ("hugepages,h", boost::program_options::value<bool>(), "Hugepages")
        ("mapped,m", boost::program_options::value<bool>(), "Mapped / page fault")
        ("stream,t", boost::program_options::value<bool>(), "Streaming interface")
        ("repst,r", boost::program_options::value<uint32_t>(), "Number of repetitions (throughput)")
        ("repsl,l", boost::program_options::value<uint32_t>(), "Number of repetitions (latency)")
        ("min_size,n", boost::program_options::value<uint32_t>(), "Starting transfer size")
        ("max_size,x", boost::program_options::value<uint32_t>(), "Ending transfer size");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    string bstream_path = "";
    uint32_t cs_dev = defDevice; 
    uint32_t n_regions = nRegions;
    bool huge = defHuge;
    bool mapped = defMappped;
    bool stream = defStream;
    uint32_t n_reps_thr = nRepsThr;
    uint32_t n_reps_lat = nRepsLat;
    uint32_t curr_size = defMinSize;
    uint32_t max_size = defMaxSize;
    uint32_t n_reps = nRepsLat;

    uint32_t complex_size = 2 * defMaxSize;       // Total floats (real + imag)
    uint32_t input_buffer_size = complex_size * sizeof(float);  // Input bytes
    uint32_t num_ffts = defMaxSize / 32;                 // Number of 32-point FFTs


    if(commandLineArgs.count("bitstream") > 0) { 
        bstream_path = commandLineArgs["bitstream"].as<string>();
        
        std::cout << std::endl << "Shell loading (path: " << bstream_path << ") ..." << std::endl;
        cRnfg crnfg(cs_dev);
        crnfg.shellReconfigure(bstream_path);
    }
    if(commandLineArgs.count("device") > 0) cs_dev = commandLineArgs["device"].as<uint32_t>();
    if(commandLineArgs.count("regions") > 0) n_regions = commandLineArgs["regions"].as<uint32_t>();
    if(commandLineArgs.count("hugepages") > 0) huge = commandLineArgs["hugepages"].as<bool>();
    if(commandLineArgs.count("mapped") > 0) mapped = commandLineArgs["mapped"].as<bool>();
    if(commandLineArgs.count("stream") > 0) stream = commandLineArgs["stream"].as<bool>();
    if(commandLineArgs.count("repst") > 0) n_reps_thr = commandLineArgs["repst"].as<uint32_t>();
    if(commandLineArgs.count("repsl") > 0) n_reps_lat = commandLineArgs["repsl"].as<uint32_t>();
    if(commandLineArgs.count("min_size") > 0) curr_size = commandLineArgs["min_size"].as<uint32_t>();
    if(commandLineArgs.count("max_size") > 0) max_size = commandLineArgs["max_size"].as<uint32_t>();

    PR_HEADER("PARAMS");
    std::cout << "Number of regions: " << n_regions << std::endl;
    std::cout << "Hugepages: " << huge << std::endl;
    std::cout << "Mapped pages: " << mapped << std::endl;
    std::cout << "Streaming: " << (stream ? "HOST" : "CARD") << std::endl;
    std::cout << "Number of repetitions (thr): " << n_reps_thr << std::endl;
    std::cout << "Number of repetitions (lat): " << n_reps_lat << std::endl;
    std::cout << "Starting transfer size: " << input_buffer_size << std::endl;
    std::cout << "Ending transfer size: " << max_size << std::endl << std::endl;

    // ---------------------------------------------------------------
    // Init 
    // ---------------------------------------------------------------

    // Handles
    std::vector<std::unique_ptr<cThread<std::any>>> cthread; // Coyote threads
    void* hMem[n_regions];
    
    // Obtain resources
    for (int i = 0; i < n_regions; i++) {
        cthread.emplace_back(new cThread<std::any>(i, getpid(), cs_dev));
        hMem[i] = mapped ? (cthread[i]->getMem({huge ? CoyoteAlloc::HPF : CoyoteAlloc::REG, input_buffer_size})) 
                        : (huge ? (mmap(NULL, input_buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0))
                                : (malloc(input_buffer_size)));
    }

    sgEntry sg[n_regions];
    memset(&sg[0], 0, sizeof(localSg));
    memset(&sg[1], 0, sizeof(localSg));

    sg[0].local.src_addr = hMem[0]; sg[0].local.src_len = curr_size; sg[0].local.src_stream = stream;
    sg[0].local.dst_addr = hMem[0]; sg[0].local.dst_len = curr_size; sg[0].local.dst_stream = stream;

    sg[1].local.src_addr = hMem[1]; sg[1].local.src_len = curr_size; sg[1].local.src_stream = stream;
    sg[1].local.dst_addr = hMem[1]; sg[1].local.dst_len = curr_size; sg[1].local.dst_stream = stream;

    // from vFPGA 0 to vFPGA 1
    sg[0].local.offset_r = 0;
    sg[0].local.offset_w = 6;
    sg[1].local.offset_r = 6;
    sg[1].local.offset_w = 0;
    cthread[0]->ioSwitch(IODevs::Inter_2_TO_CEU_1);
    cthread[0]->ioSwDbg();
    cthread[1]->ioSwitch(IODevs::Inter_2_TO_HOST_1);
    cthread[1]->ioSwDbg();

    cthread[0]->memCap(MemCapa::BASE_ADDRESS, MemCapa::END_ADDRESS, MemCapa::ALL_PASS);
    cthread[1]->memCap(MemCapa::BASE_ADDRESS, MemCapa::END_ADDRESS, MemCapa::ALL_PASS);

    std::vector<float> audio_data(complex_size, 0.0f);
    // Generate realistic audio patterns
    generateCompressibleAudio(audio_data.data(), max_size);

    // for (uint32_t i = 0; i < min(32U, max_size); ++i) {
    //     audio_data[2*i] = generateSineValue(i);      // Real part
    //     audio_data[2*i + 1] = 0.0f;                  // Imaginary part                 
    //     std::cout << std::fixed << std::setprecision(6) 
    //                 << audio_data[2*i] << " " << audio_data[2*i + 1] << " ";
    //     if ((i + 1) % 4 == 0) std::cout << "\n";
    // }
    
    // // Generate remaining data for larger sizes
    // for (uint32_t i = 32; i < max_size; ++i) {
    //     audio_data[2*i] = generateSineValue(i);      // Real part
    //     audio_data[2*i + 1] = 0.0f;                  // Imaginary part
    // }

    memcpy(hMem[0], audio_data.data(), input_buffer_size);

    curr_size = 2 * defMinSize * sizeof(float);
    max_size = 2 * defMaxSize * sizeof(float);

    // exit(1);
    // ---------------------------------------------------------------
    // Runs 
    // ---------------------------------------------------------------
    cBench bench(nBenchRuns);
    uint32_t n_runs;

    sgFlags sg_flags = { true, true, false };

    PR_HEADER("PERF HOST");
    while(curr_size <= max_size) {
        sg[0].local.src_len = curr_size;
        sg[0].local.dst_len = curr_size;
        sg[1].local.src_len = curr_size;
        sg[1].local.dst_len = curr_size / 4;

        // Latency test
        auto benchmark_lat = [&]() {
            // Transfer the data
            for(int i = 0; i < n_reps_lat; i++) {
                cthread[0]->invoke(CoyoteOper::LOCAL_TRANSFER, &sg[0], {true, true, false});
                cthread[1]->invoke(CoyoteOper::LOCAL_TRANSFER, &sg[1], {true, true, false});
                while(cthread[1]->checkCompleted(CoyoteOper::LOCAL_WRITE) != 1) 
                    if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");          

            }
        };

        bench.runtime(benchmark_lat);

        std::cout << "Size: " << std::setw(8) << curr_size << ", lat: " << std::setw(8) << bench.getAvg() / (n_reps_lat) << " ns" << std::endl;

        curr_size *= 2;
    }

    std::cout << std::endl;

    // for (int j = 0; j < n_regions; j++) {
    //     std::cout << "Data for vFPGA " << j << std::endl;
    //     for (int i = 0; i < 32 / 8; i++) {
    //         std::cout << "Number " << i << ": " << ((uint32_t *)hMem[j])[i] << std::endl;
    //     }
    // }
    // ---------------------------------------------------------------
    // Release 
    // ---------------------------------------------------------------
    
    // Print status
    for (int i = 0; i < n_regions; i++) {
        if(!mapped) {
            if(!huge) free(hMem[i]);
            else      munmap(hMem[i], max_size);  
        }
        cthread[i]->printDebug();
    }
    
    return EXIT_SUCCESS;
}

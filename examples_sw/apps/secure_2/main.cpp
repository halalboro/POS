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
constexpr auto const defMinSize = 2 * 1024 *1024;
constexpr auto const defMaxSize = 2 * 1024 * 1024;
constexpr auto const nBenchRuns = 1;

// FIXED: AES test pattern from working individual test
constexpr uint8_t test_plaintext[16] = {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
    'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p'
};


// FIXED: Generate pattern that produces AES-compatible output after RLE compression
void generatePipelineOptimizedPattern(uint8_t* buffer, size_t size) {
    memset(buffer, 0, size);
    
    std::cout << "Generating pipeline-optimized pattern for RLE + AES:" << std::endl;
    
    // Strategy: Create input that after RLE compression will match AES test format
    // Goal: RLE output should be 'abcdefghijklmnop' pattern that AES expects
    
    // FIXED: Each 64-byte input chunk should compress to 16 bytes of 'abcdefghijklmnop'
    for (size_t chunk = 0; chunk < size / 64; chunk++) {
        size_t chunk_offset = chunk * 64;
        
        // Fill each 64-byte chunk with pattern that compresses to 'abcdefghijklmnop'
        // Strategy: Repeat each character 4 times for 4:1 compression
        for (int i = 0; i < 16; i++) {
            char target_char = test_plaintext[i];  // 'a' through 'p'
            
            // Repeat each character 4 times within the chunk
            for (int repeat = 0; repeat < 4; repeat++) {
                buffer[chunk_offset + i * 4 + repeat] = target_char;
            }
        }
    }
    
    // Display pattern info
    size_t num_chunks = size / 64;
    
    std::cout << "Input pattern (per 64-byte chunk): ";
    if (size >= 64) {
        for (size_t i = 0; i < 64; i++) {
            std::cout << static_cast<char>(buffer[i]);
        }
    }
    if (num_chunks > 1) {
        std::cout << "... (repeats for " << num_chunks << " chunks)";
    }
    std::cout << " (total: " << size << " bytes)" << std::endl;
    
    std::cout << "Expected RLE compression: aaaabbbbccccdddd...pppp → abcdefghijklmnop (4:1 ratio)" << std::endl;
    std::cout << "Expected AES input format: Matches working AES test ('abcdefghijklmnop')" << std::endl;
    std::cout << "Pipeline flow: Input → RLE → AES-compatible format → AES encryption" << std::endl;
}


// Generate TRUE 4:1 compression pattern
void generateStreamingRLEPattern(uint8_t* buffer, size_t size) {
    memset(buffer, 0, size);

    // Pattern: Each character repeated 4 times for true 4:1 compression
    // 64 bytes input: AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHHIIIIJJJJKKKKLLLLMMMMNNNNOOOOPPPP
    // 16 bytes output: ABCDEFGHIJKLMNOP (true 4:1 compression!)

    for (size_t pos = 0; pos < size; pos++) {
        // Each character appears 4 times in sequence, cycling through A-P (16 chars)
        char base_char = 'A' + ((pos / 4) % 16);  // A-P, each repeated 4 times
        buffer[pos] = base_char;
    }

    // Display pattern info
    size_t num_chunks = (size + 63) / 64;

    std::cout << "Generated TRUE 4:1 RLE pattern: ";
    if (size <= 64) {
        for (size_t i = 0; i < size; i++) {
            std::cout << static_cast<char>(buffer[i]);
        }
    } else {
        // Show first 64 chars to illustrate the pattern
        for (size_t i = 0; i < 64; i++) {
            std::cout << static_cast<char>(buffer[i]);
        }
        if (size > 64) {
            std::cout << "... (pattern repeats for " << num_chunks << " chunks)";
        }
    }
    std::cout << " (total: " << size << " bytes)" << std::endl;
    std::cout << "Expected compression: AAAABBBBCCCC...PPPP → ABCDEFGHIJKLMNOP (TRUE 4:1 ratio)" << std::endl;
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
    std::cout << "Starting transfer size: " << curr_size << std::endl;
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
        hMem[i] = mapped ? (cthread[i]->getMem({huge ? CoyoteAlloc::HPF : CoyoteAlloc::REG, max_size})) 
                        : (huge ? (mmap(NULL, max_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0))
                                : (malloc(max_size)));
    }

    sgEntry sg[n_regions];
    memset(&sg[0], 0, sizeof(localSg));
    memset(&sg[1], 0, sizeof(localSg));

    sg[0].local.src_addr = hMem[0]; sg[0].local.src_len = curr_size; sg[0].local.src_stream = stream;
    sg[0].local.dst_addr = hMem[0]; sg[0].local.dst_len = curr_size / 4; sg[0].local.dst_stream = stream;

    sg[1].local.src_addr = hMem[1]; sg[1].local.src_len = curr_size / 4; sg[1].local.src_stream = stream;
    sg[1].local.dst_addr = hMem[1]; sg[1].local.dst_len = curr_size / 4; sg[1].local.dst_stream = stream;

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

    // for secure
    // generatePipelineOptimizedPattern((uint8_t *)hMem[0], curr_size);
    // for secure_2
    generateStreamingRLEPattern((uint8_t *)hMem[0], curr_size);

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
        sg[0].local.dst_len = curr_size / 4;
        sg[1].local.src_len = curr_size / 4;
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
 
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

#include "xsvm_speech_30.h"  

#include "cBench.hpp"
#include "cThread.hpp"

#define EN_THR_TESTS
#define EN_LAT_TESTS

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

constexpr auto const defSThread = 0;
constexpr auto const defNThreads = 1;
constexpr auto const defHuge = true;
constexpr auto const defMappped = true;
constexpr auto const nRepsThr = 128;
constexpr auto const defSize =  1 * 1024 * 1024;
constexpr auto const nBenchRuns = 1;

/* Test vectors */
constexpr auto const keyLow     = 0xabf7158809cf4f3c;
constexpr auto const keyHigh    = 0x2b7e151628aed2a6;
constexpr auto const ivLow      = 0x08090A0B0C0D0E0F;
constexpr auto const ivHigh     = 0x0001020304050607;
constexpr auto const plainLow   = 0xe93d7e117393172a;
constexpr auto const plainHigh  = 0x6bc1bee22e409f96;
constexpr auto const cipherLow  = 0xcee98e9b12e9197d;
constexpr auto const cipherHigh = 0x7649abac8119b246;

// SVM test data array
float test_data[32] = {
    -0.058221419, -0.382977810,  0.150129928,  1.920313787,
    0.901883048,  0.583552208,  0.379287700,  0.300380055,
    0.262813529,  0.129100603,  0.092419174,  0.090575007,
    0.107920264,  0.054621646, -0.059556407, -0.100196335,
    -0.137246172, -0.193750437, -0.170737782, -0.043867099,
    -0.004848041, -0.013693800, -0.015801475, -0.037138655,
    -0.003244255,  0.002615051, -0.010200073, -0.008551353,
    -0.008136410, -0.004720697, -0.010866166, -0.010033955
};

// SVM Processing function
float processSvm(XSvm_speech_30 *svmInst, float* data) {
    // Set all 32 inputs
    XSvm_speech_30_Set_in1(svmInst, float_to_u32(data[0]));
    XSvm_speech_30_Set_in2(svmInst, float_to_u32(data[1]));
    XSvm_speech_30_Set_in3(svmInst, float_to_u32(data[2]));
    XSvm_speech_30_Set_in4(svmInst, float_to_u32(data[3]));
    XSvm_speech_30_Set_in5(svmInst, float_to_u32(data[4]));
    XSvm_speech_30_Set_in6(svmInst, float_to_u32(data[5]));
    XSvm_speech_30_Set_in7(svmInst, float_to_u32(data[6]));
    XSvm_speech_30_Set_in8(svmInst, float_to_u32(data[7]));
    XSvm_speech_30_Set_in9(svmInst, float_to_u32(data[8]));
    XSvm_speech_30_Set_in10(svmInst, float_to_u32(data[9]));
    XSvm_speech_30_Set_in11(svmInst, float_to_u32(data[10]));
    XSvm_speech_30_Set_in12(svmInst, float_to_u32(data[11]));
    XSvm_speech_30_Set_in13(svmInst, float_to_u32(data[12]));
    XSvm_speech_30_Set_in14(svmInst, float_to_u32(data[13]));
    XSvm_speech_30_Set_in15(svmInst, float_to_u32(data[14]));
    XSvm_speech_30_Set_in16(svmInst, float_to_u32(data[15]));
    XSvm_speech_30_Set_in17(svmInst, float_to_u32(data[16]));
    XSvm_speech_30_Set_in18(svmInst, float_to_u32(data[17]));
    XSvm_speech_30_Set_in19(svmInst, float_to_u32(data[18]));
    XSvm_speech_30_Set_in20(svmInst, float_to_u32(data[19]));
    XSvm_speech_30_Set_in21(svmInst, float_to_u32(data[20]));
    XSvm_speech_30_Set_in22(svmInst, float_to_u32(data[21]));
    XSvm_speech_30_Set_in23(svmInst, float_to_u32(data[22]));
    XSvm_speech_30_Set_in24(svmInst, float_to_u32(data[23]));
    XSvm_speech_30_Set_in25(svmInst, float_to_u32(data[24]));
    XSvm_speech_30_Set_in26(svmInst, float_to_u32(data[25]));
    XSvm_speech_30_Set_in27(svmInst, float_to_u32(data[26]));
    XSvm_speech_30_Set_in28(svmInst, float_to_u32(data[27]));
    XSvm_speech_30_Set_in29(svmInst, float_to_u32(data[28]));
    XSvm_speech_30_Set_in30(svmInst, float_to_u32(data[29]));
    XSvm_speech_30_Set_in31(svmInst, float_to_u32(data[30]));
    XSvm_speech_30_Set_in32(svmInst, float_to_u32(data[31]));

    // Start processing
    XSvm_speech_30_Start(svmInst);
    while (!XSvm_speech_30_IsDone(svmInst));

    // Get result
    return u32_to_float(XSvm_speech_30_Get_return(svmInst));
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
        ("sthread,t", boost::program_options::value<uint32_t>(), "Thread number (starting)")
        ("nthreads,n", boost::program_options::value<uint32_t>(), "Number of threads")
        ("hugepages,h", boost::program_options::value<bool>(), "Hugepages")
        ("mapped,m", boost::program_options::value<bool>(), "Mapped / page fault")
        ("reps,r", boost::program_options::value<uint32_t>(), "Number of repetitions (throughput)")
        ("size,s", boost::program_options::value<uint32_t>(), "Transfer size");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    string bstream_path = "";
    uint32_t cs_dev = defDevice; 
    uint32_t s_thread = defSThread;
    uint32_t n_threads = defNThreads;
    bool huge = defHuge;
    bool mapped = defMappped;
    uint32_t n_reps = nRepsThr;
    uint32_t size = defSize;

    if(commandLineArgs.count("bitstream") > 0) { 
        bstream_path = commandLineArgs["bitstream"].as<string>();
        
        std::cout << std::endl << "Shell loading (path: " << bstream_path << ") ..." << std::endl;
        cRnfg crnfg(cs_dev);
        crnfg.shellReconfigure(bstream_path);
    }
    if(commandLineArgs.count("device") > 0) cs_dev = commandLineArgs["device"].as<uint32_t>();
    if(commandLineArgs.count("sthread") > 0) s_thread = commandLineArgs["sthread"].as<uint32_t>();
    if(commandLineArgs.count("nthreads") > 0) n_threads = commandLineArgs["nthreads"].as<uint32_t>();
    if(commandLineArgs.count("hugepages") > 0) huge = commandLineArgs["hugepages"].as<bool>();
    if(commandLineArgs.count("mapped") > 0) mapped = commandLineArgs["mapped"].as<bool>();
    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();
    if(commandLineArgs.count("size") > 0) size = commandLineArgs["size"].as<uint32_t>();

    PR_HEADER("PARAMS");
    std::cout << "Number of threads: " << n_threads << std::endl;
    std::cout << "Hugepages: " << huge << std::endl;
    std::cout << "Mapped pages: " << mapped << std::endl;
    std::cout << "Transfer size per thread: " << n_reps << " x "  << size << " B" << std::endl;

    // Initialize SVM instances for each thread
    std::vector<XSvm_speech_30> svmInsts(n_threads);
    for(int i = 0; i < n_threads; i++) {
        XSvm_speech_30_Config *svmConfig;
        svmConfig = XSvm_speech_30_LookupConfig(XPAR_SVM_SPEECH_30_0_DEVICE_ID + i);
        if (XSvm_speech_30_CfgInitialize(&svmInsts[i], svmConfig) != XST_SUCCESS) {
            std::cout << "Error initializing SVM instance " << i << std::endl;
            return -1;
        }
    }

    // ---------------------------------------------------------------
    // Init 
    // ---------------------------------------------------------------

    // Handles
    std::vector<std::unique_ptr<cThread<std::any>>> cthread; // Coyote threads
    void* hMem[n_threads];
    
    // Obtain resources
    for (int i = 0; i < n_threads; i++) {
        cthread.emplace_back(new cThread<std::any>(targetVfid, getpid(), cs_dev));
        hMem[i] = mapped ? (cthread[i]->getMem({huge ? CoyoteAlloc::HPF : CoyoteAlloc::REG, size})) 
                        : (huge ? (mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0))
                                : (malloc(size)));
                                
        // Copy test data to each thread's memory
        memcpy(hMem[i], test_data, 32 * sizeof(float));
    }

    sgEntry sg[n_threads];
    sgFlags sg_flags[n_threads];

    // Prep SG
    for(int i = 0; i < n_threads; i++) {
        // SG entries
        memset(&sg[i], 0, sizeof(localSg));
        sg[i].local.src_addr = hMem[i]; // Read
        sg[i].local.src_len = 32 * sizeof(float);
        sg[i].local.src_stream = strmHost;
        sg[i].local.src_dest = i + s_thread;

        sg[i].local.dst_addr = hMem[i]; // Write
         sg[i].local.dst_len = sizeof(float); 
        sg[i].local.dst_stream = strmHost;
        sg[i].local.dst_dest = i + s_thread;
    }

    // ---------------------------------------------------------------
    // Runs 
    // ---------------------------------------------------------------
    cBench bench(nBenchRuns);
    uint32_t n_runs;

    PR_HEADER("SVM MULTITHREADING");
               
    // Prep for throughput test
    for(int i = 0; i < n_threads; i++) {
        cthread[i]->clearCompleted();
        sg_flags[i] = { false, false, false };
    }
    n_runs = 0;

    // Keys
 /*  for(int i = 0; i < n_threads; i++) {
        cthread[i]->setCSR(keyLow, 0);
        cthread[i]->setCSR(keyHigh, 1);
        cthread[i]->setCSR(i + s_thread, 2);
        cthread[i]->setCSR(ivLow, 3);
        cthread[i]->setCSR(ivHigh, 4);
        /*
        for(int j = 0; j < size/8; j+=2) {
            ((uint64_t*) hMem[i])[j] = plainLow;
            ((uint64_t*) hMem[i])[j+1] = plainHigh;
        }
        
    }
*/
    // Throughput test
    auto benchmark_thr = [&]() {
        bool k = false;
        n_runs++;

        // Process SVM for each thread
        for(int i = 0; i < n_reps; i++) {
            for(int j = 0; j < n_threads; j++) {
                float* threadData = static_cast<float*>(hMem[j]);
                float result = processSvm(&svmInsts[j], threadData);
                
                // Store result back
                *static_cast<float*>(sg[j].local.dst_addr) = result;

                if(i == n_reps-1) sg_flags[j].last = true;
                cthread[j]->invoke(CoyoteOper::LOCAL_TRANSFER, &sg[j], sg_flags[j]);
            }
        }

        // Wait for completion
        while(!k) {
            k = true;
            for(int i = 0; i < n_threads; i++) 
                if(cthread[i]->checkCompleted(CoyoteOper::LOCAL_TRANSFER) != n_runs) k = false;
            if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");
        }  

        // Print results
        for(int i = 0; i < n_threads; i++) {
            float result = *static_cast<float*>(hMem[i]);
            std::cout << "Thread " << i << " SVM Result: " << result << std::endl;
        }
    }
    

    bench.runtime(benchmark_thr);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Throughput: " << 
        std::setw(8) << ((1000.0 * (double)n_threads * (double)n_reps * (double)size) / (bench.getAvg())) << " MB/s" << std::endl << std::endl;

    
    // ---------------------------------------------------------------
    // Release 
    // ---------------------------------------------------------------
    
    // Print status
    for (int i = 0; i < n_threads; i++) {
        if(!mapped) {
            if(!huge) free(hMem[i]);
            else      munmap(hMem[i], size);  
        }
    }
    cthread[0]->printDebug();
    
    return EXIT_SUCCESS;
}

/**
 * Copyright (c) 2021, Systems Group, ETH Zurich
 * All rights reserved.
 *
<<<<<<< HEAD
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
=======
 * Audio Processing Pipeline - Converted to use ushell API
>>>>>>> 5bb12959 (applications--scripts--API)
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
<<<<<<< HEAD
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
=======
#include <signal.h>
#include <boost/program_options.hpp>
#include <any>
#include <random>
#include <cmath>

// Include our high-level ushell API
#include "dfg.hpp"
#include "ushell.hpp"
>>>>>>> 5bb12959 (applications--scripts--API)

using namespace std;
using namespace std::chrono;
using namespace fpga;
<<<<<<< HEAD
=======
using namespace ushell;
>>>>>>> 5bb12959 (applications--scripts--API)

/* Signal handler */
std::atomic<bool> stalled(false); 
void gotInt(int) {
    stalled.store(true);
}

<<<<<<< HEAD
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

=======
/* Default parameters */
constexpr auto const defDevice = 0;
constexpr auto const nRegions = 2;
constexpr auto const defHuge = true;
constexpr auto const defMapped = true;
constexpr auto const defStream = 1;
constexpr auto const nRepsThr = 1;
constexpr auto const nRepsLat = 1;
constexpr auto const defMinSize = 4 * 1024;  // 4K samples
constexpr auto const defMaxSize = 4 * 1024;  // 4K samples
constexpr auto const nBenchRuns = 1;
constexpr float const sampleRate = 44100.0f;

// Generate compressible audio data for quantization testing
>>>>>>> 5bb12959 (applications--scripts--API)
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
        
<<<<<<< HEAD
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
=======
        audio_data[2*sample_idx] = real;        // Real part
        audio_data[2*sample_idx + 1] = 0.0f;    // Imaginary part (zero for real audio)
    }
}

// Helper function to print header
void print_header(const std::string& header) {
    std::cout << "\n-- \033[31m\e[1m" << header << "\033[0m\e[0m" << std::endl;
    std::cout << "-----------------------------------------------" << std::endl;
}

int main(int argc, char *argv[])  
{
    // ---------------------------------------------------------------
    // Signal Handler Setup
    // ---------------------------------------------------------------
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = gotInt;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    // ---------------------------------------------------------------
    // Command Line Arguments
    // ---------------------------------------------------------------
>>>>>>> 5bb12959 (applications--scripts--API)
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

<<<<<<< HEAD
=======
    // Parse arguments with defaults
>>>>>>> 5bb12959 (applications--scripts--API)
    string bstream_path = "";
    uint32_t cs_dev = defDevice; 
    uint32_t n_regions = nRegions;
    bool huge = defHuge;
<<<<<<< HEAD
    bool mapped = defMappped;
=======
    bool mapped = defMapped;
>>>>>>> 5bb12959 (applications--scripts--API)
    bool stream = defStream;
    uint32_t n_reps_thr = nRepsThr;
    uint32_t n_reps_lat = nRepsLat;
    uint32_t curr_size = defMinSize;
    uint32_t max_size = defMaxSize;
<<<<<<< HEAD
    uint32_t n_reps = nRepsLat;

    uint32_t complex_size = 2 * defMaxSize;       // Total floats (real + imag)
    uint32_t input_buffer_size = complex_size * sizeof(float);  // Input bytes
    uint32_t num_ffts = defMaxSize / 32;                 // Number of 32-point FFTs


=======

    // Calculate buffer sizes
    uint32_t complex_size = 2 * defMaxSize;                    // Total floats (real + imag)
    uint32_t input_buffer_size = complex_size * sizeof(float); // Input bytes
    uint32_t output_buffer_size = input_buffer_size / 4;       // Compressed output (25% of input)

    // Process command line arguments
>>>>>>> 5bb12959 (applications--scripts--API)
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

<<<<<<< HEAD
    PR_HEADER("PARAMS");
=======
    print_header("PARAMS");
>>>>>>> 5bb12959 (applications--scripts--API)
    std::cout << "Number of regions: " << n_regions << std::endl;
    std::cout << "Hugepages: " << huge << std::endl;
    std::cout << "Mapped pages: " << mapped << std::endl;
    std::cout << "Streaming: " << (stream ? "HOST" : "CARD") << std::endl;
    std::cout << "Number of repetitions (thr): " << n_reps_thr << std::endl;
    std::cout << "Number of repetitions (lat): " << n_reps_lat << std::endl;
<<<<<<< HEAD
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
=======
    std::cout << "Input buffer size: " << input_buffer_size << " bytes" << std::endl;
    std::cout << "Output buffer size: " << output_buffer_size << " bytes" << std::endl;
    std::cout << "Audio samples: " << max_size << std::endl;

    try {
        // ---------------------------------------------------------------
        // Dataflow Setup using ushell's fluent API
        // ---------------------------------------------------------------
        print_header("DATAFLOW SETUP");
        
        // Create an audio processing dataflow
        Dataflow audio_dataflow("audio_quantization_dataflow");
        
        // Create processing tasks
        Task& audio_preprocessor = audio_dataflow.add_task("audio_preprocessor", "processing");
        Task& quantizer_compressor = audio_dataflow.add_task("quantizer_compressor", "processing");
        
        // Create buffers
        Buffer& audio_input_buffer = audio_dataflow.add_buffer(input_buffer_size, "audio_input_buffer");
        Buffer& intermediate_buffer = audio_dataflow.add_buffer(input_buffer_size, "intermediate_buffer");
        Buffer& compressed_output_buffer = audio_dataflow.add_buffer(output_buffer_size, "compressed_output_buffer");
        
        // Set up the audio processing pipeline using fluent API
        audio_dataflow.to(audio_input_buffer, audio_preprocessor.in)
                     .to(audio_preprocessor.out, intermediate_buffer)
                     .to(intermediate_buffer, quantizer_compressor.in)
                     .to(quantizer_compressor.out, compressed_output_buffer);
        
        std::cout << "Creating audio dataflow:" << std::endl;
        std::cout << "  audio_input_buffer → audio_preprocessor → intermediate_buffer → quantizer_compressor → compressed_output_buffer" << std::endl;
        
        // Check and build the dataflow
        if (!audio_dataflow.check()) {
            throw std::runtime_error("Failed to validate dataflow");
        }
        
        // ---------------------------------------------------------------
        // Audio Data Generation and Buffer Initialization
        // ---------------------------------------------------------------
        print_header("AUDIO DATA GENERATION");
        
        // Generate audio data
        std::vector<float> audio_data(complex_size, 0.0f);
        generateCompressibleAudio(audio_data.data(), max_size);
        
        // Write audio data to input buffer
        write_dataflow_buffer(audio_input_buffer, audio_data.data(), input_buffer_size);
        std::cout << "Initialized audio input buffer with " << max_size << " complex samples" << std::endl;
        
        // ---------------------------------------------------------------
        // Performance Benchmarking
        // ---------------------------------------------------------------
        print_header("AUDIO PROCESSING PERFORMANCE");
        
        // Create benchmark object
        cBench bench(nBenchRuns);
        
        // Convert sizes to bytes for processing
        uint32_t current_byte_size = 2 * curr_size * sizeof(float);  // Complex samples to bytes
        uint32_t max_byte_size = 2 * max_size * sizeof(float);       // Complex samples to bytes
        
        while (current_byte_size <= max_byte_size) {
            audio_dataflow.clear_completed();
            
            auto benchmark_lat = [&]() {
                for (int i = 0; i < n_reps_lat; i++) {
                    audio_dataflow.execute(current_byte_size);
                }
            };
            
            bench.runtime(benchmark_lat);
            
            std::cout << "Size: " << std::setw(8) << current_byte_size 
                      << " bytes, Samples: " << std::setw(6) << (current_byte_size / (sizeof(float) * 2))
                      << ", Latency: " << std::setw(8) << bench.getAvg() / n_reps_lat << " ns" << std::endl;
            
            current_byte_size *= 2;
        }
        
        // ---------------------------------------------------------------
        // Results Verification
        // ---------------------------------------------------------------
        print_header("RESULTS VERIFICATION");
        
        // Read the compressed output for verification
        std::vector<uint8_t> compressed_data(output_buffer_size);
        read_dataflow_buffer(compressed_output_buffer, compressed_data.data(), output_buffer_size);
        
        // Calculate compression statistics
        float compression_ratio = (float)input_buffer_size / (float)output_buffer_size;
        std::cout << "Input size: " << input_buffer_size << " bytes" << std::endl;
        std::cout << "Compressed size: " << output_buffer_size << " bytes" << std::endl;
        std::cout << "Compression ratio: " << std::fixed << std::setprecision(2) 
                  << compression_ratio << ":1" << std::endl;
        
        // ---------------------------------------------------------------
        // Resource Cleanup (Automatic with RAII)
        // ---------------------------------------------------------------
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    print_header("AUDIO PROCESSING COMPLETE");
    std::cout << "Audio quantization and compression dataflow executed successfully!" << std::endl;
    
    return EXIT_SUCCESS;
}
>>>>>>> 5bb12959 (applications--scripts--API)

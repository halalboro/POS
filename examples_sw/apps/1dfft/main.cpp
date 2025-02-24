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
#include <random>
#include <vector>
#include "cBench.hpp"
#include "cThread.hpp"

using namespace std;
using namespace std::chrono;
using namespace fpga;

std::atomic<bool> stalled(false); 
void gotInt(int) {
    stalled.store(true);
}

constexpr auto const defDevice = 0;

constexpr auto const nRegions = 3;
constexpr auto const defHuge = true;
constexpr auto const defMappped = true;
constexpr auto const defStream = 1;
constexpr auto const nRepsThr = 1;
constexpr auto const nRepsLat = 1;
// constexpr auto const defMinSize = 1 * 1024;
// constexpr auto const defMaxSize = 1 * 1024 * 1024;
constexpr auto const nBenchRuns = 1;

constexpr auto const targetVfid = 0;  
constexpr auto const defReps = 1;
constexpr auto const defSize = 16384;
constexpr auto const defDW = 4;
constexpr auto const accumulateSize = 512;  
constexpr auto const outputSize = defSize / accumulateSize;
constexpr float const sampleRate = 44100.0f;  // Sample rate in Hz

// Generate sine wave value with improved frequency control
float generateSineValue(int index, int totalPoints) {
    const float amplitude = 1000.0f;
    const float frequency = 256.0f;  // Hz
    const float phase = 0.0f;
    
    float t = (float)index / sampleRate;  // Time in seconds
    return amplitude * sin(2.0f * M_PI * frequency * t + phase);
}

// Structure to hold complex numbers
struct Complex {
    float real;
    float imag;
    
    Complex() : real(0.0f), imag(0.0f) {}
    Complex(float r, float i) : real(r), imag(i) {}
    
    float magnitude() const {
        return std::sqrt(real * real + imag * imag);
    }
    
    Complex& operator+=(const Complex& other) {
        real += other.real;
        imag += other.imag;
        return *this;
    }
    
    Complex& operator/=(float div) {
        real /= div;
        imag /= div;
        return *this;
    }
};

// Improved accumulation function
void accumulateFFTOutput(float* input, float* output, int size, int accumulateSize) {
    const int numBins = size / accumulateSize;
    std::vector<float> sumMagnitudes(numBins, 0.0f);
    std::vector<int> countPerBin(numBins, 0);
    
    // First pass: calculate average magnitude per bin
    for(int i = 0; i < size/2; i++) {  // Only process up to Nyquist frequency
        int accIndex = i / accumulateSize;
        if(accIndex >= numBins) break;
        
        float real = input[2*i];
        float imag = input[2*i+1];
        float mag = std::sqrt(real * real + imag * imag);
        
        sumMagnitudes[accIndex] += mag;
        countPerBin[accIndex]++;
    }
    
    // Calculate scaled averages with improved scaling
    const float baseScaling = 100.0f;
    for(int i = 0; i < numBins; i++) {
        if(countPerBin[i] > 0) {
            float avgMagnitude = sumMagnitudes[i] / countPerBin[i];
            // Apply logarithmic scaling for better dynamic range
            output[i] = baseScaling * std::log10(1.0f + avgMagnitude);
        } else {
            output[i] = 0.0f;
        }
    }
}

// Function to calculate and print frequency analysis
void printFrequencyAnalysis(float* output, int numBins, float sampleRate) {
    std::cout << "\nFrequency Analysis:\n";
    std::cout << "Bin\tFreq Range (Hz)\tMagnitude\n";
    std::cout << "--------------------------------\n";
    
    float binWidth = (sampleRate/2) / numBins;
    
    for(int i = 0; i < numBins; i++) {
        float startFreq = i * binWidth;
        float endFreq = (i + 1) * binWidth;
        std::cout << i << "\t" 
                  << std::fixed << std::setprecision(1) 
                  << startFreq << "-" << endFreq << "\t\t"
                  << std::setprecision(2) << output[i] << "\n";
    }
}

// Process and print FFT results
void processAndPrintFFTResults(float* output_ptr, int size, int accumulateSize) {
    // Print raw FFT around interesting region
    std::cout << "\nRaw FFT values around peak region (indices 15-25):\n";
    for(int j = 15; j < 25; j++) {
        std::cout << "Bin " << std::setw(2) << j << ": (" 
                 << std::setw(10) << std::fixed << std::setprecision(6) << output_ptr[2*j]
                 << ", " << std::setw(10) << output_ptr[2*j+1] << "i) "
                 << "mag: " << std::sqrt(output_ptr[2*j]*output_ptr[2*j] + 
                                       output_ptr[2*j+1]*output_ptr[2*j+1]) << "\n";
    }
    
    const int numBins = size / accumulateSize;
    std::vector<float> accumulated(numBins, 0.0f);
    
    // Accumulate FFT output with improved scaling
    accumulateFFTOutput(output_ptr, accumulated.data(), size, accumulateSize);
    
    // Copy accumulated values back to output buffer
    memcpy(output_ptr, accumulated.data(), numBins * sizeof(float));
    
    // Print frequency analysis
    printFrequencyAnalysis(output_ptr, numBins, sampleRate);
}

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
        ("repsl,l", boost::program_options::value<uint32_t>(), "Number of repetitions (latency)");

    
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

    uint32_t size = defSize;
    uint32_t n_reps = nRepsLat;
    uint32_t buffer_size = 2 * size * sizeof(float);
    uint32_t n_pages = (buffer_size + hugePageSize - 1) / hugePageSize;
    uint32_t curr_size = buffer_size;

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

    PR_HEADER("PARAMS");
    std::cout << "Number of regions: " << n_regions << std::endl;
    std::cout << "Hugepages: " << huge << std::endl;
    std::cout << "Mapped pages: " << mapped << std::endl;
    std::cout << "Streaming: " << (stream ? "HOST" : "CARD") << std::endl;
    std::cout << "Number of repetitions (thr): " << n_reps_thr << std::endl;
    std::cout << "Number of repetitions (lat): " << n_reps_lat << std::endl;
    std::cout << "Starting transfer size: " << curr_size << std::endl;


    // ---------------------------------------------------------------
    // Init 
    // ---------------------------------------------------------------

    // Handles
    std::vector<std::unique_ptr<cThread<std::any>>> cthread; // Coyote threads
    // void* hMem[n_regions];
    
    std::vector<float*> input_buffers(n_regions, nullptr);
    std::vector<float*> output_buffers(n_regions, nullptr);

    std::vector<float> test_data(size);

    // Generate test data
    std::cout << "\nFirst 32 input values:\n";
    for (int i = 0; i < size; ++i) {
        test_data[i] = generateSineValue(i, size);
        if (i < 32) {
            std::cout << std::fixed << std::setprecision(6) << test_data[i] << " ";
            if ((i + 1) % 8 == 0) std::cout << "\n";
        }
    }
    std::cout << "\n";


    // Obtain resources
    for (int i = 0; i < n_regions; i++) {
        cthread.emplace_back(new cThread<std::any>(i, getpid(), cs_dev));
        input_buffers[i] = (float*) cthread[i]->getMem({CoyoteAlloc::HPF, n_pages});
        output_buffers[i] = (float*) cthread[i]->getMem({CoyoteAlloc::HPF, n_pages});
        // hMem[i] = mapped ? (cthread[i]->getMem({huge ? CoyoteAlloc::HPF : CoyoteAlloc::REG, n_pages})) 
        //                  : (huge ? (mmap(NULL, n_pages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0))
        //                          : (malloc(n_pages)));
        if (!input_buffers[i] || !output_buffers[i]) {
            throw std::runtime_error("Memory allocation failed");
        }
        memcpy(input_buffers[i], test_data.data(), size * sizeof(float));
        memset(output_buffers[i], 0, buffer_size);
    }

    sgEntry sg[n_regions];

    for(int i = 0; i < n_regions; i++) {
        // SG entries
        memset(&sg[i], 0, sizeof(localSg));
        sg[i].local.src_addr = input_buffers[i]; sg[i].local.src_len = buffer_size; sg[i].local.src_stream = stream;
        sg[i].local.dst_addr = input_buffers[i]; sg[i].local.dst_len = buffer_size; sg[i].local.dst_stream = stream;
    }

    // direct connection for each vFPGA


    // for cyt_top_dtu_3_0122
    sg[0].local.offset_r = 0;
    sg[0].local.offset_w = 0;
    cthread[0]->ioSwitch(IODevs::Inter_3_TO_HOST_0);
    cthread[0]->ioSwDbg();
    sg[1].local.offset_r = 0;
    sg[1].local.offset_w = 0;
    cthread[1]->ioSwitch(IODevs::Inter_3_TO_HOST_1);
    cthread[1]->ioSwDbg();
    sg[2].local.offset_r = 0;
    sg[2].local.offset_w = 0;
    cthread[2]->ioSwitch(IODevs::Inter_3_TO_HOST_2);
    cthread[2]->ioSwDbg();


    cBench bench(nBenchRuns);
    uint32_t n_runs;

    PR_HEADER("FFT PROCESSING");
    while(curr_size <= buffer_size) {

        // Prep for latency test
        for(int i = 0; i < n_regions; i++) {
            cthread[i]->clearCompleted();
            sg[i].local.src_len = curr_size; sg[i].local.dst_len = curr_size;
        }
        n_runs = 0;

        // Latency test
        auto benchmark_lat = [&]() {
            // Transfer the data
            for(int i = 0; i < n_reps_lat; i++) {
                for(int j = 0; j < 1; j++) {
                    cthread[j]->invoke(CoyoteOper::LOCAL_TRANSFER, &sg[j], {true, true, false});
                    while(cthread[j]->checkCompleted(CoyoteOper::LOCAL_WRITE) != 1) 
                        if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");           
                }
            }
        };
        bench.runtime(benchmark_lat);

        std::cout << "Size: " << std::setw(8) << curr_size << ", lat: " << std::setw(8) << bench.getAvg() / (n_reps_lat) << " ns" << std::endl;
        std::cout << "size: " << curr_size << std::endl;

        curr_size *= 2;
    }


    // Cleanup
    for(int i = 0; i < n_reps; i++) {
        cthread[i]->printDebug();
        if(input_buffers[i]) {
            cthread[i]->freeMem(input_buffers[i]);
            input_buffers[i] = nullptr;
        }
        if(output_buffers[i]) {
            cthread[i]->freeMem(output_buffers[i]);
            output_buffers[i] = nullptr;
        }
    }

    return EXIT_SUCCESS;
}



// int main(int argc, char *argv[]) {
//     struct sigaction sa;
//     memset(&sa, 0, sizeof(sa));
//     sa.sa_handler = gotInt;
//     sigfillset(&sa.sa_mask);
//     sigaction(SIGINT, &sa, NULL);

//     boost::program_options::options_description programDescription("Options:");
//     programDescription.add_options()
//         ("reps,r", boost::program_options::value<uint32_t>(), "Number of reps")
//         ("size,s", boost::program_options::value<uint32_t>(), "FFT size")
//         ("freq,f", boost::program_options::value<float>(), "Input signal frequency (Hz)");
    
//     boost::program_options::variables_map commandLineArgs;
//     boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), commandLineArgs);
//     boost::program_options::notify(commandLineArgs);

//     uint32_t size = defSize;
//     uint32_t n_reps = defReps;
    
//     if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();
//     if(commandLineArgs.count("size") > 0) size = commandLineArgs["size"].as<uint32_t>();

//     uint32_t buffer_size = 2 * size * sizeof(float);
//     uint32_t n_pages = (buffer_size + hugePageSize - 1) / hugePageSize;

//     PR_HEADER("PARAMS");
//     std::cout << "vFPGA ID: " << targetVfid << std::endl;
//     std::cout << "Number of allocated pages per run: " << n_pages << std::endl;
//     std::cout << "FFT size: " << size << std::endl;
//     std::cout << "Number of reps: " << n_reps << std::endl;
//     std::cout << "Sample rate: " << sampleRate << " Hz" << std::endl;
//     std::cout << "Starting transfer size: " << buffer_size << std::endl;
//     std::cout << "sizeof(float): " << sizeof(float) << std::endl;


//     try {
//         std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
//         cthread->start();
        
//         std::vector<float*> input_buffers(n_reps, nullptr);
//         std::vector<float*> output_buffers(n_reps, nullptr);

//         std::vector<float> test_data(size);

//         // Generate test data
//         std::cout << "\nFirst 32 input values:\n";
//         for (int i = 0; i < size; ++i) {
//             test_data[i] = generateSineValue(i, size);
//             if (i < 32) {
//                 std::cout << std::fixed << std::setprecision(6) << test_data[i] << " ";
//                 if ((i + 1) % 8 == 0) std::cout << "\n";
//             }
//         }
//         std::cout << "\n";

//         // Allocate and initialize memory
//         for(int i = 0; i < n_reps; i++) {
//             input_buffers[i] = (float*) cthread->getMem({CoyoteAlloc::HPF, n_pages});
//             output_buffers[i] = (float*) cthread->getMem({CoyoteAlloc::HPF, n_pages});
            
//             if (!input_buffers[i] || !output_buffers[i]) {
//                 throw std::runtime_error("Memory allocation failed");
//             }

//             memcpy(input_buffers[i], test_data.data(), size * sizeof(float));
//             memset(output_buffers[i], 0, buffer_size);
//         }

//         sgEntry sg;
//         sgFlags sg_flags = { true, true, false };

//         cBench bench(n_reps);
//         PR_HEADER("FFT PROCESSING");

//         cthread->clearCompleted();
//         cthread->ioSwitch(IODevs::Inter_3_TO_HOST_0);
//         cthread->ioSwDbg();
        
//         auto benchmark_thr = [&]() {
//             for(int i = 0; i < n_reps; i++) {
//                 memset(&sg, 0, sizeof(localSg));
                
//                 sg.local.src_addr = input_buffers[i];
//                 sg.local.src_len = buffer_size;
//                 sg.local.src_stream = strmHost;
//                 // sg.local.src_dest = targetVfid;

//                 sg.local.dst_addr = output_buffers[i];
//                 sg.local.dst_len = buffer_size;
//                 sg.local.dst_stream = strmHost;
//                 // sg.local.dst_dest = targetVfid;

//                 sg.local.offset_r = 0;
//                 sg.local.offset_w = 0;
                
//                 sg_flags.last = (i == n_reps-1);
                
//                 cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, sg_flags);
//             }

//             while(cthread->checkCompleted(CoyoteOper::LOCAL_WRITE) != 1) {
//                 if(stalled.load()) throw std::runtime_error("Stalled");
//             }
//         };

//         bench.runtime(benchmark_thr);

//         std::cout << std::fixed << std::setprecision(2);
//         std::cout << "Size: " << std::setw(8) << size << ", throughput: " 
//                   << std::setw(8) << (1000 * buffer_size) / (bench.getAvg() / n_reps) 
//                   << " MB/s" << std::endl;

//         // Process FFT results with improved analysis
//         for(int i = 0; i < n_reps; i++) {
//             processAndPrintFFTResults(output_buffers[i], size, accumulateSize);
//         }

//         cthread->printDebug();

//         // Cleanup
//         for(int i = 0; i < n_reps; i++) {
//             if(input_buffers[i]) {
//                 cthread->freeMem(input_buffers[i]);
//                 input_buffers[i] = nullptr;
//             }
//             if(output_buffers[i]) {
//                 cthread->freeMem(output_buffers[i]);
//                 output_buffers[i] = nullptr;
//             }
//         }

//     } catch (const std::exception& e) {
//         std::cerr << "Error: " << e.what() << std::endl;
//         return EXIT_FAILURE;
//     }
    
//     return EXIT_SUCCESS;
// }
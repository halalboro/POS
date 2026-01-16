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

/* Default parameters */
constexpr auto const defDevice = 0;
constexpr auto const targetVfid = 0;  
constexpr auto const defSize = 64;
constexpr auto const maxSize = 1024 * 1024;
constexpr auto const defHuge = true;   
constexpr auto const defMapped = true; 
constexpr auto const defStream = 1;    

// Generate 4:1 compression pattern
void generateRLEPattern(uint8_t* buffer, size_t size) {
    memset(buffer, 0, size);
    
    for (size_t pos = 0; pos < size; pos++) {
        char base_char = 'A' + ((pos / 4) % 16);  // A-P, each repeated 4 times
        buffer[pos] = base_char;
    }
}

// Analyze RLE output
void analyzeRLEOutput(uint8_t* buffer, size_t buffer_size, uint32_t input_chunks) {
    std::cout << "RLE Output Analysis:" << std::endl;
    
    // Show hex output
    size_t hex_display_size = std::min(buffer_size, size_t(64));
    std::cout << "  Raw output (hex): ";
    for (size_t i = 0; i < hex_display_size; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(buffer[i]) << " ";
    }
    std::cout << std::dec << std::endl;
    
    // Extract compressed data
    std::string compressed_string = "";
    size_t valid_chars = 0;
    
    for (size_t i = 0; i < buffer_size; i++) {
        uint8_t byte_val = buffer[i];
        if (byte_val >= 'A' && byte_val <= 'P') {
            compressed_string += static_cast<char>(byte_val);
            valid_chars++;
        } else if (byte_val == 0 && valid_chars > 0) {
            break;  // End of valid data
        }
    }
    
    std::cout << "  Compressed output: \"" << compressed_string << "\" (" << valid_chars << " chars)" << std::endl;
    
    // Validate pattern
    std::string expected = "";
    for (uint32_t i = 0; i < input_chunks; i++) {
        expected += "ABCDEFGHIJKLMNOP";
    }
    
    if (compressed_string == expected) {
        std::cout << "  Pattern CORRECT: " << input_chunks << " chunks compressed successfully" << std::endl;
    } else {
        std::cout << "  Pattern MISMATCH" << std::endl;
        std::cout << "  Expected: \"" << expected << "\"" << std::endl;
        std::cout << "  Got:      \"" << compressed_string << "\"" << std::endl;
    }
    
    // Compression stats
    size_t expected_size = input_chunks * 16;
    std::cout << "  Compression: " << (input_chunks * 64) << " → " << valid_chars << " bytes";
    if (valid_chars == expected_size) {
        std::cout << " (4:1 ratio)" << std::endl;
    } else {
        std::cout << " (expected " << expected_size << ")" << std::endl;
    }
}

int main(int argc, char *argv[]) {
    // Signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = gotInt;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    // Parse arguments
    boost::program_options::options_description programDescription("RLE Compression Test:");
    programDescription.add_options()
        ("size,s", boost::program_options::value<uint32_t>(), "Data size in bytes (default: 64)")
        ("hugepages,h", boost::program_options::value<bool>(), "Use hugepages (default: true)")
        ("mapped,m", boost::program_options::value<bool>(), "Use mapped memory (default: true)")
        ("stream,t", boost::program_options::value<bool>(), "Use streaming interface (default: true)")
        ("help", "Show help");
    
    boost::program_options::variables_map commandLineArgs;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), 
                                commandLineArgs);
    boost::program_options::notify(commandLineArgs);

    if(commandLineArgs.count("help")) {
        std::cout << programDescription << std::endl;
        return EXIT_SUCCESS;
    }

    // Get parameters
    uint32_t input_size = defSize;
    bool huge = defHuge;
    bool mapped = defMapped;
    bool stream = defStream;
    
    if(commandLineArgs.count("size") > 0) {
        input_size = commandLineArgs["size"].as<uint32_t>();
        if (input_size == 0 || input_size > maxSize) {
            std::cerr << "Error: Invalid size (max: " << maxSize << " bytes)" << std::endl;
            return EXIT_FAILURE;
        }
    }
    
    if(commandLineArgs.count("hugepages") > 0) huge = commandLineArgs["hugepages"].as<bool>();
    if(commandLineArgs.count("mapped") > 0) mapped = commandLineArgs["mapped"].as<bool>();
    if(commandLineArgs.count("stream") > 0) stream = commandLineArgs["stream"].as<bool>();
    
    // Calculate sizes
    uint32_t input_chunks = (input_size + 63) / 64;
    uint32_t expected_compressed_size = input_chunks * 16;
    uint32_t output_buffer_size = std::max(((expected_compressed_size + 63) / 64) * 64, 64u);

    PR_HEADER("RLE COMPRESSION TEST");
    std::cout << "Input: " << input_size << " bytes (" << input_chunks << " chunks)" << std::endl;
    std::cout << "Expected output: " << expected_compressed_size << " bytes (4:1 compression)" << std::endl;
    
    try {
        // Initialize FPGA
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));

        // Allocate memory
        void* inputData;
        void* outputData;
        
        if (mapped) {
            inputData = cthread->getMem({huge ? CoyoteAlloc::HPF : CoyoteAlloc::REG, 
                                        std::max(input_size, static_cast<uint32_t>(hugePageSize))});
            outputData = cthread->getMem({huge ? CoyoteAlloc::HPF : CoyoteAlloc::REG, 
                                         std::max(output_buffer_size, static_cast<uint32_t>(hugePageSize))});
        } else {
            if (huge) {
                inputData = mmap(NULL, std::max(input_size, static_cast<uint32_t>(hugePageSize)), 
                               PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
                outputData = mmap(NULL, std::max(output_buffer_size, static_cast<uint32_t>(hugePageSize)), 
                                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
            } else {
                inputData = malloc(input_size);
                outputData = malloc(output_buffer_size);
            }
        }
            
        if (!inputData || !outputData) {
            throw std::runtime_error("Memory allocation failed");
        }

        // Clear output buffer and generate input pattern
        memset(outputData, 0, output_buffer_size);
        generateRLEPattern((uint8_t*)inputData, input_size);

        // Show input sample
        std::cout << "Input pattern: ";
        for(size_t i = 0; i < std::min(input_size, 64u); i++) {
            std::cout << static_cast<char>(((uint8_t*)inputData)[i]);
        }
        std::cout << std::endl;

        // Setup transfer
        sgEntry sg;
        memset(&sg, 0, sizeof(localSg));
        sg.local.src_addr = inputData;
        sg.local.src_len = input_size;
        sg.local.src_stream = stream;
        sg.local.dst_addr = outputData;
        sg.local.dst_len = output_buffer_size;
        sg.local.dst_stream = stream;
        
        // Execute transfer
        cBench bench(1);
        cthread->clearCompleted();

        auto benchmark_thr = [&]() {
            cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, {true, true, false});
            while(cthread->checkCompleted(CoyoteOper::LOCAL_TRANSFER) != 1) {
                if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");
            }
        };

        bench.runtime(benchmark_thr);

        // Analyze results
        PR_HEADER("RESULTS");
        analyzeRLEOutput((uint8_t*)outputData, output_buffer_size, input_chunks);
        
        // Performance
        double throughput_mbps = (input_size / 1024.0 / 1024.0) / (bench.getAvg() / 1000000.0);
        std::cout << "Performance: " << bench.getAvg() << " ns, " 
                  << std::fixed << std::setprecision(2) << throughput_mbps << " MB/s" << std::endl;

        // Cleanup
        if (mapped) {
            cthread->freeMem(inputData);
            cthread->freeMem(outputData);
        } else {
            if (huge) {
                munmap(inputData, std::max(input_size, static_cast<uint32_t>(hugePageSize)));
                munmap(outputData, std::max(output_buffer_size, static_cast<uint32_t>(hugePageSize)));
            } else {
                free(inputData);
                free(outputData);
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
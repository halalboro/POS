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
constexpr auto const defSize = 1024;   // Start with smaller size: 1KB
constexpr auto const defDW = 64;       // 512-bit for AES hardware interface

// Simple test pattern - let's try minimal data first
constexpr uint8_t test_plaintext[16] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};

// Pack into 64-bit words
constexpr uint64_t plain_word0 = 
    ((uint64_t)test_plaintext[0] << 56) |
    ((uint64_t)test_plaintext[1] << 48) |
    ((uint64_t)test_plaintext[2] << 40) |
    ((uint64_t)test_plaintext[3] << 32) |
    ((uint64_t)test_plaintext[4] << 24) |
    ((uint64_t)test_plaintext[5] << 16) |
    ((uint64_t)test_plaintext[6] << 8) |
    ((uint64_t)test_plaintext[7]);

constexpr uint64_t plain_word1 = 
    ((uint64_t)test_plaintext[8] << 56) |
    ((uint64_t)test_plaintext[9] << 48) |
    ((uint64_t)test_plaintext[10] << 40) |
    ((uint64_t)test_plaintext[11] << 32) |
    ((uint64_t)test_plaintext[12] << 24) |
    ((uint64_t)test_plaintext[13] << 16) |
    ((uint64_t)test_plaintext[14] << 8) |
    ((uint64_t)test_plaintext[15]);

// Create descriptor based on NEW panic_define.v format (128-bit descriptor)
void createPanicDescriptor(uint8_t* desc_ptr, uint32_t data_size) {
    // Initialize descriptor to zero (128 bits = 16 bytes)
    memset(desc_ptr, 0, 16);
    
    // Based on NEW panic_define.v:
    // Bits 0-31: Length (PANIC_DESC_LEN_OF = 0, PANIC_DESC_LEN_SIZE = 32)
    desc_ptr[0] = data_size & 0xFF;
    desc_ptr[1] = (data_size >> 8) & 0xFF;
    desc_ptr[2] = (data_size >> 16) & 0xFF;
    desc_ptr[3] = (data_size >> 24) & 0xFF;
    
    // Bits 32-47: Cell ID (PANIC_DESC_CELL_ID_OF = 32, PANIC_DESC_CELL_ID_SIZE = 16) 
    desc_ptr[4] = 0x00;  // Cell ID low
    desc_ptr[5] = 0x00;  // Cell ID high
    
    // Bits 48-63: Chain (PANIC_DESC_CHAIN_OF = 48, PANIC_DESC_CHAIN_SIZE = 16)
    // Set chain to 0x0001 (destination 1 = DMA engine)
    desc_ptr[6] = 0x01;  // Chain low - destination 1
    desc_ptr[7] = 0x00;  // Chain high
    
    // Bits 64-71: Priority (PANIC_DESC_PRIO_OF = 64, PANIC_DESC_PRIO_SIZE = 8)
    desc_ptr[8] = 0x00;  // Priority
    
    // Bits 72-83: Time (PANIC_DESC_TIME_OF = 72, PANIC_DESC_TIME_SIZE = 12)
    desc_ptr[9] = 0x00;   // Time low
    desc_ptr[10] = 0x00;  // Time high (4 bits)
    
    // Bit 84: Drop (PANIC_DESC_DROP_OF = 84)
    // Bits 85-92: Flow (PANIC_DESC_FLOW_OF = 85, PANIC_DESC_FLOW_SIZE = 8)
    desc_ptr[10] |= 0x00; // Drop bit + Flow low bits
    desc_ptr[11] = 0x00;  // Flow high bits
    
    // Bits 93-110: Timestamp (PANIC_DESC_TS_OF = 93, PANIC_DESC_TS_SIZE = 18)
    desc_ptr[11] |= 0x00; // TS low bits
    desc_ptr[12] = 0x00;  // TS mid bits
    desc_ptr[13] = 0x00;  // TS high bits
    
    // Bit 111: Port (PANIC_DESC_PORT_OF = 111)
    desc_ptr[13] |= 0x00; // Port bit
    
    // Bits 112-127: Unused/padding
    desc_ptr[14] = 0x00;
    desc_ptr[15] = 0x00;
}

// Helper function to print hex data
void printHexData(const char* label, uint64_t* data, int words) {
    std::cout << label << std::endl;
    for(int i = 0; i < words; i++) {
        std::cout << "  [" << i << "]: 0x" << std::hex << std::setfill('0') 
                  << std::setw(16) << data[i] << std::dec << std::endl;
    }
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

    uint32_t data_size = defSize;
    uint32_t n_reps = defReps;

    if(commandLineArgs.count("size") > 0) data_size = commandLineArgs["size"].as<uint32_t>();
    if(commandLineArgs.count("reps") > 0) n_reps = commandLineArgs["reps"].as<uint32_t>();

    // Ensure data size is multiple of 128 bits (16 bytes) for AES blocks
    data_size = ((data_size + 15) / 16) * 16;
    
    // CRITICAL: Descriptor length field is only 16 bits (max 65535)
    // For 1MB support, we need to split large packets or modify the descriptor format
    if(data_size > 65535) {
        std::cout << "WARNING: Data size " << data_size << " exceeds 16-bit length field limit (65535)." << std::endl;
        std::cout << "Limiting to 65520 bytes (65535 rounded down to 16-byte boundary)." << std::endl;
        data_size = 65520;  // 65535 rounded down to 16-byte boundary
    }
    
    // The descriptor is now 128 bits (16 bytes), but we still pad to 512-bit boundary
    // AES engine expects descriptor in first 512-bit word
    constexpr uint32_t DESC_SIZE_BYTES = 64;  // 512-bit word for descriptor
    constexpr uint32_t ACTUAL_DESC_SIZE = 16; // Actual descriptor is now 128 bits
    uint32_t total_packet_size = DESC_SIZE_BYTES + data_size;

    uint32_t n_pages_host = (total_packet_size + hugePageSize - 1) / hugePageSize;

    PR_HEADER("PARAMS");
    std::cout << "vFPGA ID: " << targetVfid << std::endl;
    std::cout << "Number of allocated pages per run: " << n_pages_host << std::endl;
    std::cout << "Descriptor size: " << DESC_SIZE_BYTES << " bytes" << std::endl;
    std::cout << "Data size: " << data_size << " bytes (" << data_size/16 << " x 128-bit AES blocks)" << std::endl;
    std::cout << "Total packet size: " << total_packet_size << " bytes" << std::endl;
    std::cout << "Number of reps: " << n_reps << std::endl;

    try {
        // Initialize thread
        std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
        cthread->start();

        // Memory allocation
        std::vector<uint64_t*> input_packets(n_reps, nullptr);
        std::vector<uint64_t*> output_packets(n_reps, nullptr);

        // Memory allocation and packet setup
        for(int i = 0; i < n_reps; i++) {
            input_packets[i] = (uint64_t*) cthread->getMem({CoyoteAlloc::HPF, n_pages_host});
            output_packets[i] = (uint64_t*) cthread->getMem({CoyoteAlloc::HPF, n_pages_host});
            
            if (!input_packets[i] || !output_packets[i]) {
                throw std::runtime_error("Memory allocation failed");
            }

            // Create descriptor at the beginning (96 bits in first 512-bit word)
            memset(input_packets[i], 0, DESC_SIZE_BYTES);  // Clear entire first word
            createPanicDescriptor((uint8_t*)input_packets[i], data_size);
            
            // Fill data portion with simple test pattern
            uint64_t* data_ptr = input_packets[i] + (DESC_SIZE_BYTES / 8);
            
            // Fill with alternating pattern of our test data
            for(int j = 0; j < data_size / 16; j++) {
                data_ptr[j*2] = plain_word0;     // Low 64 bits
                data_ptr[j*2 + 1] = plain_word1; // High 64 bits
            }
            
            // Initialize output buffer to zero
            memset(output_packets[i], 0, total_packet_size);
        }

        // Print input packet for debugging
        PR_HEADER("INPUT PACKET DEBUG");
        std::cout << "128-bit Descriptor (16 bytes):" << std::endl;
        uint8_t* desc_bytes = (uint8_t*)input_packets[0];
        for(int i = 0; i < 16; i++) {
            std::cout << "  Byte[" << i << "]: 0x" << std::hex << std::setfill('0') 
                      << std::setw(2) << (int)desc_bytes[i] << std::dec << std::endl;
        }
        
        printHexData("Full Descriptor Word:", input_packets[0], 8);
        printHexData("Input Data (first 8 words):", input_packets[0] + 8, 8);

        // Setup scatter-gather entries for transfer
        sgEntry sg;
        sgFlags sg_flags = { true, true, false };

        // Benchmark setup
        cBench bench(1);
        PR_HEADER("AES ENGINE TEST");

        // Clear any previous completions
        cthread->clearCompleted();

        std::cout << "Starting transfer..." << std::endl;
        
        auto benchmark_thr = [&]() {
            // Queue transfers with detailed logging
            for(int i = 0; i < n_reps; i++) {
                std::cout << "Queuing transfer " << i << std::endl;
                
                memset(&sg, 0, sizeof(localSg));
                sg.local.src_addr = input_packets[i];
                sg.local.src_len = total_packet_size;
                sg.local.src_stream = strmHost;
                sg.local.src_dest = targetVfid;

                sg.local.dst_addr = output_packets[i];
                sg.local.dst_len = total_packet_size;
                sg.local.dst_stream = strmHost;
                sg.local.dst_dest = targetVfid;

                if(i == n_reps-1) sg_flags.last = true;
                
                cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, sg_flags);
                std::cout << "Transfer " << i << " queued" << std::endl;
            }

            // Wait for completion with frequent status updates
            auto start_time = std::chrono::high_resolution_clock::now();
            int completed = 0;
            int timeout_count = 0;
            
            while(completed < n_reps) {
                if(stalled.load()) throw std::runtime_error("Stalled, SIGINT caught");
                
                completed = cthread->checkCompleted(CoyoteOper::LOCAL_TRANSFER);
                
                auto current_time = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>
                             (current_time - start_time).count();
                
                // Print status every 5 seconds
                if(elapsed > timeout_count * 5) {
                    std::cout << "Status: " << completed << "/" << n_reps 
                              << " completed after " << elapsed << " seconds" << std::endl;
                    timeout_count++;
                }
                
                if (elapsed > 30) {
                    std::cout << "Timeout after 30 seconds" << std::endl;
                    throw std::runtime_error("Transfer timeout");
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            std::cout << "All transfers completed!" << std::endl;
        };

        bench.runtime(benchmark_thr);

        // Print results
        PR_HEADER("OUTPUT PACKET DEBUG");
        std::cout << "128-bit Output Descriptor (16 bytes):" << std::endl;
        uint8_t* out_desc_bytes = (uint8_t*)output_packets[0];
        for(int i = 0; i < 16; i++) {
            std::cout << "  Byte[" << i << "]: 0x" << std::hex << std::setfill('0') 
                      << std::setw(2) << (int)out_desc_bytes[i] << std::dec << std::endl;
        }
        
        printHexData("Full Output Descriptor Word:", output_packets[0], 8);
        printHexData("Output Data (first 8 words):", output_packets[0] + 8, 8);

        // Verify results
        PR_HEADER("VERIFICATION");
        bool success = true;
        
        for(int i = 0; i < n_reps && success; i++) {
            uint64_t* output_data = output_packets[i] + (DESC_SIZE_BYTES / 8);
            uint64_t* input_data = input_packets[i] + (DESC_SIZE_BYTES / 8);
            
            // Check for non-zero output
            bool has_output = false;
            for(int j = 0; j < data_size/8; j++) {
                if(output_data[j] != 0) {
                    has_output = true;
                    break;
                }
            }
            
            if(!has_output) {
                std::cout << "ERROR: All zero output detected" << std::endl;
                success = false;
            }
            
            // Check if data was processed (different from input)
            bool is_processed = false;
            for(int j = 0; j < data_size/8; j++) {
                if(output_data[j] != input_data[j]) {
                    is_processed = true;
                    break;
                }
            }
            
            if(!is_processed) {
                std::cout << "WARNING: Output identical to input" << std::endl;
            }
        }
        
        std::cout << "Test result: " << (success ? "PASSED" : "FAILED") << std::endl;

        // Cleanup
        for(int i = 0; i < n_reps; i++) {
            if(input_packets[i]) {
                cthread->freeMem(input_packets[i]);
                input_packets[i] = nullptr;
            }
            if(output_packets[i]) {
                cthread->freeMem(output_packets[i]);
                output_packets[i] = nullptr;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
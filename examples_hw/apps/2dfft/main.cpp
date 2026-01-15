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
constexpr auto const targetVfid = 0;  
constexpr auto const defReps = 1;
constexpr auto const defSize = 16;

struct ComplexFloat {
    float real;
    float imag;
} __attribute__((packed, aligned(32))); 

void generateTestPattern(ComplexFloat* data, int size) {
    // Single 2D sinusoid
    for(int i = 0; i < size; i++) {
        for(int j = 0; j < size; j++) {
            int idx = i * size + j;
            // 2D sinusoid with frequencies (2,3)
            data[idx].real = cos(2.0f * M_PI * (2.0f * i + 3.0f * j) / size);
            data[idx].imag = 0.0f;
        }
    }
}

void printComplexMatrix(ComplexFloat* data, int size, const std::string& label) {
   std::cout << label << ":\n";
   int total = size * size;
   for(int i = 0; i < total; i++) {
       if(i % 8 == 0) {
           std::cout << "\nIndex " << std::setw(3) << i << ": ";
       }
       std::cout << std::fixed << std::setprecision(3) 
                << data[i].real << "+" 
                << data[i].imag << "i  ";
   }
   std::cout << "\n\n";
}

void verifyOutput(ComplexFloat* output, int size) {
   float max_magnitude = 0.0f;
   int max_i = 0, max_j = 0;
   
   for(int i = 0; i < size; i++) {
       for(int j = 0; j < size; j++) {
           int idx = i * size + j;
           float mag = sqrt(output[idx].real * output[idx].real + 
                          output[idx].imag * output[idx].imag);
           if(mag > max_magnitude) {
               max_magnitude = mag;
               max_i = i;
               max_j = j;
           }
       }
   }
   printf("Maximum FFT magnitude %f at frequency bin (%d,%d)\n", max_magnitude, max_i, max_j);
}

int main(int argc, char *argv[]) {
   struct sigaction sa;
   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = gotInt;
   sigfillset(&sa.sa_mask);
   sigaction(SIGINT, &sa, NULL);

   if(defSize != 16) {
       throw std::runtime_error("Size must be 16x16 for this FFT implementation");
   }
   
   uint32_t size = defSize;
   uint32_t n_reps = defReps;
   uint32_t buffer_size = size * size * sizeof(ComplexFloat);
   uint32_t n_pages = (buffer_size + hugePageSize - 1) / hugePageSize;

   PR_HEADER("PARAMS");
   std::cout << "vFPGA ID: " << targetVfid << std::endl;
   std::cout << "Matrix size: " << size << "x" << size << std::endl;
   std::cout << "Buffer size: " << buffer_size << " bytes" << std::endl;
   std::cout << "Number of pages: " << n_pages << std::endl;

   try {
       std::unique_ptr<cThread<std::any>> cthread(new cThread<std::any>(targetVfid, getpid(), defDevice));
       cthread->start();
       
       ComplexFloat* input_buffer = (ComplexFloat*) cthread->getMem({CoyoteAlloc::HPF, n_pages});
       ComplexFloat* output_buffer = (ComplexFloat*) cthread->getMem({CoyoteAlloc::HPF, n_pages});
       
       if (!input_buffer || !output_buffer) {
           throw std::runtime_error("Memory allocation failed");
       }

       generateTestPattern(input_buffer, size);
       memset(output_buffer, 0, buffer_size);

       printComplexMatrix(input_buffer, size, "\nInput Matrix");

       PR_HEADER("FFT PROCESSING");
       sgEntry sg;
       sgFlags sg_flags = { true, true, false };
       cBench bench(n_reps);
       
       cthread->clearCompleted();

       bench.runtime([&]() {
           memset(&sg, 0, sizeof(localSg));
           
           sg.local.src_addr = input_buffer;
           sg.local.src_len = buffer_size;
           sg.local.src_stream = strmHost;
           sg.local.src_dest = targetVfid;
           
           sg.local.dst_addr = output_buffer;
           sg.local.dst_len = buffer_size;
           sg.local.dst_stream = strmHost;
           sg.local.dst_dest = targetVfid;

           cthread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg, sg_flags);

           while(cthread->checkCompleted(CoyoteOper::LOCAL_WRITE) != 1) {
               if(stalled.load()) throw std::runtime_error("Stalled");
           }
       });

       printComplexMatrix(output_buffer, size, "\nOutput Matrix");
       verifyOutput(output_buffer, size);

       cthread->printDebug();

       if(input_buffer) cthread->freeMem(input_buffer);
       if(output_buffer) cthread->freeMem(output_buffer);

   } catch (const std::exception& e) {
       std::cerr << "Error: " << e.what() << std::endl;
       return EXIT_FAILURE;
   }
   
   return EXIT_SUCCESS;
}
#include "ap_axi_sdata.h"
#include "hls_stream.h"
#include "hls_math.h"
#include <complex>

// Define AXI-Stream interface types
typedef ap_axiu<64,0,0,0> axis_fft_t;   // 64-bit for FFT interface
typedef ap_axiu<32,0,0,0> axis_svm_t;   // 32-bit for SVM interface

// Define internal data types
typedef struct {
    float data;
    bool last;
} internal_data_t;

void fft2svm_0(
    hls::stream<axis_fft_t>& s_axis_fft,
    hls::stream<axis_svm_t>& m_axis_svm) {
    #pragma HLS DATAFLOW disable_start_propagation
    #pragma HLS INTERFACE axis register port=s_axis_fft
    #pragma HLS INTERFACE axis register port=m_axis_svm
    #pragma HLS INTERFACE ap_ctrl_none port=return

    // Internal streams with non-AXI type
    static hls::stream<internal_data_t> int_stream;
    #pragma HLS STREAM depth=32 variable=int_stream
    
    // Buffer for accumulations
    static float accumulators[32] = {0};
    #pragma HLS ARRAY_PARTITION variable=accumulators complete
    
    // Initialize accumulators
    for (int i = 0; i < 32; i++) {
        #pragma HLS UNROLL
        accumulators[i] = 0;
    }
    
    // Process FFT output (16384 samples)
    for (int i = 0; i < 16384; i++) {
        #pragma HLS PIPELINE II=1
        // Read FFT output
        axis_fft_t fft_data = s_axis_fft.read();
        // Extract only real part (lower 32 bits)
        float real = *((float*)&fft_data.data);
        // Accumulate real values
        int acc_index = i >> 9;  // Divide by 512 to get accumulator index
        accumulators[acc_index] += real;
    }

    // Send results to SVM
    for (int i = 0; i < 32; i++) {
        #pragma HLS PIPELINE II=1
        // Prepare output
        axis_svm_t out_data;
        out_data.data = accumulators[i];
        out_data.last = (i == 31) ? 1 : 0;
        out_data.keep = -1;         // All bytes valid
        out_data.strb = -1;         // All bytes valid
        // Write to output stream
        m_axis_svm.write(out_data);
    }
}

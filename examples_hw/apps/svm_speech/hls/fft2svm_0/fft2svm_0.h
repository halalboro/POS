#ifndef __FFT2SVM_0_H__
#define __FFT2SVM_0_H__

#include "ap_axi_sdata.h"
#include "hls_stream.h"

// Define AXI-Stream data types
typedef ap_axiu<64,0,0,0> axis_fft_t;   // FFT interface (64-bit)
typedef ap_axiu<32,0,0,0> axis_svm_t;   // SVM interface (32-bit)

// Function prototype
void fft2svm_0(hls::stream<axis_fft_t>& s_axis_fft,
             hls::stream<axis_svm_t>& m_axis_svm);

#endif // __FFT2SVM_0_H__

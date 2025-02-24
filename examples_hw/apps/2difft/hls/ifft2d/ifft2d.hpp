#ifndef _IFFT2D_H_
#define _IFFT2D_H_

#include <ap_fixed.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <ap_axi_sdata.h>
#include <math.h>

struct ComplexFloat {
    float real;
    float imag;
};

typedef ap_axiu<64,0,0,0> axis_t;

void ifft1d(ComplexFloat signal[16], ComplexFloat F[16]);
void ifft2d(hls::stream<axis_t>& p_inStream, hls::stream<axis_t>& p_outStream);

#endif
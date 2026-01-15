#include "ifft2d.hpp"

void ifft1d(ComplexFloat input[16], ComplexFloat output[16]) {
    #pragma HLS INLINE off
    
    const float PI = 3.14159265358979323846f;
    
    // Direct IFFT computation: X[k] = 1/N * Σ(x[n] * e^(j2πnk/N))
    for(int k = 0; k < 16; k++) {
        #pragma HLS PIPELINE
        float sum_real = 0;
        float sum_imag = 0;

        // Sum up the contributions from each input frequency
        for(int n = 0; n < 16; n++) {
            float angle = (2.0f * PI * k * n) / 16.0f;  // Positive angle for IFFT
            float cos_val = hls::cos(angle);
            float sin_val = hls::sin(angle);

            // Complex multiplication with e^(j2πnk/N)
            sum_real += (input[n].real * cos_val - input[n].imag * sin_val);
            sum_imag += (input[n].real * sin_val + input[n].imag * cos_val);
        }

        // Scale by 1/N
        output[k].real = sum_real / 16.0f;
        output[k].imag = sum_imag / 16.0f;
    }
}

void ifft2d(hls::stream<axis_t>& p_inStream, hls::stream<axis_t>& p_outStream) {
    #pragma HLS INTERFACE mode=axis register_mode=both register port=p_inStream
    #pragma HLS INTERFACE mode=axis register_mode=both register port=p_outStream
    #pragma HLS INTERFACE mode=ap_ctrl_none port=return

    ComplexFloat data[16][16];
    ComplexFloat temp[16][16];

    // Read input data remains same
    READ_DATA: for(int i = 0; i < 16; i++) {
        for(int j = 0; j < 16; j++) {
            #pragma HLS PIPELINE II=1
            axis_t input = p_inStream.read();
            ap_uint<32> real_bits = input.data.range(31,0);
            ap_uint<32> imag_bits = input.data.range(63,32);
            data[i][j].real = *reinterpret_cast<float*>(&real_bits);
            data[i][j].imag = *reinterpret_cast<float*>(&imag_bits);
        }
    }

    // Column-wise IFFT
    ComplexFloat col[16];
    ComplexFloat col_result[16];

    for(int j = 0; j < 16; j++) {
        // Get column from original data
        for(int i = 0; i < 16; i++) {
            #pragma HLS PIPELINE II=1
            col[i] = data[i][j];
        }

        // Process column
        ifft1d(col, col_result);

        // Store result back in transpose order
        for(int i = 0; i < 16; i++) {
            #pragma HLS PIPELINE II=1
            temp[j][i] = col_result[i];  // Note the indices are swapped here
        }
    }

    // Row-wise IFFT
    ComplexFloat row_result[16];

    for(int i = 0; i < 16; i++) {
        // Process row directly from temp since data is now properly arranged
        ifft1d(temp[i], row_result);

        for(int j = 0; j < 16; j++) {
            #pragma HLS PIPELINE II=1
            axis_t output;
            ap_uint<32> real_bits = *reinterpret_cast<ap_uint<32>*>(&row_result[j].real);
            ap_uint<32> imag_bits = *reinterpret_cast<ap_uint<32>*>(&row_result[j].imag);
            output.data.range(31,0) = real_bits;
            output.data.range(63,32) = imag_bits;
            output.keep = -1;
            output.last = (i == 15 && j == 15);
            p_outStream.write(output);
        }
    }
}
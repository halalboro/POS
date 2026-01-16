/*
 * RLE (Run-Length Encoding) Header - 512-bit Wide Version
 */
#ifndef RLE_HPP
#define RLE_HPP

#include <ap_axi_sdata.h>
#include <hls_stream.h>
#include <ap_int.h>

// Function declaration for RLE encoder with 512-bit interface
void rle_encode(
    hls::stream<ap_axiu<512,0,0,0>>& input_stream,
    hls::stream<ap_axiu<512,0,0,0>>& output_stream
);

#endif // RLE_HPP
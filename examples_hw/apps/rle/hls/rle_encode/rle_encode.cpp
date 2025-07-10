/*
 * Fixed RLE Encoder - Proper Accumulation Logic
 * Key fixes: 
 * 1. Conditional reset based on stream start detection
 * 2. Proper state persistence between packets
 * 3. Accumulation that works across multiple packet calls
 */
#include "rle_encode.hpp"

void rle_encode(
    hls::stream<ap_axiu<512,0,0,0>>& input_stream,
    hls::stream<ap_axiu<512,0,0,0>>& output_stream
) {
    #pragma HLS INTERFACE axis port=input_stream
    #pragma HLS INTERFACE axis port=output_stream
    #pragma HLS INTERFACE ap_ctrl_none port=return
    
    // Static accumulator variables - must persist across packets
    static ap_uint<8> acc_0, acc_1, acc_2, acc_3, acc_4, acc_5, acc_6, acc_7;
    static ap_uint<8> acc_8, acc_9, acc_10, acc_11, acc_12, acc_13, acc_14, acc_15;
    static ap_uint<8> acc_16, acc_17, acc_18, acc_19, acc_20, acc_21, acc_22, acc_23;
    static ap_uint<8> acc_24, acc_25, acc_26, acc_27, acc_28, acc_29, acc_30, acc_31;
    static ap_uint<8> acc_32, acc_33, acc_34, acc_35, acc_36, acc_37, acc_38, acc_39;
    static ap_uint<8> acc_40, acc_41, acc_42, acc_43, acc_44, acc_45, acc_46, acc_47;
    static ap_uint<8> acc_48, acc_49, acc_50, acc_51, acc_52, acc_53, acc_54, acc_55;
    static ap_uint<8> acc_56, acc_57, acc_58, acc_59, acc_60, acc_61, acc_62, acc_63;
    
    // Static state variables - must persist for accumulation
    static ap_uint<7> count = 0;
    static ap_uint<8> prev = 0;
    static bool first = true;
    static bool stream_active = false;
    static bool initialized = false;
    
    // Initialize accumulator variables once only
    if (!initialized) {
        acc_0 = 0; acc_1 = 0; acc_2 = 0; acc_3 = 0; acc_4 = 0; acc_5 = 0; acc_6 = 0; acc_7 = 0;
        acc_8 = 0; acc_9 = 0; acc_10 = 0; acc_11 = 0; acc_12 = 0; acc_13 = 0; acc_14 = 0; acc_15 = 0;
        acc_16 = 0; acc_17 = 0; acc_18 = 0; acc_19 = 0; acc_20 = 0; acc_21 = 0; acc_22 = 0; acc_23 = 0;
        acc_24 = 0; acc_25 = 0; acc_26 = 0; acc_27 = 0; acc_28 = 0; acc_29 = 0; acc_30 = 0; acc_31 = 0;
        acc_32 = 0; acc_33 = 0; acc_34 = 0; acc_35 = 0; acc_36 = 0; acc_37 = 0; acc_38 = 0; acc_39 = 0;
        acc_40 = 0; acc_41 = 0; acc_42 = 0; acc_43 = 0; acc_44 = 0; acc_45 = 0; acc_46 = 0; acc_47 = 0;
        acc_48 = 0; acc_49 = 0; acc_50 = 0; acc_51 = 0; acc_52 = 0; acc_53 = 0; acc_54 = 0; acc_55 = 0;
        acc_56 = 0; acc_57 = 0; acc_58 = 0; acc_59 = 0; acc_60 = 0; acc_61 = 0; acc_62 = 0; acc_63 = 0;
        count = 0;
        prev = 0;
        first = true;
        stream_active = false;
        initialized = true;
    }
    
    // Main processing loop
    while (true) {
        #pragma HLS PIPELINE off
        
        // Read packet
        ap_axiu<512,0,0,0> pkt = input_stream.read();
        bool last = pkt.last;
        
        // CONDITIONAL RESET: Only reset on new stream start
        // Detect new stream: when we're not active and get first packet
        if (!stream_active) {
            count = 0;
            prev = 0;
            first = true;
            stream_active = true;
        }
        
        // Process each byte in the packet
        for (int byte_idx = 0; byte_idx < 64; byte_idx++) {
            #pragma HLS UNROLL factor=4
            
            ap_uint<8> curr = pkt.data.range(byte_idx*8 + 7, byte_idx*8);
            
            // RLE logic: add if first or different from previous
            if (first || (curr != prev)) {
                if (count < 64) {
                    // Hardcoded assignment instead of array indexing
                    if (count == 0) acc_0 = curr;
                    else if (count == 1) acc_1 = curr;
                    else if (count == 2) acc_2 = curr;
                    else if (count == 3) acc_3 = curr;
                    else if (count == 4) acc_4 = curr;
                    else if (count == 5) acc_5 = curr;
                    else if (count == 6) acc_6 = curr;
                    else if (count == 7) acc_7 = curr;
                    else if (count == 8) acc_8 = curr;
                    else if (count == 9) acc_9 = curr;
                    else if (count == 10) acc_10 = curr;
                    else if (count == 11) acc_11 = curr;
                    else if (count == 12) acc_12 = curr;
                    else if (count == 13) acc_13 = curr;
                    else if (count == 14) acc_14 = curr;
                    else if (count == 15) acc_15 = curr;
                    else if (count == 16) acc_16 = curr;
                    else if (count == 17) acc_17 = curr;
                    else if (count == 18) acc_18 = curr;
                    else if (count == 19) acc_19 = curr;
                    else if (count == 20) acc_20 = curr;
                    else if (count == 21) acc_21 = curr;
                    else if (count == 22) acc_22 = curr;
                    else if (count == 23) acc_23 = curr;
                    else if (count == 24) acc_24 = curr;
                    else if (count == 25) acc_25 = curr;
                    else if (count == 26) acc_26 = curr;
                    else if (count == 27) acc_27 = curr;
                    else if (count == 28) acc_28 = curr;
                    else if (count == 29) acc_29 = curr;
                    else if (count == 30) acc_30 = curr;
                    else if (count == 31) acc_31 = curr;
                    else if (count == 32) acc_32 = curr;
                    else if (count == 33) acc_33 = curr;
                    else if (count == 34) acc_34 = curr;
                    else if (count == 35) acc_35 = curr;
                    else if (count == 36) acc_36 = curr;
                    else if (count == 37) acc_37 = curr;
                    else if (count == 38) acc_38 = curr;
                    else if (count == 39) acc_39 = curr;
                    else if (count == 40) acc_40 = curr;
                    else if (count == 41) acc_41 = curr;
                    else if (count == 42) acc_42 = curr;
                    else if (count == 43) acc_43 = curr;
                    else if (count == 44) acc_44 = curr;
                    else if (count == 45) acc_45 = curr;
                    else if (count == 46) acc_46 = curr;
                    else if (count == 47) acc_47 = curr;
                    else if (count == 48) acc_48 = curr;
                    else if (count == 49) acc_49 = curr;
                    else if (count == 50) acc_50 = curr;
                    else if (count == 51) acc_51 = curr;
                    else if (count == 52) acc_52 = curr;
                    else if (count == 53) acc_53 = curr;
                    else if (count == 54) acc_54 = curr;
                    else if (count == 55) acc_55 = curr;
                    else if (count == 56) acc_56 = curr;
                    else if (count == 57) acc_57 = curr;
                    else if (count == 58) acc_58 = curr;
                    else if (count == 59) acc_59 = curr;
                    else if (count == 60) acc_60 = curr;
                    else if (count == 61) acc_61 = curr;
                    else if (count == 62) acc_62 = curr;
                    else if (count == 63) acc_63 = curr;
                    
                    count++;
                }
                first = false;
            }
            prev = curr;
            
            // Send output when accumulator is full (64 compressed bytes ready)
            if (count == 64) {
                ap_axiu<512,0,0,0> out;
                out.data = 0;
                out.keep = 0xFFFFFFFFFFFFFFFF;
                out.last = 0;  // Not last unless this is the final packet
                out.user = 0;
                out.id = 0;
                out.dest = 0;
                out.strb = 0xFFFFFFFFFFFFFFFF;
                
                // Pack compressed data
                out.data.range(7, 0) = acc_0;     out.data.range(15, 8) = acc_1;
                out.data.range(23, 16) = acc_2;   out.data.range(31, 24) = acc_3;
                out.data.range(39, 32) = acc_4;   out.data.range(47, 40) = acc_5;
                out.data.range(55, 48) = acc_6;   out.data.range(63, 56) = acc_7;
                out.data.range(71, 64) = acc_8;   out.data.range(79, 72) = acc_9;
                out.data.range(87, 80) = acc_10;  out.data.range(95, 88) = acc_11;
                out.data.range(103, 96) = acc_12; out.data.range(111, 104) = acc_13;
                out.data.range(119, 112) = acc_14; out.data.range(127, 120) = acc_15;
                out.data.range(135, 128) = acc_16; out.data.range(143, 136) = acc_17;
                out.data.range(151, 144) = acc_18; out.data.range(159, 152) = acc_19;
                out.data.range(167, 160) = acc_20; out.data.range(175, 168) = acc_21;
                out.data.range(183, 176) = acc_22; out.data.range(191, 184) = acc_23;
                out.data.range(199, 192) = acc_24; out.data.range(207, 200) = acc_25;
                out.data.range(215, 208) = acc_26; out.data.range(223, 216) = acc_27;
                out.data.range(231, 224) = acc_28; out.data.range(239, 232) = acc_29;
                out.data.range(247, 240) = acc_30; out.data.range(255, 248) = acc_31;
                out.data.range(263, 256) = acc_32; out.data.range(271, 264) = acc_33;
                out.data.range(279, 272) = acc_34; out.data.range(287, 280) = acc_35;
                out.data.range(295, 288) = acc_36; out.data.range(303, 296) = acc_37;
                out.data.range(311, 304) = acc_38; out.data.range(319, 312) = acc_39;
                out.data.range(327, 320) = acc_40; out.data.range(335, 328) = acc_41;
                out.data.range(343, 336) = acc_42; out.data.range(351, 344) = acc_43;
                out.data.range(359, 352) = acc_44; out.data.range(367, 360) = acc_45;
                out.data.range(375, 368) = acc_46; out.data.range(383, 376) = acc_47;
                out.data.range(391, 384) = acc_48; out.data.range(399, 392) = acc_49;
                out.data.range(407, 400) = acc_50; out.data.range(415, 408) = acc_51;
                out.data.range(423, 416) = acc_52; out.data.range(431, 424) = acc_53;
                out.data.range(439, 432) = acc_54; out.data.range(447, 440) = acc_55;
                out.data.range(455, 448) = acc_56; out.data.range(463, 456) = acc_57;
                out.data.range(471, 464) = acc_58; out.data.range(479, 472) = acc_59;
                out.data.range(487, 480) = acc_60; out.data.range(495, 488) = acc_61;
                out.data.range(503, 496) = acc_62; out.data.range(511, 504) = acc_63;
                
                output_stream.write(out);
                count = 0;  // Reset accumulator for next 64 bytes
            }
        }
        
        // Handle end of stream
        if (last) {
            // Send any remaining compressed data
            if (count > 0) {
                ap_axiu<512,0,0,0> final;
                final.data = 0;
                final.keep = 0xFFFFFFFFFFFFFFFF;
                final.last = 1;  // This is the final packet
                final.user = 0;
                final.id = 0;
                final.dest = 0;
                final.strb = 0xFFFFFFFFFFFFFFFF;
                
                // Pack remaining data
                if (count > 0) final.data.range(7, 0) = acc_0;
                if (count > 1) final.data.range(15, 8) = acc_1;
                if (count > 2) final.data.range(23, 16) = acc_2;
                if (count > 3) final.data.range(31, 24) = acc_3;
                if (count > 4) final.data.range(39, 32) = acc_4;
                if (count > 5) final.data.range(47, 40) = acc_5;
                if (count > 6) final.data.range(55, 48) = acc_6;
                if (count > 7) final.data.range(63, 56) = acc_7;
                if (count > 8) final.data.range(71, 64) = acc_8;
                if (count > 9) final.data.range(79, 72) = acc_9;
                if (count > 10) final.data.range(87, 80) = acc_10;
                if (count > 11) final.data.range(95, 88) = acc_11;
                if (count > 12) final.data.range(103, 96) = acc_12;
                if (count > 13) final.data.range(111, 104) = acc_13;
                if (count > 14) final.data.range(119, 112) = acc_14;
                if (count > 15) final.data.range(127, 120) = acc_15;
                if (count > 16) final.data.range(135, 128) = acc_16;
                if (count > 17) final.data.range(143, 136) = acc_17;
                if (count > 18) final.data.range(151, 144) = acc_18;
                if (count > 19) final.data.range(159, 152) = acc_19;
                if (count > 20) final.data.range(167, 160) = acc_20;
                if (count > 21) final.data.range(175, 168) = acc_21;
                if (count > 22) final.data.range(183, 176) = acc_22;
                if (count > 23) final.data.range(191, 184) = acc_23;
                if (count > 24) final.data.range(199, 192) = acc_24;
                if (count > 25) final.data.range(207, 200) = acc_25;
                if (count > 26) final.data.range(215, 208) = acc_26;
                if (count > 27) final.data.range(223, 216) = acc_27;
                if (count > 28) final.data.range(231, 224) = acc_28;
                if (count > 29) final.data.range(239, 232) = acc_29;
                if (count > 30) final.data.range(247, 240) = acc_30;
                if (count > 31) final.data.range(255, 248) = acc_31;
                if (count > 32) final.data.range(263, 256) = acc_32;
                if (count > 33) final.data.range(271, 264) = acc_33;
                if (count > 34) final.data.range(279, 272) = acc_34;
                if (count > 35) final.data.range(287, 280) = acc_35;
                if (count > 36) final.data.range(295, 288) = acc_36;
                if (count > 37) final.data.range(303, 296) = acc_37;
                if (count > 38) final.data.range(311, 304) = acc_38;
                if (count > 39) final.data.range(319, 312) = acc_39;
                if (count > 40) final.data.range(327, 320) = acc_40;
                if (count > 41) final.data.range(335, 328) = acc_41;
                if (count > 42) final.data.range(343, 336) = acc_42;
                if (count > 43) final.data.range(351, 344) = acc_43;
                if (count > 44) final.data.range(359, 352) = acc_44;
                if (count > 45) final.data.range(367, 360) = acc_45;
                if (count > 46) final.data.range(375, 368) = acc_46;
                if (count > 47) final.data.range(383, 376) = acc_47;
                if (count > 48) final.data.range(391, 384) = acc_48;
                if (count > 49) final.data.range(399, 392) = acc_49;
                if (count > 50) final.data.range(407, 400) = acc_50;
                if (count > 51) final.data.range(415, 408) = acc_51;
                if (count > 52) final.data.range(423, 416) = acc_52;
                if (count > 53) final.data.range(431, 424) = acc_53;
                if (count > 54) final.data.range(439, 432) = acc_54;
                if (count > 55) final.data.range(447, 440) = acc_55;
                if (count > 56) final.data.range(455, 448) = acc_56;
                if (count > 57) final.data.range(463, 456) = acc_57;
                if (count > 58) final.data.range(471, 464) = acc_58;
                if (count > 59) final.data.range(479, 472) = acc_59;
                if (count > 60) final.data.range(487, 480) = acc_60;
                if (count > 61) final.data.range(495, 488) = acc_61;
                if (count > 62) final.data.range(503, 496) = acc_62;
                if (count > 63) final.data.range(511, 504) = acc_63;
                
                output_stream.write(final);
            }
            
            // Reset stream state for next stream
            stream_active = false;
            break;
        }
    }
}
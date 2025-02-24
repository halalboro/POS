#ifndef XF_SECURITY_ASYMMETRIC_CRYPTOGRAPHY_HPP_
#define XF_SECURITY_ASYMMETRIC_CRYPTOGRAPHY_HPP_

#include <ap_int.h>
#include <hls_stream.h>
#include "modular.hpp"
#include <ap_axi_sdata.h>

namespace xf {
namespace security {

class rsa {
public:
    ap_uint<20> exponent;
    ap_uint<256> modulus;
    ap_uint<256> rMod;

    void updateKey(ap_uint<256> inputModulus, ap_uint<20> inputExponent) {
        // Store key parameters
        modulus = inputModulus;
        exponent = inputExponent;

        // Calculate Montgomery parameters
        // R = 2^256 mod N
        ap_uint<257> R = 0;
        R[256] = 1;
        ap_uint<256> R_mod = R % modulus;

        // Calculate R^2 mod N for Montgomery arithmetic
        rMod = xf::security::internal::productMod<256>(R_mod, R_mod, modulus);
    }

    void process(
        hls::stream<ap_axiu<256,0,0,0>>& message_in,
        hls::stream<ap_axiu<256,0,0,0>>& result_out
    ) {
        if (!message_in.empty()) {
            // Read input
            ap_axiu<256,0,0,0> message_input = message_in.read();
            ap_uint<256> msg = message_input.data;

            // Convert to Montgomery form
            ap_uint<256> mont_message = xf::security::internal::monProduct<256>(
                msg, rMod, modulus);

            // Perform modular exponentiation
            ap_uint<256> result = xf::security::internal::modularExp<256, 20>(
                mont_message, exponent, modulus, rMod);

            // Write output
            ap_axiu<256,0,0,0> result_output;
            result_output.data = result;
            result_output.keep = ~0;
            result_output.last = message_input.last;

            result_out.write(result_output);
        }
    }
};

} // namespace security
} // namespace xf
#endif
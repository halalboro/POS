/**
  * Copyright (c) 2021, Systems Group, ETH Zurich
  * All rights reserved.
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *
  * 1. Redistributions of source code must retain the above copyright notice,
  * this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  * this list of conditions and the following disclaimer in the documentation
  * and/or other materials provided with the distribution.
  * 3. Neither the name of the copyright holder nor the names of its contributors
  * may be used to endorse or promote products derived from this software
  * without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
  * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  */

`timescale 1ns / 1ps

import lynxTypes::*;

`include "axi_macros.svh"
`include "lynx_macros.svh"

// HARDCODED_TEST_MODE: When defined, route_out is hardcoded for testing
// instead of being set by route_ctrl from host. Remove for production.
`define HARDCODED_TEST_MODE

/**
 * Gateway Send Module (gate_send)
 *
 * Handles outgoing routing capability for vIO Switch.
 * Sets the route_out signal based on route_ctrl configuration.
 *
 * Route ID format (14 bits):
 *   [13:10] reserved
 *   [9:6]   sender_id (this vFPGA's ID)
 *   [5:2]   receiver_id (destination vFPGA)
 *   [1:0]   flags
 *
 * HARDCODED_TEST_MODE for shell_2:
 *   - vFPGA 0 (ID=0): route_out = 14'h0004 (send to vFPGA 1)
 *   - vFPGA 1 (ID=1): route_out = 14'h0000 (send to network/RDMA TX)
 */
module gateway_send #(
    parameter N_DESTS = 1,
    parameter ID = 0       // vFPGA ID for hardcoded test mode
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // Routing capability configuration from host
    input  logic [13:0]                         route_ctrl,

    // Route output to vIO Switch
    output logic [13:0]                         route_out
);

    // ========================================================================================
    // ROUTING CAPABILITY LOGIC
    // ========================================================================================
    // With single port design, route_out directly comes from route_ctrl
    // The route_ctrl is set by the host/control plane to define allowed routing

`ifdef HARDCODED_TEST_MODE
    // Hardcoded route_out for shell_2 testing:
    // - vFPGA 0: send to vFPGA 1 (loopback test)
    // - vFPGA 1: send to network TX (RDMA stack, port 0)
    //
    // Route ID format: [13:10]=reserved, [9:6]=sender_id, [5:2]=receiver_id, [1:0]=flags
    // vFPGA 0 -> vFPGA 1: sender=0, receiver=1 => 14'b0000_0000_0001_00 = 14'h0004
    // vFPGA 1 -> network: sender=1, receiver=0 => 14'b0000_0001_0000_00 = 14'h0040
    localparam logic [13:0] HARDCODED_ROUTE_VFPGA0 = 14'h0004;  // Route to vFPGA 1
    localparam logic [13:0] HARDCODED_ROUTE_VFPGA1 = 14'h0040;  // Route to network TX

    always_ff @(posedge aclk) begin
        if (~aresetn) begin
            route_out <= '0;
        end else begin
            // Select hardcoded route based on this vFPGA's ID
            case (ID)
                0: route_out <= HARDCODED_ROUTE_VFPGA0;
                1: route_out <= HARDCODED_ROUTE_VFPGA1;
                default: route_out <= '0;
            endcase
        end
    end
`else
    always_ff @(posedge aclk) begin
        if (~aresetn) begin
            route_out <= '0;
        end else begin
            route_out <= route_ctrl;
        end
    end
`endif

endmodule

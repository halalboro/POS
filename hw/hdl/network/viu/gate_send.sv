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

/**
 * VIU Gateway Send Module - Routing Capability Management
 *
 * This module manages routing capabilities for the VLAN Isolation Unit (VIU).
 * It validates that a given user logic port has permission to send to a destination
 * based on routing capabilities configured by the host.
 *
 * Unlike vFIU's gate_send (which does memory bounds checking), this module only
 * handles network routing permissions for VLAN-based isolation.
 */
module gate_send #(
    parameter N_DESTS = 1
) (
    input  logic                        aclk,
    input  logic                        aresetn,

    // Routing capability configuration from host
    // Format: [13:10] reserved, [9:6] sender_ul_id, [5:2] reserved, [1:0] port_id
    input  logic [13:0]                 route_ctrl,

    // Port selection for outgoing route lookup
    input  logic [1:0]                  ul_port_in,

    // Output route capability for the selected port
    output logic [13:0]                 route_out
);

    // Routing capability register array - stores route config per destination
    logic [N_DESTS-1:0][13:0] route_capa_reg;
    logic [1:0] ul_id;

    // Getting routing capability from host and providing route lookup
    always_ff @(posedge aclk) begin
        if (~aresetn) begin
            route_capa_reg <= '{default: '0};
            ul_id <= '0;
            route_out <= '0;
        end else begin
            // Extract port ID from incoming capability config
            ul_id <= route_ctrl[1:0];
            // Store the full capability config indexed by port ID
            route_capa_reg[ul_id] <= route_ctrl;
            // Output the stored capability for the requested port
            route_out <= route_capa_reg[ul_port_in];
        end
    end

endmodule

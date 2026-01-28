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
 * vFIU Gateway Recv Module - Incoming Route Validation
 *
 * This module validates incoming data routes for the vFIU.
 * It checks that the route_in matches the expected routing capability
 * and outputs the destination port for the vIO Switch.
 *
 * Naming convention:
 *   - VIU (network level): gateway_tx (outgoing), gateway_rx (incoming)
 *   - vFIU (memory level): gateway_send, gateway_recv
 */
module gateway_recv #(
    parameter N_DESTS                   = 1
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // Routing capability configuration from host
    input logic [13:0]                           route_ctrl,

    // Incoming route for validation
    input logic [13:0]                           route_in
);


// With single port design, this module validates incoming routes
// using sender_id from route_in against allowed sender in route_ctrl

logic [3:0] allowed_sender_reg;  // Allowed sender_id from route_ctrl
logic [3:0] incoming_sender;     // Sender_id from incoming route
logic       route_valid;         // Validation result

// Store allowed sender from route_ctrl and validate incoming routes
always_ff @(posedge aclk) begin
    if (~aresetn) begin
        allowed_sender_reg <= '0;
        incoming_sender <= '0;
        route_valid <= 1'b0;
    end else begin
        // Extract allowed sender from route_ctrl: [9:6] = sender_id
        allowed_sender_reg <= route_ctrl[9:6];

        // Extract sender from incoming route: [9:6] = sender_id
        incoming_sender <= route_in[9:6];

        // Validate: incoming sender must match allowed sender
        // sender_id = 0 means external/any sender allowed
        route_valid <= (incoming_sender == allowed_sender_reg) ||
                       (allowed_sender_reg == 4'b0000) ||  // Allow any sender
                       (incoming_sender == 4'b0000);       // External sender
    end
end


endmodule
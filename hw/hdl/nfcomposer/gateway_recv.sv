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

// HARDCODED_TEST_MODE: When defined, skip capability validation for testing.
// All incoming routes are considered valid. Remove for production.
`define HARDCODED_TEST_MODE

/**
 * vFIU Gateway Recv Module - Incoming Route Validation for All 5 Paths
 *
 * This module validates incoming data routes for the vFIU.
 * It handles ALL 5 incoming data paths:
 *
 * 1. HOST (sender_id == N_REGIONS): BYPASS (trusted - DMA read response)
 * 2. RDMA (sender_id == N_REGIONS+2): BYPASS (trusted - RDMA stack)
 * 3. TCP  (sender_id == N_REGIONS+3): BYPASS (trusted - TCP stack)
 * 4. BYPASS (sender_id == N_REGIONS+4): BYPASS (trusted - bypass stack)
 * 5. P2P  (sender_id < N_REGIONS): VALIDATE against route_ctrl
 *
 * TRUSTED PATHS (Host, RDMA, TCP, Bypass):
 * - Data from infrastructure stacks that already have their own validation
 * - Pass through without additional validation (bypass = true)
 *
 * UNTRUSTED PATH (P2P):
 * - Data from other vFPGAs must be explicitly allowed by route_ctrl
 * - Validates sender_id against configured allowed sender
 *
 * HARDCODED_TEST_MODE: Skips validation, all routes are accepted.
 *
 * Naming convention:
 *   - VIU (network level): gateway_tx (outgoing), gateway_rx (incoming)
 *   - vFIU (memory level): gateway_send, gateway_recv
 */
module gateway_recv #(
    parameter N_DESTS = 1,
    parameter ID = 0,                // vFPGA ID for bypass check
    parameter integer N_REGIONS = 2  // Number of vFPGA regions (for port identification)
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // Routing capability configuration from host
    input logic [13:0]                           route_ctrl,

    // Incoming route for validation (for network/bypass traffic)
    input logic [13:0]                           route_in,

    // Validation result outputs (for network/bypass traffic)
    output logic                                 route_valid,
    output logic                                 route_bypass,  // Indicates trusted bypass path

    // Host RX data path (Host → vFPGA via vIO Switch)
    AXI4SR.s                                    s_axis_host,    // From vIO Switch (with route_id)
    AXI4S.m                                     m_axis_host     // To vFPGA (stripped tdest)

    // NOTE: P2P data path is NOT handled here.
    // P2P traffic flows through vio_switch routing to user_wrapper's data ports.
    // VIU already validates P2P traffic, so no additional validation needed at vFIU level.
);

    // ========================================================================================
    // PORT ASSIGNMENTS
    // ========================================================================================
    // Port IDs for identifying trusted sources (sender_id in route_id)
    // These ports are trusted infrastructure stacks - their data bypasses validation
    localparam logic [3:0] PORT_HOST_TX   = N_REGIONS;     // DMA read response data
    localparam logic [3:0] PORT_HOST_RX   = N_REGIONS + 1; // DMA write data (not used in recv)
    localparam logic [3:0] PORT_RDMA      = N_REGIONS + 2; // RDMA stack
    localparam logic [3:0] PORT_TCP       = N_REGIONS + 3; // TCP stack
    localparam logic [3:0] PORT_BYPASS_RX = N_REGIONS + 4; // Bypass stack

    // Helper function: Check if sender_id indicates a trusted source
    // Trusted sources: Host, RDMA, TCP, Bypass (sender_id >= N_REGIONS)
    function automatic logic is_trusted_sender(input logic [3:0] sender_id);
        return (sender_id == PORT_HOST_TX) ||
               (sender_id == PORT_RDMA) ||
               (sender_id == PORT_TCP) ||
               (sender_id == PORT_BYPASS_RX);
    endfunction

    // ========================================================================================
    // HOST RX DATA PATH (Host → vFPGA) - ALWAYS BYPASS (Trusted Source)
    // ========================================================================================
    // Host data comes from DMA (PORT_HOST_TX) which is trusted infrastructure.
    // The original request was already validated by gate_mem, so the response
    // data is inherently trusted and bypasses validation.

    logic [3:0] host_incoming_sender;
    logic       host_route_bypass;
    logic       host_route_valid;

    // Extract sender_id from tdest: [9:6] = sender_id
    assign host_incoming_sender = s_axis_host.tdest[9:6];

    // Host path is ALWAYS trusted (bypass = true)
    // sender_id should be PORT_HOST_TX, but we don't need to validate it
    assign host_route_bypass = 1'b1;

`ifdef HARDCODED_TEST_MODE
    // Skip validation for testing - all routes are valid
    assign host_route_valid = 1'b1;
`else
    // Host is always valid (trusted source)
    assign host_route_valid = 1'b1;
`endif

    // Host RX Data path: strip tdest, pass through (always valid)
    assign m_axis_host.tvalid = s_axis_host.tvalid & host_route_valid;
    assign m_axis_host.tdata  = s_axis_host.tdata;
    assign m_axis_host.tkeep  = s_axis_host.tkeep;
    assign m_axis_host.tlast  = s_axis_host.tlast;
    assign s_axis_host.tready = m_axis_host.tready & host_route_valid;

    // ========================================================================================
    // NOTE: P2P DATA PATH NOT HANDLED HERE
    // ========================================================================================
    // P2P traffic flows through vio_switch routing to user_wrapper's data ports.
    // VIU already validates P2P traffic at the network boundary, so no additional
    // validation is needed at the vFIU level.
    //
    // For P2P:
    // - TX: vFPGA → gateway_send → vio_switch → target_vFPGA
    // - RX: vio_switch → user_wrapper (bypasses vFIU)

    // ========================================================================================
    // GENERAL ROUTE VALIDATION (for route_in signal from vIO Switch)
    // ========================================================================================
    // This validates the route_in signal which carries routing info for all paths.
    // The validation logic is:
    // - TRUSTED (sender_id >= N_REGIONS): BYPASS - always valid
    // - UNTRUSTED (sender_id < N_REGIONS): VALIDATE against route_ctrl

    logic [3:0] allowed_sender_reg;  // Allowed sender_id from route_ctrl
    logic [3:0] incoming_sender;     // Sender_id from incoming route
    logic       is_trusted;          // Is the sender a trusted infrastructure port?

    // Store allowed sender from route_ctrl and validate incoming routes
    always_ff @(posedge aclk) begin
        if (~aresetn) begin
            allowed_sender_reg <= '0;
            incoming_sender <= '0;
            route_valid <= 1'b0;
            route_bypass <= 1'b0;
            is_trusted <= 1'b0;
        end else begin
            // Extract allowed sender from route_ctrl: [9:6] = sender_id
            allowed_sender_reg <= route_ctrl[9:6];

            // Extract sender from incoming route: [9:6] = sender_id
            incoming_sender <= route_in[9:6];

            // Check if sender is trusted infrastructure (Host, RDMA, TCP, Bypass)
            is_trusted <= is_trusted_sender(route_in[9:6]);

            // Bypass check: trusted sources always bypass validation
            route_bypass <= is_trusted_sender(route_in[9:6]);

`ifdef HARDCODED_TEST_MODE
            // Skip validation for testing - all routes are valid
            route_valid <= 1'b1;
`else
            // Validation logic:
            // - Trusted sources (Host, RDMA, TCP, Bypass): ALWAYS valid
            // - P2P (sender_id < N_REGIONS): validate against route_ctrl
            if (is_trusted_sender(route_in[9:6])) begin
                // Trusted source - bypass validation
                route_valid <= 1'b1;
            end else begin
                // Untrusted source (P2P) - validate
                route_valid <= (route_in[9:6] == route_ctrl[9:6]) ||  // Sender matches allowed
                               (route_ctrl[9:6] == 4'b0000);          // Any sender allowed
            end
`endif
        end
    end

endmodule
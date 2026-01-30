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
// All outgoing routes are considered valid. Remove for production.
`define HARDCODED_TEST_MODE

/**
 * vFIU Gateway Send Module - Outgoing Route Validation & Attachment for All 6 Paths
 *
 * This module validates and attaches route_id (as tdest) to ALL outgoing data
 * for vIO Switch routing. It handles ALL 6 outgoing data paths:
 *
 * 1. HOST TX:       vFPGA → Host DMA      - BYPASS validation (trusted, route to HOST_RX)
 * 2. RDMA TX REQ:   vFPGA → RDMA stack    - VALIDATE then route to PORT_RDMA_TX_REQ
 * 3. RDMA TX RSP:   vFPGA → RDMA stack    - VALIDATE then route to PORT_RDMA_TX_RSP
 * 4. TCP TX:        vFPGA → TCP stack     - VALIDATE then route to PORT_TCP
 * 5. BYPASS TX:     vFPGA → Bypass stack  - VALIDATE then route to PORT_BYPASS_TX
 * 6. P2P TX:        vFPGA → Another vFPGA - VALIDATE then route to target vFPGA
 *
 * TRUSTED PATH (Host TX):
 * - Data going to Host DMA is trusted (request already validated by gate_mem)
 * - Bypasses validation, just attaches route_id for HOST_RX port
 *
 * VALIDATED PATHS (RDMA REQ/RSP, TCP, Bypass, P2P):
 * - Must be validated against route_ctrl before sending
 * - Validation checks receiver_id against allowed destinations in route_ctrl
 *
 * Route ID format (14 bits):
 *   [13:10] reserved
 *   [9:6]   sender_id (this vFPGA's ID)
 *   [5:2]   receiver_id (destination vFPGA or port)
 *   [1:0]   flags
 *
 * HARDCODED_TEST_MODE: Skips validation, all routes are accepted.
 *
 * Naming convention:
 *   - VIU (network level): gateway_tx (outgoing), gateway_rx (incoming)
 *   - vFIU (memory level): gateway_send, gateway_recv
 */
module gateway_send #(
    parameter N_DESTS = 1,
    parameter ID = 0,                // vFPGA ID (sender_id in route)
    parameter integer N_REGIONS = 2  // Number of vFPGA regions
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // Routing capability configuration from host
    input  logic [13:0]                         route_ctrl,

    // Validation result outputs
    output logic                                route_valid,

    // ========================================================================
    // Path 1: HOST TX (vFPGA → Host DMA) - BYPASS validation
    // ========================================================================
    AXI4S.s                                     s_axis_host,    // From vFPGA (data only)
    AXI4SR.m                                    m_axis_host,    // To vIO Switch (tdest = route_id)

    // ========================================================================
    // Path 2: RDMA TX REQ (vFPGA → RDMA stack read requests) - VALIDATED
    // ========================================================================
    AXI4S.s                                     s_axis_rdma_req,    // From vFPGA (data only)
    AXI4SR.m                                    m_axis_rdma_req,    // To vIO Switch (tdest = route_id)

    // ========================================================================
    // Path 3: RDMA TX RSP (vFPGA → RDMA stack read responses) - VALIDATED
    // ========================================================================
    AXI4S.s                                     s_axis_rdma_rsp,    // From vFPGA (data only)
    AXI4SR.m                                    m_axis_rdma_rsp,    // To vIO Switch (tdest = route_id)

    // ========================================================================
    // Path 4: TCP TX (vFPGA → TCP stack) - VALIDATED
    // ========================================================================
    AXI4S.s                                     s_axis_tcp,     // From vFPGA (data only)
    AXI4SR.m                                    m_axis_tcp,     // To vIO Switch (tdest = route_id)

    // ========================================================================
    // Path 5: BYPASS TX (vFPGA → Bypass stack) - VALIDATED
    // ========================================================================
    AXI4S.s                                     s_axis_bypass,  // From vFPGA (data only)
    AXI4SR.m                                    m_axis_bypass,  // To vIO Switch (tdest = route_id)

    // ========================================================================
    // Path 6: P2P TX (vFPGA → Another vFPGA) - VALIDATED (AXI4SR with tid)
    // ========================================================================
    AXI4SR.s                                    s_axis_p2p,     // From vFPGA (AXI4SR with tid)
    AXI4SR.m                                    m_axis_p2p      // To vIO Switch (tdest = route_id)
);

    // ========================================================================================
    // PORT ASSIGNMENTS
    // ========================================================================================
    // Infrastructure port IDs (receiver_id in route_id)
    // Must match vio_switch port indices for correct routing
    localparam logic [3:0] PORT_HOST_TX      = N_REGIONS;     // DMA read response (not used in send)
    localparam logic [3:0] PORT_HOST_RX      = N_REGIONS + 1; // DMA write data destination
    localparam logic [3:0] PORT_RDMA_RX      = N_REGIONS + 2; // RDMA RX (not used in send - for recv only)
    localparam logic [3:0] PORT_RDMA_TX_REQ  = N_REGIONS + 3; // RDMA TX REQ (read requests to stack)
    localparam logic [3:0] PORT_RDMA_TX_RSP  = N_REGIONS + 4; // RDMA TX RSP (read responses to stack)
    localparam logic [3:0] PORT_TCP          = N_REGIONS + 5; // TCP stack
    localparam logic [3:0] PORT_BYPASS_RX    = N_REGIONS + 6; // Bypass RX (not used in send)
    localparam logic [3:0] PORT_BYPASS_TX    = N_REGIONS + 7; // Bypass TX REQ
    localparam logic [3:0] PORT_BYPASS_TX_RSP = N_REGIONS + 8; // Bypass TX RSP (unused - merged with REQ)

    // Helper function: Build route_id with this vFPGA as sender
    // Route format: [13:10]=reserved, [9:6]=sender_id, [5:2]=receiver_id, [1:0]=flags
    function automatic logic [13:0] build_route_id(input logic [3:0] receiver_id);
        return {4'b0, ID[3:0], receiver_id, 2'b0};
    endfunction

    // Helper function: Check if receiver_id is a valid infrastructure port
    function automatic logic is_infra_port(input logic [3:0] receiver_id);
        return (receiver_id == PORT_HOST_RX) ||
               (receiver_id == PORT_RDMA_TX_REQ) ||
               (receiver_id == PORT_RDMA_TX_RSP) ||
               (receiver_id == PORT_TCP) ||
               (receiver_id == PORT_BYPASS_TX);
    endfunction

    // Helper function: Check if receiver_id is a valid vFPGA (for P2P)
    function automatic logic is_valid_vfpga(input logic [3:0] receiver_id);
        return (receiver_id < N_REGIONS) && (receiver_id != ID);
    endfunction

    // ========================================================================================
    // PATH 1: HOST TX (vFPGA → Host DMA) - BYPASS validation
    // ========================================================================================
    // Host data goes to DMA which is trusted infrastructure.
    // The request was already validated by gate_mem, so the data is inherently trusted.
    // We just attach the route_id for HOST_RX port.

    assign m_axis_host.tvalid = s_axis_host.tvalid;
    assign m_axis_host.tdata  = s_axis_host.tdata;
    assign m_axis_host.tkeep  = s_axis_host.tkeep;
    assign m_axis_host.tlast  = s_axis_host.tlast;
    assign m_axis_host.tid    = ID[PID_BITS-1:0];  // Source vFPGA ID
    assign m_axis_host.tdest  = build_route_id(PORT_HOST_RX);  // Route to HOST_RX
    assign s_axis_host.tready = m_axis_host.tready;

    // ========================================================================================
    // PATH 2: RDMA TX REQ (vFPGA → RDMA stack read requests) - VALIDATED
    // ========================================================================================
    // RDMA REQ data must be validated - check if this vFPGA is allowed to send to RDMA.

    logic rdma_req_route_valid;

`ifdef HARDCODED_TEST_MODE
    assign rdma_req_route_valid = 1'b1;  // Skip validation for testing
`else
    // Validate: check if route_ctrl allows sending to RDMA port
    assign rdma_req_route_valid = (route_ctrl[5:2] == PORT_RDMA_TX_REQ) ||   // Explicit RDMA REQ allowed
                                  (route_ctrl[5:2] == 4'b0000);               // Any destination allowed
`endif

    assign m_axis_rdma_req.tvalid = s_axis_rdma_req.tvalid & rdma_req_route_valid;
    assign m_axis_rdma_req.tdata  = s_axis_rdma_req.tdata;
    assign m_axis_rdma_req.tkeep  = s_axis_rdma_req.tkeep;
    assign m_axis_rdma_req.tlast  = s_axis_rdma_req.tlast;
    assign m_axis_rdma_req.tid    = ID[PID_BITS-1:0];
    assign m_axis_rdma_req.tdest  = build_route_id(PORT_RDMA_TX_REQ);  // Route to RDMA TX REQ port
    assign s_axis_rdma_req.tready = m_axis_rdma_req.tready & rdma_req_route_valid;

    // ========================================================================================
    // PATH 3: RDMA TX RSP (vFPGA → RDMA stack read responses) - VALIDATED
    // ========================================================================================
    // RDMA RSP data must be validated - check if this vFPGA is allowed to send to RDMA.

    logic rdma_rsp_route_valid;

`ifdef HARDCODED_TEST_MODE
    assign rdma_rsp_route_valid = 1'b1;  // Skip validation for testing
`else
    // Validate: check if route_ctrl allows sending to RDMA port
    assign rdma_rsp_route_valid = (route_ctrl[5:2] == PORT_RDMA_TX_RSP) ||   // Explicit RDMA RSP allowed
                                  (route_ctrl[5:2] == 4'b0000);               // Any destination allowed
`endif

    assign m_axis_rdma_rsp.tvalid = s_axis_rdma_rsp.tvalid & rdma_rsp_route_valid;
    assign m_axis_rdma_rsp.tdata  = s_axis_rdma_rsp.tdata;
    assign m_axis_rdma_rsp.tkeep  = s_axis_rdma_rsp.tkeep;
    assign m_axis_rdma_rsp.tlast  = s_axis_rdma_rsp.tlast;
    assign m_axis_rdma_rsp.tid    = ID[PID_BITS-1:0];
    assign m_axis_rdma_rsp.tdest  = build_route_id(PORT_RDMA_TX_RSP);  // Route to RDMA TX RSP port
    assign s_axis_rdma_rsp.tready = m_axis_rdma_rsp.tready & rdma_rsp_route_valid;

    // ========================================================================================
    // PATH 4: TCP TX (vFPGA → TCP stack) - VALIDATED
    // ========================================================================================
    // TCP data must be validated - check if this vFPGA is allowed to send to TCP.

    logic tcp_route_valid;

`ifdef HARDCODED_TEST_MODE
    assign tcp_route_valid = 1'b1;  // Skip validation for testing
`else
    // Validate: check if route_ctrl allows sending to TCP port
    assign tcp_route_valid = (route_ctrl[5:2] == PORT_TCP) ||     // Explicit TCP allowed
                             (route_ctrl[5:2] == 4'b0000);         // Any destination allowed
`endif

    assign m_axis_tcp.tvalid = s_axis_tcp.tvalid & tcp_route_valid;
    assign m_axis_tcp.tdata  = s_axis_tcp.tdata;
    assign m_axis_tcp.tkeep  = s_axis_tcp.tkeep;
    assign m_axis_tcp.tlast  = s_axis_tcp.tlast;
    assign m_axis_tcp.tid    = ID[PID_BITS-1:0];
    assign m_axis_tcp.tdest  = build_route_id(PORT_TCP);  // Route to TCP
    assign s_axis_tcp.tready = m_axis_tcp.tready & tcp_route_valid;

    // ========================================================================================
    // PATH 5: BYPASS TX (vFPGA → Bypass stack) - VALIDATED
    // ========================================================================================
    // Bypass data must be validated - check if this vFPGA is allowed to send to Bypass.

    logic bypass_route_valid;

`ifdef HARDCODED_TEST_MODE
    assign bypass_route_valid = 1'b1;  // Skip validation for testing
`else
    // Validate: check if route_ctrl allows sending to Bypass port
    assign bypass_route_valid = (route_ctrl[5:2] == PORT_BYPASS_TX) ||  // Explicit Bypass allowed
                                (route_ctrl[5:2] == 4'b0000);            // Any destination allowed
`endif

    assign m_axis_bypass.tvalid = s_axis_bypass.tvalid & bypass_route_valid;
    assign m_axis_bypass.tdata  = s_axis_bypass.tdata;
    assign m_axis_bypass.tkeep  = s_axis_bypass.tkeep;
    assign m_axis_bypass.tlast  = s_axis_bypass.tlast;
    assign m_axis_bypass.tid    = ID[PID_BITS-1:0];
    assign m_axis_bypass.tdest  = build_route_id(PORT_BYPASS_TX);  // Route to Bypass
    assign s_axis_bypass.tready = m_axis_bypass.tready & bypass_route_valid;

    // ========================================================================================
    // PATH 6: P2P TX (vFPGA → Another vFPGA) - VALIDATED
    // ========================================================================================
    // P2P data must be validated - check if this vFPGA is allowed to send to target vFPGA.
    // The target vFPGA ID comes from route_ctrl (single destination capability per vFIU).

    logic [3:0] p2p_target_vfpga;
    logic       p2p_route_valid;

    // Get target vFPGA from route_ctrl (vFIU stores single destination capability)
    // route_ctrl[5:2] = receiver_id (the target vFPGA)
    assign p2p_target_vfpga = route_ctrl[5:2];

`ifdef HARDCODED_TEST_MODE
    assign p2p_route_valid = 1'b1;  // Skip validation for testing
`else
    // Validate: target vFPGA must be valid (not self, not out of range)
    // route_ctrl[5:2] already contains the allowed target, so we just verify it's valid
    assign p2p_route_valid = is_valid_vfpga(p2p_target_vfpga);
`endif

    assign m_axis_p2p.tvalid = s_axis_p2p.tvalid & p2p_route_valid;
    assign m_axis_p2p.tdata  = s_axis_p2p.tdata;
    assign m_axis_p2p.tkeep  = s_axis_p2p.tkeep;
    assign m_axis_p2p.tlast  = s_axis_p2p.tlast;
    assign m_axis_p2p.tid    = ID[PID_BITS-1:0];
    // Build route_id: sender = this vFPGA, receiver = target vFPGA
    assign m_axis_p2p.tdest  = build_route_id(p2p_target_vfpga);
    assign s_axis_p2p.tready = m_axis_p2p.tready & p2p_route_valid;

    // ========================================================================================
    // OVERALL ROUTE VALIDATION STATUS
    // ========================================================================================
    // Aggregate validation status for external monitoring
    // This indicates if ANY path has valid data that passed validation

    always_ff @(posedge aclk) begin
        if (~aresetn) begin
            route_valid <= 1'b0;
        end else begin
            // Route is valid if any validated path has valid data
            route_valid <= (s_axis_rdma_req.tvalid & rdma_req_route_valid) |
                          (s_axis_rdma_rsp.tvalid & rdma_rsp_route_valid) |
                          (s_axis_tcp.tvalid & tcp_route_valid) |
                          (s_axis_bypass.tvalid & bypass_route_valid) |
                          (s_axis_p2p.tvalid & p2p_route_valid) |
                          s_axis_host.tvalid;  // Host always valid (bypass)
        end
    end

endmodule

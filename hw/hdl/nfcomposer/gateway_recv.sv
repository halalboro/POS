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
 * - P2P is the ONLY path that requires validation at vFIU level
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

    // ========================================================================
    // Path 1: HOST RX (Host DMA → vFPGA) - BYPASS validation
    // ========================================================================
    AXI4SR.s                                    s_axis_host,    // From vIO Switch (tdest = route_id)
    AXI4S.m                                     m_axis_host,    // To vFPGA (data only, no tid needed)

    // ========================================================================
    // Path 2: RDMA RX (RDMA stack → vFPGA) - BYPASS validation
    // ========================================================================
    AXI4SR.s                                    s_axis_rdma,    // From vIO Switch (tdest = route_id)
    AXI4S.m                                     m_axis_rdma,    // To vFPGA (data only)

    // ========================================================================
    // Path 3: TCP RX (TCP stack → vFPGA) - BYPASS validation
    // ========================================================================
    AXI4SR.s                                    s_axis_tcp,     // From vIO Switch (tdest = route_id)
    AXI4S.m                                     m_axis_tcp,     // To vFPGA (data only)

    // ========================================================================
    // Path 4: BYPASS RX (Bypass stack → vFPGA) - BYPASS validation
    // ========================================================================
    AXI4SR.s                                    s_axis_bypass,  // From vIO Switch (tdest = route_id)
    AXI4S.m                                     m_axis_bypass,  // To vFPGA (data only)

    // ========================================================================
    // Path 5: P2P RX (vFPGA → vFPGA) - VALIDATED (AXI4SR with tid = sender)
    // ========================================================================
    AXI4SR.s                                    s_axis_p2p,     // From vIO Switch (tdest = route_id)
    AXI4SR.m                                    m_axis_p2p      // To vFPGA (tid = sender vFPGA)
);

    // ========================================================================================
    // PORT ASSIGNMENTS
    // ========================================================================================
    // Port IDs for identifying trusted sources (sender_id in route_id)
    // These ports are trusted infrastructure stacks - their data bypasses validation
    // Must match vio_switch port indices
    localparam logic [3:0] PORT_HOST_TX      = N_REGIONS;     // DMA read response data
    localparam logic [3:0] PORT_HOST_RX      = N_REGIONS + 1; // DMA write data (not used in recv)
    localparam logic [3:0] PORT_RDMA_RX      = N_REGIONS + 2; // RDMA RX (data from RDMA stack to vFPGA)
    localparam logic [3:0] PORT_RDMA_TX      = N_REGIONS + 3; // RDMA TX REQ (not used in recv)
    localparam logic [3:0] PORT_RDMA_TX_RSP  = N_REGIONS + 4; // RDMA TX RSP (not used in recv)
    localparam logic [3:0] PORT_TCP          = N_REGIONS + 5; // TCP stack
    localparam logic [3:0] PORT_BYPASS_RX    = N_REGIONS + 6; // Bypass RX (data from bypass stack to vFPGA)
    localparam logic [3:0] PORT_BYPASS_TX    = N_REGIONS + 7; // Bypass TX REQ (not used in recv)
    localparam logic [3:0] PORT_BYPASS_TX_RSP = N_REGIONS + 8; // Bypass TX RSP (not used in recv)

    // Helper function: Check if sender_id indicates a trusted source
    // Trusted sources: Host, RDMA, TCP, Bypass (sender_id >= N_REGIONS)
    function automatic logic is_trusted_sender(input logic [3:0] sender_id);
        return (sender_id == PORT_HOST_TX) ||
               (sender_id == PORT_RDMA_RX) ||
               (sender_id == PORT_TCP) ||
               (sender_id == PORT_BYPASS_RX);
    endfunction

    // ========================================================================================
    // PATH 1: HOST RX (Host DMA → vFPGA) - BYPASS validation (Trusted Source)
    // ========================================================================================
    // Host data comes from DMA (PORT_HOST_TX) which is trusted infrastructure.
    // The original request was already validated by gate_mem, so the response
    // data is inherently trusted. Simply pass through as AXI4S (strip tdest).

    assign m_axis_host.tvalid = s_axis_host.tvalid;
    assign m_axis_host.tdata  = s_axis_host.tdata;
    assign m_axis_host.tkeep  = s_axis_host.tkeep;
    assign m_axis_host.tlast  = s_axis_host.tlast;
    assign s_axis_host.tready = m_axis_host.tready;

    // ========================================================================================
    // PATH 2: RDMA RX (RDMA stack → vFPGA) - BYPASS validation (Trusted Source)
    // ========================================================================================
    // RDMA data comes from the RDMA stack which is trusted infrastructure.
    // Pass through without validation.

    assign m_axis_rdma.tvalid = s_axis_rdma.tvalid;
    assign m_axis_rdma.tdata  = s_axis_rdma.tdata;
    assign m_axis_rdma.tkeep  = s_axis_rdma.tkeep;
    assign m_axis_rdma.tlast  = s_axis_rdma.tlast;
    assign s_axis_rdma.tready = m_axis_rdma.tready;

    // ========================================================================================
    // PATH 3: TCP RX (TCP stack → vFPGA) - BYPASS validation (Trusted Source)
    // ========================================================================================
    // TCP data comes from the TCP stack which is trusted infrastructure.
    // Pass through without validation.

    assign m_axis_tcp.tvalid = s_axis_tcp.tvalid;
    assign m_axis_tcp.tdata  = s_axis_tcp.tdata;
    assign m_axis_tcp.tkeep  = s_axis_tcp.tkeep;
    assign m_axis_tcp.tlast  = s_axis_tcp.tlast;
    assign s_axis_tcp.tready = m_axis_tcp.tready;

    // ========================================================================================
    // PATH 4: BYPASS RX (Bypass stack → vFPGA) - BYPASS validation (Trusted Source)
    // ========================================================================================
    // Bypass data comes from the Bypass stack which is trusted infrastructure.
    // Pass through without validation.

    assign m_axis_bypass.tvalid = s_axis_bypass.tvalid;
    assign m_axis_bypass.tdata  = s_axis_bypass.tdata;
    assign m_axis_bypass.tkeep  = s_axis_bypass.tkeep;
    assign m_axis_bypass.tlast  = s_axis_bypass.tlast;
    assign s_axis_bypass.tready = m_axis_bypass.tready;

    // ========================================================================================
    // PATH 5: P2P RX (vFPGA → vFPGA) - VALIDATED (Untrusted Source)
    // ========================================================================================
    // P2P data comes from other vFPGAs and MUST be validated against route_ctrl.
    // This is the ONLY path that requires validation - all other paths (Host, RDMA, TCP,
    // Bypass) are trusted infrastructure and bypass validation.
    //
    // Validation checks:
    // - sender_id (from tdest[9:6]) must match allowed sender in route_ctrl
    // - OR route_ctrl allows any sender (route_ctrl[9:6] == 0)

    logic [3:0] p2p_incoming_sender;
    logic       p2p_route_valid;

    // Extract sender_id from tdest: [9:6] = sender_id
    assign p2p_incoming_sender = s_axis_p2p.tdest[9:6];

`ifdef HARDCODED_TEST_MODE
    // Skip validation for testing - all P2P routes are valid
    assign p2p_route_valid = 1'b1;
`else
    // P2P validation: sender must match route_ctrl or any sender allowed
    assign p2p_route_valid = (p2p_incoming_sender == route_ctrl[9:6]) ||  // Sender matches allowed
                             (route_ctrl[9:6] == 4'b0000);                 // Any sender allowed
`endif

    // P2P RX Data path: pass through only if valid (AXI4SR with tid = sender vFPGA)
    assign m_axis_p2p.tvalid = s_axis_p2p.tvalid & p2p_route_valid;
    assign m_axis_p2p.tdata  = s_axis_p2p.tdata;
    assign m_axis_p2p.tkeep  = s_axis_p2p.tkeep;
    assign m_axis_p2p.tlast  = s_axis_p2p.tlast;
    assign m_axis_p2p.tid    = p2p_incoming_sender;  // Pass sender vFPGA ID as tid to user
    assign m_axis_p2p.tdest  = s_axis_p2p.tdest;     // Pass through tdest
    assign s_axis_p2p.tready = m_axis_p2p.tready & p2p_route_valid;

endmodule

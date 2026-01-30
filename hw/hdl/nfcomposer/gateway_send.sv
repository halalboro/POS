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
 * Gateway Send Module - Outgoing Route Attachment for All 5 Paths
 *
 * This module attaches route_id (as tdest) to ALL outgoing data for vIO Switch routing.
 * It handles ALL 5 outgoing data paths:
 *
 * 1. HOST TX: vFPGA → Host DMA (receiver = PORT_HOST_RX)
 * 2. RDMA TX: vFPGA → RDMA stack (receiver = PORT_RDMA)
 * 3. TCP TX:  vFPGA → TCP stack (receiver = PORT_TCP)
 * 4. BYPASS TX: vFPGA → Bypass stack (receiver = PORT_BYPASS_TX)
 * 5. P2P TX: vFPGA → Another vFPGA (receiver = target vFPGA ID)
 *
 * Route ID format (14 bits):
 *   [13:10] reserved
 *   [9:6]   sender_id (this vFPGA's ID)
 *   [5:2]   receiver_id (destination vFPGA or port)
 *   [1:0]   flags
 *
 * For infrastructure paths (Host, RDMA, TCP, Bypass):
 *   - sender_id = this vFPGA's ID
 *   - receiver_id = corresponding infrastructure port
 *
 * For P2P path:
 *   - sender_id = this vFPGA's ID
 *   - receiver_id = target vFPGA ID (from user)
 *
 * HARDCODED_TEST_MODE for shell_2:
 *   - vFPGA 0 (ID=0): route_out = 14'h0004 (send to vFPGA 1)
 *   - vFPGA 1 (ID=1): route_out = 14'h0000 (send to network/RDMA TX)
 */
module gateway_send #(
    parameter N_DESTS = 1,
    parameter ID = 0,              // vFPGA ID for hardcoded test mode
    parameter integer N_REGIONS = 2  // Number of vFPGA regions (for port calculation)
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // Routing capability configuration from host
    input  logic [13:0]                         route_ctrl,

    // Route output to vIO Switch (for network/bypass traffic)
    output logic [13:0]                         route_out,

    // Host TX data path (vFPGA → Host via vIO Switch)
    AXI4S.s                                     s_axis_host,    // From vFPGA (write data to host)
    AXI4SR.m                                    m_axis_host     // To vIO Switch (with tdest for routing)
);

    // ========================================================================================
    // PORT ASSIGNMENTS
    // ========================================================================================
    // Infrastructure port IDs (receiver_id in route_id)
    localparam logic [3:0] PORT_HOST_TX   = N_REGIONS;     // DMA read response (not used in send)
    localparam logic [3:0] PORT_HOST_RX   = N_REGIONS + 1; // DMA write data destination
    localparam logic [3:0] PORT_RDMA      = N_REGIONS + 2; // RDMA stack
    localparam logic [3:0] PORT_TCP       = N_REGIONS + 3; // TCP stack
    localparam logic [3:0] PORT_BYPASS_TX = N_REGIONS + 4; // Bypass stack

    // Helper function: Build route_id with this vFPGA as sender
    // Route format: [13:10]=reserved, [9:6]=sender_id, [5:2]=receiver_id, [1:0]=flags
    function automatic logic [13:0] build_route_id(input logic [3:0] receiver_id);
        return {4'b0, ID[3:0], receiver_id, 2'b0};
    endfunction

    // ========================================================================================
    // HOST TX DATA PATH (vFPGA → Host DMA)
    // ========================================================================================
    // Attach tdest for vIO Switch routing to HOST_RX port.
    // The data itself passes through since the request was already validated by gate_mem.
    // tdest format: [13:10]=reserved, [9:6]=sender_id, [5:2]=receiver_id, [1:0]=flags

    assign m_axis_host.tvalid = s_axis_host.tvalid;
    assign m_axis_host.tdata  = s_axis_host.tdata;
    assign m_axis_host.tkeep  = s_axis_host.tkeep;
    assign m_axis_host.tlast  = s_axis_host.tlast;
    assign m_axis_host.tid    = ID[PID_BITS-1:0];  // Source vFPGA ID
    // Route to HOST_RX: sender_id = this vFPGA, receiver_id = HOST_RX port
    assign m_axis_host.tdest  = build_route_id(PORT_HOST_RX);
    assign s_axis_host.tready = m_axis_host.tready;

    // ========================================================================================
    // ROUTE_OUT FOR OTHER PATHS (RDMA, TCP, Bypass, P2P)
    // ========================================================================================
    // For paths other than Host TX (which has its own data interface above), the route_out
    // signal is used by the TX mux in dynamic_top to set tdest for outgoing data.
    //
    // In HARDCODED_TEST_MODE, route_out is set based on vFPGA ID for testing.
    // In production, route_out comes from route_ctrl configured by the host.
    //
    // The actual route_id attachment for RDMA/TCP/Bypass/P2P is done in the TX mux
    // in dynamic_top (gen_vfiu_connections block), which uses this route_out value.

    // ========================================================================================
    // ROUTING CAPABILITY LOGIC (for network/bypass traffic)
    // ========================================================================================
    // With single port design, route_out directly comes from route_ctrl
    // The route_ctrl is set by the host/control plane to define allowed routing

`ifdef HARDCODED_TEST_MODE
    // Hardcoded route_out for shell_2 testing:
    // - vFPGA 0: send to vFPGA 1 (P2P loopback test)
    // - vFPGA 1: send to RDMA TX REQ port (for network transmission via VIU)
    //
    // Route ID format: [13:10]=reserved, [9:6]=sender_id, [5:2]=receiver_id, [1:0]=flags
    //
    // Port assignments for vio_switch_2 (N_REGIONS=2):
    //   Port 0: vFPGA 0, Port 1: vFPGA 1
    //   Port 2: HOST_TX, Port 3: HOST_RX
    //   Port 4: RDMA_RX, Port 5: RDMA_TX_REQ, Port 6: RDMA_TX_RSP
    //   Port 7: TCP, Port 8: BYPASS_RX, Port 9: BYPASS_TX_REQ, Port 10: BYPASS_TX_RSP
    //
    // vFPGA 0 -> vFPGA 1: sender=0, receiver=1 => 14'b0000_0000_0001_00 = 14'h0004
    // vFPGA 1 -> RDMA_TX_RSP: sender=1, receiver=6 => 14'b0000_0001_0110_00 = 14'h0058
    // Note: Using RDMA_TX_RSP (port 6) for RDMA write responses, not RDMA_TX_REQ (port 5)
    localparam logic [13:0] HARDCODED_ROUTE_VFPGA0 = 14'h0004;  // Route to vFPGA 1
    localparam logic [13:0] HARDCODED_ROUTE_VFPGA1 = 14'h0058;  // Route to RDMA_TX_RSP (port 6)

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

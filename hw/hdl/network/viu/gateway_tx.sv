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
 * VIU Gateway TX Module - TX Path Routing Capability Validation
 *
 * This module validates outgoing packets based on the route_id received from
 * the vIO Switch (via tdest on the TX AXI-Stream). It determines the VLAN tag
 * parameters to be encoded by vlan_tagger.
 *
 * DEPLOYMENT MODELS:
 *   1. Multi-FPGA Model: vFPGA-to-vFPGA communication
 *      - route_in from vIO Switch contains: src_node+vfpga (this vFPGA), dst_node+vfpga (dest)
 *      - Validates that this vFPGA is allowed to send to the destination
 *      - route_out passes through to vlan_tagger for VLAN encoding
 *
 *   2. SmartNIC Model: vFPGA sending to external clients
 *      - route_in: src_node+vfpga = this vFPGA, dst_node+vfpga = 0 (external)
 *      - All zeros for destination indicates external network
 *
 * Route Format (14 bits - Multi-FPGA with Node ID):
 *   [13:12] = src_node_id  (2 bits - source physical FPGA node, 0-3)
 *   [11:8]  = src_vfpga_id (4 bits - source vFPGA on that node, 0-15)
 *   [7:6]   = dst_node_id  (2 bits - destination physical FPGA node, 0-3)
 *   [5:2]   = dst_vfpga_id (4 bits - destination vFPGA on that node, 0-15)
 *   [1:0]   = reserved
 *
 * This supports: 4 nodes × 16 vFPGAs = 64 total vFPGAs in closed network
 * Node ID 0 + vFPGA ID 0 = external network
 *
 * Unlike vFIU's gateway_send (which does memory bounds checking), this module only
 * handles network routing permissions for VLAN-based isolation.
 *
 * Naming convention:
 *   - VIU (network level): gateway_tx (outgoing), gateway_rx (incoming)
 *   - vFIU (memory level): gateway_send, gateway_recv
 */
module gateway_tx #(
    parameter N_DESTS = 1
) (
    input  logic                        aclk,
    input  logic                        aresetn,

    // Routing capability configuration from host
    // Format: [13:12]=this_node_id, [11:8]=this_vfpga_id, [7:6]=allowed_dst_node, [5:2]=allowed_dst_vfpga, [1:0]=index
    // Host programs which destination node+vfpga this vFPGA is allowed to send to
    input  logic [13:0]                 route_ctrl,

    // Route from vIO Switch TX path (from packet's tdest field)
    // Format: [13:12]=src_node, [11:8]=src_vfpga, [7:6]=dst_node, [5:2]=dst_vfpga, [1:0]=reserved
    // This tells us which destination the packet wants to reach
    input  logic [13:0]                 route_in,

    // Output route for VLAN tag insertion
    // Format: [13:12]=src_node, [11:8]=src_vfpga, [7:6]=dst_node, [5:2]=dst_vfpga, [1:0]=reserved
    // Passed to vlan_tagger to encode in VLAN tag
    output logic [13:0]                 route_out
);

// ============================================================================
// Internal Signals
// ============================================================================
// Capability table: stores allowed destination (node_id + vfpga_id) for each index
// Host configures which destinations this vFPGA can send to
logic [N_DESTS-1:0][5:0] allowed_dest_reg;  // [5:4]=node_id (2 bits), [3:0]=vfpga_id (4 bits)

// This vFPGA's identity (configured by host, same for all outgoing packets)
logic [1:0] this_node_id_reg;    // This physical node ID (0-3)
logic [3:0] this_vfpga_id_reg;   // This vFPGA ID on node (0-15)

// Extracted fields from route_ctrl (host configuration)
logic [1:0] cfg_this_node_id;     // This node's ID (2 bits)
logic [3:0] cfg_this_vfpga_id;    // This vFPGA's ID (4 bits)
logic [1:0] cfg_allowed_dst_node; // Allowed destination node (2 bits)
logic [3:0] cfg_allowed_dst_vfpga;// Allowed destination vFPGA (4 bits)
logic [1:0] cfg_index;            // Index for capability table

// Extracted fields from route_in (from vIO Switch / packet tdest)
logic [1:0] src_node_id;         // Source node (should match this node)
logic [3:0] src_vfpga_id;        // Source vFPGA (should match this vFPGA)
logic [1:0] dst_node_id;         // Destination node
logic [3:0] dst_vfpga_id;        // Destination vFPGA

// Validation signals
logic       is_sender_valid;     // True if source matches this vFPGA
logic       is_dest_allowed;     // True if destination is in allowed list
logic       is_external_dest;    // True if dst_node+vfpga == 0 (external)
logic       route_valid;         // Overall route validity

// ============================================================================
// Capability Configuration and Validation
// ============================================================================
// Host configures which destinations this vFPGA is allowed to send to:
//   route_ctrl[13:12] = this_node_id (2 bits - this physical FPGA node)
//   route_ctrl[11:8]  = this_vfpga_id (4 bits - this vFPGA's ID on node)
//   route_ctrl[7:6]   = allowed_dst_node (2 bits - which node this entry allows)
//   route_ctrl[5:2]   = allowed_dst_vfpga (4 bits - which vFPGA on that node)
//   route_ctrl[1:0]   = index (capability table index)
//
// Validation logic:
//   - src_node+vfpga in route_in must match this node+vfpga
//   - dst_node+vfpga = 0 (external) is always allowed (SmartNIC model)
//   - dst_node+vfpga != 0 must be in allowed_dest_reg
//
always_ff @(posedge aclk) begin
    if (~aresetn) begin
        allowed_dest_reg <= '{default: '0};
        this_node_id_reg <= 2'b01;   // Default to node 1
        this_vfpga_id_reg <= 4'b0000;  // Default to vFPGA 0
        cfg_this_node_id <= '0;
        cfg_this_vfpga_id <= '0;
        cfg_allowed_dst_node <= '0;
        cfg_allowed_dst_vfpga <= '0;
        cfg_index <= '0;
        src_node_id <= '0;
        src_vfpga_id <= '0;
        dst_node_id <= '0;
        dst_vfpga_id <= '0;
        route_out <= '0;
        route_valid <= 1'b0;
        is_sender_valid <= 1'b0;
        is_dest_allowed <= 1'b0;
        is_external_dest <= 1'b0;
    end else begin
        // Parse host configuration (new format with 4-bit vfpga_id)
        cfg_this_node_id      <= route_ctrl[13:12];  // 2 bits
        cfg_this_vfpga_id     <= route_ctrl[11:8];   // 4 bits
        cfg_allowed_dst_node  <= route_ctrl[7:6];    // 2 bits
        cfg_allowed_dst_vfpga <= route_ctrl[5:2];    // 4 bits
        cfg_index             <= route_ctrl[1:0];    // 2 bits

        // Store this vFPGA's identity (from any config write with non-zero node or vfpga)
        if (route_ctrl[13:12] != 2'b00 || route_ctrl[11:8] != 4'b0000) begin
            this_node_id_reg  <= route_ctrl[13:12];
            this_vfpga_id_reg <= route_ctrl[11:8];
        end

        // Store capability: which destination is allowed at this index
        // Pack node_id (2 bits) + vfpga_id (4 bits) into 6 bits
        allowed_dest_reg[cfg_index] <= {cfg_allowed_dst_node, cfg_allowed_dst_vfpga};

        // Parse incoming route from vIO Switch (packet's tdest)
        // Format: [13:12]=src_node, [11:8]=src_vfpga, [7:6]=dst_node, [5:2]=dst_vfpga, [1:0]=reserved
        src_node_id  <= route_in[13:12];  // 2 bits
        src_vfpga_id <= route_in[11:8];   // 4 bits
        dst_node_id  <= route_in[7:6];    // 2 bits
        dst_vfpga_id <= route_in[5:2];    // 4 bits

        // Check if external destination (SmartNIC model)
        // External = dst_node == 0 AND dst_vfpga == 0
        is_external_dest <= (route_in[7:6] == 2'b00) && (route_in[5:2] == 4'b0000);

        // Validate sender: must match this vFPGA's identity
        is_sender_valid <= ((route_in[13:12] == this_node_id_reg) &&
                            (route_in[11:8] == this_vfpga_id_reg)) ||
                           ((route_in[13:12] == 2'b00) && (route_in[11:8] == 4'b0000));  // Allow unset

`ifdef HARDCODED_TEST_MODE
        // Skip validation for testing - all destinations are allowed
        is_dest_allowed <= 1'b1;

        // Overall validation - always valid in test mode
        route_valid <= 1'b1;
`else
        // Validate destination:
        // - External destinations (node+vfpga = 0) are always allowed
        // - Internal destinations must be in the allowed list
        is_dest_allowed <= ((route_in[7:6] == 2'b00) && (route_in[5:2] == 4'b0000)) ||  // External OK
                           ({route_in[7:6], route_in[5:2]} == allowed_dest_reg[0]) ||
                           ({route_in[7:6], route_in[5:2]} == allowed_dest_reg[1]) ||
                           ((N_DESTS > 2) && ({route_in[7:6], route_in[5:2]} == allowed_dest_reg[2])) ||
                           ((N_DESTS > 3) && ({route_in[7:6], route_in[5:2]} == allowed_dest_reg[3]));

        // Overall validation
        route_valid <= is_sender_valid && is_dest_allowed;
`endif

        // Build route_out for vlan_tagger:
        // Format: [13:12]=src_node, [11:8]=src_vfpga, [7:6]=dst_node, [5:2]=dst_vfpga, [1:0]=reserved
        // Use this node+vfpga as source (enforce identity)
        // Use destination from route_in
        route_out <= {this_node_id_reg, this_vfpga_id_reg,
                      route_in[7:6], route_in[5:2], 2'b00};

        // Debug output
        if (route_in != 14'b0) begin
            $display("VIU TX: src_node=%d src_vfpga=%d (this=%d.%d), dst_node=%d dst_vfpga=%d, valid=%d",
                     route_in[13:12], route_in[11:8],
                     this_node_id_reg, this_vfpga_id_reg,
                     route_in[7:6], route_in[5:2],
                     is_sender_valid && is_dest_allowed);
        end
    end
end

endmodule

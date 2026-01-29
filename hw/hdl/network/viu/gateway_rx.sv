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
 * VIU Gateway RX Module - RX Path Routing Validation
 *
 * This module validates incoming packets based on the VLAN tag extracted by
 * vlan_untagger. It determines the destination vFPGA based on the dst_node+vfpga
 * fields in the extracted route.
 *
 * DEPLOYMENT MODELS:
 *   1. Multi-FPGA Model: vFPGA-to-vFPGA communication
 *      - VLAN tag present: src_node+vfpga = source, dst_node+vfpga = this vFPGA
 *      - Validates source against configured allowed senders
 *      - Routes to the vFPGA matching dst_node+vfpga
 *
 *   2. SmartNIC Model: External clients sending to vFPGAs
 *      - Packets arrive without VLAN tag (or with src_node+vfpga = 0)
 *      - src_node+vfpga = 0 indicates external source
 *      - dst_node+vfpga determined by host configuration (default destination)
 *
 * Route Format (14 bits from vlan_untagger - Multi-FPGA with Node ID):
 *   [13:12] = src_node_id  (2 bits - source physical FPGA node, 0-3)
 *   [11:8]  = src_vfpga_id (4 bits - source vFPGA on that node, 0-15)
 *   [7:6]   = dst_node_id  (2 bits - destination physical FPGA node, 0-3)
 *   [5:2]   = dst_vfpga_id (4 bits - destination vFPGA on that node, 0-15)
 *   [1:0]   = reserved
 *
 * This supports: 4 nodes × 16 vFPGAs = 64 total vFPGAs in closed network
 * Node ID 0 + vFPGA ID 0 = external network
 *
 * The route_in carries the route_id that was encoded in the VLAN tag by the
 * sender. This route_id flows through the protocol stacks and is used by the
 * vIO Switch RX Selector to forward packets to the correct vFPGA.
 *
 * Naming convention:
 *   - VIU (network level): gateway_tx (outgoing), gateway_rx (incoming)
 *   - vFIU (memory level): gateway_send, gateway_recv
 */
module gateway_rx #(
    parameter N_DESTS                   = 1
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // Routing capability configuration from host
    // Format: [13:12]=allowed_src_node, [11:8]=allowed_src_vfpga, [7:6]=this_node, [5:2]=this_vfpga, [1:0]=index
    // Host programs which source node+vfpga combinations are allowed to send to this vFPGA
    input logic [13:0]                           route_ctrl,

    // Route extracted from incoming VLAN tag (from vlan_untagger)
    // Format: [13:12]=src_node, [11:8]=src_vfpga, [7:6]=dst_node, [5:2]=dst_vfpga, [1:0]=reserved
    input logic [13:0]                           route_in
);


// ============================================================================
// Internal Signals
// ============================================================================
// Capability table: stores allowed source (node_id + vfpga_id) for each index
// Index by capability slot, value is allowed source packed as [5:4]=node (2 bits), [3:0]=vfpga (4 bits)
logic [N_DESTS-1:0][5:0] allowed_src_reg;

// This vFPGA's identity (configured by host)
logic [1:0] this_node_id_reg;
logic [3:0] this_vfpga_id_reg;

// Extracted fields from route_ctrl (host configuration)
logic [1:0] cfg_allowed_src_node;   // Allowed source node (2 bits)
logic [3:0] cfg_allowed_src_vfpga;  // Allowed source vFPGA (4 bits)
logic [1:0] cfg_this_node;          // This node ID being configured (2 bits)
logic [3:0] cfg_this_vfpga;         // This vFPGA ID being configured (4 bits)
logic [1:0] cfg_index;              // Index for capability table

// Extracted fields from route_in (from VLAN tag)
logic [1:0] src_node_id;            // Source physical node (0-3)
logic [3:0] src_vfpga_id;           // Source vFPGA on node (0-15)
logic [1:0] dst_node_id;            // Destination physical node (0-3)
logic [3:0] dst_vfpga_id;           // Destination vFPGA on node (0-15)
logic       is_external_sender;     // True if src_node+vfpga == 0 (external client)
logic       is_valid_sender;        // True if sender matches capability
logic       is_for_this_vfpga;      // True if destination matches this vFPGA

// Default receiver for untagged packets (configured by host)
logic [1:0] default_node_reg;
logic [3:0] default_vfpga_reg;

// ============================================================================
// Capability Configuration and Validation
// ============================================================================
// Host configures which senders are allowed to send to this vFPGA:
//   route_ctrl[13:12] = allowed_src_node (2 bits - which source node is allowed)
//   route_ctrl[11:8]  = allowed_src_vfpga (4 bits - which vFPGA on that node)
//   route_ctrl[7:6]   = this_node (2 bits - this physical node's ID)
//   route_ctrl[5:2]   = this_vfpga (4 bits - this vFPGA's ID)
//   route_ctrl[1:0]   = index (capability table slot)
//
// Validation logic:
//   - If src_node+vfpga = 0 (external client): always valid (SmartNIC model)
//   - If src_node+vfpga != 0 (vFPGA): check against allowed_src_reg
//   - Destination must match this vFPGA's identity
//
always_ff @(posedge aclk) begin
    if (~aresetn) begin
        allowed_src_reg <= '{default: '0};
        this_node_id_reg <= 2'b01;    // Default to node 1
        this_vfpga_id_reg <= 4'b0000;   // Default to vFPGA 0
        default_node_reg <= 2'b01;
        default_vfpga_reg <= 4'b0000;
        cfg_allowed_src_node <= '0;
        cfg_allowed_src_vfpga <= '0;
        cfg_this_node <= '0;
        cfg_this_vfpga <= '0;
        cfg_index <= '0;
        src_node_id <= '0;
        src_vfpga_id <= '0;
        dst_node_id <= '0;
        dst_vfpga_id <= '0;
        is_external_sender <= 1'b0;
        is_valid_sender <= 1'b0;
        is_for_this_vfpga <= 1'b0;
    end else begin
        // Parse host configuration (new format with 4-bit vfpga_id)
        cfg_allowed_src_node  <= route_ctrl[13:12];  // 2 bits
        cfg_allowed_src_vfpga <= route_ctrl[11:8];   // 4 bits
        cfg_this_node         <= route_ctrl[7:6];    // 2 bits
        cfg_this_vfpga        <= route_ctrl[5:2];    // 4 bits
        cfg_index             <= route_ctrl[1:0];    // 2 bits

        // Store this vFPGA's identity
        if (route_ctrl[7:6] != 2'b00 || route_ctrl[5:2] != 4'b0000) begin
            this_node_id_reg  <= route_ctrl[7:6];
            this_vfpga_id_reg <= route_ctrl[5:2];
        end

        // Store capability: which source is allowed at this index
        // Pack node_id (2 bits) + vfpga_id (4 bits) into 6 bits
        allowed_src_reg[cfg_index] <= {cfg_allowed_src_node, cfg_allowed_src_vfpga};

        // Parse incoming route from VLAN tag
        // Format: [13:12]=src_node, [11:8]=src_vfpga, [7:6]=dst_node, [5:2]=dst_vfpga, [1:0]=reserved
        src_node_id  <= route_in[13:12];  // 2 bits
        src_vfpga_id <= route_in[11:8];   // 4 bits
        dst_node_id  <= route_in[7:6];    // 2 bits
        dst_vfpga_id <= route_in[5:2];    // 4 bits

        // Check if external sender (SmartNIC model)
        // External = src_node == 0 AND src_vfpga == 0
        is_external_sender <= (route_in[13:12] == 2'b00) && (route_in[11:8] == 4'b0000);

        // Check if packet is destined for this vFPGA
        is_for_this_vfpga <= (route_in[7:6] == this_node_id_reg) &&
                             (route_in[5:2] == this_vfpga_id_reg);

`ifdef HARDCODED_TEST_MODE
        // Skip validation for testing - all senders are valid
        is_valid_sender <= 1'b1;
`else
        // Validate sender:
        // - External senders (src_node+vfpga = 0) are always valid
        // - vFPGA senders must match one of the allowed sources
        is_valid_sender <= ((route_in[13:12] == 2'b00) && (route_in[11:8] == 4'b0000)) ||
                           ({route_in[13:12], route_in[11:8]} == allowed_src_reg[0]) ||
                           ({route_in[13:12], route_in[11:8]} == allowed_src_reg[1]) ||
                           ((N_DESTS > 2) && ({route_in[13:12], route_in[11:8]} == allowed_src_reg[2])) ||
                           ((N_DESTS > 3) && ({route_in[13:12], route_in[11:8]} == allowed_src_reg[3]));
`endif

        // Debug output
        if (route_in != 14'b0) begin
            $display("VIU RX: src_node=%d src_vfpga=%d, dst_node=%d dst_vfpga=%d (this=%d.%d), valid=%d",
                     route_in[13:12], route_in[11:8],
                     route_in[7:6], route_in[5:2],
                     this_node_id_reg, this_vfpga_id_reg,
                     is_valid_sender && is_for_this_vfpga);
        end
    end
end

endmodule

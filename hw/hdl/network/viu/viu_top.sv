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
 * @brief   VLAN Isolation Unit (VIU) - Network isolation gateway
 *
 * The VIU provides network-level isolation between vFPGA regions by enforcing
 * VLAN-based routing capabilities. It validates that packets are routed according
 * to configured permissions (similar to vFIU but without memory bounds checking).
 *
 * DEPLOYMENT MODELS:
 *
 *   1. Multi-FPGA Model: vFPGA-to-vFPGA communication across FPGAs
 *      - VLAN tags carry node_id + vfpga_id for both source and destination
 *      - Both TX and RX paths use VLAN tagging
 *      - Enables isolation between vFPGAs on different physical FPGAs
 *      - Supports up to 4 nodes × 16 vFPGAs = 64 total vFPGAs
 *
 *   2. SmartNIC Model: External clients communicating with vFPGAs
 *      - TX: vFPGA -> external (src_node+vfpga filled, dst_node+vfpga = 0)
 *      - RX: external -> vFPGA (packets may be untagged, all IDs = 0)
 *      - Supports traditional NIC use cases with FPGA acceleration
 *
 * TX Path: vIO Switch -> Network Stack -> VIU (add VLAN tag) -> CMAC
 *   - s_axis_tx_tdest carries route_id from vIO Switch
 *   - gateway_tx validates the route and provides route_out for VLAN encoding
 *   - vlan_tagger encodes route_out into 802.1Q VLAN tag
 *
 * RX Path: CMAC -> VIU (extract VLAN tag -> validate) -> Network Stack
 *   - vlan_untagger decodes VLAN tag to route_id (14-bit)
 *   - gateway_rx validates source and determines destination vFPGA
 *   - route_id is output on m_axis_rx_tdest, synchronized with packet data
 *   - This tdest field flows through network stacks to vIO Switch for routing
 *   - vIO Switch RX Selector uses tdest (route_id) to forward packet to correct vFPGA
 *
 * Route ID Format (14-bit):
 *   [13:12] src_node_id  (2 bits - source physical FPGA node, 0-3)
 *   [11:8]  src_vfpga_id (4 bits - source vFPGA on that node, 0-15)
 *   [7:6]   dst_node_id  (2 bits - destination physical FPGA node, 0-3)
 *   [5:2]   dst_vfpga_id (4 bits - destination vFPGA on that node, 0-15)
 *   [1:0]   reserved
 *
 * VLAN ID Format (12-bit, within 802.1Q tag):
 *   [11:10] src_node_id  (2 bits - source node, 0-3)
 *   [9:6]   src_vfpga_id (4 bits - source vFPGA, 0-15)
 *   [5:4]   dst_node_id  (2 bits - destination node, 0-3)
 *   [3:0]   dst_vfpga_id (4 bits - destination vFPGA, 0-15)
 *
 * Node ID 0 + vFPGA ID 0 = external network (outside closed VLAN network)
 *
 * The routing capability format is the same as vFIU since both feed the same
 * vIO_switch for routing to the correct vFPGA region.
 *
 *  @param N_ID  Number of vFPGA regions per node
 */
module viu_top #(
    parameter integer N_ID = N_REGIONS
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // ============================================================================
    // Routing Capability Interface
    // ============================================================================
    // Format: [13:10] reserved, [9:6] sender_id, [5:2] receiver_id, [1:0] flags
    // Same format as vFIU since both feed vIO_switch
    input  logic [13:0]                         route_ctrl,

    // ============================================================================
    // AXI4-Stream Network Interfaces (TX path - to CMAC)
    // ============================================================================
    // From network stack (untagged packets with route_id in tdest)
    input  logic [AXI_NET_BITS-1:0]             s_axis_tx_tdata,
    input  logic [AXI_NET_BITS/8-1:0]           s_axis_tx_tkeep,
    input  logic                                s_axis_tx_tlast,
    output logic                                s_axis_tx_tready,
    input  logic                                s_axis_tx_tvalid,
    input  logic [13:0]                         s_axis_tx_tdest,

    // To CMAC (VLAN tagged packets)
    output logic [AXI_NET_BITS-1:0]             m_axis_tx_tdata,
    output logic [AXI_NET_BITS/8-1:0]           m_axis_tx_tkeep,
    output logic                                m_axis_tx_tlast,
    input  logic                                m_axis_tx_tready,
    output logic                                m_axis_tx_tvalid,

    // ============================================================================
    // AXI4-Stream Network Interfaces (RX path - from CMAC)
    // ============================================================================
    // From CMAC (VLAN tagged packets)
    input  logic [AXI_NET_BITS-1:0]             s_axis_rx_tdata,
    input  logic [AXI_NET_BITS/8-1:0]           s_axis_rx_tkeep,
    input  logic                                s_axis_rx_tlast,
    output logic                                s_axis_rx_tready,
    input  logic                                s_axis_rx_tvalid,

    // To network stack (untagged packets with synchronized route_id in tdest)
    output logic [AXI_NET_BITS-1:0]             m_axis_rx_tdata,
    output logic [AXI_NET_BITS/8-1:0]           m_axis_rx_tkeep,
    output logic                                m_axis_rx_tlast,
    input  logic                                m_axis_rx_tready,
    output logic                                m_axis_rx_tvalid,
    output logic [13:0]                         m_axis_rx_tdest 
);

    // ============================================================================
    // Internal Signals
    // ============================================================================

    // Route extracted from incoming VLAN tag
    logic [13:0] extracted_route;
    logic        extracted_route_valid;

    // Route for TX path VLAN tag insertion (from gateway_tx)
    logic [13:0] tx_route_out;

    // AXI-Stream for RX path (after VLAN extraction)
    logic [AXI_NET_BITS-1:0]     axis_rx_untagged_tdata;
    logic [AXI_NET_BITS/8-1:0]   axis_rx_untagged_tkeep;
    logic                        axis_rx_untagged_tlast;
    logic                        axis_rx_untagged_tready;
    logic                        axis_rx_untagged_tvalid;
    logic [13:0]                 axis_rx_untagged_tdest;

    // ============================================================================
    // Gateway TX - TX path routing capability validation
    // ============================================================================
    // Validates outgoing packets based on route_id from vIO Switch (s_axis_tx_tdest).
    // Provides route_out (sender_id, receiver_id) to be encoded in VLAN tag.
    // The sender_id is enforced to be this vFPGA's ID (configured by host).

    gateway_tx #(
        .N_DESTS(N_ID)
    ) inst_gateway_tx (
        .aclk(aclk),
        .aresetn(aresetn),
        .route_ctrl(route_ctrl),
        .route_in(s_axis_tx_tdest),      // Route from vIO Switch (packet's destination)
        .route_out(tx_route_out)          // Route for VLAN tag encoding
    );

    // ============================================================================
    // TX Path: VLAN Tag Insertion
    // ============================================================================
    // Insert 802.1Q VLAN tag into outgoing packets.
    // The VLAN tag encodes tx_route_out (sender_id, receiver_id) from gateway_tx.

    vlan_tagger #(
        .DATA_WIDTH(AXI_NET_BITS)
    ) inst_vlan_tagger (
        .aclk(aclk),
        .aresetn(aresetn),
        // Routing info to encode in VLAN tag (from gateway_tx)
        .route_out(tx_route_out),
        // Input: untagged packets from network stack
        .s_axis_tdata(s_axis_tx_tdata),
        .s_axis_tkeep(s_axis_tx_tkeep),
        .s_axis_tlast(s_axis_tx_tlast),
        .s_axis_tready(s_axis_tx_tready),
        .s_axis_tvalid(s_axis_tx_tvalid),
        // Output: VLAN tagged packets to CMAC
        .m_axis_tdata(m_axis_tx_tdata),
        .m_axis_tkeep(m_axis_tx_tkeep),
        .m_axis_tlast(m_axis_tx_tlast),
        .m_axis_tready(m_axis_tx_tready),
        .m_axis_tvalid(m_axis_tx_tvalid)
    );

    // ============================================================================
    // RX Path: VLAN Tag Extraction
    // ============================================================================
    // Extract and remove 802.1Q VLAN tag from incoming packets.
    // The extracted routing info is passed to gateway_rx for validation.

    vlan_untagger #(
        .DATA_WIDTH(AXI_NET_BITS)
    ) inst_vlan_untagger (
        .aclk(aclk),
        .aresetn(aresetn),
        // Extracted routing info for gateway_rx (sideband)
        .route_out(extracted_route),
        .route_valid(extracted_route_valid),
        // Input: VLAN tagged packets from CMAC
        .s_axis_tdata(s_axis_rx_tdata),
        .s_axis_tkeep(s_axis_rx_tkeep),
        .s_axis_tlast(s_axis_rx_tlast),
        .s_axis_tready(s_axis_rx_tready),
        .s_axis_tvalid(s_axis_rx_tvalid),
        // Output: untagged packets with synchronized route_id in tdest
        .m_axis_tdata(axis_rx_untagged_tdata),
        .m_axis_tkeep(axis_rx_untagged_tkeep),
        .m_axis_tlast(axis_rx_untagged_tlast),
        .m_axis_tready(axis_rx_untagged_tready),
        .m_axis_tvalid(axis_rx_untagged_tvalid),
        .m_axis_tdest(axis_rx_untagged_tdest)
    );

    // ============================================================================
    // Gateway RX - RX path routing capability validation
    // ============================================================================
    // Validates that incoming packets (based on extracted VLAN tag) match the
    // expected routing capability for the destination port.
    // Uses the extracted_route from vlan_untagger.
    //
    // The route_id (extracted from VLAN tag) flows through this gateway and
    // is used by the vIO Switch RX Selector to forward packets to the correct vFPGA.

    gateway_rx #(
        .N_DESTS(N_ID)
    ) inst_gateway_rx (
        .aclk(aclk),
        .aresetn(aresetn),
        .route_ctrl(route_ctrl),
        .route_in(extracted_route)
    );

    // ============================================================================
    // RX Path Output
    // ============================================================================
    // Forward untagged packets to network stack along with the synchronized route_id.
    // The route_id is carried in m_axis_rx_tdest, traveling with the packet data
    // through the network protocol stacks (TCP/IP, RDMA, Bypass) to vIO Switch.

    assign m_axis_rx_tdata  = axis_rx_untagged_tdata;
    assign m_axis_rx_tkeep  = axis_rx_untagged_tkeep;
    assign m_axis_rx_tlast  = axis_rx_untagged_tlast;
    assign m_axis_rx_tvalid = axis_rx_untagged_tvalid;
    assign m_axis_rx_tdest  = axis_rx_untagged_tdest;  // Synchronized route_id for vIO Switch
    assign axis_rx_untagged_tready = m_axis_rx_tready;

endmodule

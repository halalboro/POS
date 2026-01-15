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
 * TX Path: Network Stack -> VIU (add VLAN tag with route_out) -> CMAC
 * RX Path: CMAC -> VIU (extract VLAN tag -> route_in -> validate) -> Network Stack
 *
 * The routing capability format is the same as vFIU since both feed the same
 * vIO_switch for routing to the correct vFPGA region.
 *
 *  @param N_ID  Number of vFPGA regions
 */
module viu_top #(
    parameter integer N_ID = N_REGIONS
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // ============================================================================
    // Routing Capability Interface
    // ============================================================================
    // Format: [13:10] reserved, [9:6] sender_ul_id, [5:2] reserved, [1:0] port_id
    // Same format as vFIU since both feed vIO_switch
    input  logic [13:0]                         route_ctrl,

    // Port selection for TX path
    input  logic [1:0]                          port_in,

    // Route output from gate_send (used for VLAN tag insertion)
    output logic [13:0]                         route_out,

    // Port output from gate_recv (destination vFPGA for RX packets)
    output logic [1:0]                          port_out,

    // ============================================================================
    // AXI4-Stream Network Interfaces (TX path - to CMAC)
    // ============================================================================
    // From network stack (untagged packets)
    input  logic [AXI_NET_BITS-1:0]             s_axis_tx_tdata,
    input  logic [AXI_NET_BITS/8-1:0]           s_axis_tx_tkeep,
    input  logic                                s_axis_tx_tlast,
    output logic                                s_axis_tx_tready,
    input  logic                                s_axis_tx_tvalid,

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

    // To network stack (untagged packets)
    output logic [AXI_NET_BITS-1:0]             m_axis_rx_tdata,
    output logic [AXI_NET_BITS/8-1:0]           m_axis_rx_tkeep,
    output logic                                m_axis_rx_tlast,
    input  logic                                m_axis_rx_tready,
    output logic                                m_axis_rx_tvalid
);

    // ============================================================================
    // Internal Signals
    // ============================================================================

    // Route extracted from incoming VLAN tag
    logic [13:0] extracted_route;
    logic        extracted_route_valid;

    // Intermediate AXI-Stream for RX path (after VLAN extraction, before validation)
    logic [AXI_NET_BITS-1:0]     axis_rx_untagged_tdata;
    logic [AXI_NET_BITS/8-1:0]   axis_rx_untagged_tkeep;
    logic                        axis_rx_untagged_tlast;
    logic                        axis_rx_untagged_tready;
    logic                        axis_rx_untagged_tvalid;

    // ============================================================================
    // Gate Send - TX path routing capability validation
    // ============================================================================
    // Validates that the sending port has permission to transmit based on
    // routing capabilities configured by the host. Provides route_out which
    // is encoded into the VLAN tag by vlan_tag_insert.

    gate_send #(
        .N_DESTS(N_ID)
    ) inst_gate_send (
        .aclk(aclk),
        .aresetn(aresetn),
        .route_ctrl(route_ctrl),
        .ul_port_in(port_in),
        .route_out(route_out)
    );

    // ============================================================================
    // TX Path: VLAN Tag Insertion
    // ============================================================================
    // Insert 802.1Q VLAN tag into outgoing packets.
    // The VLAN tag encodes route_out (sender_ul_id, port_id) for the receiver.

    vlan_tag_insert #(
        .DATA_WIDTH(AXI_NET_BITS)
    ) inst_vlan_tag_insert (
        .aclk(aclk),
        .aresetn(aresetn),
        // Routing info to encode in VLAN tag
        .route_out(route_out),
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
    // The extracted routing info is passed to gate_recv for validation.

    vlan_tag_extract #(
        .DATA_WIDTH(AXI_NET_BITS)
    ) inst_vlan_tag_extract (
        .aclk(aclk),
        .aresetn(aresetn),
        // Extracted routing info for gate_recv
        .route_out(extracted_route),
        .route_valid(extracted_route_valid),
        // Input: VLAN tagged packets from CMAC
        .s_axis_tdata(s_axis_rx_tdata),
        .s_axis_tkeep(s_axis_rx_tkeep),
        .s_axis_tlast(s_axis_rx_tlast),
        .s_axis_tready(s_axis_rx_tready),
        .s_axis_tvalid(s_axis_rx_tvalid),
        // Output: untagged packets
        .m_axis_tdata(axis_rx_untagged_tdata),
        .m_axis_tkeep(axis_rx_untagged_tkeep),
        .m_axis_tlast(axis_rx_untagged_tlast),
        .m_axis_tready(axis_rx_untagged_tready),
        .m_axis_tvalid(axis_rx_untagged_tvalid)
    );

    // ============================================================================
    // Gate Recv - RX path routing capability validation
    // ============================================================================
    // Validates that incoming packets (based on extracted VLAN tag) match the
    // expected routing capability for the destination port.
    // Uses the extracted_route from vlan_tag_extract.

    gate_recv #(
        .N_DESTS(N_ID)
    ) inst_gate_recv (
        .aclk(aclk),
        .aresetn(aresetn),
        .route_ctrl(route_ctrl),
        .route_in(extracted_route),
        .ul_port_out(port_out)
    );

    // ============================================================================
    // RX Path Output
    // ============================================================================
    // Forward untagged packets to network stack.
    // TODO: Add filtering based on gate_recv validation result if needed.
    // Currently, gate_recv provides port_out for routing decisions but doesn't
    // block invalid packets. For security, you may want to add a drop mechanism.

    assign m_axis_rx_tdata  = axis_rx_untagged_tdata;
    assign m_axis_rx_tkeep  = axis_rx_untagged_tkeep;
    assign m_axis_rx_tlast  = axis_rx_untagged_tlast;
    assign m_axis_rx_tvalid = axis_rx_untagged_tvalid;
    assign axis_rx_untagged_tready = m_axis_rx_tready;

endmodule

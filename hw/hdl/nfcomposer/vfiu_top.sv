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
 * @brief   vFPGA Isolation Unit (vFIU) - Security gateway for multi-tenant isolation
 *
 * This module provides security isolation for a vFPGA region. It has:
 *   - Single bidirectional AXI4SR port to vIO Switch (handles demux/mux internally)
 *   - 6 data paths to/from the vFPGA: HOST, RDMA_REQ, RDMA_RSP, TCP, BYPASS, P2P
 *   - Memory request validation via gate_mem
 *
 * Internal components:
 *   - RX Demux: Routes incoming vIO Switch data to correct path based on sender_id
 *   - TX Mux: Combines 6 TX paths into single stream for vIO Switch
 *   - gateway_send: Attaches route_id (tdest) to outgoing data
 *   - gateway_recv: Validates incoming routes
 *   - gate_mem: Validates memory requests against configured endpoints
 *
 * Route ID format (14 bits):
 *   [13:10] reserved
 *   [9:6]   sender_id (source port/vFPGA)
 *   [5:2]   receiver_id (destination port/vFPGA)
 *   [1:0]   flags
 *
 *  @param N_REGIONS    Number of vFPGA regions
 *  @param N_ENDPOINTS  Number of memory endpoints for access control
 *  @param VFPGA_ID     vFPGA ID for route sender identification
 */
module vfiu_top #(
    parameter integer N_REGIONS = N_REGIONS,
    parameter integer N_ENDPOINTS = 4,
    parameter integer VFPGA_ID = 0
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // ========================================================================
    // vIO Switch Interface (2 unidirectional ports, matching vio_switch_N.sv)
    // ========================================================================
    // Connects to vio_switch ports: data_vfiu_src[i] and data_vfiu_sink[i]
    AXI4SR.s                                    s_axis_switch,  // RX: vIO Switch src → vFIU (incoming data)
    AXI4SR.m                                    m_axis_switch,  // TX: vFIU → vIO Switch sink (outgoing data)

    // ========================================================================
    // Memory Endpoint Control
    // ========================================================================
    input logic [(99*N_ENDPOINTS)-1:0]          mem_ctrl,

    // ========================================================================
    // Memory Request Interfaces (gate_mem)
    // ========================================================================
    metaIntf.s                                  s_rd_req,
    metaIntf.s                                  s_wr_req,
    metaIntf.m                                  m_rd_req,
    metaIntf.m                                  m_wr_req,

    // ========================================================================
    // Routing Control
    // ========================================================================
    input  logic [13:0]                         route_ctrl,

    // ========================================================================
    // HOST Path (vFPGA ↔ Host DMA) - AXI4S (no tid needed)
    // ========================================================================
    AXI4S.s                                     s_axis_host_tx,  // From user/credits
    AXI4S.m                                     m_axis_host_rx,  // To user/credits

    // ========================================================================
    // RDMA Path (vFPGA ↔ RDMA stack) - AXI4S, separate REQ and RSP
    // ========================================================================
    AXI4S.s                                     s_axis_rdma_tx_req,  // RDMA read requests from vFPGA
    AXI4S.s                                     s_axis_rdma_tx_rsp,  // RDMA read responses from vFPGA
    AXI4S.m                                     m_axis_rdma_rx,      // RDMA data to vFPGA (single RX path)

    // ========================================================================
    // TCP Path (vFPGA ↔ TCP stack) - AXI4S
    // ========================================================================
    AXI4S.s                                     s_axis_tcp_tx,
    AXI4S.m                                     m_axis_tcp_rx,

    // ========================================================================
    // BYPASS Path (vFPGA ↔ Bypass stack) - AXI4S
    // ========================================================================
    AXI4S.s                                     s_axis_bypass_tx,
    AXI4S.m                                     m_axis_bypass_rx,

    // ========================================================================
    // P2P Path (vFPGA ↔ vFPGA) - AXI4SR with tid = sender vFPGA ID
    // ========================================================================
    AXI4SR.s                                    s_axis_p2p_tx,   // tid ignored on TX (we know sender)
    AXI4SR.m                                    m_axis_p2p_rx    // tid = sender vFPGA ID on RX
);

    // ========================================================================================
    // Port ID Constants (must match vio_switch port assignments)
    // ========================================================================================
    localparam logic [3:0] PORT_HOST_TX       = N_REGIONS + 0;  // DMA read response
    localparam logic [3:0] PORT_HOST_RX       = N_REGIONS + 1;  // DMA write data
    localparam logic [3:0] PORT_RDMA_RX       = N_REGIONS + 2;  // RDMA RX (data to vFPGA)
    localparam logic [3:0] PORT_RDMA_TX_REQ   = N_REGIONS + 3;  // RDMA TX REQ (read requests to stack)
    localparam logic [3:0] PORT_RDMA_TX_RSP   = N_REGIONS + 4;  // RDMA TX RSP (read responses to stack)
    localparam logic [3:0] PORT_TCP           = N_REGIONS + 5;  // TCP stack
    localparam logic [3:0] PORT_BYPASS_RX     = N_REGIONS + 6;  // Bypass stack

    // ========================================================================================
    // Internal Interfaces
    // ========================================================================================

    // gateway_send outputs (after route_id attached)
    AXI4SR axis_host_tx_from_gateway ();
    AXI4SR axis_rdma_req_tx_from_gateway ();
    AXI4SR axis_rdma_rsp_tx_from_gateway ();
    AXI4SR axis_tcp_tx_from_gateway ();
    AXI4SR axis_bypass_tx_from_gateway ();
    AXI4SR axis_p2p_tx_from_gateway ();

    // Demuxed RX streams (from vIO Switch, before gateway_recv)
    AXI4SR axis_host_rx_to_gateway ();
    AXI4SR axis_rdma_rx_to_gateway ();
    AXI4SR axis_tcp_rx_to_gateway ();
    AXI4SR axis_bypass_rx_to_gateway ();
    AXI4SR axis_p2p_rx_to_gateway ();

    // gateway_recv outputs (validated, to user)
    AXI4S  axis_host_rx_from_gateway ();
    AXI4S  axis_rdma_rx_from_gateway ();
    AXI4S  axis_tcp_rx_from_gateway ();
    AXI4S  axis_bypass_rx_from_gateway ();
    AXI4SR axis_p2p_rx_from_gateway ();

    // ========================================================================================
    // RX DEMUX: vIO Switch → 5 paths based on sender_id in tdest[9:6]
    // ========================================================================================
    // The vIO Switch sends all data destined for this vFIU on s_axis_switch.
    // We demux based on sender_id to route to the correct protocol path.

    logic [3:0] rx_sender_id;
    assign rx_sender_id = s_axis_switch.tdest[9:6];

    // Determine which path this data belongs to
    logic rx_is_host, rx_is_rdma, rx_is_tcp, rx_is_bypass, rx_is_p2p;
    assign rx_is_host   = (rx_sender_id == PORT_HOST_TX);
    assign rx_is_rdma   = (rx_sender_id == PORT_RDMA_RX);
    assign rx_is_tcp    = (rx_sender_id == PORT_TCP);
    assign rx_is_bypass = (rx_sender_id == PORT_BYPASS_RX);
    assign rx_is_p2p    = (rx_sender_id < N_REGIONS) && (rx_sender_id != VFPGA_ID);

    // HOST RX demux
    assign axis_host_rx_to_gateway.tvalid = s_axis_switch.tvalid & rx_is_host;
    assign axis_host_rx_to_gateway.tdata  = s_axis_switch.tdata;
    assign axis_host_rx_to_gateway.tkeep  = s_axis_switch.tkeep;
    assign axis_host_rx_to_gateway.tlast  = s_axis_switch.tlast;
    assign axis_host_rx_to_gateway.tid    = s_axis_switch.tid;
    assign axis_host_rx_to_gateway.tdest  = s_axis_switch.tdest;

    // RDMA RX demux
    assign axis_rdma_rx_to_gateway.tvalid = s_axis_switch.tvalid & rx_is_rdma;
    assign axis_rdma_rx_to_gateway.tdata  = s_axis_switch.tdata;
    assign axis_rdma_rx_to_gateway.tkeep  = s_axis_switch.tkeep;
    assign axis_rdma_rx_to_gateway.tlast  = s_axis_switch.tlast;
    assign axis_rdma_rx_to_gateway.tid    = s_axis_switch.tid;
    assign axis_rdma_rx_to_gateway.tdest  = s_axis_switch.tdest;

    // TCP RX demux
    assign axis_tcp_rx_to_gateway.tvalid = s_axis_switch.tvalid & rx_is_tcp;
    assign axis_tcp_rx_to_gateway.tdata  = s_axis_switch.tdata;
    assign axis_tcp_rx_to_gateway.tkeep  = s_axis_switch.tkeep;
    assign axis_tcp_rx_to_gateway.tlast  = s_axis_switch.tlast;
    assign axis_tcp_rx_to_gateway.tid    = s_axis_switch.tid;
    assign axis_tcp_rx_to_gateway.tdest  = s_axis_switch.tdest;

    // BYPASS RX demux
    assign axis_bypass_rx_to_gateway.tvalid = s_axis_switch.tvalid & rx_is_bypass;
    assign axis_bypass_rx_to_gateway.tdata  = s_axis_switch.tdata;
    assign axis_bypass_rx_to_gateway.tkeep  = s_axis_switch.tkeep;
    assign axis_bypass_rx_to_gateway.tlast  = s_axis_switch.tlast;
    assign axis_bypass_rx_to_gateway.tid    = s_axis_switch.tid;
    assign axis_bypass_rx_to_gateway.tdest  = s_axis_switch.tdest;

    // P2P RX demux
    assign axis_p2p_rx_to_gateway.tvalid = s_axis_switch.tvalid & rx_is_p2p;
    assign axis_p2p_rx_to_gateway.tdata  = s_axis_switch.tdata;
    assign axis_p2p_rx_to_gateway.tkeep  = s_axis_switch.tkeep;
    assign axis_p2p_rx_to_gateway.tlast  = s_axis_switch.tlast;
    assign axis_p2p_rx_to_gateway.tid    = s_axis_switch.tid;
    assign axis_p2p_rx_to_gateway.tdest  = s_axis_switch.tdest;

    // Mux tready back to vIO Switch from whichever path is selected
    assign s_axis_switch.tready =
        rx_is_host   ? axis_host_rx_to_gateway.tready :
        rx_is_rdma   ? axis_rdma_rx_to_gateway.tready :
        rx_is_tcp    ? axis_tcp_rx_to_gateway.tready :
        rx_is_bypass ? axis_bypass_rx_to_gateway.tready :
        rx_is_p2p    ? axis_p2p_rx_to_gateway.tready :
                       1'b1;  // Discard unknown traffic

    // ========================================================================================
    // TX MUX: 6 paths → vIO Switch (priority: RDMA_REQ > RDMA_RSP > TCP > BYPASS > P2P > HOST)
    // ========================================================================================

    // Priority encoding (RDMA REQ and RSP have highest priority for latency-sensitive RDMA ops)
    logic tx_sel_rdma_req, tx_sel_rdma_rsp, tx_sel_tcp, tx_sel_bypass, tx_sel_p2p, tx_sel_host;
    assign tx_sel_rdma_req = axis_rdma_req_tx_from_gateway.tvalid;
    assign tx_sel_rdma_rsp = axis_rdma_rsp_tx_from_gateway.tvalid & ~tx_sel_rdma_req;
    assign tx_sel_tcp      = axis_tcp_tx_from_gateway.tvalid & ~tx_sel_rdma_req & ~tx_sel_rdma_rsp;
    assign tx_sel_bypass   = axis_bypass_tx_from_gateway.tvalid & ~tx_sel_rdma_req & ~tx_sel_rdma_rsp & ~tx_sel_tcp;
    assign tx_sel_p2p      = axis_p2p_tx_from_gateway.tvalid & ~tx_sel_rdma_req & ~tx_sel_rdma_rsp & ~tx_sel_tcp & ~tx_sel_bypass;
    assign tx_sel_host     = axis_host_tx_from_gateway.tvalid & ~tx_sel_rdma_req & ~tx_sel_rdma_rsp & ~tx_sel_tcp & ~tx_sel_bypass & ~tx_sel_p2p;

    // Mux data to vIO Switch
    assign m_axis_switch.tvalid = tx_sel_rdma_req | tx_sel_rdma_rsp | tx_sel_tcp | tx_sel_bypass | tx_sel_p2p | tx_sel_host;
    assign m_axis_switch.tdata  = tx_sel_rdma_req ? axis_rdma_req_tx_from_gateway.tdata :
                                  tx_sel_rdma_rsp ? axis_rdma_rsp_tx_from_gateway.tdata :
                                  tx_sel_tcp      ? axis_tcp_tx_from_gateway.tdata :
                                  tx_sel_bypass   ? axis_bypass_tx_from_gateway.tdata :
                                  tx_sel_p2p      ? axis_p2p_tx_from_gateway.tdata :
                                                    axis_host_tx_from_gateway.tdata;
    assign m_axis_switch.tkeep  = tx_sel_rdma_req ? axis_rdma_req_tx_from_gateway.tkeep :
                                  tx_sel_rdma_rsp ? axis_rdma_rsp_tx_from_gateway.tkeep :
                                  tx_sel_tcp      ? axis_tcp_tx_from_gateway.tkeep :
                                  tx_sel_bypass   ? axis_bypass_tx_from_gateway.tkeep :
                                  tx_sel_p2p      ? axis_p2p_tx_from_gateway.tkeep :
                                                    axis_host_tx_from_gateway.tkeep;
    assign m_axis_switch.tlast  = tx_sel_rdma_req ? axis_rdma_req_tx_from_gateway.tlast :
                                  tx_sel_rdma_rsp ? axis_rdma_rsp_tx_from_gateway.tlast :
                                  tx_sel_tcp      ? axis_tcp_tx_from_gateway.tlast :
                                  tx_sel_bypass   ? axis_bypass_tx_from_gateway.tlast :
                                  tx_sel_p2p      ? axis_p2p_tx_from_gateway.tlast :
                                                    axis_host_tx_from_gateway.tlast;
    assign m_axis_switch.tid    = tx_sel_rdma_req ? axis_rdma_req_tx_from_gateway.tid :
                                  tx_sel_rdma_rsp ? axis_rdma_rsp_tx_from_gateway.tid :
                                  tx_sel_tcp      ? axis_tcp_tx_from_gateway.tid :
                                  tx_sel_bypass   ? axis_bypass_tx_from_gateway.tid :
                                  tx_sel_p2p      ? axis_p2p_tx_from_gateway.tid :
                                                    axis_host_tx_from_gateway.tid;
    assign m_axis_switch.tdest  = tx_sel_rdma_req ? axis_rdma_req_tx_from_gateway.tdest :
                                  tx_sel_rdma_rsp ? axis_rdma_rsp_tx_from_gateway.tdest :
                                  tx_sel_tcp      ? axis_tcp_tx_from_gateway.tdest :
                                  tx_sel_bypass   ? axis_bypass_tx_from_gateway.tdest :
                                  tx_sel_p2p      ? axis_p2p_tx_from_gateway.tdest :
                                                    axis_host_tx_from_gateway.tdest;

    // Distribute tready back to each TX path
    assign axis_rdma_req_tx_from_gateway.tready = m_axis_switch.tready & tx_sel_rdma_req;
    assign axis_rdma_rsp_tx_from_gateway.tready = m_axis_switch.tready & tx_sel_rdma_rsp;
    assign axis_tcp_tx_from_gateway.tready      = m_axis_switch.tready & tx_sel_tcp;
    assign axis_bypass_tx_from_gateway.tready   = m_axis_switch.tready & tx_sel_bypass;
    assign axis_p2p_tx_from_gateway.tready      = m_axis_switch.tready & tx_sel_p2p;
    assign axis_host_tx_from_gateway.tready     = m_axis_switch.tready & tx_sel_host;

    // ========================================================================================
    // gateway_recv outputs → external ports (to credits/user logic)
    // ========================================================================================

    `AXIS_ASSIGN(axis_host_rx_from_gateway, m_axis_host_rx)
    `AXIS_ASSIGN(axis_rdma_rx_from_gateway, m_axis_rdma_rx)
    `AXIS_ASSIGN(axis_tcp_rx_from_gateway, m_axis_tcp_rx)
    `AXIS_ASSIGN(axis_bypass_rx_from_gateway, m_axis_bypass_rx)
    `AXISR_ASSIGN(axis_p2p_rx_from_gateway, m_axis_p2p_rx)

    // ========================================================================================
    // Gateway Send: Attaches route_id (tdest) for vIO Switch routing
    // ========================================================================================
    logic gateway_send_route_valid;

    gateway_send #(
        .N_DESTS(1),
        .ID(VFPGA_ID),
        .N_REGIONS(N_REGIONS)
    ) inst_gateway_send (
        .aclk(aclk),
        .aresetn(aresetn),
        .route_ctrl(route_ctrl),
        .route_valid(gateway_send_route_valid),
        // Host TX
        .s_axis_host(s_axis_host_tx),
        .m_axis_host(axis_host_tx_from_gateway),
        // RDMA TX REQ
        .s_axis_rdma_req(s_axis_rdma_tx_req),
        .m_axis_rdma_req(axis_rdma_req_tx_from_gateway),
        // RDMA TX RSP
        .s_axis_rdma_rsp(s_axis_rdma_tx_rsp),
        .m_axis_rdma_rsp(axis_rdma_rsp_tx_from_gateway),
        // TCP TX
        .s_axis_tcp(s_axis_tcp_tx),
        .m_axis_tcp(axis_tcp_tx_from_gateway),
        // BYPASS TX
        .s_axis_bypass(s_axis_bypass_tx),
        .m_axis_bypass(axis_bypass_tx_from_gateway),
        // P2P TX
        .s_axis_p2p(s_axis_p2p_tx),
        .m_axis_p2p(axis_p2p_tx_from_gateway)
    );

    // ========================================================================================
    // Gateway Recv: Validates incoming routes from vIO Switch
    // ========================================================================================

    gateway_recv #(
        .N_DESTS(1),
        .ID(VFPGA_ID),
        .N_REGIONS(N_REGIONS)
    ) inst_gateway_recv (
        .aclk(aclk),
        .aresetn(aresetn),
        .route_ctrl(route_ctrl),
        // Host RX
        .s_axis_host(axis_host_rx_to_gateway),
        .m_axis_host(axis_host_rx_from_gateway),
        // RDMA RX
        .s_axis_rdma(axis_rdma_rx_to_gateway),
        .m_axis_rdma(axis_rdma_rx_from_gateway),
        // TCP RX
        .s_axis_tcp(axis_tcp_rx_to_gateway),
        .m_axis_tcp(axis_tcp_rx_from_gateway),
        // BYPASS RX
        .s_axis_bypass(axis_bypass_rx_to_gateway),
        .m_axis_bypass(axis_bypass_rx_from_gateway),
        // P2P RX
        .s_axis_p2p(axis_p2p_rx_to_gateway),
        .m_axis_p2p(axis_p2p_rx_from_gateway)
    );

    // ========================================================================================
    // Gate Mem: Memory endpoint validation
    // ========================================================================================

    gate_mem #(
        .N_ENDPOINTS(N_ENDPOINTS)
    ) inst_gate_mem (
        .aclk(aclk),
        .aresetn(aresetn),
        .ep_ctrl(mem_ctrl),
        .s_rd_req(s_rd_req),
        .s_wr_req(s_wr_req),
        .m_rd_req(m_rd_req),
        .m_wr_req(m_wr_req)
    );

endmodule

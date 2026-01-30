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

// POS vIO Switch (N_ID=6, 15 ports): vFIU[0-5], Host_TX, Host_RX, RDMA_RX, RDMA_TX_REQ, RDMA_TX_RSP, TCP, Bypass_RX, Bypass_TX_REQ, Bypass_TX_RSP
// Port formula: N + 9 (N vFIU + 2 Host + 3 RDMA + 1 TCP + 3 Bypass)
module vio_switch_6 #(
    parameter integer N_ID = N_REGIONS
) (
    input  logic                             aclk,
    input  logic                             aresetn,

    // Host DMA: 2 ports (TX and RX, shared via tdest routing)
    AXI4SR.s                                 data_host_tx_sink,
    AXI4SR.m                                 data_host_tx_src,
    AXI4SR.s                                 data_host_rx_sink,
    AXI4SR.m                                 data_host_rx_src,

    // vFIU: N ports
    AXI4SR.s                                 data_vfiu_sink [N_ID],
    AXI4SR.m                                 data_vfiu_src [N_ID],

    // RDMA: 3 ports (RX, TX_REQ, TX_RSP)
    AXI4SR.s                                 data_rdma_rx_sink,
    AXI4SR.m                                 data_rdma_rx_src,
    AXI4SR.s                                 data_rdma_tx_req_sink,
    AXI4SR.m                                 data_rdma_tx_req_src,
    AXI4SR.s                                 data_rdma_tx_rsp_sink,
    AXI4SR.m                                 data_rdma_tx_rsp_src,

    // TCP: 1 port (bidirectional)
    AXI4SR.s                                 data_tcp_sink,
    AXI4SR.m                                 data_tcp_src,

    // Bypass: 3 ports (RX, TX_REQ, TX_RSP)
    AXI4SR.s                                 data_bypass_rx_sink,
    AXI4SR.m                                 data_bypass_rx_src,
    AXI4SR.s                                 data_bypass_tx_req_sink,
    AXI4SR.m                                 data_bypass_tx_req_src,
    AXI4SR.s                                 data_bypass_tx_rsp_sink,
    AXI4SR.m                                 data_bypass_tx_rsp_src
);

localparam integer N_TOTAL_PORTS = N_ID + 9;

// Port indices
localparam integer PORT_VFIU_BASE = 0;
localparam integer PORT_HOST_TX = N_ID;
localparam integer PORT_HOST_RX = N_ID + 1;
localparam integer PORT_RDMA_RX = N_ID + 2;
localparam integer PORT_RDMA_TX_REQ = N_ID + 3;
localparam integer PORT_RDMA_TX_RSP = N_ID + 4;
localparam integer PORT_TCP = N_ID + 5;
localparam integer PORT_BYPASS_RX = N_ID + 6;
localparam integer PORT_BYPASS_TX_REQ = N_ID + 7;
localparam integer PORT_BYPASS_TX_RSP = N_ID + 8;

logic [N_TOTAL_PORTS-1:0] axis_switch_s_decode_err;
logic [N_ID-1:0][13:0] axis_switch_m_tdest_vfiu;
logic [13:0] axis_switch_m_tdest_host_tx;
logic [13:0] axis_switch_m_tdest_host_rx;
logic [13:0] axis_switch_m_tdest_rdma_rx;
logic [13:0] axis_switch_m_tdest_rdma_tx_req;
logic [13:0] axis_switch_m_tdest_rdma_tx_rsp;
logic [13:0] axis_switch_m_tdest_tcp;
logic [13:0] axis_switch_m_tdest_bypass_rx;
logic [13:0] axis_switch_m_tdest_bypass_tx_req;
logic [13:0] axis_switch_m_tdest_bypass_tx_rsp;

// Host TX signals
logic                        data_host_tx_sink_tvalid;
logic                        data_host_tx_sink_tready;
logic [AXI_DATA_BITS-1:0]    data_host_tx_sink_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_host_tx_sink_tkeep;
logic                        data_host_tx_sink_tlast;
logic [PID_BITS-1:0]         data_host_tx_sink_tid;
logic [13:0]                 data_host_tx_sink_tdest;

logic                        data_host_tx_src_tvalid;
logic                        data_host_tx_src_tready;
logic [AXI_DATA_BITS-1:0]    data_host_tx_src_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_host_tx_src_tkeep;
logic                        data_host_tx_src_tlast;
logic [PID_BITS-1:0]         data_host_tx_src_tid;

assign data_host_tx_sink_tvalid = data_host_tx_sink.tvalid;
assign data_host_tx_sink_tdata = data_host_tx_sink.tdata;
assign data_host_tx_sink_tkeep = data_host_tx_sink.tkeep;
assign data_host_tx_sink_tlast = data_host_tx_sink.tlast;
assign data_host_tx_sink_tid = data_host_tx_sink.tid;
assign data_host_tx_sink_tdest = data_host_tx_sink.tdest;
assign data_host_tx_sink.tready = data_host_tx_sink_tready;

assign data_host_tx_src.tvalid = data_host_tx_src_tvalid;
assign data_host_tx_src.tdata = data_host_tx_src_tdata;
assign data_host_tx_src.tkeep = data_host_tx_src_tkeep;
assign data_host_tx_src.tlast = data_host_tx_src_tlast;
assign data_host_tx_src.tid = data_host_tx_src_tid;
assign data_host_tx_src.tdest = axis_switch_m_tdest_host_tx;
assign data_host_tx_src_tready = data_host_tx_src.tready;

// Host RX signals
logic                        data_host_rx_sink_tvalid;
logic                        data_host_rx_sink_tready;
logic [AXI_DATA_BITS-1:0]    data_host_rx_sink_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_host_rx_sink_tkeep;
logic                        data_host_rx_sink_tlast;
logic [PID_BITS-1:0]         data_host_rx_sink_tid;
logic [13:0]                 data_host_rx_sink_tdest;

logic                        data_host_rx_src_tvalid;
logic                        data_host_rx_src_tready;
logic [AXI_DATA_BITS-1:0]    data_host_rx_src_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_host_rx_src_tkeep;
logic                        data_host_rx_src_tlast;
logic [PID_BITS-1:0]         data_host_rx_src_tid;

assign data_host_rx_sink_tvalid = data_host_rx_sink.tvalid;
assign data_host_rx_sink_tdata = data_host_rx_sink.tdata;
assign data_host_rx_sink_tkeep = data_host_rx_sink.tkeep;
assign data_host_rx_sink_tlast = data_host_rx_sink.tlast;
assign data_host_rx_sink_tid = data_host_rx_sink.tid;
assign data_host_rx_sink_tdest = data_host_rx_sink.tdest;
assign data_host_rx_sink.tready = data_host_rx_sink_tready;

assign data_host_rx_src.tvalid = data_host_rx_src_tvalid;
assign data_host_rx_src.tdata = data_host_rx_src_tdata;
assign data_host_rx_src.tkeep = data_host_rx_src_tkeep;
assign data_host_rx_src.tlast = data_host_rx_src_tlast;
assign data_host_rx_src.tid = data_host_rx_src_tid;
assign data_host_rx_src.tdest = axis_switch_m_tdest_host_rx;
assign data_host_rx_src_tready = data_host_rx_src.tready;

// vFIU signals
logic [N_ID-1:0]                        data_vfiu_sink_tvalid;
logic [N_ID-1:0]                        data_vfiu_sink_tready;
logic [N_ID-1:0][AXI_DATA_BITS-1:0]     data_vfiu_sink_tdata;
logic [N_ID-1:0][AXI_DATA_BITS/8-1:0]   data_vfiu_sink_tkeep;
logic [N_ID-1:0]                        data_vfiu_sink_tlast;
logic [N_ID-1:0][PID_BITS-1:0]          data_vfiu_sink_tid;
logic [N_ID-1:0][13:0]                  data_vfiu_sink_tdest;

logic [N_ID-1:0]                        data_vfiu_src_tvalid;
logic [N_ID-1:0]                        data_vfiu_src_tready;
logic [N_ID-1:0][AXI_DATA_BITS-1:0]     data_vfiu_src_tdata;
logic [N_ID-1:0][AXI_DATA_BITS/8-1:0]   data_vfiu_src_tkeep;
logic [N_ID-1:0]                        data_vfiu_src_tlast;
logic [N_ID-1:0][PID_BITS-1:0]          data_vfiu_src_tid;

for(genvar i = 0; i < N_ID; i++) begin
    assign data_vfiu_sink_tvalid[i] = data_vfiu_sink[i].tvalid;
    assign data_vfiu_sink_tdata[i] = data_vfiu_sink[i].tdata;
    assign data_vfiu_sink_tkeep[i] = data_vfiu_sink[i].tkeep;
    assign data_vfiu_sink_tlast[i] = data_vfiu_sink[i].tlast;
    assign data_vfiu_sink_tid[i] = data_vfiu_sink[i].tid;
    assign data_vfiu_sink_tdest[i] = data_vfiu_sink[i].tdest;
    assign data_vfiu_sink[i].tready = data_vfiu_sink_tready[i];
end

for(genvar i = 0; i < N_ID; i++) begin
    assign data_vfiu_src[i].tvalid = data_vfiu_src_tvalid[i];
    assign data_vfiu_src[i].tdata = data_vfiu_src_tdata[i];
    assign data_vfiu_src[i].tkeep = data_vfiu_src_tkeep[i];
    assign data_vfiu_src[i].tlast = data_vfiu_src_tlast[i];
    assign data_vfiu_src[i].tid = data_vfiu_src_tid[i];
    assign data_vfiu_src[i].tdest = axis_switch_m_tdest_vfiu[i];
    assign data_vfiu_src_tready[i] = data_vfiu_src[i].tready;
end

// RDMA RX signals (Stack → vFPGA)
logic                        data_rdma_rx_sink_tvalid;
logic                        data_rdma_rx_sink_tready;
logic [AXI_DATA_BITS-1:0]    data_rdma_rx_sink_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_rdma_rx_sink_tkeep;
logic                        data_rdma_rx_sink_tlast;
logic [PID_BITS-1:0]         data_rdma_rx_sink_tid;
logic [13:0]                 data_rdma_rx_sink_tdest;

logic                        data_rdma_rx_src_tvalid;
logic                        data_rdma_rx_src_tready;
logic [AXI_DATA_BITS-1:0]    data_rdma_rx_src_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_rdma_rx_src_tkeep;
logic                        data_rdma_rx_src_tlast;
logic [PID_BITS-1:0]         data_rdma_rx_src_tid;

assign data_rdma_rx_sink_tvalid = data_rdma_rx_sink.tvalid;
assign data_rdma_rx_sink_tdata = data_rdma_rx_sink.tdata;
assign data_rdma_rx_sink_tkeep = data_rdma_rx_sink.tkeep;
assign data_rdma_rx_sink_tlast = data_rdma_rx_sink.tlast;
assign data_rdma_rx_sink_tid = data_rdma_rx_sink.tid;
assign data_rdma_rx_sink_tdest = data_rdma_rx_sink.tdest;
assign data_rdma_rx_sink.tready = data_rdma_rx_sink_tready;

assign data_rdma_rx_src.tvalid = data_rdma_rx_src_tvalid;
assign data_rdma_rx_src.tdata = data_rdma_rx_src_tdata;
assign data_rdma_rx_src.tkeep = data_rdma_rx_src_tkeep;
assign data_rdma_rx_src.tlast = data_rdma_rx_src_tlast;
assign data_rdma_rx_src.tid = data_rdma_rx_src_tid;
assign data_rdma_rx_src.tdest = axis_switch_m_tdest_rdma_rx;
assign data_rdma_rx_src_tready = data_rdma_rx_src.tready;

// RDMA TX REQ signals (vFPGA → Stack, requests)
logic                        data_rdma_tx_req_sink_tvalid;
logic                        data_rdma_tx_req_sink_tready;
logic [AXI_DATA_BITS-1:0]    data_rdma_tx_req_sink_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_rdma_tx_req_sink_tkeep;
logic                        data_rdma_tx_req_sink_tlast;
logic [PID_BITS-1:0]         data_rdma_tx_req_sink_tid;
logic [13:0]                 data_rdma_tx_req_sink_tdest;

logic                        data_rdma_tx_req_src_tvalid;
logic                        data_rdma_tx_req_src_tready;
logic [AXI_DATA_BITS-1:0]    data_rdma_tx_req_src_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_rdma_tx_req_src_tkeep;
logic                        data_rdma_tx_req_src_tlast;
logic [PID_BITS-1:0]         data_rdma_tx_req_src_tid;

assign data_rdma_tx_req_sink_tvalid = data_rdma_tx_req_sink.tvalid;
assign data_rdma_tx_req_sink_tdata = data_rdma_tx_req_sink.tdata;
assign data_rdma_tx_req_sink_tkeep = data_rdma_tx_req_sink.tkeep;
assign data_rdma_tx_req_sink_tlast = data_rdma_tx_req_sink.tlast;
assign data_rdma_tx_req_sink_tid = data_rdma_tx_req_sink.tid;
assign data_rdma_tx_req_sink_tdest = data_rdma_tx_req_sink.tdest;
assign data_rdma_tx_req_sink.tready = data_rdma_tx_req_sink_tready;

assign data_rdma_tx_req_src.tvalid = data_rdma_tx_req_src_tvalid;
assign data_rdma_tx_req_src.tdata = data_rdma_tx_req_src_tdata;
assign data_rdma_tx_req_src.tkeep = data_rdma_tx_req_src_tkeep;
assign data_rdma_tx_req_src.tlast = data_rdma_tx_req_src_tlast;
assign data_rdma_tx_req_src.tid = data_rdma_tx_req_src_tid;
assign data_rdma_tx_req_src.tdest = axis_switch_m_tdest_rdma_tx_req;
assign data_rdma_tx_req_src_tready = data_rdma_tx_req_src.tready;

// RDMA TX RSP signals (vFPGA → Stack, responses)
logic                        data_rdma_tx_rsp_sink_tvalid;
logic                        data_rdma_tx_rsp_sink_tready;
logic [AXI_DATA_BITS-1:0]    data_rdma_tx_rsp_sink_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_rdma_tx_rsp_sink_tkeep;
logic                        data_rdma_tx_rsp_sink_tlast;
logic [PID_BITS-1:0]         data_rdma_tx_rsp_sink_tid;
logic [13:0]                 data_rdma_tx_rsp_sink_tdest;

logic                        data_rdma_tx_rsp_src_tvalid;
logic                        data_rdma_tx_rsp_src_tready;
logic [AXI_DATA_BITS-1:0]    data_rdma_tx_rsp_src_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_rdma_tx_rsp_src_tkeep;
logic                        data_rdma_tx_rsp_src_tlast;
logic [PID_BITS-1:0]         data_rdma_tx_rsp_src_tid;

assign data_rdma_tx_rsp_sink_tvalid = data_rdma_tx_rsp_sink.tvalid;
assign data_rdma_tx_rsp_sink_tdata = data_rdma_tx_rsp_sink.tdata;
assign data_rdma_tx_rsp_sink_tkeep = data_rdma_tx_rsp_sink.tkeep;
assign data_rdma_tx_rsp_sink_tlast = data_rdma_tx_rsp_sink.tlast;
assign data_rdma_tx_rsp_sink_tid = data_rdma_tx_rsp_sink.tid;
assign data_rdma_tx_rsp_sink_tdest = data_rdma_tx_rsp_sink.tdest;
assign data_rdma_tx_rsp_sink.tready = data_rdma_tx_rsp_sink_tready;

assign data_rdma_tx_rsp_src.tvalid = data_rdma_tx_rsp_src_tvalid;
assign data_rdma_tx_rsp_src.tdata = data_rdma_tx_rsp_src_tdata;
assign data_rdma_tx_rsp_src.tkeep = data_rdma_tx_rsp_src_tkeep;
assign data_rdma_tx_rsp_src.tlast = data_rdma_tx_rsp_src_tlast;
assign data_rdma_tx_rsp_src.tid = data_rdma_tx_rsp_src_tid;
assign data_rdma_tx_rsp_src.tdest = axis_switch_m_tdest_rdma_tx_rsp;
assign data_rdma_tx_rsp_src_tready = data_rdma_tx_rsp_src.tready;

// TCP signals
logic                        data_tcp_sink_tvalid;
logic                        data_tcp_sink_tready;
logic [AXI_DATA_BITS-1:0]    data_tcp_sink_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_tcp_sink_tkeep;
logic                        data_tcp_sink_tlast;
logic [PID_BITS-1:0]         data_tcp_sink_tid;
logic [13:0]                 data_tcp_sink_tdest;

logic                        data_tcp_src_tvalid;
logic                        data_tcp_src_tready;
logic [AXI_DATA_BITS-1:0]    data_tcp_src_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_tcp_src_tkeep;
logic                        data_tcp_src_tlast;
logic [PID_BITS-1:0]         data_tcp_src_tid;

assign data_tcp_sink_tvalid = data_tcp_sink.tvalid;
assign data_tcp_sink_tdata = data_tcp_sink.tdata;
assign data_tcp_sink_tkeep = data_tcp_sink.tkeep;
assign data_tcp_sink_tlast = data_tcp_sink.tlast;
assign data_tcp_sink_tid = data_tcp_sink.tid;
assign data_tcp_sink_tdest = data_tcp_sink.tdest;
assign data_tcp_sink.tready = data_tcp_sink_tready;

assign data_tcp_src.tvalid = data_tcp_src_tvalid;
assign data_tcp_src.tdata = data_tcp_src_tdata;
assign data_tcp_src.tkeep = data_tcp_src_tkeep;
assign data_tcp_src.tlast = data_tcp_src_tlast;
assign data_tcp_src.tid = data_tcp_src_tid;
assign data_tcp_src.tdest = axis_switch_m_tdest_tcp;
assign data_tcp_src_tready = data_tcp_src.tready;

// Bypass RX signals (Stack → vFPGA)
logic                        data_bypass_rx_sink_tvalid;
logic                        data_bypass_rx_sink_tready;
logic [AXI_DATA_BITS-1:0]    data_bypass_rx_sink_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_bypass_rx_sink_tkeep;
logic                        data_bypass_rx_sink_tlast;
logic [PID_BITS-1:0]         data_bypass_rx_sink_tid;
logic [13:0]                 data_bypass_rx_sink_tdest;

logic                        data_bypass_rx_src_tvalid;
logic                        data_bypass_rx_src_tready;
logic [AXI_DATA_BITS-1:0]    data_bypass_rx_src_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_bypass_rx_src_tkeep;
logic                        data_bypass_rx_src_tlast;
logic [PID_BITS-1:0]         data_bypass_rx_src_tid;

assign data_bypass_rx_sink_tvalid = data_bypass_rx_sink.tvalid;
assign data_bypass_rx_sink_tdata = data_bypass_rx_sink.tdata;
assign data_bypass_rx_sink_tkeep = data_bypass_rx_sink.tkeep;
assign data_bypass_rx_sink_tlast = data_bypass_rx_sink.tlast;
assign data_bypass_rx_sink_tid = data_bypass_rx_sink.tid;
assign data_bypass_rx_sink_tdest = data_bypass_rx_sink.tdest;
assign data_bypass_rx_sink.tready = data_bypass_rx_sink_tready;

assign data_bypass_rx_src.tvalid = data_bypass_rx_src_tvalid;
assign data_bypass_rx_src.tdata = data_bypass_rx_src_tdata;
assign data_bypass_rx_src.tkeep = data_bypass_rx_src_tkeep;
assign data_bypass_rx_src.tlast = data_bypass_rx_src_tlast;
assign data_bypass_rx_src.tid = data_bypass_rx_src_tid;
assign data_bypass_rx_src.tdest = axis_switch_m_tdest_bypass_rx;
assign data_bypass_rx_src_tready = data_bypass_rx_src.tready;

// Bypass TX REQ signals (vFPGA → Stack, requests)
logic                        data_bypass_tx_req_sink_tvalid;
logic                        data_bypass_tx_req_sink_tready;
logic [AXI_DATA_BITS-1:0]    data_bypass_tx_req_sink_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_bypass_tx_req_sink_tkeep;
logic                        data_bypass_tx_req_sink_tlast;
logic [PID_BITS-1:0]         data_bypass_tx_req_sink_tid;
logic [13:0]                 data_bypass_tx_req_sink_tdest;

logic                        data_bypass_tx_req_src_tvalid;
logic                        data_bypass_tx_req_src_tready;
logic [AXI_DATA_BITS-1:0]    data_bypass_tx_req_src_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_bypass_tx_req_src_tkeep;
logic                        data_bypass_tx_req_src_tlast;
logic [PID_BITS-1:0]         data_bypass_tx_req_src_tid;

assign data_bypass_tx_req_sink_tvalid = data_bypass_tx_req_sink.tvalid;
assign data_bypass_tx_req_sink_tdata = data_bypass_tx_req_sink.tdata;
assign data_bypass_tx_req_sink_tkeep = data_bypass_tx_req_sink.tkeep;
assign data_bypass_tx_req_sink_tlast = data_bypass_tx_req_sink.tlast;
assign data_bypass_tx_req_sink_tid = data_bypass_tx_req_sink.tid;
assign data_bypass_tx_req_sink_tdest = data_bypass_tx_req_sink.tdest;
assign data_bypass_tx_req_sink.tready = data_bypass_tx_req_sink_tready;

assign data_bypass_tx_req_src.tvalid = data_bypass_tx_req_src_tvalid;
assign data_bypass_tx_req_src.tdata = data_bypass_tx_req_src_tdata;
assign data_bypass_tx_req_src.tkeep = data_bypass_tx_req_src_tkeep;
assign data_bypass_tx_req_src.tlast = data_bypass_tx_req_src_tlast;
assign data_bypass_tx_req_src.tid = data_bypass_tx_req_src_tid;
assign data_bypass_tx_req_src.tdest = axis_switch_m_tdest_bypass_tx_req;
assign data_bypass_tx_req_src_tready = data_bypass_tx_req_src.tready;

// Bypass TX RSP signals (vFPGA → Stack, responses)
logic                        data_bypass_tx_rsp_sink_tvalid;
logic                        data_bypass_tx_rsp_sink_tready;
logic [AXI_DATA_BITS-1:0]    data_bypass_tx_rsp_sink_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_bypass_tx_rsp_sink_tkeep;
logic                        data_bypass_tx_rsp_sink_tlast;
logic [PID_BITS-1:0]         data_bypass_tx_rsp_sink_tid;
logic [13:0]                 data_bypass_tx_rsp_sink_tdest;

logic                        data_bypass_tx_rsp_src_tvalid;
logic                        data_bypass_tx_rsp_src_tready;
logic [AXI_DATA_BITS-1:0]    data_bypass_tx_rsp_src_tdata;
logic [AXI_DATA_BITS/8-1:0]  data_bypass_tx_rsp_src_tkeep;
logic                        data_bypass_tx_rsp_src_tlast;
logic [PID_BITS-1:0]         data_bypass_tx_rsp_src_tid;

assign data_bypass_tx_rsp_sink_tvalid = data_bypass_tx_rsp_sink.tvalid;
assign data_bypass_tx_rsp_sink_tdata = data_bypass_tx_rsp_sink.tdata;
assign data_bypass_tx_rsp_sink_tkeep = data_bypass_tx_rsp_sink.tkeep;
assign data_bypass_tx_rsp_sink_tlast = data_bypass_tx_rsp_sink.tlast;
assign data_bypass_tx_rsp_sink_tid = data_bypass_tx_rsp_sink.tid;
assign data_bypass_tx_rsp_sink_tdest = data_bypass_tx_rsp_sink.tdest;
assign data_bypass_tx_rsp_sink.tready = data_bypass_tx_rsp_sink_tready;

assign data_bypass_tx_rsp_src.tvalid = data_bypass_tx_rsp_src_tvalid;
assign data_bypass_tx_rsp_src.tdata = data_bypass_tx_rsp_src_tdata;
assign data_bypass_tx_rsp_src.tkeep = data_bypass_tx_rsp_src_tkeep;
assign data_bypass_tx_rsp_src.tlast = data_bypass_tx_rsp_src_tlast;
assign data_bypass_tx_rsp_src.tid = data_bypass_tx_rsp_src_tid;
assign data_bypass_tx_rsp_src.tdest = axis_switch_m_tdest_bypass_tx_rsp;
assign data_bypass_tx_rsp_src_tready = data_bypass_tx_rsp_src.tready;

// Instantiate axis_switch IP (15 ports for N_ID=6)
vio_switch_ip_15 inst_vio_switch_ip_0 (
    .aclk(aclk),
    .aresetn(aresetn),

    // Master (output) ports - from switch to destinations
    .m_axis_tdata({
        data_bypass_tx_rsp_src_tdata,
        data_bypass_tx_req_src_tdata,
        data_bypass_rx_src_tdata,
        data_tcp_src_tdata,
        data_rdma_tx_rsp_src_tdata,
        data_rdma_tx_req_src_tdata,
        data_rdma_rx_src_tdata,
        data_host_rx_src_tdata,
        data_host_tx_src_tdata,
        data_vfiu_src_tdata[5], data_vfiu_src_tdata[4], data_vfiu_src_tdata[3], data_vfiu_src_tdata[2], data_vfiu_src_tdata[1], data_vfiu_src_tdata[0]
    }),
    .m_axis_tdest({
        axis_switch_m_tdest_bypass_tx_rsp,
        axis_switch_m_tdest_bypass_tx_req,
        axis_switch_m_tdest_bypass_rx,
        axis_switch_m_tdest_tcp,
        axis_switch_m_tdest_rdma_tx_rsp,
        axis_switch_m_tdest_rdma_tx_req,
        axis_switch_m_tdest_rdma_rx,
        axis_switch_m_tdest_host_rx,
        axis_switch_m_tdest_host_tx,
        axis_switch_m_tdest_vfiu[5], axis_switch_m_tdest_vfiu[4], axis_switch_m_tdest_vfiu[3], axis_switch_m_tdest_vfiu[2], axis_switch_m_tdest_vfiu[1], axis_switch_m_tdest_vfiu[0]
    }),
    .m_axis_tready({
        data_bypass_tx_rsp_src_tready,
        data_bypass_tx_req_src_tready,
        data_bypass_rx_src_tready,
        data_tcp_src_tready,
        data_rdma_tx_rsp_src_tready,
        data_rdma_tx_req_src_tready,
        data_rdma_rx_src_tready,
        data_host_rx_src_tready,
        data_host_tx_src_tready,
        data_vfiu_src_tready[5], data_vfiu_src_tready[4], data_vfiu_src_tready[3], data_vfiu_src_tready[2], data_vfiu_src_tready[1], data_vfiu_src_tready[0]
    }),
    .m_axis_tvalid({
        data_bypass_tx_rsp_src_tvalid,
        data_bypass_tx_req_src_tvalid,
        data_bypass_rx_src_tvalid,
        data_tcp_src_tvalid,
        data_rdma_tx_rsp_src_tvalid,
        data_rdma_tx_req_src_tvalid,
        data_rdma_rx_src_tvalid,
        data_host_rx_src_tvalid,
        data_host_tx_src_tvalid,
        data_vfiu_src_tvalid[5], data_vfiu_src_tvalid[4], data_vfiu_src_tvalid[3], data_vfiu_src_tvalid[2], data_vfiu_src_tvalid[1], data_vfiu_src_tvalid[0]
    }),
    .m_axis_tlast({
        data_bypass_tx_rsp_src_tlast,
        data_bypass_tx_req_src_tlast,
        data_bypass_rx_src_tlast,
        data_tcp_src_tlast,
        data_rdma_tx_rsp_src_tlast,
        data_rdma_tx_req_src_tlast,
        data_rdma_rx_src_tlast,
        data_host_rx_src_tlast,
        data_host_tx_src_tlast,
        data_vfiu_src_tlast[5], data_vfiu_src_tlast[4], data_vfiu_src_tlast[3], data_vfiu_src_tlast[2], data_vfiu_src_tlast[1], data_vfiu_src_tlast[0]
    }),
    .m_axis_tid({
        data_bypass_tx_rsp_src_tid,
        data_bypass_tx_req_src_tid,
        data_bypass_rx_src_tid,
        data_tcp_src_tid,
        data_rdma_tx_rsp_src_tid,
        data_rdma_tx_req_src_tid,
        data_rdma_rx_src_tid,
        data_host_rx_src_tid,
        data_host_tx_src_tid,
        data_vfiu_src_tid[5], data_vfiu_src_tid[4], data_vfiu_src_tid[3], data_vfiu_src_tid[2], data_vfiu_src_tid[1], data_vfiu_src_tid[0]
    }),
    .m_axis_tkeep({
        data_bypass_tx_rsp_src_tkeep,
        data_bypass_tx_req_src_tkeep,
        data_bypass_rx_src_tkeep,
        data_tcp_src_tkeep,
        data_rdma_tx_rsp_src_tkeep,
        data_rdma_tx_req_src_tkeep,
        data_rdma_rx_src_tkeep,
        data_host_rx_src_tkeep,
        data_host_tx_src_tkeep,
        data_vfiu_src_tkeep[5], data_vfiu_src_tkeep[4], data_vfiu_src_tkeep[3], data_vfiu_src_tkeep[2], data_vfiu_src_tkeep[1], data_vfiu_src_tkeep[0]
    }),

    // Slave (input) ports - from sources to switch
    .s_axis_tdata({
        data_bypass_tx_rsp_sink_tdata,
        data_bypass_tx_req_sink_tdata,
        data_bypass_rx_sink_tdata,
        data_tcp_sink_tdata,
        data_rdma_tx_rsp_sink_tdata,
        data_rdma_tx_req_sink_tdata,
        data_rdma_rx_sink_tdata,
        data_host_rx_sink_tdata,
        data_host_tx_sink_tdata,
        data_vfiu_sink_tdata[5], data_vfiu_sink_tdata[4], data_vfiu_sink_tdata[3], data_vfiu_sink_tdata[2], data_vfiu_sink_tdata[1], data_vfiu_sink_tdata[0]
    }),
    .s_axis_tdest({
        data_bypass_tx_rsp_sink_tdest,
        data_bypass_tx_req_sink_tdest,
        data_bypass_rx_sink_tdest,
        data_tcp_sink_tdest,
        data_rdma_tx_rsp_sink_tdest,
        data_rdma_tx_req_sink_tdest,
        data_rdma_rx_sink_tdest,
        data_host_rx_sink_tdest,
        data_host_tx_sink_tdest,
        data_vfiu_sink_tdest[5], data_vfiu_sink_tdest[4], data_vfiu_sink_tdest[3], data_vfiu_sink_tdest[2], data_vfiu_sink_tdest[1], data_vfiu_sink_tdest[0]
    }),
    .s_axis_tready({
        data_bypass_tx_rsp_sink_tready,
        data_bypass_tx_req_sink_tready,
        data_bypass_rx_sink_tready,
        data_tcp_sink_tready,
        data_rdma_tx_rsp_sink_tready,
        data_rdma_tx_req_sink_tready,
        data_rdma_rx_sink_tready,
        data_host_rx_sink_tready,
        data_host_tx_sink_tready,
        data_vfiu_sink_tready[5], data_vfiu_sink_tready[4], data_vfiu_sink_tready[3], data_vfiu_sink_tready[2], data_vfiu_sink_tready[1], data_vfiu_sink_tready[0]
    }),
    .s_axis_tvalid({
        data_bypass_tx_rsp_sink_tvalid,
        data_bypass_tx_req_sink_tvalid,
        data_bypass_rx_sink_tvalid,
        data_tcp_sink_tvalid,
        data_rdma_tx_rsp_sink_tvalid,
        data_rdma_tx_req_sink_tvalid,
        data_rdma_rx_sink_tvalid,
        data_host_rx_sink_tvalid,
        data_host_tx_sink_tvalid,
        data_vfiu_sink_tvalid[5], data_vfiu_sink_tvalid[4], data_vfiu_sink_tvalid[3], data_vfiu_sink_tvalid[2], data_vfiu_sink_tvalid[1], data_vfiu_sink_tvalid[0]
    }),
    .s_axis_tlast({
        data_bypass_tx_rsp_sink_tlast,
        data_bypass_tx_req_sink_tlast,
        data_bypass_rx_sink_tlast,
        data_tcp_sink_tlast,
        data_rdma_tx_rsp_sink_tlast,
        data_rdma_tx_req_sink_tlast,
        data_rdma_rx_sink_tlast,
        data_host_rx_sink_tlast,
        data_host_tx_sink_tlast,
        data_vfiu_sink_tlast[5], data_vfiu_sink_tlast[4], data_vfiu_sink_tlast[3], data_vfiu_sink_tlast[2], data_vfiu_sink_tlast[1], data_vfiu_sink_tlast[0]
    }),
    .s_axis_tid({
        data_bypass_tx_rsp_sink_tid,
        data_bypass_tx_req_sink_tid,
        data_bypass_rx_sink_tid,
        data_tcp_sink_tid,
        data_rdma_tx_rsp_sink_tid,
        data_rdma_tx_req_sink_tid,
        data_rdma_rx_sink_tid,
        data_host_rx_sink_tid,
        data_host_tx_sink_tid,
        data_vfiu_sink_tid[5], data_vfiu_sink_tid[4], data_vfiu_sink_tid[3], data_vfiu_sink_tid[2], data_vfiu_sink_tid[1], data_vfiu_sink_tid[0]
    }),
    .s_axis_tkeep({
        data_bypass_tx_rsp_sink_tkeep,
        data_bypass_tx_req_sink_tkeep,
        data_bypass_rx_sink_tkeep,
        data_tcp_sink_tkeep,
        data_rdma_tx_rsp_sink_tkeep,
        data_rdma_tx_req_sink_tkeep,
        data_rdma_rx_sink_tkeep,
        data_host_rx_sink_tkeep,
        data_host_tx_sink_tkeep,
        data_vfiu_sink_tkeep[5], data_vfiu_sink_tkeep[4], data_vfiu_sink_tkeep[3], data_vfiu_sink_tkeep[2], data_vfiu_sink_tkeep[1], data_vfiu_sink_tkeep[0]
    }),
    .s_decode_err(axis_switch_s_decode_err)
);

endmodule

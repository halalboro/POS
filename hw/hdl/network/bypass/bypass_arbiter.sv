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
 * @brief   Bypass arbitration
 *
 * Arbitration layer between all present user regions for raw Ethernet bypass.
 * Simpler than RDMA arbiter - no complex protocol state, just routes DMA requests.
 * Network-side uses AXI4SR with tid/tdest for vIO Switch routing.
 *
 * TX: Arbitrates user SQ requests + read response data to single network interface
 * RX: Routes read/write commands + write data from network to correct region based on vfid
 */
module bypass_arbiter (
    input  wire             aclk,
    input  wire             aresetn,

    // Network side (AXI4SR with routing info for vIO Switch)
    metaIntf.m              m_bypass_sq_net,
    metaIntf.s              s_bypass_rq_rd_net,
    metaIntf.s              s_bypass_rq_wr_net,
    AXI4SR.m                m_axis_bypass_rd_rsp_net,  // TX to network
    AXI4SR.s                s_axis_bypass_wr_net,      // RX from network

    // User side (per-region interfaces)
    metaIntf.s              s_bypass_sq_user [N_REGIONS],
    metaIntf.m              m_bypass_rq_rd_user [N_REGIONS],
    metaIntf.m              m_bypass_rq_wr_user [N_REGIONS],
    AXI4S.s                 s_axis_bypass_rd_rsp_user [N_REGIONS],
    AXI4S.m                 m_axis_bypass_wr_user [N_REGIONS]
);

//
// Internal AXI4S signals (for internal modules that use AXI4S)
//
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_bypass_rd_rsp_int ();
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_bypass_wr_int ();

// vfid signals for routing
logic [N_REGIONS_BITS-1:0] vfid_tx_int;

//
// TX Arbitration - User SQ requests + read response data to Network
//
// The rdma_meta_tx_arbiter handles:
// 1. Round-robin arbitration of SQ metadata from multiple regions
// 2. Muxing of TX read response data (s_axis_rd) from selected region to network (m_axis_rd)
//
rdma_meta_tx_arbiter inst_bypass_tx_arbiter (
    .aclk(aclk),
    .aresetn(aresetn),
    .s_meta(s_bypass_sq_user),
    .m_meta(m_bypass_sq_net),
    .s_axis_rd(s_axis_bypass_rd_rsp_user),
    .m_axis_rd(axis_bypass_rd_rsp_int),
    .vfid(vfid_tx_int)
);

//
// RX Memory Commands - Route to correct region based on vfid
//

// Read command routing (metadata only, no data - data goes through TX arbiter)
// The rdma_mux_cmd_rd expects data inputs for muxing to network, but for bypass
// the TX arbiter already handles data muxing. We use a simplified routing here.
`ifdef MULT_REGIONS
logic [N_REGIONS-1:0] rd_ready_src;
logic [N_REGIONS-1:0] rd_valid_src;
req_t [N_REGIONS-1:0] rd_req_src;
logic rd_ready_snk;
logic rd_valid_snk;
req_t rd_req_snk;
logic [N_REGIONS_BITS-1:0] rd_vfid;
logic rd_host;

metaIntf #(.STYPE(req_t)) rd_meta_que [N_REGIONS] (.*);

// Input from network
assign rd_valid_snk = s_bypass_rq_rd_net.valid;
assign s_bypass_rq_rd_net.ready = rd_ready_snk;
assign rd_req_snk = s_bypass_rq_rd_net.data;
assign rd_vfid = rd_req_snk.vfid;
assign rd_host = rd_req_snk.host;

// Demux to correct region (only if host=1)
always_comb begin
    rd_valid_src = '0;
    for (int i = 0; i < N_REGIONS; i++) begin
        rd_valid_src[i] = ((rd_vfid == i) && rd_host) ? rd_valid_snk : 1'b0;
        rd_req_src[i] = rd_req_snk;
    end
    rd_ready_snk = rd_host ? rd_ready_src[rd_vfid] : 1'b1;
end

// Output queues to users
for (genvar i = 0; i < N_REGIONS; i++) begin : gen_rd_queues
    assign rd_meta_que[i].valid = rd_valid_src[i];
    assign rd_ready_src[i] = rd_meta_que[i].ready;
    assign rd_meta_que[i].data = rd_req_src[i];

    meta_queue #(.DATA_BITS($bits(req_t))) inst_rd_queue (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_meta(rd_meta_que[i]),
        .m_meta(m_bypass_rq_rd_user[i])
    );
end
`else
`META_ASSIGN(s_bypass_rq_rd_net, m_bypass_rq_rd_user[0])
`endif

// Write command routing (for RX DMA writes) - includes data muxing
rdma_mux_cmd_wr inst_bypass_mux_cmd_wr (
    .aclk(aclk),
    .aresetn(aresetn),
    .s_req(s_bypass_rq_wr_net),
    .m_req(m_bypass_rq_wr_user),
    .s_axis_wr(axis_bypass_wr_int),
    .m_axis_wr(m_axis_bypass_wr_user),
    .m_wr_rdy()
);

//
// AXI4S to AXI4SR conversion (add tid/tdest for vIO Switch routing)
//

// TX Read Response: AXI4S -> AXI4SR (set tdest for vIO Switch)
assign m_axis_bypass_rd_rsp_net.tvalid = axis_bypass_rd_rsp_int.tvalid;
assign m_axis_bypass_rd_rsp_net.tdata  = axis_bypass_rd_rsp_int.tdata;
assign m_axis_bypass_rd_rsp_net.tkeep  = axis_bypass_rd_rsp_int.tkeep;
assign m_axis_bypass_rd_rsp_net.tlast  = axis_bypass_rd_rsp_int.tlast;
assign m_axis_bypass_rd_rsp_net.tid    = vfid_tx_int;
assign m_axis_bypass_rd_rsp_net.tdest  = '0;  // Network stack destination (external)
assign axis_bypass_rd_rsp_int.tready   = m_axis_bypass_rd_rsp_net.tready;

// RX Write Data: AXI4SR -> AXI4S (strip tid/tdest, use for internal routing)
assign axis_bypass_wr_int.tvalid = s_axis_bypass_wr_net.tvalid;
assign axis_bypass_wr_int.tdata  = s_axis_bypass_wr_net.tdata;
assign axis_bypass_wr_int.tkeep  = s_axis_bypass_wr_net.tkeep;
assign axis_bypass_wr_int.tlast  = s_axis_bypass_wr_net.tlast;
assign s_axis_bypass_wr_net.tready = axis_bypass_wr_int.tready;

endmodule

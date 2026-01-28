/**
 * This file is part of the Coyote <https://github.com/fpgasystems/Coyote>
 *
 * MIT Licence
 * Copyright (c) 2021-2025, Systems Group, ETH Zurich
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

`timescale 1ns / 1ps

import lynxTypes::*;

`include "axi_macros.svh"
`include "lynx_macros.svh"

/**
 * @brief   RDMA arbitration
 *
 * Arbitration layer between all present user regions.
 * Network-side uses AXI4SR with tid/tdest for vIO Switch routing.
 */
module rdma_arbiter (
    input  wire             aclk,
    input  wire             aresetn,

    // Network (AXI4SR with routing info for vIO Switch)
    metaIntf.m              m_rdma_sq_net,
    metaIntf.s              s_rdma_cq_net,

    metaIntf.s              s_rdma_rq_rd_net,
    metaIntf.s              s_rdma_rq_wr_net,
    AXI4SR.m                m_axis_rdma_rd_req_net,  // TX to network (read request data)
    AXI4SR.m                m_axis_rdma_rd_rsp_net,  // TX to network (read response data)
    AXI4SR.s                s_axis_rdma_wr_net,      // RX from network (write data)

    // User
    metaIntf.s              s_rdma_sq_user [N_REGIONS],
    metaIntf.m              m_rdma_cq_user [N_REGIONS],
    metaIntf.m              m_rdma_host_cq_user,

    metaIntf.m              m_rdma_rq_rd_user [N_REGIONS],
    metaIntf.m              m_rdma_rq_wr_user [N_REGIONS],
    AXI4S.s                 s_axis_rdma_rd_req_user [N_REGIONS],
    AXI4S.s                 s_axis_rdma_rd_rsp_user [N_REGIONS],
    AXI4S.m                 m_axis_rdma_wr_user [N_REGIONS]
);

//
// Internal AXI4S signals (for internal modules that use AXI4S)
//
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_rdma_rd_req_int ();
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_rdma_rd_rsp_int ();
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_rdma_wr_int ();

// vfid signals for routing
logic [N_REGIONS_BITS-1:0] vfid_tx_int;
logic [N_REGIONS_BITS-1:0] vfid_rx_int;

//
// Arbitration
//

// Arbitration RDMA requests host
rdma_meta_tx_arbiter inst_rdma_req_host_arbiter (
    .aclk(aclk),
    .aresetn(aresetn),
    .s_meta(s_rdma_sq_user),
    .m_meta(m_rdma_sq_net),
    .s_axis_rd(s_axis_rdma_rd_req_user),
    .m_axis_rd(axis_rdma_rd_req_int),
    .vfid(vfid_tx_int)
);

// Arbitration ACKs
metaIntf #(.STYPE(ack_t)) rdma_cq_user [N_REGIONS] (.*);

rdma_meta_rx_arbiter inst_rdma_ack_arbiter (
    .aclk(aclk),
    .aresetn(aresetn),
    .s_meta(s_rdma_cq_net),
    .m_meta_user(rdma_cq_user),
    .m_meta_host(m_rdma_host_cq_user),
    .vfid()  // vfid extracted from CQ metadata internally
);

for(genvar i = 0; i < N_REGIONS; i++) begin
    assign m_rdma_cq_user[i].valid = rdma_cq_user[i].valid;
    assign m_rdma_cq_user[i].data  = rdma_cq_user[i].data;

    assign rdma_cq_user[i].ready = 1'b1;
end

//
// Memory
//

// Read command and data
rdma_mux_cmd_rd inst_mux_cmd_rd (
    .aclk(aclk),
    .aresetn(aresetn),
    .s_req(s_rdma_rq_rd_net),
    .m_req(m_rdma_rq_rd_user),
    .s_axis_rd(s_axis_rdma_rd_rsp_user),
    .m_axis_rd(axis_rdma_rd_rsp_int)
);

// Write command crossing
rdma_mux_cmd_wr inst_mux_cmd_wr (
    .aclk(aclk),
    .aresetn(aresetn),
    .s_req(s_rdma_rq_wr_net),
    .m_req(m_rdma_rq_wr_user),
    .s_axis_wr(axis_rdma_wr_int),
    .m_axis_wr(m_axis_rdma_wr_user),
    .m_wr_rdy()
);

// Extract vfid from RX write request metadata for routing
assign vfid_rx_int = s_rdma_rq_wr_net.data.vfid;

//
// AXI4S to AXI4SR conversion (add tid/tdest for vIO Switch routing)
//

// TX Read Request: AXI4S -> AXI4SR (set tdest for vIO Switch)
assign m_axis_rdma_rd_req_net.tvalid = axis_rdma_rd_req_int.tvalid;
assign m_axis_rdma_rd_req_net.tdata  = axis_rdma_rd_req_int.tdata;
assign m_axis_rdma_rd_req_net.tkeep  = axis_rdma_rd_req_int.tkeep;
assign m_axis_rdma_rd_req_net.tlast  = axis_rdma_rd_req_int.tlast;
assign m_axis_rdma_rd_req_net.tid    = vfid_tx_int;
assign m_axis_rdma_rd_req_net.tdest  = '0;  // Network stack destination (external)
assign axis_rdma_rd_req_int.tready   = m_axis_rdma_rd_req_net.tready;

// TX Read Response: AXI4S -> AXI4SR (set tdest for vIO Switch)
assign m_axis_rdma_rd_rsp_net.tvalid = axis_rdma_rd_rsp_int.tvalid;
assign m_axis_rdma_rd_rsp_net.tdata  = axis_rdma_rd_rsp_int.tdata;
assign m_axis_rdma_rd_rsp_net.tkeep  = axis_rdma_rd_rsp_int.tkeep;
assign m_axis_rdma_rd_rsp_net.tlast  = axis_rdma_rd_rsp_int.tlast;
assign m_axis_rdma_rd_rsp_net.tid    = s_rdma_rq_rd_net.data.vfid;  // Source vfid from read request
assign m_axis_rdma_rd_rsp_net.tdest  = '0;  // Network stack destination (external)
assign axis_rdma_rd_rsp_int.tready   = m_axis_rdma_rd_rsp_net.tready;

// RX Write Data: AXI4SR -> AXI4S (strip tid/tdest, use for internal routing)
assign axis_rdma_wr_int.tvalid = s_axis_rdma_wr_net.tvalid;
assign axis_rdma_wr_int.tdata  = s_axis_rdma_wr_net.tdata;
assign axis_rdma_wr_int.tkeep  = s_axis_rdma_wr_net.tkeep;
assign axis_rdma_wr_int.tlast  = s_axis_rdma_wr_net.tlast;
assign s_axis_rdma_wr_net.tready = axis_rdma_wr_int.tready;

endmodule
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

import lynxTypes::*;

`include "axi_macros.svh"
`include "lynx_macros.svh"

/**
 * @brief   Bypass slice array
 *
 * Pipeline register slices for bypass stack interfaces.
 * User-side uses AXI4SR for vIO Switch routing.
 */
module bypass_slice_array_dyn #(
    parameter integer       N_STAGES = 2
) (
    // Network side
    metaIntf.m              m_bypass_qp_interface_n,
    metaIntf.m              m_bypass_conn_interface_n,
    metaIntf.m              m_bypass_sq_n,
    metaIntf.s              s_bypass_rq_rd_n,
    metaIntf.s              s_bypass_rq_wr_n,
    AXI4S.s                 s_axis_bypass_wr_n,
    AXI4S.m                 m_axis_bypass_rd_rsp_n,

    // User side (AXI4SR for vIO Switch routing)
    metaIntf.s              s_bypass_qp_interface_u,
    metaIntf.s              s_bypass_conn_interface_u,
    metaIntf.s              s_bypass_sq_u,
    metaIntf.m              m_bypass_rq_rd_u,
    metaIntf.m              m_bypass_rq_wr_u,
    AXI4SR.m                m_axis_bypass_wr_u,
    AXI4SR.s                s_axis_bypass_rd_rsp_u,

    input  wire             aclk,
    input  wire             aresetn
);

metaIntf #(.STYPE(qp_ctx_t)) bypass_qp_interface_s [N_STAGES+1] (.*);
metaIntf #(.STYPE(conn_ctx_t)) bypass_conn_interface_s [N_STAGES+1] (.*);
metaIntf #(.STYPE(dreq_t)) bypass_sq_s [N_STAGES+1] (.*);
metaIntf #(.STYPE(req_t)) bypass_rq_rd_s [N_STAGES+1] (.*);
metaIntf #(.STYPE(req_t)) bypass_rq_wr_s [N_STAGES+1] (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_bypass_rd_rsp_s [N_STAGES+1] (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_bypass_wr_s [N_STAGES+1] (.*);

// Slaves - Network to User direction
`META_ASSIGN(s_bypass_rq_rd_n, bypass_rq_rd_s[0])
`META_ASSIGN(s_bypass_rq_wr_n, bypass_rq_wr_s[0])
`AXIS_ASSIGN(s_axis_bypass_wr_n, axis_bypass_wr_s[0])

// Slaves - User to Network direction (AXI4SR input, convert to internal AXI4S)
`META_ASSIGN(s_bypass_qp_interface_u, bypass_qp_interface_s[0])
`META_ASSIGN(s_bypass_conn_interface_u, bypass_conn_interface_s[0])
`META_ASSIGN(s_bypass_sq_u, bypass_sq_s[0])
// AXI4SR -> AXI4S conversion (strip tid/tdest)
assign axis_bypass_rd_rsp_s[0].tvalid = s_axis_bypass_rd_rsp_u.tvalid;
assign axis_bypass_rd_rsp_s[0].tdata  = s_axis_bypass_rd_rsp_u.tdata;
assign axis_bypass_rd_rsp_s[0].tkeep  = s_axis_bypass_rd_rsp_u.tkeep;
assign axis_bypass_rd_rsp_s[0].tlast  = s_axis_bypass_rd_rsp_u.tlast;
assign s_axis_bypass_rd_rsp_u.tready  = axis_bypass_rd_rsp_s[0].tready;

// Masters - User to Network direction
`META_ASSIGN(bypass_qp_interface_s[N_STAGES], m_bypass_qp_interface_n)
`META_ASSIGN(bypass_conn_interface_s[N_STAGES], m_bypass_conn_interface_n)
`META_ASSIGN(bypass_sq_s[N_STAGES], m_bypass_sq_n)
`AXIS_ASSIGN(axis_bypass_rd_rsp_s[N_STAGES], m_axis_bypass_rd_rsp_n)

// Masters - Network to User direction (AXI4S to AXI4SR conversion)
`META_ASSIGN(bypass_rq_rd_s[N_STAGES], m_bypass_rq_rd_u)
`META_ASSIGN(bypass_rq_wr_s[N_STAGES], m_bypass_rq_wr_u)
// AXI4S -> AXI4SR conversion (add tid/tdest from metadata)
assign m_axis_bypass_wr_u.tvalid = axis_bypass_wr_s[N_STAGES].tvalid;
assign m_axis_bypass_wr_u.tdata  = axis_bypass_wr_s[N_STAGES].tdata;
assign m_axis_bypass_wr_u.tkeep  = axis_bypass_wr_s[N_STAGES].tkeep;
assign m_axis_bypass_wr_u.tlast  = axis_bypass_wr_s[N_STAGES].tlast;
assign m_axis_bypass_wr_u.tid    = m_bypass_rq_wr_u.data.vfid;  // vfid for local identification
assign m_axis_bypass_wr_u.tdest  = m_bypass_rq_wr_u.data.route_id;
assign axis_bypass_wr_s[N_STAGES].tready = m_axis_bypass_wr_u.tready;

for(genvar i = 0; i < N_STAGES; i++) begin

    // Bypass QP interface (reuse RDMA slice - same data width)
    axis_register_slice_rdma_184 inst_bypass_qp_interface (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(bypass_qp_interface_s[i].valid),
        .s_axis_tready(bypass_qp_interface_s[i].ready),
        .s_axis_tdata (bypass_qp_interface_s[i].data),
        .m_axis_tvalid(bypass_qp_interface_s[i+1].valid),
        .m_axis_tready(bypass_qp_interface_s[i+1].ready),
        .m_axis_tdata (bypass_qp_interface_s[i+1].data)
    );

    // Bypass conn interface (reuse RDMA slice)
    axis_register_slice_rdma_184 inst_bypass_conn_interface (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(bypass_conn_interface_s[i].valid),
        .s_axis_tready(bypass_conn_interface_s[i].ready),
        .s_axis_tdata (bypass_conn_interface_s[i].data),
        .m_axis_tvalid(bypass_conn_interface_s[i+1].valid),
        .m_axis_tready(bypass_conn_interface_s[i+1].ready),
        .m_axis_tdata (bypass_conn_interface_s[i+1].data)
    );

    // Bypass send queue (reuse RDMA slice - 256 bits for dreq_t)
    axis_register_slice_rdma_256 inst_bypass_sq (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(bypass_sq_s[i].valid),
        .s_axis_tready(bypass_sq_s[i].ready),
        .s_axis_tdata (bypass_sq_s[i].data),
        .m_axis_tvalid(bypass_sq_s[i+1].valid),
        .m_axis_tready(bypass_sq_s[i+1].ready),
        .m_axis_tdata (bypass_sq_s[i+1].data)
    );

    // Bypass rd command (reuse RDMA slice - 128 bits for req_t)
    axis_register_slice_rdma_128 inst_bypass_rq_rd (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(bypass_rq_rd_s[i].valid),
        .s_axis_tready(bypass_rq_rd_s[i].ready),
        .s_axis_tdata (bypass_rq_rd_s[i].data),
        .m_axis_tvalid(bypass_rq_rd_s[i+1].valid),
        .m_axis_tready(bypass_rq_rd_s[i+1].ready),
        .m_axis_tdata (bypass_rq_rd_s[i+1].data)
    );

    // Bypass wr command (reuse RDMA slice - 128 bits for req_t)
    axis_register_slice_rdma_128 inst_bypass_rq_wr (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(bypass_rq_wr_s[i].valid),
        .s_axis_tready(bypass_rq_wr_s[i].ready),
        .s_axis_tdata (bypass_rq_wr_s[i].data),
        .m_axis_tvalid(bypass_rq_wr_s[i+1].valid),
        .m_axis_tready(bypass_rq_wr_s[i+1].ready),
        .m_axis_tdata (bypass_rq_wr_s[i+1].data)
    );

    // Read response data (reuse RDMA data slice - 512 bits)
    axis_register_slice_rdma_data_512 inst_bypass_rd_rsp_data (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(axis_bypass_rd_rsp_s[i].tvalid),
        .s_axis_tready(axis_bypass_rd_rsp_s[i].tready),
        .s_axis_tdata (axis_bypass_rd_rsp_s[i].tdata),
        .s_axis_tkeep (axis_bypass_rd_rsp_s[i].tkeep),
        .s_axis_tlast (axis_bypass_rd_rsp_s[i].tlast),
        .m_axis_tvalid(axis_bypass_rd_rsp_s[i+1].tvalid),
        .m_axis_tready(axis_bypass_rd_rsp_s[i+1].tready),
        .m_axis_tdata (axis_bypass_rd_rsp_s[i+1].tdata),
        .m_axis_tkeep (axis_bypass_rd_rsp_s[i+1].tkeep),
        .m_axis_tlast (axis_bypass_rd_rsp_s[i+1].tlast)
    );

    // Write data (reuse RDMA data slice - 512 bits)
    axis_register_slice_rdma_data_512 inst_bypass_wr_data (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(axis_bypass_wr_s[i].tvalid),
        .s_axis_tready(axis_bypass_wr_s[i].tready),
        .s_axis_tdata (axis_bypass_wr_s[i].tdata),
        .s_axis_tkeep (axis_bypass_wr_s[i].tkeep),
        .s_axis_tlast (axis_bypass_wr_s[i].tlast),
        .m_axis_tvalid(axis_bypass_wr_s[i+1].tvalid),
        .m_axis_tready(axis_bypass_wr_s[i+1].tready),
        .m_axis_tdata (axis_bypass_wr_s[i+1].tdata),
        .m_axis_tkeep (axis_bypass_wr_s[i+1].tkeep),
        .m_axis_tlast (axis_bypass_wr_s[i+1].tlast)
    );

end

endmodule

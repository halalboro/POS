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
 * @brief   Bypass slice array (network side)
 *
 * Pipeline register slices for bypass stack interfaces.
 * Uses AXI4S on both sides for simplicity.
 * Route handling is done elsewhere in the VIU/vFIU gateways.
 *
 * This sits between bypass_meta_only_arbiter and bypass_stack.
 */
module bypass_slice_array_dyn #(
    parameter integer       N_STAGES = 2
) (
    // Network side (to/from bypass_stack)
    metaIntf.m              m_bypass_sq_n,
    metaIntf.s              s_bypass_rq_rd_n,
    metaIntf.s              s_bypass_rq_wr_n,
    AXI4S.s                 s_axis_bypass_wr_n,
    AXI4S.m                 m_axis_bypass_rd_rsp_n,

    // User side (to/from meta_only_arbiter or vIO Switch gateway)
    metaIntf.s              s_bypass_sq_u,
    metaIntf.m              m_bypass_rq_rd_u,
    metaIntf.m              m_bypass_rq_wr_u,
    AXI4S.s                 s_axis_bypass_rd_rsp_u,
    AXI4S.m                 m_axis_bypass_wr_u,

    input  wire             aclk,
    input  wire             aresetn
);

metaIntf #(.STYPE(dreq_t)) bypass_sq_s [N_STAGES+1] (aclk, aresetn);
metaIntf #(.STYPE(req_t)) bypass_rq_rd_s [N_STAGES+1] (aclk, aresetn);
metaIntf #(.STYPE(req_t)) bypass_rq_wr_s [N_STAGES+1] (aclk, aresetn);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_bypass_rd_rsp_s [N_STAGES+1] (aclk, aresetn);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_bypass_wr_s [N_STAGES+1] (aclk, aresetn);

// Slaves - Network side (from bypass_stack)
`META_ASSIGN(s_bypass_rq_rd_n, bypass_rq_rd_s[0])
`META_ASSIGN(s_bypass_rq_wr_n, bypass_rq_wr_s[0])
`AXIS_ASSIGN(s_axis_bypass_wr_n, axis_bypass_wr_s[0])

// Slaves - User side (from arbiter/vIO Switch gateway)
`META_ASSIGN(s_bypass_sq_u, bypass_sq_s[0])
`AXIS_ASSIGN(s_axis_bypass_rd_rsp_u, axis_bypass_rd_rsp_s[0])

// Masters - Network side (to bypass_stack)
`META_ASSIGN(bypass_sq_s[N_STAGES], m_bypass_sq_n)
`AXIS_ASSIGN(axis_bypass_rd_rsp_s[N_STAGES], m_axis_bypass_rd_rsp_n)

// Masters - User side (to arbiter/vIO Switch gateway)
`META_ASSIGN(bypass_rq_rd_s[N_STAGES], m_bypass_rq_rd_u)
`META_ASSIGN(bypass_rq_wr_s[N_STAGES], m_bypass_rq_wr_u)
`AXIS_ASSIGN(axis_bypass_wr_s[N_STAGES], m_axis_bypass_wr_u)

for(genvar i = 0; i < N_STAGES; i++) begin

    // Bypass send queue (256 bits for dreq_t)
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

    // Bypass rd command (128 bits for req_t)
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

    // Bypass wr command (128 bits for req_t)
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

    // Read response data (512 bits)
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

    // Write data (512 bits)
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

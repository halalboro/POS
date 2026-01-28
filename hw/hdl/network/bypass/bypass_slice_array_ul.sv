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
 * @brief   Bypass slice array (user-side)
 *
 * Pipeline register slices for bypass stack on user side.
 * Simplified version of RDMA slice_array_ul - no ACK path (bypass doesn't use completion queue).
 */
module bypass_slice_array_ul #(
    parameter integer       N_STAGES = 2
) (
    // Network side (towards arbiter/ccross)
    metaIntf.m              m_bypass_sq_n,
    metaIntf.s              s_bypass_rd_req_n,
    metaIntf.s              s_bypass_wr_req_n,
    AXI4S.m                 m_axis_bypass_rd_rsp_n,
    AXI4S.s                 s_axis_bypass_wr_n,

    // User side (towards decoupler/user_wrapper)
    metaIntf.s              s_bypass_sq_u,
    metaIntf.m              m_bypass_rd_req_u,
    metaIntf.m              m_bypass_wr_req_u,
    AXI4S.s                 s_axis_bypass_rd_rsp_u,
    AXI4S.m                 m_axis_bypass_wr_u,

    input  wire             aclk,
    input  wire             aresetn
);

metaIntf #(.STYPE(dreq_t)) bypass_sq_s [N_STAGES+1] (.*);
metaIntf #(.STYPE(req_t)) bypass_rd_req_s [N_STAGES+1] (.*);
metaIntf #(.STYPE(req_t)) bypass_wr_req_s [N_STAGES+1] (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_bypass_rd_rsp_s [N_STAGES+1] (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_bypass_wr_s [N_STAGES+1] (.*);

// Slaves - Network to User direction
`META_ASSIGN(s_bypass_rd_req_n, bypass_rd_req_s[0])
`META_ASSIGN(s_bypass_wr_req_n, bypass_wr_req_s[0])
`AXIS_ASSIGN(s_axis_bypass_wr_n, axis_bypass_wr_s[0])

// Slaves - User to Network direction
`META_ASSIGN(s_bypass_sq_u, bypass_sq_s[0])
`AXIS_ASSIGN(s_axis_bypass_rd_rsp_u, axis_bypass_rd_rsp_s[0])

// Masters - User to Network direction
`META_ASSIGN(bypass_sq_s[N_STAGES], m_bypass_sq_n)
`AXIS_ASSIGN(axis_bypass_rd_rsp_s[N_STAGES], m_axis_bypass_rd_rsp_n)

// Masters - Network to User direction
`META_ASSIGN(bypass_rd_req_s[N_STAGES], m_bypass_rd_req_u)
`META_ASSIGN(bypass_wr_req_s[N_STAGES], m_bypass_wr_req_u)
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
    axis_register_slice_rdma_128 inst_bypass_req_rd (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(bypass_rd_req_s[i].valid),
        .s_axis_tready(bypass_rd_req_s[i].ready),
        .s_axis_tdata (bypass_rd_req_s[i].data),
        .m_axis_tvalid(bypass_rd_req_s[i+1].valid),
        .m_axis_tready(bypass_rd_req_s[i+1].ready),
        .m_axis_tdata (bypass_rd_req_s[i+1].data)
    );

    // Bypass wr command (128 bits for req_t)
    axis_register_slice_rdma_128 inst_bypass_req_wr (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(bypass_wr_req_s[i].valid),
        .s_axis_tready(bypass_wr_req_s[i].ready),
        .s_axis_tdata (bypass_wr_req_s[i].data),
        .m_axis_tvalid(bypass_wr_req_s[i+1].valid),
        .m_axis_tready(bypass_wr_req_s[i+1].ready),
        .m_axis_tdata (bypass_wr_req_s[i+1].data)
    );

    // Read response data (512 bits)
    axis_register_slice_rdma_data_512 inst_bypass_data_rd_rsp (
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
    axis_register_slice_rdma_data_512 inst_bypass_data_wr (
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

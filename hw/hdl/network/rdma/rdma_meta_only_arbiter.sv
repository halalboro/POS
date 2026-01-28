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
 * @brief   RDMA Metadata-Only Arbitration (for vIO Switch routing)
 *
 * This module handles ONLY metadata arbitration between user regions.
 * Data routing is handled by vIO Switch instead of internal muxes.
 *
 * Metadata handled:
 *   - SQ (Send Queue) requests: N_REGIONS → 1 network (round-robin mux)
 *   - CQ (Completion Queue) acks: 1 network → N_REGIONS (vfid-based demux)
 *   - RD commands: 1 network → N_REGIONS (vfid-based demux)
 *   - WR commands: 1 network → N_REGIONS (vfid-based demux)
 *
 * Data NOT handled (routed through vIO Switch instead):
 *   - TX read request data (vFPGA → network)
 *   - TX read response data (vFPGA → network)
 *   - RX write data (network → vFPGA)
 *
 * Outputs vfid_tx and vfid_rx for vIO Switch tdest calculation.
 */
module rdma_meta_only_arbiter (
    input  wire             aclk,
    input  wire             aresetn,

    // Network side - Metadata only
    metaIntf.m              m_rdma_sq_net,      // SQ to network
    metaIntf.s              s_rdma_cq_net,      // CQ from network
    metaIntf.s              s_rdma_rq_rd_net,   // RD commands from network
    metaIntf.s              s_rdma_rq_wr_net,   // WR commands from network

    // User side - Metadata only (per region)
    metaIntf.s              s_rdma_sq_user [N_REGIONS],      // SQ from users
    metaIntf.m              m_rdma_cq_user [N_REGIONS],      // CQ to users
    metaIntf.m              m_rdma_host_cq_user,             // CQ to host (for completion tracking)
    metaIntf.m              m_rdma_rq_rd_user [N_REGIONS],   // RD commands to users
    metaIntf.m              m_rdma_rq_wr_user [N_REGIONS],   // WR commands to users

    // vIO Switch routing - vfid for tdest calculation
    output logic [N_REGIONS_BITS-1:0] vfid_rx,  // vfid for incoming data (network → vFPGA)
    output logic [N_REGIONS_BITS-1:0] vfid_tx   // vfid for outgoing data (vFPGA → network)
);

// ============================================================================
// TX Path: SQ Arbitration (N_REGIONS → 1 network)
// ============================================================================
// Round-robin arbitrate SQ requests from all regions

`ifdef MULT_REGIONS

logic [N_REGIONS-1:0] sq_ready_snk;
logic [N_REGIONS-1:0] sq_valid_snk;
dreq_t [N_REGIONS-1:0] sq_req_snk;

logic sq_ready_src;
logic sq_valid_src;
dreq_t sq_req_src;

logic [N_REGIONS_BITS-1:0] rr_reg;

metaIntf #(.STYPE(dreq_t)) sq_meta_que [N_REGIONS] (.*);

// Input queues for SQ requests
for(genvar i = 0; i < N_REGIONS; i++) begin : gen_sq_queues
    axis_data_fifo_cnfg_rdma_256 inst_sq_queue (
        .s_axis_aresetn(aresetn),
        .s_axis_aclk(aclk),
        .s_axis_tvalid(s_rdma_sq_user[i].valid),
        .s_axis_tready(s_rdma_sq_user[i].ready),
        .s_axis_tdata(s_rdma_sq_user[i].data),
        .m_axis_tvalid(sq_meta_que[i].valid),
        .m_axis_tready(sq_meta_que[i].ready),
        .m_axis_tdata(sq_meta_que[i].data),
        .axis_wr_data_count()
    );

    assign sq_valid_snk[i] = sq_meta_que[i].valid;
    assign sq_meta_que[i].ready = sq_ready_snk[i];
    assign sq_req_snk[i] = sq_meta_que[i].data;
end

// Output to network
assign m_rdma_sq_net.valid = sq_valid_src;
assign sq_ready_src = m_rdma_sq_net.ready;
assign m_rdma_sq_net.data = sq_req_src;

// Round-robin counter
always_ff @(posedge aclk) begin
    if (aresetn == 1'b0) begin
        rr_reg <= 0;
    end else begin
        if (sq_valid_src & sq_ready_src) begin
            rr_reg <= (rr_reg >= N_REGIONS-1) ? '0 : rr_reg + 1;
        end
    end
end

// Round-robin arbitration logic
always_comb begin
    sq_ready_snk = '0;
    sq_valid_src = 1'b0;
    vfid_tx = '0;
    sq_req_src = sq_req_snk[0];

    for (int i = 0; i < N_REGIONS; i++) begin
        automatic int idx = (i + rr_reg >= N_REGIONS) ? (i + rr_reg - N_REGIONS) : (i + rr_reg);
        if (sq_valid_snk[idx]) begin
            vfid_tx = idx[N_REGIONS_BITS-1:0];
            sq_valid_src = 1'b1;
            sq_req_src = sq_req_snk[idx];
            sq_ready_snk[idx] = sq_ready_src;
            break;
        end
    end
end

`else // Single region

axis_data_fifo_cnfg_rdma_256 inst_sq_queue (
    .s_axis_aresetn(aresetn),
    .s_axis_aclk(aclk),
    .s_axis_tvalid(s_rdma_sq_user[0].valid),
    .s_axis_tready(s_rdma_sq_user[0].ready),
    .s_axis_tdata(s_rdma_sq_user[0].data),
    .m_axis_tvalid(m_rdma_sq_net.valid),
    .m_axis_tready(m_rdma_sq_net.ready),
    .m_axis_tdata(m_rdma_sq_net.data),
    .axis_wr_data_count()
);

assign vfid_tx = '0;

`endif

// ============================================================================
// RX Path: CQ Distribution (1 network → N_REGIONS)
// ============================================================================
// Route CQ (completion/ACK) to correct region based on vfid field

// Forward to host for completion tracking
assign m_rdma_host_cq_user.valid = s_rdma_cq_net.valid;
assign m_rdma_host_cq_user.data = s_rdma_cq_net.data;

`ifdef MULT_REGIONS

logic [N_REGIONS-1:0] cq_ready_src;
logic [N_REGIONS-1:0] cq_valid_src;
ack_t [N_REGIONS-1:0] cq_req_src;

logic cq_ready_snk;
logic cq_valid_snk;
ack_t cq_req_snk;

metaIntf #(.STYPE(ack_t)) cq_meta_que [N_REGIONS] (.*);

// Input from network
assign cq_valid_snk = s_rdma_cq_net.valid;
assign s_rdma_cq_net.ready = cq_ready_snk;
assign cq_req_snk = s_rdma_cq_net.data;

// Extract vfid for routing
assign vfid_rx = cq_req_snk.vfid;

// Demux to correct region
always_comb begin
    cq_valid_src = '0;
    for (int i = 0; i < N_REGIONS; i++) begin
        cq_valid_src[i] = (vfid_rx == i) ? cq_valid_snk : 1'b0;
        cq_req_src[i] = cq_req_snk;
    end
    cq_ready_snk = cq_ready_src[vfid_rx];
end

// Output queues to users
for (genvar i = 0; i < N_REGIONS; i++) begin : gen_cq_queues
    assign cq_meta_que[i].valid = cq_valid_src[i];
    assign cq_ready_src[i] = cq_meta_que[i].ready;
    assign cq_meta_que[i].data = cq_req_src[i];

    axis_data_fifo_cnfg_rdma_32 inst_cq_queue (
        .s_axis_aresetn(aresetn),
        .s_axis_aclk(aclk),
        .s_axis_tvalid(cq_meta_que[i].valid),
        .s_axis_tready(cq_meta_que[i].ready),
        .s_axis_tdata(cq_meta_que[i].data),
        .m_axis_tvalid(m_rdma_cq_user[i].valid),
        .m_axis_tready(m_rdma_cq_user[i].ready),
        .m_axis_tdata(m_rdma_cq_user[i].data),
        .axis_wr_data_count()
    );
end

`else // Single region

axis_data_fifo_cnfg_rdma_32 inst_cq_queue (
    .s_axis_aresetn(aresetn),
    .s_axis_aclk(aclk),
    .s_axis_tvalid(s_rdma_cq_net.valid),
    .s_axis_tready(s_rdma_cq_net.ready),
    .s_axis_tdata(s_rdma_cq_net.data),
    .m_axis_tvalid(m_rdma_cq_user[0].valid),
    .m_axis_tready(m_rdma_cq_user[0].ready),
    .m_axis_tdata(m_rdma_cq_user[0].data),
    .axis_wr_data_count()
);

assign vfid_rx = '0;

`endif

// ============================================================================
// RX Path: RD Command Distribution (1 network → N_REGIONS)
// ============================================================================
// Route RD commands to correct region based on vfid field (only if host=1)

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
assign rd_valid_snk = s_rdma_rq_rd_net.valid;
assign s_rdma_rq_rd_net.ready = rd_ready_snk;
assign rd_req_snk = s_rdma_rq_rd_net.data;

// Extract vfid and host flag
assign rd_vfid = rd_req_snk.vfid;
assign rd_host = rd_req_snk.host;

// Demux to correct region (only if host=1, otherwise don't route)
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
        .m_meta(m_rdma_rq_rd_user[i])
    );
end

`else // Single region

`META_ASSIGN(s_rdma_rq_rd_net, m_rdma_rq_rd_user[0])

`endif

// ============================================================================
// RX Path: WR Command Distribution (1 network → N_REGIONS)
// ============================================================================
// Route WR commands to correct region based on vfid field

`ifdef MULT_REGIONS

logic [N_REGIONS-1:0] wr_ready_src;
logic [N_REGIONS-1:0] wr_valid_src;
req_t [N_REGIONS-1:0] wr_req_src;

logic wr_ready_snk;
logic wr_valid_snk;
req_t wr_req_snk;

logic [N_REGIONS_BITS-1:0] wr_vfid;

metaIntf #(.STYPE(req_t)) wr_meta_que [N_REGIONS] (.*);

// Input from network
assign wr_valid_snk = s_rdma_rq_wr_net.valid;
assign s_rdma_rq_wr_net.ready = wr_ready_snk;
assign wr_req_snk = s_rdma_rq_wr_net.data;

// Extract vfid
assign wr_vfid = wr_req_snk.vfid;

// Demux to correct region
always_comb begin
    wr_valid_src = '0;
    for (int i = 0; i < N_REGIONS; i++) begin
        wr_valid_src[i] = (wr_vfid == i) ? wr_valid_snk : 1'b0;
        wr_req_src[i] = wr_req_snk;
    end
    wr_ready_snk = wr_ready_src[wr_vfid];
end

// Output queues to users
for (genvar i = 0; i < N_REGIONS; i++) begin : gen_wr_queues
    assign wr_meta_que[i].valid = wr_valid_src[i];
    assign wr_ready_src[i] = wr_meta_que[i].ready;
    assign wr_meta_que[i].data = wr_req_src[i];

    meta_queue #(.DATA_BITS($bits(req_t))) inst_wr_queue (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_meta(wr_meta_que[i]),
        .m_meta(m_rdma_rq_wr_user[i])
    );
end

`else // Single region

`META_ASSIGN(s_rdma_rq_wr_net, m_rdma_rq_wr_user[0])

`endif

endmodule

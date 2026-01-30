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
 * @brief   TCP Metadata-Only Arbitration (for vIO Switch routing)
 *
 * This module handles ONLY metadata arbitration between user regions.
 * Data routing is handled by vIO Switch instead of internal muxes.
 *
 * TCP Metadata handled:
 *   - listen_req: N_REGIONS -> 1 network (mux)
 *   - listen_rsp: 1 network -> N_REGIONS (demux via port_table)
 *   - open_req: N_REGIONS -> 1 network (mux)
 *   - open_rsp: 1 network -> N_REGIONS (demux via conn_table)
 *   - close_req: N_REGIONS -> 1 network (mux)
 *   - notify: 1 network -> N_REGIONS (demux via conn_table)
 *   - rd_pkg: N_REGIONS -> 1 network (mux, tracks vfid for RX data routing)
 *   - rx_meta: 1 network -> N_REGIONS (demux based on rd_pkg vfid)
 *   - tx_meta: N_REGIONS -> 1 network (mux, provides route_id for TX)
 *   - tx_stat: 1 network -> N_REGIONS (demux based on tx_meta vfid)
 *
 * Data NOT handled (routed through vIO Switch instead):
 *   - TX data (vFPGA -> network): vIO Switch routes from vFIU to TCP port
 *   - RX data (network -> vFPGA): vIO Switch routes from TCP port to vFIU
 *
 * Outputs route_id_rx and route_id_tx for vIO Switch tdest.
 * Route format: [13:10]=reserved, [9:6]=sender_id, [5:2]=receiver_id, [1:0]=flags
 * For TCP RX: sender_id = PORT_TCP (N_REGIONS + 5), receiver_id = vfid
 */
module tcp_meta_only_arbiter (
    input  wire             aclk,
    input  wire             aresetn,

    // ========================================================================
    // Network side - To/from TCP stack
    // ========================================================================
    metaIntf.m              m_tcp_listen_req_net,
    metaIntf.s              s_tcp_listen_rsp_net,
    metaIntf.m              m_tcp_open_req_net,
    metaIntf.s              s_tcp_open_rsp_net,
    metaIntf.m              m_tcp_close_req_net,
    metaIntf.s              s_tcp_notify_net,
    metaIntf.m              m_tcp_rd_pkg_net,
    metaIntf.s              s_tcp_rx_meta_net,
    metaIntf.m              m_tcp_tx_meta_net,
    metaIntf.s              s_tcp_tx_stat_net,

    // ========================================================================
    // User side - Per-region metadata interfaces
    // ========================================================================
    metaIntf.s              s_tcp_listen_req_user [N_REGIONS],
    metaIntf.m              m_tcp_listen_rsp_user [N_REGIONS],
    metaIntf.s              s_tcp_open_req_user [N_REGIONS],
    metaIntf.m              m_tcp_open_rsp_user [N_REGIONS],
    metaIntf.s              s_tcp_close_req_user [N_REGIONS],
    metaIntf.m              m_tcp_notify_user [N_REGIONS],
    metaIntf.s              s_tcp_rd_pkg_user [N_REGIONS],
    metaIntf.m              m_tcp_rx_meta_user [N_REGIONS],
    metaIntf.s              s_tcp_tx_meta_user [N_REGIONS],
    metaIntf.m              m_tcp_tx_stat_user [N_REGIONS],

    // ========================================================================
    // vIO Switch routing signals - full route_id for tdest
    // Route format: [13:10]=reserved, [9:6]=sender_id, [5:2]=receiver_id, [1:0]=flags
    // ========================================================================
    output logic [13:0] route_id_rx,  // Route ID for incoming data (network -> vFPGA)
    output logic [13:0] route_id_tx   // Route ID for outgoing data (vFPGA -> network)
);

// Safe VF bitwidth
localparam int VF_BITS = (N_REGIONS <= 1) ? 1 : $clog2(N_REGIONS);

// Port ID for route encoding (must match vIO Switch port assignments)
localparam logic [3:0] PORT_TCP = N_REGIONS + 5;  // TCP port in vIO Switch

// Internal vfid signals
logic [N_REGIONS_BITS-1:0] vfid_rx;
logic [N_REGIONS_BITS-1:0] vfid_tx;

// ============================================================================
// Port Table and Connection Table (session tracking)
// ============================================================================
// These tables track which vFPGA owns which port/session for routing

`ifdef MULT_REGIONS

logic [TCP_IP_PORT_BITS-1:0]            port_addr;
logic [TCP_PORT_TABLE_DATA_BITS-1:0]    rsid;
logic [13:0]                            route_id;

// TX route_id lookup signals
logic [TCP_SESSION_BITS-1:0]            tx_sid;
logic                                   tx_sid_valid;
logic [13:0]                            tx_route_id;
logic                                   tx_route_id_valid;

// Port table: tracks which vFPGA listens on which port
tcp_port_table inst_port_table (
    .aclk         (aclk),
    .aresetn      (aresetn),
    .s_listen_req (s_tcp_listen_req_user),
    .m_listen_req (m_tcp_listen_req_net),
    .s_listen_rsp (s_tcp_listen_rsp_net),
    .m_listen_rsp (m_tcp_listen_rsp_user),
    .port_addr    (port_addr),
    .rsid_out     (rsid),
    .route_id_out (route_id)
);

// Connection table: tracks which vFPGA owns which session
tcp_conn_table inst_conn_table (
    .aclk        (aclk),
    .aresetn     (aresetn),
    .s_open_req  (s_tcp_open_req_user),
    .m_open_req  (m_tcp_open_req_net),
    .s_close_req (s_tcp_close_req_user),
    .m_close_req (m_tcp_close_req_net),
    .s_open_rsp  (s_tcp_open_rsp_net),
    .m_open_rsp  (m_tcp_open_rsp_user),
    .s_notify    (s_tcp_notify_net),
    .m_notify    (m_tcp_notify_user),
    .port_addr   (port_addr),
    .rsid_in     (rsid),
    .route_id_in       (route_id),
    .tx_sid            (tx_sid),
    .tx_sid_valid      (tx_sid_valid),
    .tx_route_id       (tx_route_id),
    .tx_route_id_valid (tx_route_id_valid)
);

// ============================================================================
// RX Metadata Path: rd_pkg arbitration + rx_meta demux
// ============================================================================
// rd_pkg: vFPGA requests to read data -> mux to network, track vfid for routing
// rx_meta: network sends metadata -> demux to correct vFPGA based on rd_pkg vfid

// rd_pkg arbiter with vfid tracking
logic [VF_BITS-1:0] rx_vfid_pick;
metaIntf #(.STYPE(tcp_rd_pkg_t)) rd_pkg_arb ();

meta_arbiter #(.DATA_BITS($bits(tcp_rd_pkg_t))) inst_rd_pkg_arbiter (
    .aclk    (aclk),
    .aresetn (aresetn),
    .s_meta  (s_tcp_rd_pkg_user),
    .m_meta  (rd_pkg_arb),
    .id_out  (rx_vfid_pick)
);

// Queues for vfid tracking (RX data and RX meta routing)
metaIntf #(.STYPE(logic[VF_BITS-1:0])) rx_seq_snk (), rx_seq_src ();
metaIntf #(.STYPE(logic[VF_BITS-1:0])) rx_meta_snk(), rx_meta_src();

// Forward rd_pkg to network while queuing vfid
always_comb begin
    m_tcp_rd_pkg_net.valid = 1'b0;
    m_tcp_rd_pkg_net.data  = rd_pkg_arb.data;
    rd_pkg_arb.ready       = 1'b0;
    rx_seq_snk.valid       = 1'b0;
    rx_seq_snk.data        = rx_vfid_pick;
    rx_meta_snk.valid      = 1'b0;
    rx_meta_snk.data       = rx_vfid_pick;

    if (rd_pkg_arb.valid && m_tcp_rd_pkg_net.ready && rx_seq_snk.ready && rx_meta_snk.ready) begin
        m_tcp_rd_pkg_net.valid = 1'b1;
        rd_pkg_arb.ready       = 1'b1;
        rx_seq_snk.valid       = 1'b1;
        rx_meta_snk.valid      = 1'b1;
    end
end

queue #(.QTYPE(logic[VF_BITS-1:0]), .QDEPTH(32)) inst_rx_seq_q (
    .aclk     (aclk),
    .aresetn  (aresetn),
    .val_snk  (rx_seq_snk.valid),
    .rdy_snk  (rx_seq_snk.ready),
    .data_snk (rx_seq_snk.data),
    .val_src  (rx_seq_src.valid),
    .rdy_src  (rx_seq_src.ready),
    .data_src (rx_seq_src.data)
);

queue #(.QTYPE(logic[VF_BITS-1:0]), .QDEPTH(32)) inst_rx_meta_q (
    .aclk     (aclk),
    .aresetn  (aresetn),
    .val_snk  (rx_meta_snk.valid),
    .rdy_snk  (rx_meta_snk.ready),
    .data_snk (rx_meta_snk.data),
    .val_src  (rx_meta_src.valid),
    .rdy_src  (rx_meta_src.ready),
    .data_src (rx_meta_src.data)
);

// RX meta demux to correct vFPGA
logic [N_REGIONS-1:0] rx_meta_ready;
logic [N_REGIONS-1:0] rx_meta_valid;
logic [$bits(tcp_rx_meta_t)-1:0] rx_meta_data [N_REGIONS];

for (genvar i = 0; i < N_REGIONS; i++) begin : gen_rx_meta_assign
    assign rx_meta_ready[i] = m_tcp_rx_meta_user[i].ready;
    assign m_tcp_rx_meta_user[i].valid = rx_meta_valid[i];
    assign m_tcp_rx_meta_user[i].data = rx_meta_data[i];
end

// RX meta routing FSM
typedef enum logic [1:0] { RM_IDLE, RM_ROUTE } rx_meta_state_t;
rx_meta_state_t rx_meta_state_C, rx_meta_state_N;
logic [VF_BITS-1:0] rxm_vfid_C, rxm_vfid_N;

always_ff @(posedge aclk) begin
    if (!aresetn) begin
        rx_meta_state_C <= RM_IDLE;
        rxm_vfid_C <= '0;
    end else begin
        rx_meta_state_C <= rx_meta_state_N;
        rxm_vfid_C <= rxm_vfid_N;
    end
end

always_comb begin
    rx_meta_state_N = rx_meta_state_C;
    case (rx_meta_state_C)
        RM_IDLE: if (rx_meta_src.valid) rx_meta_state_N = RM_ROUTE;
        RM_ROUTE: if (s_tcp_rx_meta_net.valid && rx_meta_ready[rxm_vfid_C]) rx_meta_state_N = RM_IDLE;
    endcase
end

always_comb begin
    rx_meta_src.ready = 1'b0;
    s_tcp_rx_meta_net.ready = 1'b0;
    for (int i = 0; i < N_REGIONS; i++) begin
        rx_meta_valid[i] = 1'b0;
        rx_meta_data[i] = '0;
    end
    rxm_vfid_N = rxm_vfid_C;

    case (rx_meta_state_C)
        RM_IDLE: begin
            rx_meta_src.ready = 1'b1;
            if (rx_meta_src.valid) rxm_vfid_N = rx_meta_src.data;
        end
        RM_ROUTE: begin
            rx_meta_valid[rxm_vfid_C] = s_tcp_rx_meta_net.valid;
            rx_meta_data[rxm_vfid_C] = s_tcp_rx_meta_net.data;
            s_tcp_rx_meta_net.ready = rx_meta_ready[rxm_vfid_C];
        end
    endcase
end

// vfid_rx output: current vfid for RX data routing (from rx_seq queue)
// This is used by vIO Switch to route incoming TCP data to correct vFIU
typedef enum logic { RX_IDLE, RX_ACTIVE } rx_data_state_t;
rx_data_state_t rx_data_state_C, rx_data_state_N;
logic [VF_BITS-1:0] rxd_vfid_C, rxd_vfid_N;

always_ff @(posedge aclk) begin
    if (!aresetn) begin
        rx_data_state_C <= RX_IDLE;
        rxd_vfid_C <= '0;
    end else begin
        rx_data_state_C <= rx_data_state_N;
        rxd_vfid_C <= rxd_vfid_N;
    end
end

// Note: The actual data routing happens in vIO Switch based on vfid_rx
// This FSM tracks the expected destination for each RX packet
always_comb begin
    rx_data_state_N = rx_data_state_C;
    rx_seq_src.ready = 1'b0;
    rxd_vfid_N = rxd_vfid_C;

    case (rx_data_state_C)
        RX_IDLE: begin
            rx_seq_src.ready = 1'b1;
            if (rx_seq_src.valid) begin
                rxd_vfid_N = rx_seq_src.data;
                rx_data_state_N = RX_ACTIVE;
            end
        end
        RX_ACTIVE: begin
            // Stay active until packet completes (tracked externally or via tlast)
            // For simplicity, we provide vfid_rx continuously
            // The vIO Switch uses this to set tdest for routing
            rx_data_state_N = RX_IDLE; // Simplified: assume single-beat or external tracking
        end
    endcase
end

assign vfid_rx = rxd_vfid_C;

// ============================================================================
// TX Metadata Path: tx_meta arbitration + tx_stat demux
// ============================================================================
// tx_meta: vFPGA sends metadata -> mux to network, track vfid + lookup route_id
// tx_stat: network sends status -> demux to correct vFPGA

logic [VF_BITS-1:0] tx_vfid_pick;
metaIntf #(.STYPE(tcp_tx_meta_t)) tx_meta_arb ();

meta_arbiter #(.DATA_BITS($bits(tcp_tx_meta_t))) inst_tx_meta_arbiter (
    .aclk    (aclk),
    .aresetn (aresetn),
    .s_meta  (s_tcp_tx_meta_user),
    .m_meta  (tx_meta_arb),
    .id_out  (tx_vfid_pick)
);

// Queues for TX vfid tracking (for tx_stat routing) and SID (for route_id lookup)
metaIntf #(.STYPE(logic[VF_BITS-1:0])) tx_vfid_snk (), tx_vfid_src ();
metaIntf #(.STYPE(logic[TCP_SESSION_BITS-1:0])) tx_sid_snk (), tx_sid_src ();

// Forward tx_meta to network while queuing vfid and SID
always_comb begin
    m_tcp_tx_meta_net.valid = 1'b0;
    m_tcp_tx_meta_net.data  = tx_meta_arb.data;
    tx_meta_arb.ready       = 1'b0;
    tx_vfid_snk.valid       = 1'b0;
    tx_vfid_snk.data        = tx_vfid_pick;
    tx_sid_snk.valid        = 1'b0;
    tx_sid_snk.data         = tx_meta_arb.data.sid;

    if (tx_meta_arb.valid && m_tcp_tx_meta_net.ready && tx_vfid_snk.ready && tx_sid_snk.ready) begin
        m_tcp_tx_meta_net.valid = 1'b1;
        tx_meta_arb.ready       = 1'b1;
        tx_vfid_snk.valid       = 1'b1;
        tx_sid_snk.valid        = 1'b1;
    end
end

queue #(.QTYPE(logic[VF_BITS-1:0]), .QDEPTH(32)) inst_tx_vfid_q (
    .aclk     (aclk),
    .aresetn  (aresetn),
    .val_snk  (tx_vfid_snk.valid),
    .rdy_snk  (tx_vfid_snk.ready),
    .data_snk (tx_vfid_snk.data),
    .val_src  (tx_vfid_src.valid),
    .rdy_src  (tx_vfid_src.ready),
    .data_src (tx_vfid_src.data)
);

queue #(.QTYPE(logic[TCP_SESSION_BITS-1:0]), .QDEPTH(32)) inst_tx_sid_q (
    .aclk     (aclk),
    .aresetn  (aresetn),
    .val_snk  (tx_sid_snk.valid),
    .rdy_snk  (tx_sid_snk.ready),
    .data_snk (tx_sid_snk.data),
    .val_src  (tx_sid_src.valid),
    .rdy_src  (tx_sid_src.ready),
    .data_src (tx_sid_src.data)
);

// TX stat demux to correct vFPGA
logic [N_REGIONS-1:0] tx_stat_ready;
logic [N_REGIONS-1:0] tx_stat_valid;
logic [$bits(tcp_tx_stat_t)-1:0] tx_stat_data [N_REGIONS];

for (genvar i = 0; i < N_REGIONS; i++) begin : gen_tx_stat_assign
    assign tx_stat_ready[i] = m_tcp_tx_stat_user[i].ready;
    assign m_tcp_tx_stat_user[i].valid = tx_stat_valid[i];
    assign m_tcp_tx_stat_user[i].data = tx_stat_data[i];
end

// TX stat routing FSM
typedef enum logic [1:0] { TS_IDLE, TS_ROUTE, TS_WAIT } tx_stat_state_t;
tx_stat_state_t tx_stat_state_C, tx_stat_state_N;
logic [VF_BITS-1:0] txs_vfid_C, txs_vfid_N;
logic [$bits(tcp_tx_stat_t)-1:0] tx_stat_buf_C, tx_stat_buf_N;

always_ff @(posedge aclk) begin
    if (!aresetn) begin
        tx_stat_state_C <= TS_IDLE;
        txs_vfid_C <= '0;
        tx_stat_buf_C <= '0;
    end else begin
        tx_stat_state_C <= tx_stat_state_N;
        txs_vfid_C <= txs_vfid_N;
        tx_stat_buf_C <= tx_stat_buf_N;
    end
end

always_comb begin
    tx_stat_state_N = tx_stat_state_C;
    case (tx_stat_state_C)
        TS_IDLE: if (tx_vfid_src.valid) tx_stat_state_N = TS_ROUTE;
        TS_ROUTE: if (s_tcp_tx_stat_net.valid) begin
            tx_stat_state_N = tx_stat_ready[txs_vfid_C] ? TS_IDLE : TS_WAIT;
        end
        TS_WAIT: if (tx_stat_ready[txs_vfid_C]) tx_stat_state_N = TS_IDLE;
    endcase
end

always_comb begin
    tx_vfid_src.ready = 1'b0;
    s_tcp_tx_stat_net.ready = 1'b0;
    for (int i = 0; i < N_REGIONS; i++) begin
        tx_stat_valid[i] = 1'b0;
        tx_stat_data[i] = '0;
    end
    txs_vfid_N = txs_vfid_C;
    tx_stat_buf_N = tx_stat_buf_C;

    case (tx_stat_state_C)
        TS_IDLE: begin
            tx_vfid_src.ready = 1'b1;
            if (tx_vfid_src.valid) txs_vfid_N = tx_vfid_src.data;
        end
        TS_ROUTE: begin
            if (s_tcp_tx_stat_net.valid) begin
                if (tx_stat_ready[txs_vfid_C]) begin
                    tx_stat_valid[txs_vfid_C] = 1'b1;
                    tx_stat_data[txs_vfid_C] = s_tcp_tx_stat_net.data;
                    s_tcp_tx_stat_net.ready = 1'b1;
                end else begin
                    tx_stat_buf_N = s_tcp_tx_stat_net.data;
                    s_tcp_tx_stat_net.ready = 1'b1;
                end
            end
        end
        TS_WAIT: begin
            tx_stat_valid[txs_vfid_C] = 1'b1;
            tx_stat_data[txs_vfid_C] = tx_stat_buf_C;
        end
    endcase
end

// TX data vfid and route_id lookup
// vfid_tx: current vfid for TX data (from vFIU to TCP)
// This FSM tracks which vFPGA is sending data and looks up route_id
typedef enum logic [1:0] { TX_IDLE, TX_ROUTE_WAIT, TX_ACTIVE } tx_data_state_t;
tx_data_state_t tx_data_state_C, tx_data_state_N;
logic [VF_BITS-1:0] txd_vfid_C, txd_vfid_N;
logic [13:0] txd_route_id_C, txd_route_id_N;

always_ff @(posedge aclk) begin
    if (!aresetn) begin
        tx_data_state_C <= TX_IDLE;
        txd_vfid_C <= '0;
        txd_route_id_C <= '0;
    end else begin
        tx_data_state_C <= tx_data_state_N;
        txd_vfid_C <= txd_vfid_N;
        txd_route_id_C <= txd_route_id_N;
    end
end

// Route_id lookup interface to conn_table
assign tx_sid = tx_sid_src.data;
assign tx_sid_valid = (tx_data_state_C == TX_ROUTE_WAIT);

always_comb begin
    tx_data_state_N = tx_data_state_C;
    tx_sid_src.ready = 1'b0;
    txd_vfid_N = txd_vfid_C;
    txd_route_id_N = txd_route_id_C;

    case (tx_data_state_C)
        TX_IDLE: begin
            tx_sid_src.ready = 1'b1;
            if (tx_sid_src.valid) begin
                tx_data_state_N = TX_ROUTE_WAIT;
            end
        end
        TX_ROUTE_WAIT: begin
            // Wait for route_id lookup from conn_table
            if (tx_route_id_valid) begin
                txd_route_id_N = tx_route_id;
                tx_data_state_N = TX_ACTIVE;
            end
        end
        TX_ACTIVE: begin
            // Stay active until packet completes
            // Simplified: return to idle immediately
            tx_data_state_N = TX_IDLE;
        end
    endcase
end

assign vfid_tx = txd_vfid_C;

`else // Single region

// Single region: direct passthrough
`META_ASSIGN(s_tcp_listen_req_user[0], m_tcp_listen_req_net)
`META_ASSIGN(s_tcp_listen_rsp_net, m_tcp_listen_rsp_user[0])
`META_ASSIGN(s_tcp_open_req_user[0], m_tcp_open_req_net)
`META_ASSIGN(s_tcp_close_req_user[0], m_tcp_close_req_net)
`META_ASSIGN(s_tcp_open_rsp_net, m_tcp_open_rsp_user[0])
`META_ASSIGN(s_tcp_notify_net, m_tcp_notify_user[0])
`META_ASSIGN(s_tcp_rd_pkg_user[0], m_tcp_rd_pkg_net)
`META_ASSIGN(s_tcp_rx_meta_net, m_tcp_rx_meta_user[0])
`META_ASSIGN(s_tcp_tx_meta_user[0], m_tcp_tx_meta_net)
`META_ASSIGN(s_tcp_tx_stat_net, m_tcp_tx_stat_user[0])

assign vfid_rx = '0;
assign vfid_tx = '0;

`endif

// ============================================================================
// Route ID Encoding
// ============================================================================
// Encode full route_id with sender_id for vIO Switch demux at vFIU
// Route format: [13:10]=reserved, [9:6]=sender_id, [5:2]=receiver_id, [1:0]=flags

// RX route: sender = PORT_TCP, receiver = vfid (destination vFPGA)
assign route_id_rx = {4'b0, PORT_TCP, vfid_rx[3:0], 2'b0};

// TX route: sender = vfid (source vFPGA), receiver = PORT_TCP (destination is TCP stack)
assign route_id_tx = {4'b0, vfid_tx[3:0], PORT_TCP, 2'b0};

endmodule

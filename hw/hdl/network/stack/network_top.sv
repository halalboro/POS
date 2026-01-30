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

/**
 * @brief   Network top
 *
 * Top level network stack
 * 
 *  @param CROSS_EARLY      Crossing early 322 -> nclk
 *  @param CROSS_LATE       Crossing late nclk -> aclk
 */
module network_top #(
    parameter integer CROSS_EARLY = 0,
    parameter integer CROSS_LATE = 1
) (
    // Network physical
    input  wire                 sys_reset,  
    input  wire                 init_clk,             
    input  wire                 gt_refclk_p,
    input  wire                 gt_refclk_n,

    input  wire [3:0]           gt_rxp_in,         
    input  wire [3:0]           gt_rxn_in,            
    output wire [3:0]           gt_txp_out,
    output wire [3:0]           gt_txn_out,

    // Init
    metaIntf.s                  s_arp_lookup_request,
    metaIntf.s                  s_set_ip_addr,
    metaIntf.s                  s_set_mac_addr,
`ifdef EN_STATS
    output net_stat_t           m_net_stats,
`endif

`ifdef EN_RDMA
    metaIntf.s                  s_rdma_qp_interface,
    metaIntf.s                  s_rdma_conn_interface,

    // Commands
    metaIntf.s                  s_rdma_sq,
    metaIntf.m                  m_rdma_ack,

    // RDMA ctrl + data
    metaIntf.m                  m_rdma_rd_req,
    metaIntf.m                  m_rdma_wr_req,
    AXI4S.s                     s_axis_rdma_rd_req,
    AXI4S.s                     s_axis_rdma_rd_rsp,
    AXI4S.m                     m_axis_rdma_wr,

    // RDMA memory
    input logic [63:0]          s_ddr_offset_addr_rdma,
    AXI4.m                      m_axi_rdma_ddr,                   
`endif

`ifdef EN_TCP
    // TCP interface
    metaIntf.s                  s_tcp_listen_req,
    metaIntf.m                  m_tcp_listen_rsp,   
    metaIntf.s                  s_tcp_open_req,
    metaIntf.m                  m_tcp_open_rsp,
    metaIntf.s                  s_tcp_close_req,
    metaIntf.m                  m_tcp_notify,
    metaIntf.s                  s_tcp_rd_pkg,
    metaIntf.m                  m_tcp_rx_meta,
    metaIntf.s                  s_tcp_tx_meta,
    metaIntf.m                  m_tcp_tx_stat,
    AXI4S.s                     s_axis_tcp_tx,
    AXI4S.m                     m_axis_tcp_rx,  

    // TCP memory
    input logic [63:0]          s_ddr_offset_addr_tcp,
    AXI4.m                      m_axi_tcp_ddr,

    // TCP route_id for VIU (POS) - from tcp_arbiter connection table
    input  logic [13:0]         tcp_tx_route_id,
    input  logic                tcp_tx_route_id_valid,

`endif

`ifdef EN_SNIFFER
    AXI4S.m                     m_rx_sniffer,
    AXI4S.m                     m_tx_sniffer,
    metaIntf.s                  s_filter_config,
`endif

    // VIU control (VLAN routing capability)
    input  logic [13:0]         vlan_ctrl,

    // TX path: route_id from vIO Switch (via tdest) for VLAN tag encoding
    // Format: [9:6] sender_id, [5:2] receiver_id, [1:0] flags
    input  logic [13:0]         viu_tx_tdest,

    // Synchronized route_id on RX path (travels with packet data via tdest)
    // This is the primary signal for vIO Switch routing - same approach as vFIU
    output logic [13:0]         viu_rx_tdest,

    // Clocks
    input  wire                 aclk,
    input  wire                 aresetn,
    input  wire                 nclk,
    input  wire                 nresetn
);

/**
 * Raw CMAC clock - 322 MHz
 */
logic r_resetn;
logic r_clk;

/**
 * Stack clock
 */
logic n_resetn;
logic n_clk;

if(CROSS_EARLY == 1) begin
    assign n_clk = nclk;
    assign n_resetn = nresetn;
end
else begin
    assign n_clk = r_clk;
    assign n_resetn = r_resetn;
end

/**
 * Network module
 */
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_r_clk_rx_data (.aclk(r_clk), .aresetn(r_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_r_clk_tx_data (.aclk(r_clk), .aresetn(r_resetn));

network_module #(
    .N_STGS(N_REG_NET_S2)
) inst_network_module (
    .init_clk (init_clk),
    .sys_reset (sys_reset),
    .rclk(r_clk),
    .rresetn(r_resetn),

    .gt_refclk_p(gt_refclk_p),
    .gt_refclk_n(gt_refclk_n),

    .gt_rxp_in(gt_rxp_in),
    .gt_rxn_in(gt_rxn_in),
    .gt_txp_out(gt_txp_out),
    .gt_txn_out(gt_txn_out),

    //master 0
    .m_axis_net_rx(axis_r_clk_rx_data),
    .s_axis_net_tx(axis_r_clk_tx_data)
);

/**
 * Cross early
 */
// RX path uses AXI4SR to carry route_id (tdest) from VIU through network stack to vIO Switch
AXI4SR #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axisr_n_clk_rx_data (.aclk(n_clk), .aresetn(n_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_n_clk_tx_data (.aclk(n_clk), .aresetn(n_resetn));

// VIU interface signals - declared before use in clock crossing
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_viu_rx_data (.aclk(n_clk), .aresetn(n_resetn));  // From clock crossing (VLAN tagged)
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_viu_tx_data (.aclk(n_clk), .aresetn(n_resetn));  // To clock crossing (VLAN tagged)

network_ccross_early #(
    .ENABLED(CROSS_EARLY),
    .N_STGS(N_REG_NET_S1)
) inst_early_ccross (
    .rclk(r_clk),
    .rresetn(r_resetn),
    .nclk(n_clk),
    .nresetn(n_resetn),
    .s_axis_rclk(axis_r_clk_rx_data),
    .m_axis_rclk(axis_r_clk_tx_data),
    .s_axis_nclk(axis_viu_tx_data),     // VIU TX -> clock crossing -> CMAC
    .m_axis_nclk(axis_viu_rx_data)      // CMAC -> clock crossing -> VIU RX
);

/**
 * VLAN Isolation Unit (VIU)
 * Sits between clock crossing and network stack for VLAN-based routing validation.
 * TX path: Network Stack -> VIU (insert VLAN tag) -> CMAC
 * RX path: CMAC -> VIU (extract VLAN tag, validate) -> Network Stack
 */

// TX path route_id selection:
// - When RDMA generates TX packets, use the route_id from RDMA connection table
// - When TCP generates TX packets, use the route_id from TCP connection table
// - Otherwise, use the externally provided viu_tx_tdest
// Route_ids are looked up when commands are processed and held until the next command.
logic [13:0] viu_tx_tdest_mux;

`ifdef EN_RDMA
logic [13:0] rdma_tx_route_id_held;

// Hold the RDMA route_id - it's available when SQ command is processed
// but packet data arrives later through the network stack
always_ff @(posedge n_clk) begin
    if (~n_resetn) begin
        rdma_tx_route_id_held <= '0;
    end else begin
        if (rdma_tx_route_id_valid_n_clk) begin
            rdma_tx_route_id_held <= rdma_tx_route_id_n_clk;
        end
    end
end
`endif

`ifdef EN_TCP
logic [13:0] tcp_tx_route_id_held;

// Hold the TCP route_id - it's available when TX meta is processed
// but packet data arrives later
always_ff @(posedge n_clk) begin
    if (~n_resetn) begin
        tcp_tx_route_id_held <= '0;
    end else begin
        if (tcp_tx_route_id_valid) begin
            tcp_tx_route_id_held <= tcp_tx_route_id;
        end
    end
end
`endif

// Priority mux for route_id selection:
// 1. RDMA route_id (when RDMA is enabled and has valid route)
// 2. TCP route_id (when TCP is enabled and has valid route)
// 3. External viu_tx_tdest (fallback)
`ifdef EN_RDMA
    `ifdef EN_TCP
        // Both RDMA and TCP enabled - RDMA takes priority, then TCP
        assign viu_tx_tdest_mux = (rdma_tx_route_id_held != '0) ? rdma_tx_route_id_held :
                                  (tcp_tx_route_id_held != '0) ? tcp_tx_route_id_held :
                                  viu_tx_tdest;
    `else
        // Only RDMA enabled
        assign viu_tx_tdest_mux = (rdma_tx_route_id_held != '0) ? rdma_tx_route_id_held : viu_tx_tdest;
    `endif
`else
    `ifdef EN_TCP
        // Only TCP enabled
        assign viu_tx_tdest_mux = (tcp_tx_route_id_held != '0) ? tcp_tx_route_id_held : viu_tx_tdest;
    `else
        // Neither RDMA nor TCP enabled
        assign viu_tx_tdest_mux = viu_tx_tdest;
    `endif
`endif

viu_top #(
    .N_ID(N_REGIONS)
) inst_viu (
    .aclk(n_clk),
    .aresetn(n_resetn),

    // Routing capability from host
    .route_ctrl(vlan_ctrl),

    // TX path: untagged from network stack -> VLAN tagged to CMAC
    // s_axis_tx_tdest carries route_id from RDMA connection table or external source
    .s_axis_tx_tdata(axis_n_clk_tx_data.tdata),
    .s_axis_tx_tkeep(axis_n_clk_tx_data.tkeep),
    .s_axis_tx_tlast(axis_n_clk_tx_data.tlast),
    .s_axis_tx_tready(axis_n_clk_tx_data.tready),
    .s_axis_tx_tvalid(axis_n_clk_tx_data.tvalid),
    .s_axis_tx_tdest(viu_tx_tdest_mux),  // Route ID from RDMA connection or external

    .m_axis_tx_tdata(axis_viu_tx_data.tdata),
    .m_axis_tx_tkeep(axis_viu_tx_data.tkeep),
    .m_axis_tx_tlast(axis_viu_tx_data.tlast),
    .m_axis_tx_tready(axis_viu_tx_data.tready),
    .m_axis_tx_tvalid(axis_viu_tx_data.tvalid),

    // RX path: VLAN tagged from CMAC -> untagged to network stack
    // VIU extracts route_id from VLAN tag and outputs it as tdest
    // This flows through network stack as AXI4SR to vIO Switch
    .s_axis_rx_tdata(axis_viu_rx_data.tdata),
    .s_axis_rx_tkeep(axis_viu_rx_data.tkeep),
    .s_axis_rx_tlast(axis_viu_rx_data.tlast),
    .s_axis_rx_tready(axis_viu_rx_data.tready),
    .s_axis_rx_tvalid(axis_viu_rx_data.tvalid),

    .m_axis_rx_tdata(axisr_n_clk_rx_data.tdata),
    .m_axis_rx_tkeep(axisr_n_clk_rx_data.tkeep),
    .m_axis_rx_tlast(axisr_n_clk_rx_data.tlast),
    .m_axis_rx_tready(axisr_n_clk_rx_data.tready),
    .m_axis_rx_tvalid(axisr_n_clk_rx_data.tvalid),
    .m_axis_rx_tdest(axisr_n_clk_rx_data.tdest)  // Route_id flows as tdest through network stack
);

// tid is not used for RX path (set to 0)
assign axisr_n_clk_rx_data.tid = '0;

// Assign viu_rx_tdest output - route_id extracted from VLAN tag for vIO Switch routing
// This is the synchronized route_id that travels with packet data through the network stack
assign viu_rx_tdest = axisr_n_clk_rx_data.tdest;

/**
 * Network stack
 */

// Network
metaIntf #(.STYPE(logic[ARP_LUP_REQ_BITS-1:0])) arp_lookup_request_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(logic[IP_ADDR_BITS-1:0])) set_ip_addr_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(logic[MAC_ADDR_BITS-1:0])) set_mac_addr_n_clk (.aclk(n_clk), .aresetn(n_resetn));
net_stat_t net_stats_n_clk;

metaIntf #(.STYPE(logic[ARP_LUP_REQ_BITS-1:0])) arp_lookup_request_aclk_slice (.*);
metaIntf #(.STYPE(logic[IP_ADDR_BITS-1:0])) set_ip_addr_aclk_slice (.*);
metaIntf #(.STYPE(logic[MAC_ADDR_BITS-1:0])) set_mac_addr_aclk_slice (.*);
net_stat_t net_stats_aclk_slice;

// RDMA
metaIntf #(.STYPE(rdma_qp_ctx_t)) rdma_qp_interface_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(rdma_qp_conn_t)) rdma_conn_interface_n_clk (.aclk(n_clk), .aresetn(n_resetn));

metaIntf #(.STYPE(rdma_qp_ctx_t)) rdma_qp_interface_aclk_slice (.*);
metaIntf #(.STYPE(rdma_qp_conn_t)) rdma_conn_interface_aclk_slice (.*);

metaIntf #(.STYPE(dreq_t)) rdma_sq_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(ack_t)) rdma_ack_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(req_t)) rdma_rd_req_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(req_t)) rdma_wr_req_n_clk (.aclk(n_clk), .aresetn(n_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_rdma_rd_req_n_clk (.aclk(n_clk), .aresetn(n_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_rdma_rd_rsp_n_clk (.aclk(n_clk), .aresetn(n_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_rdma_wr_n_clk (.aclk(n_clk), .aresetn(n_resetn));

metaIntf #(.STYPE(dreq_t)) rdma_sq_aclk (.*);
metaIntf #(.STYPE(ack_t)) rdma_ack_aclk (.*);
metaIntf #(.STYPE(req_t)) rdma_rd_req_aclk (.*);
metaIntf #(.STYPE(req_t)) rdma_wr_req_aclk (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_rdma_rd_req_aclk (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_rdma_rd_rsp_aclk (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_rdma_wr_aclk (.*);

// RDMA memory
metaIntf #(.STYPE(logic[MEM_CMD_BITS-1:0])) rdma_mem_rd_cmd_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(logic[MEM_CMD_BITS-1:0])) rdma_mem_wr_cmd_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(logic[MEM_STS_BITS-1:0])) rdma_mem_rd_sts_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(logic[MEM_STS_BITS-1:0])) rdma_mem_wr_sts_n_clk (.aclk(n_clk), .aresetn(n_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_DDR_BITS)) axis_rdma_mem_rd_n_clk (.aclk(n_clk), .aresetn(n_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_DDR_BITS)) axis_rdma_mem_wr_n_clk (.aclk(n_clk), .aresetn(n_resetn));

metaIntf #(.STYPE(logic[MEM_CMD_BITS-1:0])) rdma_mem_rd_cmd_aclk (.*);
metaIntf #(.STYPE(logic[MEM_CMD_BITS-1:0])) rdma_mem_wr_cmd_aclk (.*);
metaIntf #(.STYPE(logic[MEM_STS_BITS-1:0])) rdma_mem_rd_sts_aclk (.*);
metaIntf #(.STYPE(logic[MEM_STS_BITS-1:0])) rdma_mem_wr_sts_aclk (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_DDR_BITS)) axis_rdma_mem_rd_aclk (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_DDR_BITS)) axis_rdma_mem_wr_aclk (.*);

logic [N_REG_NET_S0:0][63:0] ddr_offset_addr_rdma;
AXI4 axi_rdma_ddr_slice ();

// RDMA route_id for VIU (POS)
logic [13:0] rdma_tx_route_id_n_clk;
logic        rdma_tx_route_id_valid_n_clk;

// TCP/IP
metaIntf #(.STYPE(tcp_listen_req_t)) tcp_listen_req_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(tcp_listen_rsp_t)) tcp_listen_rsp_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(tcp_open_req_t)) tcp_open_req_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(tcp_open_rsp_t)) tcp_open_rsp_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(tcp_close_req_t)) tcp_close_req_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(tcp_notify_t)) tcp_notify_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(tcp_rd_pkg_t)) tcp_rd_pkg_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(tcp_rx_meta_t)) tcp_rx_meta_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(tcp_tx_meta_t)) tcp_tx_meta_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(tcp_tx_stat_t)) tcp_tx_stat_n_clk (.aclk(n_clk), .aresetn(n_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_tcp_rx_n_clk (.aclk(n_clk), .aresetn(n_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_tcp_tx_n_clk (.aclk(n_clk), .aresetn(n_resetn));

metaIntf #(.STYPE(tcp_listen_req_t)) tcp_listen_req_aclk (.*);
metaIntf #(.STYPE(tcp_listen_rsp_t)) tcp_listen_rsp_aclk (.*);
metaIntf #(.STYPE(tcp_open_req_t)) tcp_open_req_aclk (.*);
metaIntf #(.STYPE(tcp_open_rsp_t)) tcp_open_rsp_aclk (.*);
metaIntf #(.STYPE(tcp_close_req_t)) tcp_close_req_aclk (.*);
metaIntf #(.STYPE(tcp_notify_t)) tcp_notify_aclk (.*);
metaIntf #(.STYPE(tcp_rd_pkg_t)) tcp_rd_pkg_aclk (.*);
metaIntf #(.STYPE(tcp_rx_meta_t)) tcp_rx_meta_aclk (.*);
metaIntf #(.STYPE(tcp_tx_meta_t)) tcp_tx_meta_aclk (.*);
metaIntf #(.STYPE(tcp_tx_stat_t)) tcp_tx_stat_aclk (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_tcp_rx_aclk (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_tcp_tx_aclk (.*);

// TCP/IP memory
metaIntf #(.STYPE(logic[MEM_CMD_BITS-1:0])) tcp_mem_rd_cmd_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(logic[MEM_CMD_BITS-1:0])) tcp_mem_wr_cmd_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(logic[MEM_STS_BITS-1:0])) tcp_mem_rd_sts_n_clk (.aclk(n_clk), .aresetn(n_resetn));
metaIntf #(.STYPE(logic[MEM_STS_BITS-1:0])) tcp_mem_wr_sts_n_clk (.aclk(n_clk), .aresetn(n_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_DDR_BITS)) axis_tcp_mem_rd_n_clk (.aclk(n_clk), .aresetn(n_resetn));
AXI4S #(.AXI4S_DATA_BITS(AXI_DDR_BITS)) axis_tcp_mem_wr_n_clk (.aclk(n_clk), .aresetn(n_resetn));

metaIntf #(.STYPE(logic[MEM_CMD_BITS-1:0])) tcp_mem_rd_cmd_aclk (.*);
metaIntf #(.STYPE(logic[MEM_CMD_BITS-1:0])) tcp_mem_wr_cmd_aclk (.*);
metaIntf #(.STYPE(logic[MEM_STS_BITS-1:0])) tcp_mem_rd_sts_aclk (.*);
metaIntf #(.STYPE(logic[MEM_STS_BITS-1:0])) tcp_mem_wr_sts_aclk (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_DDR_BITS)) axis_tcp_mem_rd_aclk (.*);
AXI4S #(.AXI4S_DATA_BITS(AXI_DDR_BITS)) axis_tcp_mem_wr_aclk (.*);

logic [N_REG_NET_S0:0][63:0] ddr_offset_addr_tcp;
AXI4 axi_tcp_ddr_slice ();

/**
 * Network stack
 */
network_stack inst_network_stack (
    .s_axis_net(axisr_n_clk_rx_data),  // AXI4SR with route_id in tdest
    .m_axis_net(axis_n_clk_tx_data),

    .s_arp_lookup_request(arp_lookup_request_n_clk),
    .s_set_ip_addr(set_ip_addr_n_clk),
    .s_set_mac_addr(set_mac_addr_n_clk),
`ifdef EN_STATS
    .m_net_stats(net_stats_n_clk),
`endif

`ifdef EN_RDMA
    .s_rdma_qp_interface(rdma_qp_interface_n_clk),
    .s_rdma_conn_interface(rdma_conn_interface_n_clk),

    .s_rdma_sq(rdma_sq_n_clk),
    .m_rdma_ack(rdma_ack_n_clk),
    .m_rdma_rd_req(rdma_rd_req_n_clk),
    .m_rdma_wr_req(rdma_wr_req_n_clk),
    .s_axis_rdma_rd_req(axis_rdma_rd_req_n_clk),
    .s_axis_rdma_rd_rsp(axis_rdma_rd_rsp_n_clk),
    .m_axis_rdma_wr(axis_rdma_wr_n_clk),

    .m_rdma_mem_rd_cmd(rdma_mem_rd_cmd_n_clk),
    .m_rdma_mem_wr_cmd(rdma_mem_wr_cmd_n_clk),
    .s_rdma_mem_rd_sts(rdma_mem_rd_sts_n_clk),
    .s_rdma_mem_wr_sts(rdma_mem_wr_sts_n_clk),
    .s_axis_rdma_mem_rd(axis_rdma_mem_rd_n_clk),
    .m_axis_rdma_mem_wr(axis_rdma_mem_wr_n_clk),

    // RDMA route_id for VIU (POS)
    .rdma_tx_route_id(rdma_tx_route_id_n_clk),
    .rdma_tx_route_id_valid(rdma_tx_route_id_valid_n_clk),
`endif

`ifdef EN_TCP
    .s_tcp_listen_req(tcp_listen_req_n_clk),
    .m_tcp_listen_rsp(tcp_listen_rsp_n_clk),
    .s_tcp_open_req(tcp_open_req_n_clk),
    .m_tcp_open_rsp(tcp_open_rsp_n_clk),
    .s_tcp_close_req(tcp_close_req_n_clk),
    .m_tcp_notify(tcp_notify_n_clk),
    .s_tcp_rd_pkg(tcp_rd_pkg_n_clk),
    .m_tcp_rx_meta(tcp_rx_meta_n_clk),
    .s_tcp_tx_meta(tcp_tx_meta_n_clk),
    .m_tcp_tx_stat(tcp_tx_stat_n_clk),
    .s_axis_tcp_tx(axis_tcp_tx_n_clk),
    .m_axis_tcp_rx(axis_tcp_rx_n_clk),

    .m_tcp_mem_rd_cmd(tcp_mem_rd_cmd_n_clk),
    .m_tcp_mem_wr_cmd(tcp_mem_wr_cmd_n_clk),
    .s_tcp_mem_rd_sts(tcp_mem_rd_sts_n_clk),
    .s_tcp_mem_wr_sts(tcp_mem_wr_sts_n_clk),
    .s_axis_tcp_mem_rd(axis_tcp_mem_rd_n_clk),
    .m_axis_tcp_mem_wr(axis_tcp_mem_wr_n_clk),
`endif

`ifdef EN_SNIFFER
    .m_rx_sniffer(m_rx_sniffer),
    .m_tx_sniffer(m_tx_sniffer),
    .s_filter_config(s_filter_config),
`endif

    .nclk(n_clk),
    .nresetn(n_resetn)
);

/**
 * Config 
 */
network_ccross_late #(
    .ENABLED(CROSS_LATE)
) inst_network_ccross_late (
    // Network
    .m_arp_lookup_request_nclk(arp_lookup_request_n_clk),
    .m_set_ip_addr_nclk(set_ip_addr_n_clk),
    .m_set_mac_addr_nclk(set_mac_addr_n_clk),
`ifdef EN_STATS
    .s_net_stats_nclk(net_stats_n_clk), 
`endif
    
    // User
    .s_arp_lookup_request_aclk(arp_lookup_request_aclk_slice),
    .s_set_ip_addr_aclk(set_ip_addr_aclk_slice),
    .s_set_mac_addr_aclk(set_mac_addr_aclk_slice),
`ifdef EN_STATS
    .m_net_stats_aclk(net_stats_aclk_slice),
`endif

    .nclk(n_clk),
    .nresetn(n_resetn),
    .aclk(aclk),
    .aresetn(aresetn)
);

// Slicing
network_slice_array #(
    .N_STAGES(N_REG_NET_S0)  
) inst_network_slice_array (
    // Network
    .m_arp_lookup_request_n(arp_lookup_request_aclk_slice),
    .m_set_ip_addr_n(set_ip_addr_aclk_slice),
    .m_set_mac_addr_n(set_mac_addr_aclk_slice),
`ifdef EN_STATS
    .s_net_stats_n(net_stats_aclk_slice),
`endif
    
    // User
    .s_arp_lookup_request_u(s_arp_lookup_request),
    .s_set_ip_addr_u(s_set_ip_addr),
    .s_set_mac_addr_u(s_set_mac_addr),
`ifdef EN_STATS
    .m_net_stats_u(m_net_stats),
`endif

    .aclk(aclk),
    .aresetn(aresetn)
);

/**
 * RDMA
 */
`ifdef EN_RDMA

    // RDMA late cross
    rdma_ccross_net_late #(
        .ENABLED(CROSS_LATE)
    ) inst_rdma_clk_cross_late (
        // Network
        .m_rdma_qp_interface_nclk(rdma_qp_interface_n_clk),
        .m_rdma_conn_interface_nclk(rdma_conn_interface_n_clk),

        .m_rdma_sq_nclk(rdma_sq_n_clk),
        .s_rdma_ack_nclk(rdma_ack_n_clk),
        .s_rdma_rd_req_nclk(rdma_rd_req_n_clk),
        .s_rdma_wr_req_nclk(rdma_wr_req_n_clk),
        .m_axis_rdma_rd_req_nclk(axis_rdma_rd_req_n_clk),
        .m_axis_rdma_rd_rsp_nclk(axis_rdma_rd_rsp_n_clk),
        .s_axis_rdma_wr_nclk(axis_rdma_wr_n_clk),

        .s_rdma_mem_rd_cmd_nclk(rdma_mem_rd_cmd_n_clk),
        .s_rdma_mem_wr_cmd_nclk(rdma_mem_wr_cmd_n_clk),
        .m_rdma_mem_rd_sts_nclk(rdma_mem_rd_sts_n_clk),
        .m_rdma_mem_wr_sts_nclk(rdma_mem_wr_sts_n_clk),
        .m_axis_rdma_mem_rd_nclk(axis_rdma_mem_rd_n_clk),
        .s_axis_rdma_mem_wr_nclk(axis_rdma_mem_wr_n_clk),
        
        // User
        .s_rdma_qp_interface_aclk(rdma_qp_interface_aclk_slice),
        .s_rdma_conn_interface_aclk(rdma_conn_interface_aclk_slice),

        .s_rdma_sq_aclk(rdma_sq_aclk),
        .m_rdma_ack_aclk(rdma_ack_aclk),
        .m_rdma_rd_req_aclk(rdma_rd_req_aclk),
        .m_rdma_wr_req_aclk(rdma_wr_req_aclk),
        .s_axis_rdma_rd_req_aclk(axis_rdma_rd_req_aclk),
        .s_axis_rdma_rd_rsp_aclk(axis_rdma_rd_rsp_aclk),
        .m_axis_rdma_wr_aclk(axis_rdma_wr_aclk),

        .m_rdma_mem_rd_cmd_aclk(rdma_mem_rd_cmd_aclk),
        .m_rdma_mem_wr_cmd_aclk(rdma_mem_wr_cmd_aclk),
        .s_rdma_mem_rd_sts_aclk(rdma_mem_rd_sts_aclk),
        .s_rdma_mem_wr_sts_aclk(rdma_mem_wr_sts_aclk),
        .s_axis_rdma_mem_rd_aclk(axis_rdma_mem_rd_aclk),
        .m_axis_rdma_mem_wr_aclk(axis_rdma_mem_wr_aclk),

        .nclk(n_clk),
        .nresetn(n_resetn),
        .aclk(aclk),
        .aresetn(aresetn)
    );

    // RDMA slicing
    rdma_slice_array_net #( 
        .N_STAGES(N_REG_NET_S0)
    ) inst_rdma_slice_array (
        // Network
        .m_rdma_qp_interface_n(rdma_qp_interface_aclk_slice),
        .m_rdma_conn_interface_n(rdma_conn_interface_aclk_slice),
        .m_rdma_sq_n(rdma_sq_aclk),
        .s_rdma_ack_n(rdma_ack_aclk),
        .s_rdma_rd_req_n(rdma_rd_req_aclk),
        .s_rdma_wr_req_n(rdma_wr_req_aclk),
        .m_axis_rdma_rd_req_n(axis_rdma_rd_req_aclk),
        .m_axis_rdma_rd_rsp_n(axis_rdma_rd_rsp_aclk),
        .s_axis_rdma_wr_n(axis_rdma_wr_aclk),

        // User
        .s_rdma_qp_interface_u(s_rdma_qp_interface),
        .s_rdma_conn_interface_u(s_rdma_conn_interface),
        .s_rdma_sq_u(s_rdma_sq),
        .m_rdma_ack_u(m_rdma_ack),
        .m_rdma_rd_req_u(m_rdma_rd_req),
        .m_rdma_wr_req_u(m_rdma_wr_req),
        .s_axis_rdma_rd_req_u(s_axis_rdma_rd_req),
        .s_axis_rdma_rd_rsp_u(s_axis_rdma_rd_rsp),
        .m_axis_rdma_wr_u(m_axis_rdma_wr),
        
        .aclk(aclk),
        .aresetn(aresetn)
    );

    // RDMA memory
    net_mem_intf #(
        .ENABLE(1),
        .UNALIGNED(1)
    ) inst_rdma_mem_intf_0 (
        .aclk(aclk),
        .aresetn(aresetn),
        .addr_offset(ddr_offset_addr_rdma[N_REG_NET_S0]),
        .s_mem_rd_cmd(rdma_mem_rd_cmd_aclk),
        .s_mem_wr_cmd(rdma_mem_wr_cmd_aclk),
        .m_mem_rd_sts(rdma_mem_rd_sts_aclk),
        .m_mem_wr_sts(rdma_mem_wr_sts_aclk),
        .m_axis_rd_data(axis_rdma_mem_rd_aclk),
        .s_axis_wr_data(axis_rdma_mem_wr_aclk),
        .m_axi_mem(axi_rdma_ddr_slice)
    );

    // Memory commands slicing
    assign ddr_offset_addr_rdma[0] = s_ddr_offset_addr_rdma;

    always_ff @( posedge  aclk ) begin
        if(~aresetn)
            for(int i = 0; i < N_REG_NET_S0; i++)
                ddr_offset_addr_rdma[i+1] <= 'X;
        else
            for(int i = 0; i < N_REG_NET_S0; i++)
                ddr_offset_addr_rdma[i+1] <= ddr_offset_addr_rdma[i];
    end    

    axi_reg_array #(.N_STAGES(N_REG_NET_S0)) inst_ddr_rdma_reg (.aclk(aclk), .aresetn(aresetn), .s_axi(axi_rdma_ddr_slice), .m_axi(m_axi_rdma_ddr));

`endif

/**
 * TCP/IP
 */
`ifdef EN_TCP

    tcp_ccross_late_net #(
        .ENABLED(CROSS_LATE)
    ) inst_tcp_ccross_late (
        // Network
        .m_tcp_listen_req_nclk(tcp_listen_req_n_clk),
        .s_tcp_listen_rsp_nclk(tcp_listen_rsp_n_clk),    
        .m_tcp_open_req_nclk(tcp_open_req_n_clk),
        .s_tcp_open_rsp_nclk(tcp_open_rsp_n_clk),
        .m_tcp_close_req_nclk(tcp_close_req_n_clk),
        .s_tcp_notify_nclk(tcp_notify_n_clk),
        .m_tcp_rd_pkg_nclk(tcp_rd_pkg_n_clk),
        .s_tcp_rx_meta_nclk(tcp_rx_meta_n_clk),
        .m_tcp_tx_meta_nclk(tcp_tx_meta_n_clk),
        .s_tcp_tx_stat_nclk(tcp_tx_stat_n_clk),
        .m_axis_tcp_tx_nclk(axis_tcp_tx_n_clk),
        .s_axis_tcp_rx_nclk(axis_tcp_rx_n_clk),

        .s_tcp_mem_rd_cmd_nclk(tcp_mem_rd_cmd_n_clk),
        .s_tcp_mem_wr_cmd_nclk(tcp_mem_wr_cmd_n_clk),
        .m_tcp_mem_rd_sts_nclk(tcp_mem_rd_sts_n_clk),
        .m_tcp_mem_wr_sts_nclk(tcp_mem_wr_sts_n_clk),
        .m_axis_tcp_mem_rd_nclk(axis_tcp_mem_rd_n_clk),
        .s_axis_tcp_mem_wr_nclk(axis_tcp_mem_wr_n_clk),
        
        
        // User
        .s_tcp_listen_req_aclk(tcp_listen_req_aclk),
        .m_tcp_listen_rsp_aclk(tcp_listen_rsp_aclk),   
        .s_tcp_open_req_aclk(tcp_open_req_aclk),
        .m_tcp_open_rsp_aclk(tcp_open_rsp_aclk),
        .s_tcp_close_req_aclk(tcp_close_req_aclk),      
        .m_tcp_notify_aclk(tcp_notify_aclk),
        .s_tcp_rd_pkg_aclk(tcp_rd_pkg_aclk),       
        .m_tcp_rx_meta_aclk(tcp_rx_meta_aclk),
        .s_tcp_tx_meta_aclk(tcp_tx_meta_aclk),
        .m_tcp_tx_stat_aclk(tcp_tx_stat_aclk),  
        .s_axis_tcp_tx_aclk(axis_tcp_tx_aclk),
        .m_axis_tcp_rx_aclk(axis_tcp_rx_aclk),

        .m_tcp_mem_rd_cmd_aclk(tcp_mem_rd_cmd_aclk),
        .m_tcp_mem_wr_cmd_aclk(tcp_mem_wr_cmd_aclk),
        .s_tcp_mem_rd_sts_aclk(tcp_mem_rd_sts_aclk),
        .s_tcp_mem_wr_sts_aclk(tcp_mem_wr_sts_aclk),
        .s_axis_tcp_mem_rd_aclk(axis_tcp_mem_rd_aclk),
        .m_axis_tcp_mem_wr_aclk(axis_tcp_mem_wr_aclk),

        .nclk(n_clk),
        .nresetn(n_resetn),
        .aclk(aclk),
        .aresetn(aresetn)
    );

    // TCP slicing
    tcp_slice_array_net #( 
        .N_STAGES(N_REG_NET_S0)
    ) inst_tcp_slice_array (
        // Network
        .m_tcp_listen_req_n(tcp_listen_req_aclk),
        .s_tcp_listen_rsp_n(tcp_listen_rsp_aclk),
        .m_tcp_open_req_n(tcp_open_req_aclk),
        .s_tcp_open_rsp_n(tcp_open_rsp_aclk),
        .m_tcp_close_req_n(tcp_close_req_aclk),
        .s_tcp_notify_n(tcp_notify_aclk),
        .m_tcp_rd_pkg_n(tcp_rd_pkg_aclk),
        .s_tcp_rx_meta_n(tcp_rx_meta_aclk),
        .m_tcp_tx_meta_n(tcp_tx_meta_aclk),
        .s_tcp_tx_stat_n(tcp_tx_stat_aclk),
        .m_axis_tcp_tx_n(axis_tcp_tx_aclk),
        .s_axis_tcp_rx_n(axis_tcp_rx_aclk),
        

        // User
        .s_tcp_listen_req_u(s_tcp_listen_req),
        .m_tcp_listen_rsp_u(m_tcp_listen_rsp),
        .s_tcp_open_req_u(s_tcp_open_req),
        .m_tcp_open_rsp_u(m_tcp_open_rsp),
        .s_tcp_close_req_u(s_tcp_close_req),
        .m_tcp_notify_u(m_tcp_notify),
        .s_tcp_rd_pkg_u(s_tcp_rd_pkg),
        .m_tcp_rx_meta_u(m_tcp_rx_meta),
        .s_tcp_tx_meta_u(s_tcp_tx_meta),
        .m_tcp_tx_stat_u(m_tcp_tx_stat),
        .s_axis_tcp_tx_u(s_axis_tcp_tx),
        .m_axis_tcp_rx_u(m_axis_tcp_rx),
        
        .aclk(aclk),
        .aresetn(aresetn)
    );

    // TCP memory
    net_mem_intf #(
        .ENABLE(1),
        .UNALIGNED(1)
    ) inst_tcp_mem_intf_0 (
        .aclk(aclk),
        .aresetn(aresetn),
        .addr_offset(ddr_offset_addr_tcp[N_REG_NET_S0]),
        .s_mem_rd_cmd(tcp_mem_rd_cmd_aclk),
        .s_mem_wr_cmd(tcp_mem_wr_cmd_aclk),
        .m_mem_rd_sts(tcp_mem_rd_sts_aclk),
        .m_mem_wr_sts(tcp_mem_wr_sts_aclk),
        .m_axis_rd_data(axis_tcp_mem_rd_aclk),
        .s_axis_wr_data(axis_tcp_mem_wr_aclk),
        .m_axi_mem(axi_tcp_ddr_slice)
    );

    // Memory commands slicing
    assign ddr_offset_addr_tcp[0] = s_ddr_offset_addr_tcp;

    always_ff @( posedge  aclk ) begin
        if(~aresetn)
            for(int i = 0; i < N_REG_NET_S0; i++)
                ddr_offset_addr_tcp[i+1] <= 'X;
        else
            for(int i = 0; i < N_REG_NET_S0; i++)
                ddr_offset_addr_tcp[i+1] <= ddr_offset_addr_tcp[i];
    end    

    axi_reg_array #(.N_STAGES(N_REG_NET_S0)) inst_ddr_tcp_reg (.aclk(aclk), .aresetn(aresetn), .s_axi(axi_tcp_ddr_slice), .m_axi(m_axi_tcp_ddr));

`endif


endmodule

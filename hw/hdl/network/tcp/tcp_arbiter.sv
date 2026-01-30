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
 * @brief   Top level TCP arbiter
 *
 * POS: Modified for vIO Switch integration.
 * - TX path outputs AXI4SR with route_id in tdest for vIO Switch routing
 * - RX path outputs AXI4SR with vfid in tdest for vIO Switch routing to vFIUs
 */
module tcp_arbiter (
    // Network (from/to TCP stack)
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
    AXI4S.s                 s_axis_tcp_rx_net,     // RX data from TCP stack
    AXI4SR.m                m_axis_tcp_tx_net,     // POS: TX data to vIO Switch (AXI4SR for routing)

    // vIO Switch interface (RX path)
    AXI4SR.m                m_axis_tcp_rx_switch,  // POS: RX data to vIO Switch (AXI4SR for routing)

    // User (per-region metadata - data goes through vIO Switch)
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
    AXI4S.s                 s_axis_tcp_tx_user [N_REGIONS],  // TX data from users

    // POS: Route ID sideband outputs
    output logic [13:0]     tcp_tx_route_id,
    output logic            tcp_tx_route_id_valid,
    output logic [13:0]     tcp_rx_route_id,       // POS: RX route_id for vFIU gateway

    input  wire             aclk,
    input  wire             aresetn
);



//
// Arbiters
//
`ifdef MULT_REGIONS

    logic [TCP_IP_PORT_BITS-1:0]            port_addr;
    logic [TCP_PORT_TABLE_DATA_BITS-1:0]    rsid;

    // POS: Route ID from port table to conn table
    logic [13:0]                            route_id;

    // POS: TX route_id lookup signals (from TX arbiter)
    logic [TCP_SESSION_BITS-1:0]            tx_sid;
    logic                                   tx_sid_valid;
    logic [13:0]                            tx_route_id;
    logic                                   tx_route_id_valid;

    // Listen on the port (table)
    tcp_port_table inst_port_table (
        .aclk         (aclk),
        .aresetn      (aresetn),
        .s_listen_req (s_tcp_listen_req_user),
        .m_listen_req (m_tcp_listen_req_net),
        .s_listen_rsp (s_tcp_listen_rsp_net),
        .m_listen_rsp (m_tcp_listen_rsp_user),
        .port_addr    (port_addr),
        .rsid_out     (rsid),
        .route_id_out (route_id)           // POS: route_id output
    );

    // Open/Close connections + notify routing
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

        // POS: Route ID support
        .route_id_in       (route_id),
        .tx_sid            (tx_sid),
        .tx_sid_valid      (tx_sid_valid),
        .tx_route_id       (tx_route_id),
        .tx_route_id_valid (tx_route_id_valid)
    );

    // POS: Internal RX route_id signal
    logic [13:0] rx_route_id_int;

    // RX data - POS: Now outputs AXI4SR to vIO Switch
    tcp_rx_arbiter inst_rx_arb (
        .aclk       (aclk),
        .aresetn    (aresetn),
        .s_rd_pkg   (s_tcp_rd_pkg_user),
        .m_rd_pkg   (m_tcp_rd_pkg_net),
        .s_rx_meta  (s_tcp_rx_meta_net),
        .m_rx_meta  (m_tcp_rx_meta_user),
        .s_axis_rx  (s_axis_tcp_rx_net),
        .m_axis_rx  (m_axis_tcp_rx_switch),  // POS: To vIO Switch
        .rx_route_id(rx_route_id_int)        // POS: Route ID for vFIU gateway
    );

    // TX data - POS: Now outputs AXI4SR to vIO Switch
    tcp_tx_arbiter inst_tx_arb (
        .aclk     (aclk),
        .aresetn  (aresetn),
        .s_tx_meta(s_tcp_tx_meta_user),
        .m_tx_meta(m_tcp_tx_meta_net),
        .s_tx_stat(s_tcp_tx_stat_net),
        .m_tx_stat(m_tcp_tx_stat_user),
        .s_axis_tx(s_axis_tcp_tx_user),
        .m_axis_tx(m_axis_tcp_tx_net),       // POS: To vIO Switch (AXI4SR)
        // POS: Route ID lookup interface
        .tx_sid            (tx_sid),
        .tx_sid_valid      (tx_sid_valid),
        .tx_route_id       (tx_route_id),
        .tx_route_id_valid (tx_route_id_valid)
    );

    // POS: Route ID sideband outputs
    assign tcp_tx_route_id       = tx_route_id;
    assign tcp_tx_route_id_valid = tx_route_id_valid;
    assign tcp_rx_route_id       = rx_route_id_int;

`else
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

    // POS: Single region - convert AXI4S to AXI4SR for vIO Switch
    // RX path: network -> vIO Switch -> vFIU
    assign m_axis_tcp_rx_switch.tvalid = s_axis_tcp_rx_net.tvalid;
    assign m_axis_tcp_rx_switch.tdata  = s_axis_tcp_rx_net.tdata;
    assign m_axis_tcp_rx_switch.tkeep  = s_axis_tcp_rx_net.tkeep;
    assign m_axis_tcp_rx_switch.tlast  = s_axis_tcp_rx_net.tlast;
    assign m_axis_tcp_rx_switch.tid    = '0;
    assign m_axis_tcp_rx_switch.tdest  = '0;  // Single region, goes to vFIU 0
    assign s_axis_tcp_rx_net.tready    = m_axis_tcp_rx_switch.tready;

    // TX path: user -> vIO Switch -> network
    assign m_axis_tcp_tx_net.tvalid = s_axis_tcp_tx_user[0].tvalid;
    assign m_axis_tcp_tx_net.tdata  = s_axis_tcp_tx_user[0].tdata;
    assign m_axis_tcp_tx_net.tkeep  = s_axis_tcp_tx_user[0].tkeep;
    assign m_axis_tcp_tx_net.tlast  = s_axis_tcp_tx_user[0].tlast;
    assign m_axis_tcp_tx_net.tid    = '0;
    assign m_axis_tcp_tx_net.tdest  = '0;  // Single region, sender=0
    assign s_axis_tcp_tx_user[0].tready = m_axis_tcp_tx_net.tready;

    // POS: Single region - no route_id lookup needed
    assign tcp_tx_route_id       = '0;
    assign tcp_tx_route_id_valid = 1'b0;
    assign tcp_rx_route_id       = '0;

`endif

endmodule
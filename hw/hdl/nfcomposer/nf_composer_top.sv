/**
 * Copyright (c) 2021, Systems Group, ETH Zurich
 * All rights reserved.
 *
 * NF Composer Top Module
 *
 * This module implements the POS NF Composer architecture (Figure 8):
 * - Instantiates one vFIU per vFPGA region
 * - Instantiates the vIO Switch for inter-vFPGA and I/O stack routing
 * - Wires vFIUs to vIO Switch (data_dtu_sink/src)
 * - Exposes I/O stack interfaces (data_host_sink/src) for RDMA/TCP connection
 */

`timescale 1ns / 1ps

import lynxTypes::*;

`include "axi_macros.svh"
`include "lynx_macros.svh"

module nf_composer_top #(
    parameter integer N_REGIONS = 4,
    parameter integer N_ENDPOINTS = 4
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // =========================================================================
    // vFPGA User Logic Interfaces (one per region)
    // These connect to user_wrapper/design_user_logic
    // =========================================================================

    // User descriptors (from user logic)
    input  logic [N_REGIONS-1:0]                user_rd_sq_valid,
    output logic [N_REGIONS-1:0]                user_rd_sq_ready,
    input  req_t                                user_rd_sq_data [N_REGIONS],
    input  logic [N_REGIONS-1:0]                user_wr_sq_valid,
    output logic [N_REGIONS-1:0]                user_wr_sq_ready,
    input  req_t                                user_wr_sq_data [N_REGIONS],

    // Host descriptors (from host/driver)
    input  logic [N_REGIONS-1:0]                host_sq_valid,
    output logic [N_REGIONS-1:0]                host_sq_ready,
    input  dreq_t                               host_sq_data [N_REGIONS],

    // Bypass DMA requests (to DMA engine)
    output logic [N_REGIONS-1:0]                bpss_rd_sq_valid,
    input  logic [N_REGIONS-1:0]                bpss_rd_sq_ready,
    output req_t                                bpss_rd_sq_data [N_REGIONS],
    output logic [N_REGIONS-1:0]                bpss_wr_sq_valid,
    input  logic [N_REGIONS-1:0]                bpss_wr_sq_ready,
    output req_t                                bpss_wr_sq_data [N_REGIONS],

    // User logic data streams (axisr_ul_* in vFIU)
    // TX: User -> vFIU (user sends data)
    AXI4SR.s                                    axis_user_sink [N_REGIONS],
    // RX: vFIU -> User (user receives data)
    AXI4SR.m                                    axis_user_src [N_REGIONS],

    // =========================================================================
    // I/O Stack Interfaces (connect to RDMA/TCP stacks)
    // These are the data_host_sink/src ports of the vIO Switch
    // =========================================================================

    // From I/O stacks to vIO Switch (incoming from network)
    AXI4S.s                                     axis_io_sink [N_REGIONS],
    // From vIO Switch to I/O stacks (outgoing to network)
    AXI4S.m                                     axis_io_src [N_REGIONS],

    // =========================================================================
    // Control/Configuration
    // =========================================================================

    // Memory endpoint control for security validation (per region)
    input  logic [(99*N_ENDPOINTS)-1:0]         mem_ctrl [N_REGIONS],

    // Route control for each vFIU (14-bit routing capability)
    input  logic [N_REGIONS-1:0][13:0]          route_ctrl
);

    // =========================================================================
    // Internal signals between vFIUs and vIO Switch
    // =========================================================================

    // vFIU -> vIO Switch (data_dtu_sink)
    AXI4SR axis_vfiu_to_switch [N_REGIONS] ();

    // vIO Switch -> vFIU (data_dtu_src)
    AXI4SR axis_switch_to_vfiu [N_REGIONS] ();

    // Route IDs for vIO Switch
    logic [N_REGIONS-1:0][13:0] route_in;   // From vFIU to switch
    logic [N_REGIONS-1:0][13:0] route_out;  // From switch to vFIU

    // =========================================================================
    // Instantiate vFIUs (one per region)
    // =========================================================================

    for (genvar i = 0; i < N_REGIONS; i++) begin : gen_vfiu

        // Temporary wires for vFIU ports
        logic [AXI_DATA_BITS-1:0]     axis_dtu_sink_tdata;
        logic [AXI_DATA_BITS/8-1:0]   axis_dtu_sink_tkeep;
        logic                         axis_dtu_sink_tlast;
        logic                         axis_dtu_sink_tready;
        logic                         axis_dtu_sink_tvalid;
        logic [PID_BITS-1:0]          axis_dtu_sink_tid;

        logic [AXI_DATA_BITS-1:0]     axis_dtu_src_tdata;
        logic [AXI_DATA_BITS/8-1:0]   axis_dtu_src_tkeep;
        logic                         axis_dtu_src_tlast;
        logic                         axis_dtu_src_tready;
        logic                         axis_dtu_src_tvalid;
        logic [PID_BITS-1:0]          axis_dtu_src_tid;

        logic [AXI_DATA_BITS-1:0]     axisr_ul_sink_tdata;
        logic [AXI_DATA_BITS/8-1:0]   axisr_ul_sink_tkeep;
        logic                         axisr_ul_sink_tlast;
        logic                         axisr_ul_sink_tready;
        logic                         axisr_ul_sink_tvalid;
        logic [PID_BITS-1:0]          axisr_ul_sink_tid;

        logic [AXI_DATA_BITS-1:0]     axisr_ul_src_tdata;
        logic [AXI_DATA_BITS/8-1:0]   axisr_ul_src_tkeep;
        logic                         axisr_ul_src_tlast;
        logic                         axisr_ul_src_tready;
        logic                         axisr_ul_src_tvalid;
        logic [PID_BITS-1:0]          axisr_ul_src_tid;

        // Host data path via vIO Switch
        logic [AXI_DATA_BITS-1:0]     axis_host_tx_to_switch_tdata;
        logic [AXI_DATA_BITS/8-1:0]   axis_host_tx_to_switch_tkeep;
        logic                         axis_host_tx_to_switch_tlast;
        logic                         axis_host_tx_to_switch_tready;
        logic                         axis_host_tx_to_switch_tvalid;
        logic [PID_BITS-1:0]          axis_host_tx_to_switch_tid;
        logic [13:0]                  axis_host_tx_to_switch_tdest;

        logic [AXI_DATA_BITS-1:0]     axis_host_rx_from_switch_tdata;
        logic [AXI_DATA_BITS/8-1:0]   axis_host_rx_from_switch_tkeep;
        logic                         axis_host_rx_from_switch_tlast;
        logic                         axis_host_rx_from_switch_tready;
        logic                         axis_host_rx_from_switch_tvalid;
        logic [PID_BITS-1:0]          axis_host_rx_from_switch_tid;
        logic [13:0]                  axis_host_rx_from_switch_tdest;

        // Connect user logic interfaces to vFIU
        assign axisr_ul_sink_tvalid = axis_user_sink[i].tvalid;
        assign axisr_ul_sink_tdata  = axis_user_sink[i].tdata;
        assign axisr_ul_sink_tkeep  = axis_user_sink[i].tkeep;
        assign axisr_ul_sink_tlast  = axis_user_sink[i].tlast;
        assign axisr_ul_sink_tid    = axis_user_sink[i].tid;
        assign axis_user_sink[i].tready = axisr_ul_sink_tready;

        assign axis_user_src[i].tvalid = axisr_ul_src_tvalid;
        assign axis_user_src[i].tdata  = axisr_ul_src_tdata;
        assign axis_user_src[i].tkeep  = axisr_ul_src_tkeep;
        assign axis_user_src[i].tlast  = axisr_ul_src_tlast;
        assign axis_user_src[i].tid    = axisr_ul_src_tid;
        assign axisr_ul_src_tready     = axis_user_src[i].tready;

        // Connect vFIU to vIO Switch (DTU side)
        assign axis_vfiu_to_switch[i].tvalid = axis_dtu_src_tvalid;
        assign axis_vfiu_to_switch[i].tdata  = axis_dtu_src_tdata;
        assign axis_vfiu_to_switch[i].tkeep  = axis_dtu_src_tkeep;
        assign axis_vfiu_to_switch[i].tlast  = axis_dtu_src_tlast;
        assign axis_vfiu_to_switch[i].tid    = axis_dtu_src_tid;
        assign axis_dtu_src_tready           = axis_vfiu_to_switch[i].tready;

        assign axis_dtu_sink_tvalid          = axis_switch_to_vfiu[i].tvalid;
        assign axis_dtu_sink_tdata           = axis_switch_to_vfiu[i].tdata;
        assign axis_dtu_sink_tkeep           = axis_switch_to_vfiu[i].tkeep;
        assign axis_dtu_sink_tlast           = axis_switch_to_vfiu[i].tlast;
        assign axis_dtu_sink_tid             = axis_switch_to_vfiu[i].tid;
        assign axis_switch_to_vfiu[i].tready = axis_dtu_sink_tready;

        vfiu_top #(
            .N_ID(1),
            .N_ENDPOINTS(N_ENDPOINTS),
            .VFPGA_ID(i)    // Pass vFPGA ID for hardcoded test mode routing
        ) inst_vfiu (
            .aclk(aclk),
            .aresetn(aresetn),

            // Memory endpoint control
            .mem_ctrl(mem_ctrl[i]),

            // User descriptors
            .user_rd_sq_valid(user_rd_sq_valid[i]),
            .user_rd_sq_ready(user_rd_sq_ready[i]),
            .user_rd_sq_data(user_rd_sq_data[i]),
            .user_wr_sq_valid(user_wr_sq_valid[i]),
            .user_wr_sq_ready(user_wr_sq_ready[i]),
            .user_wr_sq_data(user_wr_sq_data[i]),

            // Host descriptors
            .host_sq_valid(host_sq_valid[i]),
            .host_sq_ready(host_sq_ready[i]),
            .host_sq_data(host_sq_data[i]),

            // Bypass DMA requests
            .bpss_rd_sq_valid(bpss_rd_sq_valid[i]),
            .bpss_rd_sq_ready(bpss_rd_sq_ready[i]),
            .bpss_rd_sq_data(bpss_rd_sq_data[i]),
            .bpss_wr_sq_valid(bpss_wr_sq_valid[i]),
            .bpss_wr_sq_ready(bpss_wr_sq_ready[i]),
            .bpss_wr_sq_data(bpss_wr_sq_data[i]),

            // DTU interface (to vIO Switch)
            .axis_dtu_sink_tdata(axis_dtu_sink_tdata),
            .axis_dtu_sink_tkeep(axis_dtu_sink_tkeep),
            .axis_dtu_sink_tlast(axis_dtu_sink_tlast),
            .axis_dtu_sink_tready(axis_dtu_sink_tready),
            .axis_dtu_sink_tvalid(axis_dtu_sink_tvalid),
            .axis_dtu_sink_tid(axis_dtu_sink_tid),

            .axis_dtu_src_tdata(axis_dtu_src_tdata),
            .axis_dtu_src_tkeep(axis_dtu_src_tkeep),
            .axis_dtu_src_tlast(axis_dtu_src_tlast),
            .axis_dtu_src_tready(axis_dtu_src_tready),
            .axis_dtu_src_tvalid(axis_dtu_src_tvalid),
            .axis_dtu_src_tid(axis_dtu_src_tid),

            // User logic interface
            .axisr_ul_sink_tdata(axisr_ul_sink_tdata),
            .axisr_ul_sink_tkeep(axisr_ul_sink_tkeep),
            .axisr_ul_sink_tlast(axisr_ul_sink_tlast),
            .axisr_ul_sink_tready(axisr_ul_sink_tready),
            .axisr_ul_sink_tvalid(axisr_ul_sink_tvalid),
            .axisr_ul_sink_tid(axisr_ul_sink_tid),

            .axisr_ul_src_tdata(axisr_ul_src_tdata),
            .axisr_ul_src_tkeep(axisr_ul_src_tkeep),
            .axisr_ul_src_tlast(axisr_ul_src_tlast),
            .axisr_ul_src_tready(axisr_ul_src_tready),
            .axisr_ul_src_tvalid(axisr_ul_src_tvalid),
            .axisr_ul_src_tid(axisr_ul_src_tid),

            // Routing control
            .route_ctrl(route_ctrl[i]),
            .route_in(route_out[i]),    // From switch to this vFIU
            .route_out(route_in[i]),    // From this vFIU to switch

            // Host data path via vIO Switch (TX: vFPGA → DMA, RX: DMA → vFPGA)
            .axis_host_tx_to_switch_tdata(axis_host_tx_to_switch_tdata),
            .axis_host_tx_to_switch_tkeep(axis_host_tx_to_switch_tkeep),
            .axis_host_tx_to_switch_tlast(axis_host_tx_to_switch_tlast),
            .axis_host_tx_to_switch_tready(axis_host_tx_to_switch_tready),
            .axis_host_tx_to_switch_tvalid(axis_host_tx_to_switch_tvalid),
            .axis_host_tx_to_switch_tid(axis_host_tx_to_switch_tid),
            .axis_host_tx_to_switch_tdest(axis_host_tx_to_switch_tdest),

            .axis_host_rx_from_switch_tdata(axis_host_rx_from_switch_tdata),
            .axis_host_rx_from_switch_tkeep(axis_host_rx_from_switch_tkeep),
            .axis_host_rx_from_switch_tlast(axis_host_rx_from_switch_tlast),
            .axis_host_rx_from_switch_tready(axis_host_rx_from_switch_tready),
            .axis_host_rx_from_switch_tvalid(axis_host_rx_from_switch_tvalid),
            .axis_host_rx_from_switch_tid(axis_host_rx_from_switch_tid),
            .axis_host_rx_from_switch_tdest(axis_host_rx_from_switch_tdest)
        );

        // Note: In the full dynamic_top_tmplt.txt integration, these host data paths
        // connect to the vIO Switch HOST_TX/HOST_RX ports. For this simpler
        // nf_composer_top architecture, they should be tied off or connected
        // to the appropriate external interfaces.
        //
        // TODO: Add vIO Switch HOST_TX/HOST_RX ports or external DMA interfaces
        // to complete the host data path routing through vIO Switch.
        assign axis_host_tx_to_switch_tready = 1'b1;  // Temporary tie-off
        assign axis_host_rx_from_switch_tvalid = 1'b0;
        assign axis_host_rx_from_switch_tdata = '0;
        assign axis_host_rx_from_switch_tkeep = '0;
        assign axis_host_rx_from_switch_tlast = 1'b0;
        assign axis_host_rx_from_switch_tid = '0;
        assign axis_host_rx_from_switch_tdest = '0;

    end

    // =========================================================================
    // Instantiate vIO Switch
    // Routes packets between vFPGAs and I/O stacks based on route_id/tdest
    // =========================================================================

    generate
        if (N_REGIONS == 4) begin : gen_switch_4
            vio_switch_4 #(
                .N_ID(N_REGIONS)
            ) inst_vio_switch (
                .aclk(aclk),
                .aresetn(aresetn),

                // Route IDs
                .route_in(route_in),
                .route_out(route_out),

                // I/O stack side (data_host_*)
                .data_host_sink(axis_io_sink),
                .data_host_src(axis_io_src),

                // vFIU side (data_dtu_*)
                .data_dtu_sink(axis_vfiu_to_switch),
                .data_dtu_src(axis_switch_to_vfiu)
            );
        end else if (N_REGIONS == 16) begin : gen_switch_16
            vio_switch_16 #(
                .N_ID(N_REGIONS)
            ) inst_vio_switch (
                .aclk(aclk),
                .aresetn(aresetn),

                // Route IDs
                .route_in(route_in),
                .route_out(route_out),

                // I/O stack side (data_host_*)
                .data_host_sink(axis_io_sink),
                .data_host_src(axis_io_src),

                // vFIU side (data_dtu_*)
                .data_dtu_sink(axis_vfiu_to_switch),
                .data_dtu_src(axis_switch_to_vfiu)
            );
        end else begin : gen_switch_default
            // Default to 4-port switch for other configurations
            vio_switch_4 #(
                .N_ID(N_REGIONS)
            ) inst_vio_switch (
                .aclk(aclk),
                .aresetn(aresetn),
                .route_in(route_in),
                .route_out(route_out),
                .data_host_sink(axis_io_sink),
                .data_host_src(axis_io_src),
                .data_dtu_sink(axis_vfiu_to_switch),
                .data_dtu_src(axis_switch_to_vfiu)
            );
        end
    endgenerate

endmodule

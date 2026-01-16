`timescale 1ns / 1ps

import lynxTypes::*;

`include "axi_macros.svh"

/**
 * Perf local example
 * 
 */
module sha2_top (
    AXI4SR.s                                axis_sink,
    AXI4SR.m                                axis_src,

    input  logic                            aclk,
    input  logic                            aresetn
);

// Interface declarations
AXI4SR axis_sink_int ();
AXI4SR axis_src_int ();

// Signals for connecting to SHA2 module
logic [511:0] axis_sink_tdata;
logic         axis_sink_tvalid;
logic         axis_sink_tlast;
logic         axis_sink_tready;

logic [511:0] axis_src_tdata;
logic [63:0]  axis_src_tkeep;
logic [7:0]   axis_src_tid;
logic         axis_src_tvalid;
logic         axis_src_tlast;
logic         axis_src_tready;

// Host stream routing
axisr_reg inst_reg_sink (.aclk(aclk),.aresetn(aresetn),.s_axis(axis_sink),.m_axis(axis_sink_int));
axisr_reg inst_reg_src (.aclk(aclk),.aresetn(aresetn),.s_axis(axis_src_int),.m_axis(axis_src));

// Connect SHA2 input to interface
assign axis_sink_tdata  			= axis_sink_int.tdata;
assign axis_sink_tvalid 			= axis_sink_int.tvalid;
assign axis_sink_tlast  			= axis_sink_int.tlast;
assign axis_sink_int.tready 	= axis_sink_tready;

// Connect SHA2 output to interface
assign axis_src_int.tdata 		= axis_src_tdata;
assign axis_src_int.tkeep       = axis_src_tkeep;
assign axis_src_int.tid         = axis_src_tid;
assign axis_src_int.tvalid      = axis_src_tvalid;
assign axis_src_int.tlast       = axis_src_tlast;
assign axis_src_tready          = axis_src_int.tready;


// Instantiate SHA2 module
sha2 inst_sha2 (
    .aclk(aclk),
    .aresetn(aresetn),
    .axis_sink_tdata(axis_sink_tdata),
    .axis_sink_tvalid(axis_sink_tvalid),
    .axis_sink_tlast(axis_sink_tlast),
    .axis_sink_tready(axis_sink_tready),
    .axis_src_tdata(axis_src_tdata),
    .axis_src_tkeep(axis_src_tkeep),
    .axis_src_tid(axis_src_tid),
    .axis_src_tvalid(axis_src_tvalid),
    .axis_src_tlast(axis_src_tlast),
    .axis_src_tready(axis_src_tready)
);

endmodule

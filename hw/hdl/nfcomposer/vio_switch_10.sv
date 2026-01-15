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

/**
 * @brief   Data switch source
 *
 * Data switch for all vFPGA streams.
 *
 *  @param N_ID    Number of vFPGA regions
 */
module vio_switch_10 #(
    parameter integer N_ID = N_REGIONS
) (
    input  logic                             aclk,
    input  logic                             aresetn,

    // IO control
    input logic [N_REGIONS-1:0][13:0]         route_in,
    output logic [N_REGIONS-1:0][13:0]        route_out,


    // data input from shell to switch
    AXI4S.s                                 data_host_sink [N_ID],
    // data output from switch to shell
    AXI4S.m                                 data_host_src [N_ID],

    // data input from user logic to switch
    AXI4SR.s                                 data_dtu_sink [N_ID],
    // data output from switch to user logic
    AXI4SR.m                                 data_dtu_src [N_ID]

);


// For axi stream switch decode error
logic [19:0] axis_switch_0_s_decode_err;
logic [N_ID-1:0][13:0] axis_switch_0_m_tdest;
logic [N_ID-1:0] region_src_id, region_dest_id;


// -----------------------------------------------------------------------------------------------------------------------
// -- Mux
// -----------------------------------------------------------------------------------------------------------------------
// -- interface loop issues => temp signals

logic [N_ID-1:0]                        data_host_sink_tvalid;
logic [N_ID-1:0]                        data_host_sink_tready;
logic [N_ID-1:0][AXI_DATA_BITS-1:0]     data_host_sink_tdata;
logic [N_ID-1:0][AXI_DATA_BITS/8-1:0]   data_host_sink_tkeep;
logic [N_ID-1:0]                        data_host_sink_tlast;
logic [N_ID-1:0][PID_BITS-1:0]          data_host_sink_tid;

logic [N_ID-1:0]                        data_host_src_tvalid;
logic [N_ID-1:0]                        data_host_src_tready;
logic [N_ID-1:0][AXI_DATA_BITS-1:0]     data_host_src_tdata;
logic [N_ID-1:0][AXI_DATA_BITS/8-1:0]   data_host_src_tkeep;
logic [N_ID-1:0]                        data_host_src_tlast;
logic [N_ID-1:0][PID_BITS-1:0]          data_host_src_tid;


for(genvar i = 0; i < N_ID; i++) begin
    assign data_host_sink_tvalid[i] = data_host_sink[i].tvalid;
    assign data_host_sink_tdata[i] = data_host_sink[i].tdata;
    assign data_host_sink_tkeep[i] = data_host_sink[i].tkeep;
    assign data_host_sink_tlast[i] = data_host_sink[i].tlast;
    assign data_host_sink_tid[i] = 0;
    assign data_host_sink[i].tready = data_host_sink_tready[i];
end

for(genvar i = 0; i < N_ID; i++) begin
    assign data_host_src[i].tvalid = data_host_src_tvalid[i];
    assign data_host_src[i].tdata = data_host_src_tdata[i];
    assign data_host_src[i].tkeep = data_host_src_tkeep[i];
    assign data_host_src[i].tlast = data_host_src_tlast[i];
    assign data_host_src_tready[i] = data_host_src[i].tready;
end


logic [N_ID-1:0]                        data_dtu_sink_tvalid;
logic [N_ID-1:0]                        data_dtu_sink_tready;
logic [N_ID-1:0][AXI_DATA_BITS-1:0]     data_dtu_sink_tdata;
logic [N_ID-1:0][AXI_DATA_BITS/8-1:0]   data_dtu_sink_tkeep;
logic [N_ID-1:0]                        data_dtu_sink_tlast;
logic [N_ID-1:0][PID_BITS-1:0]          data_dtu_sink_tid;

logic [N_ID-1:0]                        data_dtu_src_tvalid;
logic [N_ID-1:0]                        data_dtu_src_tready;
logic [N_ID-1:0][AXI_DATA_BITS-1:0]     data_dtu_src_tdata;
logic [N_ID-1:0][AXI_DATA_BITS/8-1:0]   data_dtu_src_tkeep;
logic [N_ID-1:0]                        data_dtu_src_tlast;
logic [N_ID-1:0][PID_BITS-1:0]          data_dtu_src_tid;


for(genvar i = 0; i < N_ID; i++) begin
    assign data_dtu_sink_tvalid[i] = data_dtu_sink[i].tvalid;
    assign data_dtu_sink_tdata[i] = data_dtu_sink[i].tdata;
    assign data_dtu_sink_tkeep[i] = data_dtu_sink[i].tkeep;
    assign data_dtu_sink_tlast[i] = data_dtu_sink[i].tlast;
    assign data_dtu_sink_tid[i] = data_dtu_sink[i].tid;
    assign data_dtu_sink[i].tready = data_dtu_sink_tready[i];
end

for(genvar i = 0; i < N_ID; i++) begin
    assign data_dtu_src[i].tvalid = data_dtu_src_tvalid[i];
    assign data_dtu_src[i].tdata = data_dtu_src_tdata[i];
    assign data_dtu_src[i].tkeep = data_dtu_src_tkeep[i];
    assign data_dtu_src[i].tlast = data_dtu_src_tlast[i];
    assign data_dtu_src[i].tid = data_dtu_src_tid[i];
    assign data_dtu_src_tready[i] = data_dtu_src[i].tready;
end


axis_switch_ceu_20_0 inst_axis_switch_ceu_0 (
    .aclk(aclk),
    .aresetn(aresetn),
    .m_axis_tdata({data_dtu_src_tdata, data_host_src_tdata}),
    .m_axis_tdest({route_out, axis_switch_0_m_tdest}),
    .m_axis_tready({data_dtu_src_tready, data_host_src_tready}),
    .m_axis_tvalid({data_dtu_src_tvalid, data_host_src_tvalid}),
    .m_axis_tlast({data_dtu_src_tlast, data_host_src_tlast}),
    .m_axis_tid({data_dtu_src_tid, data_host_src_tid}),
    .s_axis_tdata({data_dtu_sink_tdata, data_host_sink_tdata}),
    .s_axis_tdest({route_in[9], route_in[8], route_in[7], route_in[6], route_in[5], route_in[4], route_in[3], route_in[2], route_in[1], route_in[0],
                   14'b10011111111100, 14'b10001111111100, 14'b01111111111100, 14'b01101111111100, 14'b01011111111100,
                   14'b01001111111100, 14'b00111111111100, 14'b00101111111100, 14'b00011111111100, 14'b00001111111100}),
    .s_axis_tready({data_dtu_sink_tready, data_host_sink_tready}),
    .s_axis_tvalid({data_dtu_sink_tvalid, data_host_sink_tvalid}),
    .s_axis_tlast({data_dtu_sink_tlast, data_host_sink_tlast}),
    .s_axis_tid({data_dtu_sink_tid, data_host_sink_tid}),
    .s_decode_err(axis_switch_0_s_decode_err)
);


endmodule

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
 * @brief   DMA Read Route Synchronizer
 *
 * Synchronizes DMA read response data with routing metadata from mmu_arbiter.
 *
 * Problem: DMA read responses arrive on axis data bus, but we need to know
 * which vFPGA each data beat belongs to. The mmu_arbiter provides this info
 * via mux metadata (vfid, len, last), but metadata and data are asynchronous.
 *
 * Solution: This module uses an FSM with beat counter to:
 * 1. Capture mux metadata (vfid, len) when a transfer starts
 * 2. Count data beats to track when the current transfer ends
 * 3. Attach vfid as tdest for vIO Switch routing
 * 4. Consume next metadata only when current transfer completes
 *
 * The vIO Switch then routes data to the correct vFPGA based on tdest.
 *
 *  @param MUX_DATA_BITS    Data bus size
 *  @param N_REGIONS        Number of vFPGA regions
 */
module dma_rd_route_sync #(
    parameter integer MUX_DATA_BITS = AXI_DATA_BITS,
    parameter integer N_REGIONS = 2
) (
    input  logic                            aclk,
    input  logic                            aresetn,

    // Mux metadata from mmu_arbiter (vfid, len, last)
    metaIntf.s                              s_mux,

    // DMA read response data (from XDMA)
    AXI4S.s                                 s_axis,

    // Output to vIO Switch HOST_TX port (with tdest)
    AXI4SR.m                                m_axis
);

// -- Constants
localparam integer BEAT_LOG_BITS = $clog2(MUX_DATA_BITS/8);
localparam integer BLEN_BITS = LEN_BITS - BEAT_LOG_BITS;
localparam integer N_REGIONS_BITS = clog2s(N_REGIONS);

// PORT_HOST_TX = N_REGIONS (sender_id for HOST_TX port)
localparam logic [3:0] PORT_HOST_TX = N_REGIONS[3:0];

// -- FSM
typedef enum logic[0:0] {ST_IDLE, ST_MUX} state_t;
logic [0:0] state_C, state_N;

// -- Internal regs
logic [N_REGIONS_BITS-1:0] vfid_C, vfid_N;
logic [BLEN_BITS-1:0] cnt_C, cnt_N;
logic last_C, last_N;

// -- Internal signals
logic tr_done;

// -- Mux data path with tdest attachment
always_comb begin
    // Default: pass through data when in MUX state
    m_axis.tvalid = 1'b0;
    m_axis.tdata  = s_axis.tdata;
    m_axis.tkeep  = s_axis.tkeep;
    m_axis.tlast  = s_axis.tlast & last_C;  // Use last from mux metadata
    m_axis.tid    = '0;
    // Route format: [13:10]=reserved, [9:6]=sender_id, [5:2]=receiver_id, [1:0]=flags
    // sender_id = PORT_HOST_TX, receiver_id = target vFPGA
    m_axis.tdest  = {4'b0, PORT_HOST_TX, vfid_C[3:0], 2'b0};

    s_axis.tready = 1'b0;

    if (state_C == ST_MUX) begin
        m_axis.tvalid = s_axis.tvalid;
        s_axis.tready = m_axis.tready;
    end
end

// -- REG
always_ff @(posedge aclk) begin: PROC_REG
    if (aresetn == 1'b0) begin
        state_C <= ST_IDLE;
        cnt_C   <= 'X;
        vfid_C  <= 'X;
        last_C  <= 'X;
    end
    else begin
        state_C <= state_N;
        cnt_C   <= cnt_N;
        vfid_C  <= vfid_N;
        last_C  <= last_N;
    end
end

// -- NSL
always_comb begin: NSL
    state_N = state_C;

    case (state_C)
        ST_IDLE:
            state_N = s_mux.valid ? ST_MUX : ST_IDLE;

        ST_MUX:
            state_N = tr_done ? (s_mux.valid ? ST_MUX : ST_IDLE) : ST_MUX;
    endcase
end

// -- DP
always_comb begin : DP
    cnt_N  = cnt_C;
    vfid_N = vfid_C;
    last_N = last_C;

    // Transfer done when counter reaches 0 and we complete a beat
    tr_done = (cnt_C == 0) && (s_axis.tvalid & s_axis.tready);

    // Mux metadata consumption
    s_mux.ready = 1'b0;

    case (state_C)
        ST_IDLE: begin
            if (s_mux.valid) begin
                s_mux.ready = 1'b1;
                vfid_N = s_mux.data.vfid;
                cnt_N  = s_mux.data.len;
                last_N = s_mux.data.last;
            end
        end

        ST_MUX: begin
            if (tr_done) begin
                if (s_mux.valid) begin
                    s_mux.ready = 1'b1;
                    vfid_N = s_mux.data.vfid;
                    cnt_N  = s_mux.data.len;
                    last_N = s_mux.data.last;
                end
            end
            else begin
                cnt_N = (s_axis.tvalid & s_axis.tready) ? cnt_C - 1 : cnt_C;
            end
        end
    endcase
end

endmodule

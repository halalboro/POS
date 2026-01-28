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
 * @brief   Connection Route Table
 *
 * Stores route_id for RDMA QP and TCP session connections.
 * The route_id is set during connection establishment and looked up
 * when transmitting packets to determine vIO Switch routing.
 *
 * For RDMA: QPN (16-bit) is used as the connection index
 * For TCP: Session ID (16-bit) is used as the connection index
 *
 * Route ID Format (14-bit):
 *   [13:10] reserved
 *   [9:6]   sender_id (0 = external client, 1-15 = vFPGA ID)
 *   [5:2]   receiver_id (destination vFPGA ID)
 *   [1:0]   flags (reserved)
 *
 * @param NUM_ENTRIES   Number of connection entries (default: 256)
 */
module route_table #(
    parameter integer NUM_ENTRIES = 256,
    parameter integer INDEX_BITS = $clog2(NUM_ENTRIES)
) (
    input  logic                    aclk,
    input  logic                    aresetn,

    // Write interface (during connection setup)
    input  logic                    wr_en,
    input  logic [INDEX_BITS-1:0]   wr_index,
    input  logic [13:0]             wr_route_id,

    // Read interface (during packet transmission)
    input  logic                    rd_en,
    input  logic [INDEX_BITS-1:0]   rd_index,
    output logic [13:0]             rd_route_id,
    output logic                    rd_valid
);

    // Route table storage
    logic [13:0] route_table [NUM_ENTRIES-1:0];
    logic [NUM_ENTRIES-1:0] valid_table;

    // Write logic
    always_ff @(posedge aclk) begin
        if (~aresetn) begin
            valid_table <= '0;
        end else begin
            if (wr_en) begin
                route_table[wr_index] <= wr_route_id;
                valid_table[wr_index] <= 1'b1;
            end
        end
    end

    // Read logic (registered output)
    always_ff @(posedge aclk) begin
        if (~aresetn) begin
            rd_route_id <= '0;
            rd_valid <= 1'b0;
        end else begin
            if (rd_en) begin
                rd_route_id <= route_table[rd_index];
                rd_valid <= valid_table[rd_index];
            end else begin
                rd_valid <= 1'b0;
            end
        end
    end

endmodule

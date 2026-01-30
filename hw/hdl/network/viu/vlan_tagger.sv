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

// HARDCODED_TEST_MODE: When defined, bypass VLAN tag insertion (pass-through mode)
// since test packets don't have VLAN tags. Remove for production.
`define HARDCODED_TEST_MODE

/**
 * @brief   VLAN Tag Insertion Module for VIU TX Path
 *
 * This module inserts an 802.1Q VLAN tag into outgoing Ethernet frames.
 * The VLAN tag encodes routing information that enables the receiving VIU
 * to identify the sender and route packets to the correct destination.
 *
 * DEPLOYMENT MODELS:
 *   1. Multi-FPGA Model: vFPGA-to-vFPGA communication across FPGAs
 *      - src_node_id + src_vfpga_id = source identity
 *      - dst_node_id + dst_vfpga_id = destination identity
 *
 *   2. SmartNIC Model: External clients communicating with vFPGAs
 *      - TX path: vFPGA sending to external client
 *      - src_node_id + src_vfpga_id = source vFPGA
 *      - dst_node_id = 0, dst_vfpga_id = 0 (external network destination)
 *
 * 802.1Q VLAN Tag (4 bytes, inserted after src MAC at byte offset 12):
 *   [15:0]  TPID = 0x8100 (VLAN protocol identifier)
 *   [15:13] PCP  = Priority Code Point (set to 0)
 *   [12]    DEI  = Drop Eligible Indicator (set to 0)
 *   [11:0]  VID  = VLAN ID encoding routing info
 *
 * VLAN ID encoding (12 bits) - Multi-FPGA Format:
 *   [11:10] = src_node_id   (2 bits - source physical FPGA node, 0-3)
 *   [9:6]   = src_vfpga_id  (4 bits - source vFPGA on that node, 0-15)
 *   [5:4]   = dst_node_id   (2 bits - destination physical FPGA node, 0-3)
 *   [3:0]   = dst_vfpga_id  (4 bits - destination vFPGA on that node, 0-15)
 *
 * This encoding supports: 4 nodes × 16 vFPGAs = 64 total vFPGAs in closed network
 * Node ID 0 + vFPGA ID 0 = external network (outside closed VLAN network)
 *
 * Route Control Format (14 bits - input from gateway_tx):
 *   [13:12] = src_node_id  (2 bits - source physical node, 0-3)
 *   [11:8]  = src_vfpga_id (4 bits - source vFPGA on node, 0-15)
 *   [7:6]   = dst_node_id  (2 bits - destination physical node, 0-3)
 *   [5:2]   = dst_vfpga_id (4 bits - destination vFPGA on node, 0-15)
 *   [1:0]   = reserved
 *
 * Ethernet frame structure:
 *   Before: | Dst MAC (6B) | Src MAC (6B) | EtherType (2B) | Payload ... |
 *   After:  | Dst MAC (6B) | Src MAC (6B) | VLAN Tag (4B) | EtherType (2B) | Payload ... |
 */
module vlan_tagger #(
    parameter integer DATA_WIDTH = AXI_NET_BITS  // 512 bits = 64 bytes
) (
    input  logic                        aclk,
    input  logic                        aresetn,

    // Routing info from gateway_tx
    input  logic [13:0]                 route_out,

    // Input AXI-Stream (untagged packets from network stack)
    input  logic [DATA_WIDTH-1:0]       s_axis_tdata,
    input  logic [DATA_WIDTH/8-1:0]     s_axis_tkeep,
    input  logic                        s_axis_tlast,
    output logic                        s_axis_tready,
    input  logic                        s_axis_tvalid,

    // Output AXI-Stream (VLAN tagged packets to CMAC)
    output logic [DATA_WIDTH-1:0]       m_axis_tdata,
    output logic [DATA_WIDTH/8-1:0]     m_axis_tkeep,
    output logic                        m_axis_tlast,
    input  logic                        m_axis_tready,
    output logic                        m_axis_tvalid
);

    // ============================================================================
    // Constants
    // ============================================================================
    localparam [15:0] VLAN_TPID = 16'h8100;  // 802.1Q VLAN tag protocol ID
    localparam integer BYTES_PER_BEAT = DATA_WIDTH / 8;  // 64 bytes for 512-bit

    // ============================================================================
    // VLAN Tag Construction
    // ============================================================================
    // Extract routing info and build VLAN tag
    //
    // Multi-FPGA format: Node ID (2-bit) + vFPGA ID (4-bit) for both source and destination
    //   - src_node_id + src_vfpga_id identify the sending vFPGA
    //   - dst_node_id + dst_vfpga_id identify the receiving vFPGA
    //   - All zeros (node=0, vfpga=0) indicates external network
    //
    logic [1:0]  src_node_id;    // Source physical FPGA node (0-3)
    logic [3:0]  src_vfpga_id;   // Source vFPGA on that node (0-15)
    logic [1:0]  dst_node_id;    // Destination physical FPGA node (0-3)
    logic [3:0]  dst_vfpga_id;   // Destination vFPGA on that node (0-15)
    logic [11:0] vlan_id;
    logic [15:0] tci;  // Tag Control Information (PCP + DEI + VID)
    logic [31:0] vlan_tag;  // Full 4-byte VLAN tag

    // Extract from route_out (14-bit format):
    //   [13:12] = src_node_id  (2 bits)
    //   [11:8]  = src_vfpga_id (4 bits)
    //   [7:6]   = dst_node_id  (2 bits)
    //   [5:2]   = dst_vfpga_id (4 bits)
    //   [1:0]   = reserved
    assign src_node_id  = route_out[13:12];  // Source node ID (2 bits)
    assign src_vfpga_id = route_out[11:8];   // Source vFPGA ID (4 bits)
    assign dst_node_id  = route_out[7:6];    // Destination node ID (2 bits)
    assign dst_vfpga_id = route_out[5:2];    // Destination vFPGA ID (4 bits)

    // VLAN ID (12 bits): [11:10]=src_node, [9:6]=src_vfpga, [5:4]=dst_node, [3:0]=dst_vfpga
    assign vlan_id = {src_node_id, src_vfpga_id, dst_node_id, dst_vfpga_id};
    assign tci = {3'b000, 1'b0, vlan_id};  // PCP=0, DEI=0, VID=vlan_id
    assign vlan_tag = {tci, VLAN_TPID};  // Note: Will be byte-swapped in insertion

    // ============================================================================
    // State Machine
    // ============================================================================
    typedef enum logic [1:0] {
        ST_FIRST_BEAT,   // Process first beat: insert VLAN tag
        ST_SHIFT_DATA,   // Shift remaining data by 4 bytes
        ST_DRAIN_LAST    // Output final shifted bytes if needed
    } state_t;

    state_t state, state_next;

    // Registered signals
    logic [DATA_WIDTH-1:0] data_reg;
    logic [DATA_WIDTH/8-1:0] keep_reg;
    logic last_reg;
    logic [31:0] carry_data;  // 4 bytes carried over from previous beat
    logic [3:0] carry_keep;

    // ============================================================================
    // State Machine Logic
    // ============================================================================
    always_ff @(posedge aclk) begin
        if (~aresetn) begin
            state <= ST_FIRST_BEAT;
            data_reg <= '0;
            keep_reg <= '0;
            last_reg <= 1'b0;
            carry_data <= '0;
            carry_keep <= '0;
        end else begin
            state <= state_next;

            if (s_axis_tvalid && s_axis_tready) begin
                data_reg <= s_axis_tdata;
                keep_reg <= s_axis_tkeep;
                last_reg <= s_axis_tlast;
            end

            // Store carry bytes when outputting
            if (m_axis_tvalid && m_axis_tready && state == ST_SHIFT_DATA) begin
                // Top 4 bytes of input become carry for next beat
                carry_data <= s_axis_tdata[DATA_WIDTH-1 -: 32];
                carry_keep <= s_axis_tkeep[BYTES_PER_BEAT-1 -: 4];
            end
        end
    end

`ifdef HARDCODED_TEST_MODE
    // ============================================================================
    // BYPASS MODE - Pass-through without VLAN tag insertion
    // ============================================================================
    // In test mode, packets don't have VLAN tags, so just pass data through
    assign m_axis_tvalid = s_axis_tvalid;
    assign m_axis_tdata  = s_axis_tdata;
    assign m_axis_tkeep  = s_axis_tkeep;
    assign m_axis_tlast  = s_axis_tlast;
    assign s_axis_tready = m_axis_tready;

    // Unused signals in bypass mode
    always_comb begin
        state_next = ST_FIRST_BEAT;
    end

`else
    // ============================================================================
    // Combinational Output Logic
    // ============================================================================
    always_comb begin
        // Defaults
        state_next = state;
        m_axis_tvalid = 1'b0;
        m_axis_tdata = '0;
        m_axis_tkeep = '0;
        m_axis_tlast = 1'b0;
        s_axis_tready = 1'b0;

        case (state)
            ST_FIRST_BEAT: begin
                // Wait for first beat of packet
                if (s_axis_tvalid) begin
                    m_axis_tvalid = 1'b1;

                    // Insert VLAN tag after byte 12 (after Dst MAC + Src MAC)
                    // Original: [Dst MAC 6B][Src MAC 6B][EtherType 2B][Payload...]
                    // Bytes 0-11: Dst MAC + Src MAC (keep as-is)
                    // Bytes 12-13: Original EtherType -> shifted to bytes 16-17
                    // Bytes 12-15: Insert VLAN tag (TPID + TCI)

                    // Build the output data:
                    // Bytes 0-11: Copy from input (Dst + Src MAC)
                    m_axis_tdata[95:0] = s_axis_tdata[95:0];

                    // Bytes 12-13: VLAN TPID (0x8100) - network byte order
                    m_axis_tdata[103:96] = 8'h81;   // TPID high byte
                    m_axis_tdata[111:104] = 8'h00;  // TPID low byte

                    // Bytes 14-15: TCI (PCP + DEI + VID) - network byte order
                    m_axis_tdata[119:112] = tci[15:8];  // TCI high byte
                    m_axis_tdata[127:120] = tci[7:0];   // TCI low byte

                    // Bytes 16+: Shifted original data starting from byte 12
                    // Original bytes 12-59 -> Output bytes 16-63 (shift by 4)
                    m_axis_tdata[DATA_WIDTH-1:128] = s_axis_tdata[DATA_WIDTH-33:96];

                    // Adjust tkeep: first 12 bytes + 4 VLAN bytes + shifted keeps
                    m_axis_tkeep[11:0] = s_axis_tkeep[11:0];  // Dst + Src MAC
                    m_axis_tkeep[15:12] = 4'b1111;             // VLAN tag
                    m_axis_tkeep[BYTES_PER_BEAT-1:16] = s_axis_tkeep[BYTES_PER_BEAT-5:12];

                    if (m_axis_tready) begin
                        s_axis_tready = 1'b1;

                        if (s_axis_tlast) begin
                            // Single-beat packet: check if we need extra beat for overflow
                            // Original bytes 60-63 need to go somewhere
                            if (s_axis_tkeep[BYTES_PER_BEAT-1 -: 4] != 4'b0000) begin
                                // There's data in the last 4 bytes that got pushed out
                                m_axis_tlast = 1'b0;
                                state_next = ST_DRAIN_LAST;
                            end else begin
                                m_axis_tlast = 1'b1;
                                state_next = ST_FIRST_BEAT;
                            end
                        end else begin
                            state_next = ST_SHIFT_DATA;
                        end
                    end
                end
            end

            ST_SHIFT_DATA: begin
                // Continue shifting data by 4 bytes
                if (s_axis_tvalid) begin
                    m_axis_tvalid = 1'b1;

                    // Carry from previous beat goes to bytes 0-3
                    m_axis_tdata[31:0] = carry_data;
                    m_axis_tkeep[3:0] = carry_keep;

                    // Current beat shifted: bytes 0-59 -> output bytes 4-63
                    m_axis_tdata[DATA_WIDTH-1:32] = s_axis_tdata[DATA_WIDTH-33:0];
                    m_axis_tkeep[BYTES_PER_BEAT-1:4] = s_axis_tkeep[BYTES_PER_BEAT-5:0];

                    if (m_axis_tready) begin
                        s_axis_tready = 1'b1;

                        if (s_axis_tlast) begin
                            // Check if last 4 bytes have valid data
                            if (s_axis_tkeep[BYTES_PER_BEAT-1 -: 4] != 4'b0000) begin
                                m_axis_tlast = 1'b0;
                                state_next = ST_DRAIN_LAST;
                            end else begin
                                m_axis_tlast = 1'b1;
                                state_next = ST_FIRST_BEAT;
                            end
                        end
                    end
                end
            end

            ST_DRAIN_LAST: begin
                // Output the final 4 carried bytes
                m_axis_tvalid = 1'b1;
                m_axis_tdata[31:0] = carry_data;
                m_axis_tkeep[3:0] = carry_keep;
                m_axis_tkeep[BYTES_PER_BEAT-1:4] = '0;
                m_axis_tlast = 1'b1;

                if (m_axis_tready) begin
                    state_next = ST_FIRST_BEAT;
                end
            end

            default: begin
                state_next = ST_FIRST_BEAT;
            end
        endcase
    end

`endif

endmodule

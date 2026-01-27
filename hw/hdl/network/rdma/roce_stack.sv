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

`define DBG_IBV

import lynxTypes::*;

/**
 * @brief   RoCE instantiation
 *
 * RoCE stack with optional Raw Ethernet bypass mode
 */
module roce_stack #(
    parameter integer BYPASS_MODE = 0  // 0=Full RoCE, 1=Raw Ethernet passthrough
) 
(
    input  logic                nclk,
    input  logic                nresetn,

    // Network interface
    AXI4S.s                     s_axis_rx,
    AXI4S.m                     m_axis_tx,

    // Control
    metaIntf.s                  s_rdma_qp_interface,
    metaIntf.s                  s_rdma_conn_interface,

    // User command
    metaIntf.s                  s_rdma_sq,
    metaIntf.m                  m_rdma_ack,

    // Memory
    metaIntf.m                  m_rdma_rd_req,
    metaIntf.m                  m_rdma_wr_req,
    AXI4S.s                     s_axis_rdma_rd_req,
    AXI4S.s                     s_axis_rdma_rd_rsp,
    AXI4S.m                     m_axis_rdma_wr,

    // IP
    input  logic [31:0]         local_ip_address,

    // Memory
    metaIntf.m                  m_rdma_mem_rd_cmd,
    metaIntf.m                  m_rdma_mem_wr_cmd,
    metaIntf.s                  s_rdma_mem_rd_sts,
    metaIntf.s                  s_rdma_mem_wr_sts,
    AXI4S.s                     s_axis_rdma_mem_rd,
    AXI4S.m                     m_axis_rdma_mem_wr,

    // Debug
    output logic                ibv_rx_pkg_count_valid,
    output logic [31:0]         ibv_rx_pkg_count_data,    
    output logic                ibv_tx_pkg_count_valid,
    output logic [31:0]         ibv_tx_pkg_count_data,    
    output logic                crc_drop_pkg_count_valid,
    output logic [31:0]         crc_drop_pkg_count_data,
    output logic                psn_drop_pkg_count_valid,
    output logic [31:0]         psn_drop_pkg_count_data,
    output logic                retrans_count_valid,
    output logic [31:0]         retrans_count_data
);

///////////////////////////////////////////////////////////////////////////////
// BYPASS MODE vs ROCE MODE
///////////////////////////////////////////////////////////////////////////////

if (BYPASS_MODE == 1) begin : gen_bypass_mode

    // ========== BUFFER CONFIGURATION ==========
    // NUM_RX_SLOTS must match software allocation: qpair->local.size
    // 1024 slots × 4KB = 4MB circular buffer for high-throughput benchmarking
    localparam int NUM_RX_SLOTS = 1024;
    localparam int TOTAL_BUFFER_SIZE = NUM_RX_SLOTS * PMTU_BYTES;

    // ========== CAPTURE LOCAL CONTEXT FROM QP ==========
    logic [VADDR_BITS-1:0] local_buffer_base;
    logic [PID_BITS-1:0] local_pid;
    logic [DEST_BITS-1:0] local_vfid;

    always_ff @(posedge nclk) begin
        if (~nresetn) begin
            local_buffer_base <= '0;
            local_pid <= '0;
            local_vfid <= '0;
        end else begin
            if (s_rdma_qp_interface.valid && s_rdma_qp_interface.ready) begin
                local_buffer_base <= s_rdma_qp_interface.data.vaddr;
                // Extract PID/vFID from QP number
                local_vfid <= s_rdma_qp_interface.data.qp_num[PID_BITS +: DEST_BITS];
                local_pid <= s_rdma_qp_interface.data.qp_num[0 +: PID_BITS];
            end
        end
    end

    assign s_rdma_qp_interface.ready = 1'b1;
    assign s_rdma_conn_interface.ready = 1'b1;

    // ========== TX PATH - PURE PASSTHROUGH ==========
    // Direct passthrough from SQ to DMA read request
    assign m_rdma_rd_req.valid = s_rdma_sq.valid;
    assign s_rdma_sq.ready = m_rdma_rd_req.ready;

    assign m_rdma_rd_req.data.opcode = s_rdma_sq.data.req_1.opcode;
    assign m_rdma_rd_req.data.mode = RDMA_MODE_RAW;
    assign m_rdma_rd_req.data.rdma = 1'b1;
    assign m_rdma_rd_req.data.remote = 1'b0;
    assign m_rdma_rd_req.data.pid = s_rdma_sq.data.req_1.pid;
    assign m_rdma_rd_req.data.vfid = s_rdma_sq.data.req_1.vfid;
    assign m_rdma_rd_req.data.last = s_rdma_sq.data.req_1.last;
    assign m_rdma_rd_req.data.vaddr = s_rdma_sq.data.req_1.vaddr;
    assign m_rdma_rd_req.data.dest = s_rdma_sq.data.req_1.dest;
    assign m_rdma_rd_req.data.strm = s_rdma_sq.data.req_1.strm;
    assign m_rdma_rd_req.data.len = s_rdma_sq.data.req_1.len;
    assign m_rdma_rd_req.data.actv = 1'b0;
    assign m_rdma_rd_req.data.host = s_rdma_sq.data.req_1.host;
    assign m_rdma_rd_req.data.offs = s_rdma_sq.data.req_1.offs;
    assign m_rdma_rd_req.data.rsrvd = '0;

    // ========== TX PATH WITH FIFO BUFFER ==========
    //
    // CRITICAL FIX: Add FIFO buffer between DMA read response and network TX.
    // Without this buffer, packets larger than ~256B (4 beats) cause stalls because:
    //   1. The downstream path (register slices + ip_merger) has minimal buffering
    //   2. DMA produces data continuously, but network may have backpressure
    //   3. For 8+ beat packets (512B+), backpressure causes deadlock
    //
    // The FIFO decouples DMA production rate from network consumption rate.
    //
    axis_data_fifo_512_cc_tx tx_buffer_fifo (
        .s_axis_aresetn(nresetn),
        .s_axis_aclk(nclk),
        .s_axis_tvalid(s_axis_rdma_rd_rsp.tvalid),
        .s_axis_tready(s_axis_rdma_rd_rsp.tready),
        .s_axis_tdata(s_axis_rdma_rd_rsp.tdata),
        .s_axis_tkeep(s_axis_rdma_rd_rsp.tkeep),
        .s_axis_tlast(s_axis_rdma_rd_rsp.tlast),
        .m_axis_tvalid(m_axis_tx.tvalid),
        .m_axis_tready(m_axis_tx.tready),
        .m_axis_tdata(m_axis_tx.tdata),
        .m_axis_tkeep(m_axis_tx.tkeep),
        .m_axis_tlast(m_axis_tx.tlast)
    );

    // ========== RX PATH WITH LENGTH MEASUREMENT ==========
    //
    // CRITICAL INSIGHT: The downstream axis_mux_user_rq expects:
    //   1. Descriptor first (with correct len field)
    //   2. Then exactly (len-1)>>BEAT_LOG_BITS + 1 beats of data
    //
    // If we send len=4096 but only 64 bytes of data, it DEADLOCKS!
    //
    // Solution: Dual-FIFO architecture
    //   1. Data FIFO: stores packet data (existing axis_data_fifo_512_cc_tx)
    //   2. Length FIFO: stores measured packet lengths (small, 16 entries)
    //
    // Flow:
    //   Input side: As packets arrive, measure length and store in both FIFOs
    //   Output side: Wait for length available, send descriptor, then send data

    // ========== DATA FIFO ==========
    AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) rx_data_fifo_out();

    axis_data_fifo_512_cc_tx incoming_traffic_fifo (
        .s_axis_aresetn(nresetn),
        .s_axis_aclk(nclk),
        .s_axis_tvalid(s_axis_rx.tvalid),
        .s_axis_tready(s_axis_rx.tready),
        .s_axis_tdata(s_axis_rx.tdata),
        .s_axis_tkeep(s_axis_rx.tkeep),
        .s_axis_tlast(s_axis_rx.tlast),
        .m_axis_tvalid(rx_data_fifo_out.tvalid),
        .m_axis_tready(rx_data_fifo_out.tready),
        .m_axis_tdata(rx_data_fifo_out.tdata),
        .m_axis_tkeep(rx_data_fifo_out.tkeep),
        .m_axis_tlast(rx_data_fifo_out.tlast)
    );

    // ========== LENGTH MEASUREMENT ON INPUT ==========
    // Count bytes as packet enters data FIFO using popcount on tkeep
    logic [LEN_BITS-1:0] rx_len_accumulator;
    logic [LEN_BITS-1:0] rx_len_to_store;
    logic rx_len_store_valid;

    // Popcount for tkeep (AXI_NET_BITS/8 = 64 bytes for 512-bit bus)
    // Using a tree-based popcount for better synthesis
    function automatic logic [6:0] popcount64(input logic [63:0] tkeep);
        logic [6:0] result;
        logic [5:0] sum_l4 [3:0];  // 4 groups of 16 bits
        logic [4:0] sum_l3 [7:0];  // 8 groups of 8 bits
        integer i;

        // Level 1: count pairs
        for (i = 0; i < 8; i++) begin
            sum_l3[i] = tkeep[i*8 +: 1] + tkeep[i*8+1 +: 1] + tkeep[i*8+2 +: 1] + tkeep[i*8+3 +: 1] +
                        tkeep[i*8+4 +: 1] + tkeep[i*8+5 +: 1] + tkeep[i*8+6 +: 1] + tkeep[i*8+7 +: 1];
        end

        // Level 2: sum groups
        for (i = 0; i < 4; i++) begin
            sum_l4[i] = sum_l3[i*2] + sum_l3[i*2+1];
        end

        // Level 3: final sum
        result = sum_l4[0] + sum_l4[1] + sum_l4[2] + sum_l4[3];
        return result;
    endfunction

    always_ff @(posedge nclk) begin
        if (~nresetn) begin
            rx_len_accumulator <= '0;
            rx_len_to_store <= '0;
            rx_len_store_valid <= 1'b0;
        end else begin
            rx_len_store_valid <= 1'b0;  // Default: pulse signal

            if (s_axis_rx.tvalid && s_axis_rx.tready) begin
                if (s_axis_rx.tlast) begin
                    // End of packet - compute final length and signal store
                    rx_len_to_store <= rx_len_accumulator + popcount64(s_axis_rx.tkeep);
                    rx_len_store_valid <= 1'b1;
                    rx_len_accumulator <= '0;  // Reset for next packet
                end else begin
                    // Middle of packet - accumulate byte count
                    rx_len_accumulator <= rx_len_accumulator + popcount64(s_axis_rx.tkeep);
                end
            end
        end
    end

    // ========== LENGTH FIFO (Simple Circular Buffer) ==========
    localparam int LEN_FIFO_DEPTH = 16;
    localparam int LEN_FIFO_PTR_BITS = $clog2(LEN_FIFO_DEPTH);

    logic [LEN_BITS-1:0] len_fifo_mem [0:LEN_FIFO_DEPTH-1];
    logic [LEN_FIFO_PTR_BITS-1:0] len_fifo_wr_ptr;
    logic [LEN_FIFO_PTR_BITS-1:0] len_fifo_rd_ptr;
    logic [LEN_FIFO_PTR_BITS:0] len_fifo_count;  // Extra bit for full detection

    wire len_fifo_empty = (len_fifo_count == 0);
    wire len_fifo_full = (len_fifo_count == LEN_FIFO_DEPTH);
    wire [LEN_BITS-1:0] len_fifo_rd_data = len_fifo_mem[len_fifo_rd_ptr];

    logic len_fifo_pop;  // Read strobe from output FSM

    always_ff @(posedge nclk) begin
        if (~nresetn) begin
            len_fifo_wr_ptr <= '0;
            len_fifo_rd_ptr <= '0;
            len_fifo_count <= '0;
        end else begin
            // Simultaneous read and write
            case ({rx_len_store_valid && !len_fifo_full, len_fifo_pop && !len_fifo_empty})
                2'b10: begin  // Write only
                    len_fifo_mem[len_fifo_wr_ptr] <= rx_len_to_store;
                    len_fifo_wr_ptr <= len_fifo_wr_ptr + 1;
                    len_fifo_count <= len_fifo_count + 1;
                end
                2'b01: begin  // Read only
                    len_fifo_rd_ptr <= len_fifo_rd_ptr + 1;
                    len_fifo_count <= len_fifo_count - 1;
                end
                2'b11: begin  // Read and write simultaneously
                    len_fifo_mem[len_fifo_wr_ptr] <= rx_len_to_store;
                    len_fifo_wr_ptr <= len_fifo_wr_ptr + 1;
                    len_fifo_rd_ptr <= len_fifo_rd_ptr + 1;
                    // Count stays the same
                end
                default: ;  // No operation
            endcase
        end
    end

    // ========== OUTPUT STATE MACHINE ==========
    typedef enum logic[1:0] {
        RX_WAIT_PKT,    // Wait for complete packet (length available in FIFO)
        RX_SEND_DESC,   // Send descriptor to downstream
        RX_SEND_DATA    // Send data beats from data FIFO
    } rx_state_t;

    rx_state_t rx_state;
    logic [VADDR_BITS-1:0] rx_buffer_offset;
    logic [LEN_BITS-1:0] rx_current_len;

    always_ff @(posedge nclk) begin
        if (~nresetn) begin
            rx_state <= RX_WAIT_PKT;
            rx_buffer_offset <= '0;
            rx_current_len <= '0;
        end else begin
            case (rx_state)
                RX_WAIT_PKT: begin
                    // Wait until length FIFO has entry (complete packet available)
                    if (!len_fifo_empty) begin
                        rx_current_len <= len_fifo_rd_data;
                        rx_state <= RX_SEND_DESC;
                    end
                end

                RX_SEND_DESC: begin
                    // Send descriptor, wait for downstream acceptance
                    if (m_rdma_wr_req.valid && m_rdma_wr_req.ready) begin
                        rx_state <= RX_SEND_DATA;
                    end
                end

                RX_SEND_DATA: begin
                    // Stream data until tlast
                    if (rx_data_fifo_out.tvalid && rx_data_fifo_out.tready && rx_data_fifo_out.tlast) begin
                        // Packet complete - advance circular buffer offset
                        if (rx_buffer_offset + PMTU_BYTES >= TOTAL_BUFFER_SIZE) begin
                            rx_buffer_offset <= '0;
                        end else begin
                            rx_buffer_offset <= rx_buffer_offset + PMTU_BYTES;
                        end
                        rx_state <= RX_WAIT_PKT;
                    end
                end

                default: rx_state <= RX_WAIT_PKT;
            endcase
        end
    end

    // Pop length FIFO when transitioning from WAIT_PKT to SEND_DESC
    assign len_fifo_pop = (rx_state == RX_WAIT_PKT) && !len_fifo_empty;

    // ========== RX DESCRIPTOR GENERATION ==========
    assign m_rdma_wr_req.valid = (rx_state == RX_SEND_DESC);

    assign m_rdma_wr_req.data.opcode = RC_RDMA_WRITE_ONLY;
    assign m_rdma_wr_req.data.mode = RDMA_MODE_RAW;
    assign m_rdma_wr_req.data.rdma = 1'b1;
    assign m_rdma_wr_req.data.remote = 1'b0;
    assign m_rdma_wr_req.data.pid = local_pid;
    assign m_rdma_wr_req.data.vfid = local_vfid;
    assign m_rdma_wr_req.data.vaddr = local_buffer_base + rx_buffer_offset;
    assign m_rdma_wr_req.data.last = 1'b1;
    assign m_rdma_wr_req.data.dest = 4'b0;  // Not used in bypass mode - data routed directly to axis_rdma_recv_int[1]
    assign m_rdma_wr_req.data.strm = STRM_HOST;
    assign m_rdma_wr_req.data.len = rx_current_len;  // Measured packet length
    assign m_rdma_wr_req.data.actv = 1'b0;
    assign m_rdma_wr_req.data.host = 1'b1;
    assign m_rdma_wr_req.data.offs = 6'b0;
    assign m_rdma_wr_req.data.rsrvd = '0;

    // ========== RX DATA PATH ==========
    // Only forward data when in RX_SEND_DATA state
    assign m_axis_rdma_wr.tvalid = rx_data_fifo_out.tvalid && (rx_state == RX_SEND_DATA);
    assign m_axis_rdma_wr.tdata = rx_data_fifo_out.tdata;
    assign m_axis_rdma_wr.tkeep = rx_data_fifo_out.tkeep;
    assign m_axis_rdma_wr.tlast = rx_data_fifo_out.tlast;

    // Only drain data FIFO in RX_SEND_DATA state
    assign rx_data_fifo_out.tready = (rx_state == RX_SEND_DATA) && m_axis_rdma_wr.tready;

    // ========== TIE OFF UNUSED ==========
    assign m_rdma_ack.valid = 1'b0;
    assign m_rdma_ack.data = '0;
    
    // ========== STATISTICS COUNTERS ==========
    logic [31:0] tx_packet_count;
    logic [31:0] rx_packet_count;
    logic [31:0] rx_byte_count;
    logic [31:0] tx_byte_count;
    
    always_ff @(posedge nclk) begin
        if (~nresetn) begin
            tx_packet_count <= '0;
            rx_packet_count <= '0;
            rx_byte_count <= '0;
            tx_byte_count <= '0;
        end else begin
            // Count TX packets (data leaving to network)
            if (m_axis_tx.tvalid && m_axis_tx.tready) begin
                tx_byte_count <= tx_byte_count + $countones(m_axis_tx.tkeep);
                if (m_axis_tx.tlast) begin
                    tx_packet_count <= tx_packet_count + 1;
                end
            end
            
            // Count RX packets (data written to memory)
            if (m_axis_rdma_wr.tvalid && m_axis_rdma_wr.tready) begin
                rx_byte_count <= rx_byte_count + $countones(m_axis_rdma_wr.tkeep);
                if (m_axis_rdma_wr.tlast) begin
                    rx_packet_count <= rx_packet_count + 1;
                end
            end
        end
    end
    
    assign ibv_rx_pkg_count_valid = 1'b1;
    assign ibv_rx_pkg_count_data = rx_packet_count;
    assign ibv_tx_pkg_count_valid = 1'b1;
    assign ibv_tx_pkg_count_data = tx_packet_count;
    
    // ========== TIE OFF UNUSED INTERFACES ==========
    assign s_axis_rdma_rd_req.tready = 1'b0;
    assign m_rdma_mem_rd_cmd.valid = 1'b0;
    assign m_rdma_mem_rd_cmd.data = '0;
    assign m_rdma_mem_wr_cmd.valid = 1'b0;
    assign m_rdma_mem_wr_cmd.data = '0;
    assign s_rdma_mem_rd_sts.ready = 1'b1;
    assign s_rdma_mem_wr_sts.ready = 1'b1;
    assign s_axis_rdma_mem_rd.tready = 1'b0;
    assign m_axis_rdma_mem_wr.tvalid = 1'b0;
    assign m_axis_rdma_mem_wr.tdata = '0;
    assign m_axis_rdma_mem_wr.tkeep = '0;
    assign m_axis_rdma_mem_wr.tlast = 1'b0;
    assign crc_drop_pkg_count_valid = 1'b0;
    assign crc_drop_pkg_count_data = '0;
    assign psn_drop_pkg_count_valid = 1'b0;
    assign psn_drop_pkg_count_data = '0;
    assign retrans_count_valid = 1'b0;
    assign retrans_count_data = '0;

end else begin : gen_roce_mode

    /////////////////////////////////////////////////////////////////////////
    // FULL ROCE MODE
    // Complete RoCE protocol processing
    /////////////////////////////////////////////////////////////////////////

    //
    // SQ
    //
    
    metaIntf #(.STYPE(dreq_t)) rdma_sq ();
    logic [RDMA_REQ_BITS-1:0] rdma_sq_data;
    
    always_comb begin
      rdma_sq_data                                                      = 0;
      
      rdma_sq_data[0+:RDMA_OPCODE_BITS]                                 = rdma_sq.data.req_1.opcode;
      rdma_sq_data[32+:RDMA_QPN_BITS]                                   = {{RDMA_QPN_BITS-DEST_BITS-PID_BITS{1'b0}}, rdma_sq.data.req_1.vfid, rdma_sq.data.req_1.pid};
      rdma_sq_data[32+RDMA_QPN_BITS+0+:1]                               = rdma_sq.data.req_1.host;
      rdma_sq_data[32+RDMA_QPN_BITS+1+:1]                               = rdma_sq.data.req_1.last;
      rdma_sq_data[32+RDMA_QPN_BITS+2+:OFFS_BITS]                       = rdma_sq.data.req_1.offs;
      rdma_sq_data[32+RDMA_QPN_BITS+2+OFFS_BITS+:RDMA_VADDR_BITS]       = 
        {16'h0000, rdma_sq.data.req_1.vaddr};
      rdma_sq_data[32+RDMA_QPN_BITS+2+OFFS_BITS+RDMA_VADDR_BITS+:RDMA_VADDR_BITS] = 
        {10'h000, rdma_sq.data.req_2.strm, rdma_sq.data.req_1.dest, rdma_sq.data.req_2.vaddr};
      rdma_sq_data[32+RDMA_QPN_BITS+2+OFFS_BITS+2*RDMA_VADDR_BITS+:RDMA_LEN_BITS] = rdma_sq.data.req_1.len;
      rdma_sq_data[32+RDMA_QPN_BITS+2+OFFS_BITS+2*RDMA_VADDR_BITS+RDMA_LEN_BITS+:RDMA_IMM_BITS] = {rdma_sq.data.req_2.offs[3:0], rdma_sq.data.req_2.len};
    end
    
    //
    // FC and CQ
    //
    
    metaIntf #(.STYPE(dack_t)) rdma_ack ();
    logic [RDMA_ACK_BITS-1:0] ack_meta_data;
    
    assign rdma_ack.data.ack.opcode = ack_meta_data[0+:OPCODE_BITS];
    assign rdma_ack.data.ack.remote = 1'b1;
    assign rdma_ack.data.ack.pid  = ack_meta_data[32+:PID_BITS];
    assign rdma_ack.data.ack.vfid = ack_meta_data[32+PID_BITS+:DEST_BITS];
    assign rdma_ack.data.ack.host = ack_meta_data[32+RDMA_QPN_BITS+:1];
    assign rdma_ack.data.ack.dest = ack_meta_data[32+RDMA_QPN_BITS+1+:DEST_BITS];
    assign rdma_ack.data.ack.strm = ack_meta_data[32+RDMA_QPN_BITS+1+DEST_BITS+:STRM_BITS];
    assign rdma_ack.data.ack.rsrvd = 0;
    assign rdma_ack.data.last = ack_meta_data[32+RDMA_QPN_BITS+1+DEST_BITS+STRM_BITS+:1];
    
    rdma_flow inst_rdma_flow (
        .aclk(nclk),
        .aresetn(nresetn),
        .s_req(s_rdma_sq),
        .m_req(rdma_sq),
        .s_ack(rdma_ack),
        .m_ack(m_rdma_ack)
    );
    
    ///////////////////////////////////////////////////////////////////////////
    //
    // TX-path: Read Request Cutter + ICRC
    //
    ///////////////////////////////////////////////////////////////////////////
    
    AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) roce_to_icrc();
    AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) trimmer_to_icrc();
    
    read_request_trimmer inst_read_request_trimmer (
        .nclk(nclk), 
        .nresetn(nresetn), 
        .input_stream(roce_to_icrc), 
        .output_stream(trimmer_to_icrc)
    );
    
    icrc inst_icrc (
        .m_axis_rx(trimmer_to_icrc), 
        .m_axis_tx(m_axis_tx), 
        .nclk(nclk), 
        .nresetn(nresetn)
    );
    
    ///////////////////////////////////////////////////////////////////////////
    //
    // RX-path: FIFO + ACK Gap Enforcer
    //
    ///////////////////////////////////////////////////////////////////////////
    
    AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) fifo_to_gap_enforcer();
    AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) gap_enforcer_to_hls();
    
    axis_data_fifo_512_cc_tx incoming_traffic_fifo (
      .s_axis_aresetn(nresetn),
      .s_axis_aclk(nclk),
      .s_axis_tvalid(s_axis_rx.tvalid),
      .s_axis_tready(s_axis_rx.tready),
      .s_axis_tdata(s_axis_rx.tdata),
      .s_axis_tkeep(s_axis_rx.tkeep),
      .s_axis_tlast(s_axis_rx.tlast),
      .m_axis_tvalid(fifo_to_gap_enforcer.tvalid),
      .m_axis_tready(fifo_to_gap_enforcer.tready),
      .m_axis_tdata(fifo_to_gap_enforcer.tdata),
      .m_axis_tkeep(fifo_to_gap_enforcer.tkeep),
      .m_axis_tlast(fifo_to_gap_enforcer.tlast)
    );
    
    ack_gap_enforcer inst_ack_gap_enforcer (
        .nclk(nclk),
        .nresetn(nresetn),
        .input_stream(fifo_to_gap_enforcer),
        .output_stream(gap_enforcer_to_hls)
    );
    
    ///////////////////////////////////////////////////////////////////////////
    //
    // Retransmission Buffer
    //
    ///////////////////////////////////////////////////////////////////////////
    
    metaIntf #(.STYPE(req_t)) rdma_rd_req ();
    metaIntf #(.STYPE(req_t)) rdma_wr_req ();
    logic [RDMA_BASE_REQ_BITS-1:0] rd_cmd_data;
    logic [RDMA_BASE_REQ_BITS-1:0] wr_cmd_data;
    
    AXI4S #(.AXI4S_DATA_BITS(AXI_NET_BITS)) axis_rdma_rd ();
    
    // RD
    assign rdma_rd_req.data.opcode            = rd_cmd_data[0+:OPCODE_BITS];
    assign rdma_rd_req.data.mode              = RDMA_MODE_RAW;
    assign rdma_rd_req.data.rdma              = 1'b1;
    assign rdma_rd_req.data.remote            = 1'b0;
    
    assign rdma_rd_req.data.pid               = rd_cmd_data[32+:PID_BITS];
    assign rdma_rd_req.data.vfid              = rd_cmd_data[32+PID_BITS+:DEST_BITS];
    
    assign rdma_rd_req.data.last              = rd_cmd_data[32+RDMA_QPN_BITS+0+:1];
    assign rdma_rd_req.data.vaddr             = rd_cmd_data[32+RDMA_QPN_BITS+1+:VADDR_BITS];
    assign rdma_rd_req.data.dest              = rd_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+:DEST_BITS];
    assign rdma_rd_req.data.strm              = rd_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+DEST_BITS+:STRM_BITS];
    assign rdma_rd_req.data.len               = rd_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+DEST_BITS+STRM_BITS+:LEN_BITS];
    assign rdma_rd_req.data.actv              = rd_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+DEST_BITS+STRM_BITS+LEN_BITS+0+:1];
    assign rdma_rd_req.data.host              = rd_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+DEST_BITS+STRM_BITS+LEN_BITS+1+:1];
    assign rdma_rd_req.data.offs              = rd_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+DEST_BITS+STRM_BITS+LEN_BITS+2+:OFFS_BITS];
    
    // WR
    assign rdma_wr_req.data.opcode            = wr_cmd_data[0+:OPCODE_BITS];
    assign rdma_wr_req.data.mode              = RDMA_MODE_RAW;
    assign rdma_wr_req.data.rdma              = 1'b1;
    assign rdma_wr_req.data.remote            = 1'b0;
    
    assign rdma_wr_req.data.pid               = wr_cmd_data[32+:PID_BITS];
    assign rdma_wr_req.data.vfid              = wr_cmd_data[32+PID_BITS+:DEST_BITS];
    
    assign rdma_wr_req.data.last              = wr_cmd_data[32+RDMA_QPN_BITS+0+:1];
    assign rdma_wr_req.data.vaddr             = wr_cmd_data[32+RDMA_QPN_BITS+1+:VADDR_BITS];
    assign rdma_wr_req.data.dest              = wr_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+:DEST_BITS];
    assign rdma_wr_req.data.strm              = wr_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+DEST_BITS+:STRM_BITS];
    assign rdma_wr_req.data.len               = wr_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+DEST_BITS+STRM_BITS+:LEN_BITS];
    assign rdma_wr_req.data.actv              = wr_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+DEST_BITS+STRM_BITS+LEN_BITS+0+:1];
    assign rdma_wr_req.data.host              = wr_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+DEST_BITS+STRM_BITS+LEN_BITS+1+:1];
    assign rdma_wr_req.data.offs              = wr_cmd_data[32+RDMA_QPN_BITS+1+VADDR_BITS+DEST_BITS+STRM_BITS+LEN_BITS+2+:OFFS_BITS];
    
    rdma_mux_retrans inst_mux_retrans (
      .aclk(nclk),
      .aresetn(nresetn),
    
      .s_req_net(rdma_rd_req),
      .m_req_user(m_rdma_rd_req),
    
      .s_axis_user_req(s_axis_rdma_rd_req),
      .s_axis_user_rsp(s_axis_rdma_rd_rsp),
      .m_axis_net(axis_rdma_rd),
      
      .m_req_ddr_rd(m_rdma_mem_rd_cmd),
      .m_req_ddr_wr(m_rdma_mem_wr_cmd),
      .s_axis_ddr(s_axis_rdma_mem_rd),
      .m_axis_ddr(m_axis_rdma_mem_wr)
    );  
    
    assign s_rdma_mem_rd_sts.ready = 1'b1;
    assign s_rdma_mem_wr_sts.ready = 1'b1;
    
    assign m_rdma_wr_req.valid = rdma_wr_req.valid;
    assign m_rdma_wr_req.data = rdma_wr_req.data;
    assign rdma_wr_req.ready = m_rdma_wr_req.ready;
    
    ///////////////////////////////////////////////////////////////////////////
    //
    // RoCE HLS IP
    //
    ///////////////////////////////////////////////////////////////////////////
    
    metaIntf #(.STYPE(logic[103:0])) m_axis_dbg_0 ();
    metaIntf #(.STYPE(logic[103:0])) m_axis_dbg_1 ();
    metaIntf #(.STYPE(logic[103:0])) m_axis_dbg_2 ();
    assign m_axis_dbg_0.ready = 1'b1;
    assign m_axis_dbg_1.ready = 1'b1;
    assign m_axis_dbg_2.ready = 1'b1;
    
    rocev2_ip rocev2_inst(
        .ap_clk(nclk),
        .ap_rst_n(nresetn),
        
    `ifdef VITIS_HLS
    
        // Debug
    `ifdef DBG_IBV
        .m_axis_dbg_0_TVALID(m_axis_dbg_0.valid),
        .m_axis_dbg_0_TREADY(m_axis_dbg_0.ready),
        .m_axis_dbg_0_TDATA(m_axis_dbg_0.data),
        
        .m_axis_dbg_1_TVALID(m_axis_dbg_1.valid),
        .m_axis_dbg_1_TREADY(m_axis_dbg_1.ready),
        .m_axis_dbg_1_TDATA(m_axis_dbg_1.data),
        
        .m_axis_dbg_2_TVALID(m_axis_dbg_2.valid),
        .m_axis_dbg_2_TREADY(m_axis_dbg_2.ready),
        .m_axis_dbg_2_TDATA(m_axis_dbg_2.data),
    `endif
    
        // RX
        .s_axis_rx_data_TVALID(gap_enforcer_to_hls.tvalid),
        .s_axis_rx_data_TREADY(gap_enforcer_to_hls.tready),
        .s_axis_rx_data_TDATA(gap_enforcer_to_hls.tdata),
        .s_axis_rx_data_TKEEP(gap_enforcer_to_hls.tkeep),
        .s_axis_rx_data_TLAST(gap_enforcer_to_hls.tlast),
        
        // TX
        .m_axis_tx_data_TVALID(roce_to_icrc.tvalid),
        .m_axis_tx_data_TREADY(roce_to_icrc.tready),
        .m_axis_tx_data_TDATA(roce_to_icrc.tdata),
        .m_axis_tx_data_TKEEP(roce_to_icrc.tkeep),
        .m_axis_tx_data_TLAST(roce_to_icrc.tlast),
        
        // User commands    
        .s_axis_sq_meta_TVALID(rdma_sq.valid),
        .s_axis_sq_meta_TREADY(rdma_sq.ready),
        .s_axis_sq_meta_TDATA(rdma_sq_data), 
        
        // Memory
        .m_axis_mem_write_cmd_TVALID(rdma_wr_req.valid),
        .m_axis_mem_write_cmd_TREADY(rdma_wr_req.ready),
        .m_axis_mem_write_cmd_TDATA(wr_cmd_data),
        .m_axis_mem_read_cmd_TVALID(rdma_rd_req.valid),
        .m_axis_mem_read_cmd_TREADY(rdma_rd_req.ready),
        .m_axis_mem_read_cmd_TDATA(rd_cmd_data),
        .m_axis_mem_write_data_TVALID(m_axis_rdma_wr.tvalid),
        .m_axis_mem_write_data_TREADY(m_axis_rdma_wr.tready),
        .m_axis_mem_write_data_TDATA(m_axis_rdma_wr.tdata),
        .m_axis_mem_write_data_TKEEP(m_axis_rdma_wr.tkeep),
        .m_axis_mem_write_data_TLAST(m_axis_rdma_wr.tlast),
        .s_axis_mem_read_data_TVALID(axis_rdma_rd.tvalid),
        .s_axis_mem_read_data_TREADY(axis_rdma_rd.tready),
        .s_axis_mem_read_data_TDATA(axis_rdma_rd.tdata),
        .s_axis_mem_read_data_TKEEP(axis_rdma_rd.tkeep),
        .s_axis_mem_read_data_TLAST(axis_rdma_rd.tlast),
    
        // QP intf
        .s_axis_qp_interface_TVALID(s_rdma_qp_interface.valid),
        .s_axis_qp_interface_TREADY(s_rdma_qp_interface.ready),
        .s_axis_qp_interface_TDATA(s_rdma_qp_interface.data),
        .s_axis_qp_conn_interface_TVALID(s_rdma_conn_interface.valid),
        .s_axis_qp_conn_interface_TREADY(s_rdma_conn_interface.ready),
        .s_axis_qp_conn_interface_TDATA(s_rdma_conn_interface.data),
    
        // ACK
        .m_axis_rx_ack_meta_TVALID(rdma_ack.valid),
        .m_axis_rx_ack_meta_TREADY(rdma_ack.ready),
        .m_axis_rx_ack_meta_TDATA(ack_meta_data),
    
        // IP
        .local_ip_address({local_ip_address,local_ip_address,local_ip_address,local_ip_address}),
        
        // DBG
        .regIbvCountRx(ibv_rx_pkg_count_data),
        .regIbvCountRx_ap_vld(ibv_rx_pkg_count_valid),
        .regIbvCountTx(ibv_tx_pkg_count_data),
        .regIbvCountTx_ap_vld(ibv_tx_pkg_count_valid),
        .regCrcDropPkgCount(crc_drop_pkg_count_data),
        .regCrcDropPkgCount_ap_vld(crc_drop_pkg_count_valid),
        .regInvalidPsnDropCount(psn_drop_pkg_count_data),
        .regInvalidPsnDropCount_ap_vld(psn_drop_pkg_count_valid),
        .regRetransCount(retrans_count_data),
        .regRetransCount_ap_vld(retrans_count_valid)
        
    `else
    
        // Debug
    `ifdef DBG_IBV
    `endif
    
        // RX
        .s_axis_rx_data_TVALID(gap_enforcer_to_hls.tvalid),
        .s_axis_rx_data_TREADY(gap_enforcer_to_hls.tready),
        .s_axis_rx_data_TDATA(gap_enforcer_to_hls.tdata),
        .s_axis_rx_data_TKEEP(gap_enforcer_to_hls.tkeep),
        .s_axis_rx_data_TLAST(gap_enforcer_to_hls.tlast),
        
        // TX
        .m_axis_tx_data_TVALID(roce_to_icrc.tvalid),
        .m_axis_tx_data_TREADY(roce_to_icrc.tready),
        .m_axis_tx_data_TDATA(roce_to_icrc.tdata),
        .m_axis_tx_data_TKEEP(roce_to_icrc.tkeep),
        .m_axis_tx_data_TLAST(roce_to_icrc.tlast),
        
        // User commands    
        .s_axis_sq_meta_V_TVALID(rdma_sq.valid),
        .s_axis_sq_meta_V_TREADY(rdma_sq.ready),
        .s_axis_sq_meta_V_TDATA(rdma_sq_data), 
        
        // Memory
        .m_axis_mem_write_cmd_V_TVALID(rdma_wr_req.valid),
        .m_axis_mem_write_cmd_V_TREADY(rdma_wr_req.ready),
        .m_axis_mem_write_cmd_V_TDATA(wr_cmd_data),
        .m_axis_mem_read_cmd_V_TVALID(rdma_rd_req.valid),
        .m_axis_mem_read_cmd_V_TREADY(rdma_rd_req.ready),
        .m_axis_mem_read_cmd_V_TDATA(rd_cmd_data),
        .m_axis_mem_write_data_TVALID(m_axis_rdma_wr.tvalid),
        .m_axis_mem_write_data_TREADY(m_axis_rdma_wr.tready),
        .m_axis_mem_write_data_TDATA(m_axis_rdma_wr.tdata),
        .m_axis_mem_write_data_TKEEP(m_axis_rdma_wr.tkeep),
        .m_axis_mem_write_data_TLAST(m_axis_rdma_wr.tlast),
        .s_axis_mem_read_data_TVALID(axis_rdma_rd.tvalid),
        .s_axis_mem_read_data_TREADY(axis_rdma_rd.tready),
        .s_axis_mem_read_data_TDATA(axis_rdma_rd.tdata),
        .s_axis_mem_read_data_TKEEP(axis_rdma_rd.tkeep),
        .s_axis_mem_read_data_TLAST(axis_rdma_rd.tlast),
    
        // QP intf
        .s_axis_qp_interface_V_TVALID(s_rdma_qp_interface.valid),
        .s_axis_qp_interface_V_TREADY(s_rdma_qp_interface.ready),
        .s_axis_qp_interface_V_TDATA(s_rdma_qp_interface.data),
        .s_axis_qp_conn_interface_V_TVALID(s_rdma_conn_interface.valid),
        .s_axis_qp_conn_interface_V_TREADY(s_rdma_conn_interface.ready),
        .s_axis_qp_conn_interface_V_TDATA(s_rdma_conn_interface.data),
    
        // ACK
        .m_axis_rx_ack_meta_V_TVALID(rdma_ack.valid),
        .m_axis_rx_ack_meta_V_TREADY(rdma_ack.ready),
        .m_axis_rx_ack_meta_V_TDATA(ack_meta_data),
    
        // IP
        .local_ip_address_V({local_ip_address,local_ip_address,local_ip_address,local_ip_address}),
    
        .regIbvCountRx_V(ibv_rx_pkg_count_data),
        .regIbvCountRx_V_ap_vld(ibv_rx_pkg_count_valid),
        .regIbvCountTx_V(ibv_tx_pkg_count_data),
        .regIbvCountTx_V_ap_vld(ibv_tx_pkg_count_valid),
        .regCrcDropPkgCount_V(crc_drop_pkg_count_data),
        .regCrcDropPkgCount_V_ap_vld(crc_drop_pkg_count_valid),
        .regInvalidPsnDropCount_V(psn_drop_pkg_count_data),
        .regInvalidPsnDropCount_V_ap_vld(psn_drop_pkg_count_valid),
        .regRetransCount_V(retrans_count_data),
        .regRetransCount_V_ap_vld(retrans_count_valid)
    
    `endif
    );

end // gen_roce_mode

endmodule
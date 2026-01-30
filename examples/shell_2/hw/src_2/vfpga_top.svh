// vFPGA 1 (src_2): P2P RX → RDMA TX loopback
// Receives P2P data from vFPGA 0, forwards it to network via RDMA TX
import lynxTypes::*;

logic [31:0] user_recv_counter;
logic [31:0] user_send_counter;

assign o_user_recv_counter = user_recv_counter;
assign o_user_send_counter = user_send_counter;

// Count packets on P2P RX (incoming from vFPGA 0) and RDMA TX (outgoing to network)
always_ff @(posedge aclk) begin
    if (~aresetn) begin
        user_recv_counter <= '0;
        user_send_counter <= '0;
    end else begin
        // Count P2P RX packets (axis_p2p_recv = P2P from another vFPGA)
        if (axis_p2p_recv.tvalid && axis_p2p_recv.tready && axis_p2p_recv.tlast)
            user_recv_counter <= user_recv_counter + 1;
        // Count RDMA TX packets (axis_rreq_send = RDMA requester send = data to network)
        if (axis_rreq_send[0].tvalid && axis_rreq_send[0].tready && axis_rreq_send[0].tlast)
            user_send_counter <= user_send_counter + 1;
    end
end

// Control path: forward RDMA requests to host DMA (for completions)
always_comb begin
    sq_wr.valid = rq_wr.valid;
    rq_wr.ready = sq_wr.ready;
    sq_wr.data = rq_wr.data;
    sq_wr.data.strm = STRM_HOST;
    sq_wr.data.dest = 0;

    sq_rd.valid = rq_rd.valid;
    rq_rd.ready = sq_rd.ready;
    sq_rd.data = rq_rd.data;
    sq_rd.data.strm = STRM_HOST;
    sq_rd.data.dest = 0;
end

// Data path: P2P RX → RDMA TX
// axis_p2p_recv = P2P receive (data from another vFPGA via vIO Switch)
// axis_rreq_send[0] = RDMA requester send (data going to network for RDMA read response)
// Convert AXI4SR to AXI4SR (forward the data)
assign axis_rreq_send[0].tvalid = axis_p2p_recv.tvalid;
assign axis_rreq_send[0].tdata  = axis_p2p_recv.tdata;
assign axis_rreq_send[0].tkeep  = axis_p2p_recv.tkeep;
assign axis_rreq_send[0].tlast  = axis_p2p_recv.tlast;
assign axis_rreq_send[0].tid    = axis_p2p_recv.tid;  // Preserve sender vFPGA ID (could be used for tracking)
assign axis_p2p_recv.tready = axis_rreq_send[0].tready;

// Tie-off unused interfaces
always_comb cq_wr.tie_off_s();
always_comb cq_rd.tie_off_s();
always_comb axi_ctrl.tie_off_s();
always_comb notify.tie_off_m();

// Tie-off unused host streams
always_comb axis_host_recv[0].tie_off_s();
always_comb axis_host_send[0].tie_off_m();
always_comb axis_host_recv[1].tie_off_s();
always_comb axis_host_send[1].tie_off_m();

// Tie-off unused RDMA requester RX (we only use TX)
always_comb axis_rreq_recv[0].tie_off_s();

// Tie-off unused RDMA responder (we only use requester TX)
always_comb axis_rrsp_recv[0].tie_off_s();
always_comb axis_rrsp_send[0].tie_off_m();

// Tie-off P2P TX (we only use RX)
always_comb axis_p2p_send.tie_off_m();

ila_vfpga_top inst_ila_vfpga_top (
    .clk(aclk),
    .probe0(rq_rd.valid),
    .probe1(rq_rd.ready),
    .probe2(rq_wr.valid),
    .probe3(rq_wr.ready),
    .probe4(sq_rd.valid),
    .probe5(sq_rd.ready),
    .probe6(sq_wr.valid),
    .probe7(sq_wr.ready),
    .probe8(axis_p2p_recv.tvalid),
    .probe9(axis_p2p_recv.tready),
    .probe10(axis_p2p_recv.tlast),
    .probe11(axis_p2p_recv.tdata[63:0]),
    .probe12(axis_rreq_send[0].tvalid),
    .probe13(axis_rreq_send[0].tready),
    .probe14(axis_rreq_send[0].tlast),
    .probe15(axis_rreq_send[0].tdata[63:0])
);

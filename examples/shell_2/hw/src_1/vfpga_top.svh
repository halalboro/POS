// vFPGA 0 (src_1): RDMA RX → P2P TX loopback
// Receives RDMA write data from network, forwards it to vFPGA 1 via P2P
import lynxTypes::*;

logic [31:0] user_recv_counter;
logic [31:0] user_send_counter;

assign o_user_recv_counter = user_recv_counter;
assign o_user_send_counter = user_send_counter;

// Count packets on RDMA RX (incoming from network) and P2P TX (outgoing to vFPGA 1)
always_ff @(posedge aclk) begin
    if (~aresetn) begin
        user_recv_counter <= '0;
        user_send_counter <= '0;
    end else begin
        // Count RDMA RX packets (axis_rrsp_recv = RDMA responder receive = data from network)
        if (axis_rrsp_recv[0].tvalid && axis_rrsp_recv[0].tready && axis_rrsp_recv[0].tlast)
            user_recv_counter <= user_recv_counter + 1;
        // Count P2P TX packets (axis_p2p_send = P2P to another vFPGA)
        if (axis_p2p_send.tvalid && axis_p2p_send.tready && axis_p2p_send.tlast)
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

// Data path: RDMA RX → P2P TX
// axis_rrsp_recv[0] = RDMA responder receive (data from remote RDMA write to us)
// axis_p2p_send = P2P send (data to another vFPGA via vIO Switch)
// Convert AXI4SR to AXI4SR (just forward, P2P uses tid for sender ID)
assign axis_p2p_send.tvalid = axis_rrsp_recv[0].tvalid;
assign axis_p2p_send.tdata  = axis_rrsp_recv[0].tdata;
assign axis_p2p_send.tkeep  = axis_rrsp_recv[0].tkeep;
assign axis_p2p_send.tlast  = axis_rrsp_recv[0].tlast;
assign axis_p2p_send.tid    = '0;  // Will be overwritten by gateway_send with our vFPGA ID
assign axis_rrsp_recv[0].tready = axis_p2p_send.tready;

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

// Tie-off unused RDMA requester (we only use responder RX)
always_comb axis_rreq_recv[0].tie_off_s();
always_comb axis_rreq_send[0].tie_off_m();

// Tie-off RDMA responder TX (we only use RX)
always_comb axis_rrsp_send[0].tie_off_m();

// Tie-off P2P RX (we only use TX)
always_comb axis_p2p_recv.tie_off_s();

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
    .probe8(axis_rrsp_recv[0].tvalid),
    .probe9(axis_rrsp_recv[0].tready),
    .probe10(axis_rrsp_recv[0].tlast),
    .probe11(axis_rrsp_recv[0].tdata[63:0]),
    .probe12(axis_p2p_send.tvalid),
    .probe13(axis_p2p_send.tready),
    .probe14(axis_p2p_send.tlast),
    .probe15(axis_p2p_send.tdata[63:0])
);

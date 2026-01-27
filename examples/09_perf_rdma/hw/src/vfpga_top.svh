// Debug counters
logic [31:0] user_recv_counter;
logic [31:0] user_send_counter;

always_ff @(posedge aclk) begin
    if (~aresetn) begin
        user_recv_counter <= '0;
        user_send_counter <= '0;
    end else begin
        // Counter: packets entering user logic from network
        if (axis_rrsp_recv[1].tvalid && axis_rrsp_recv[1].tready && axis_rrsp_recv[1].tlast) begin
            user_recv_counter <= user_recv_counter + 1;
        end
        // Counter: packets leaving user logic to DMA
        if (axis_host_send[0].tvalid && axis_host_send[0].tready && axis_host_send[0].tlast) begin
            user_send_counter <= user_send_counter + 1;
        end
    end
end

// Assign counters to output ports
assign o_user_recv_counter = user_recv_counter;
assign o_user_send_counter = user_send_counter;

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

// TX path: Host → Network (READ requests - requester side)
// axis_host_recv[0] = data from host memory to send as RDMA READ request payload
// axis_rreq_send[0] = RDMA requester send (to network)
`AXISR_ASSIGN(axis_host_recv[0], axis_rreq_send[0])

// TX path: READ response data from network (requester receives response)
// axis_rreq_recv[0] = RDMA requester receive (from network)
// For READ operations, this would go to host - but we're doing WRITE, tie off
always_comb axis_rreq_recv[0].tie_off_s();

// RX path: Network → Host (WRITE receives - responder side)
// axis_rrsp_recv[1] = RDMA WRITE data arriving from network
// axis_host_send[0] = DMA write to host memory
`AXISR_ASSIGN(axis_rrsp_recv[1], axis_host_send[0])

// Tie off unused responder send (would be for READ response data)
// axis_host_recv[1] = data from host to send as response
// axis_rrsp_send[0] = RDMA responder send (to network)
`AXISR_ASSIGN(axis_host_recv[1], axis_rrsp_send[0])  

always_comb cq_wr.tie_off_s();
always_comb cq_rd.tie_off_s();
always_comb axi_ctrl.tie_off_s();
always_comb notify.tie_off_m();

/*
// ILA
ila_perf_rdma inst_ila_perf_rdma (
    .clk(aclk),
    .probe0(axis_host_recv[0].tvalid),
    .probe1(axis_host_recv[0].tready),
    .probe2(axis_host_recv[0].tlast),
    .probe3(axis_host_recv[1].tvalid),
    .probe4(axis_host_recv[1].tready),
    .probe5(axis_host_recv[1].tlast),
    .probe6(axis_host_send[0].tvalid),
    .probe7(axis_host_send[0].tready),
    .probe8(axis_host_send[0].tlast),
    .probe9(axis_host_send[1].tvalid),
    .probe10(axis_host_send[1].tready),
    .probe11(axis_host_send[1].tlast),
    .probe12(sq_wr.valid),
    .probe13(sq_wr.ready),
    .probe14(sq_wr.data),
    .probe15(sq_rd.valid),
    .probe16(sq_rd.ready),
    .probe17(sq_rd.data)
);
*/

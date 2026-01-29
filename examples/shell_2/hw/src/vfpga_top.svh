// RDMA loopback with vIO Switch - control path direct, data path via switch
import lynxTypes::*;

logic [31:0] user_recv_counter;
logic [31:0] user_send_counter;

assign o_user_recv_counter = user_recv_counter;
assign o_user_send_counter = user_send_counter;

always_ff @(posedge aclk) begin
    if (~aresetn) begin
        user_recv_counter <= '0;
        user_send_counter <= '0;
    end else begin
        if (axis_host_recv[0].tvalid && axis_host_recv[0].tready && axis_host_recv[0].tlast)
            user_recv_counter <= user_recv_counter + 1;
        if (axis_host_send[0].tvalid && axis_host_send[0].tready && axis_host_send[0].tlast)
            user_send_counter <= user_send_counter + 1;
    end
end

// Control path: forward RDMA requests
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

// Data path: loopback through vIO Switch
`AXISR_ASSIGN(axis_host_recv[0], axis_host_send[0])
`AXISR_ASSIGN(axis_host_recv[1], axis_host_send[1])

// Tie-off unused
always_comb cq_wr.tie_off_s();
always_comb cq_rd.tie_off_s();
always_comb axi_ctrl.tie_off_s();
always_comb notify.tie_off_m();

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
    .probe8(axis_host_recv[0].tvalid),
    .probe9(axis_host_recv[0].tready),
    .probe10(axis_host_recv[0].tlast),
    .probe11(axis_host_recv[0].tdata[63:0]),
    .probe12(axis_host_send[0].tvalid),
    .probe13(axis_host_send[0].tready),
    .probe14(axis_host_send[0].tlast),
    .probe15(axis_host_send[0].tdata[63:0])
);

// Interface declarations
import lynxTypes::*;


`ifdef EN_STRM
sha2_top inst_host_sha2 (
    .axis_sink          (axis_host_recv[0]),
    .axis_src           (axis_host_send[0]),

    .aclk               (aclk),
    .aresetn            (aresetn)
);

// ILA
ila_sha2_host inst_ila_sha2_host_c1 (
    .clk(aclk),
    .probe0(axis_host_recv[0].tvalid),
    .probe1(axis_host_recv[0].tready),
    .probe2(axis_host_recv[0].tlast),
    .probe3(axis_host_send[0].tvalid),
    .probe4(axis_host_send[0].tready),
    .probe5(axis_host_send[0].tlast)
);
`endif

`ifdef EN_MEM
sha2_top inst_card_sha2 (
    .axis_sink          (axis_card_recv[0]),
    .axis_src           (axis_card_send[0]),

    .aclk               (aclk),
    .aresetn            (aresetn)
);

// ILA
ila_sha2_host_2 inst_ila_sha2_host_c1 (
    .clk(aclk),
    .probe0(axis_host_recv[0].tvalid),
    .probe1(axis_host_recv[0].tready),
    .probe2(axis_host_recv[0].tlast),
    .probe3(axis_host_send[0].tvalid),
    .probe4(axis_host_send[0].tready),
    .probe5(axis_host_send[0].tlast),
    .probe6(axis_card_recv[0].tvalid),
    .probe7(axis_card_recv[0].tready),
    .probe8(axis_card_recv[0].tlast),
    .probe9(axis_card_send[0].tvalid),
    .probe10(axis_card_send[0].tready),
    .probe11(axis_card_send[0].tlast)
);
`endif

// Tie-off unused
always_comb axi_ctrl.tie_off_s();
always_comb notify.tie_off_m();
always_comb sq_rd.tie_off_m();
always_comb sq_wr.tie_off_m();
always_comb cq_rd.tie_off_s();
always_comb cq_wr.tie_off_s();

assign user_data = 0;

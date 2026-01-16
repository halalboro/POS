/**
 * VFPGA TOP
 *
 * Tie up all signals to the user kernels
 * Still to this day, interfaces are not supported by Vivado packager ...
 * This means verilog style port connections are needed.
 * 
 */

import lynxTypes::*;

// Instantiate top level
`ifdef EN_STRM
perf_counter inst_host_link (
    .axis_sink          (axis_host_recv[0]),
    .axis_src           (axis_host_send[0]),
    .last_idle_cycles   (user_data),

    .aclk               (aclk),
    .aresetn            (aresetn)
);

// ILA
ila_perf_host inst_ila_perf_host_c1 (
    .clk(aclk),
    .probe0(axis_host_recv[0].tvalid),
    .probe1(axis_host_recv[0].tready),
    .probe2(axis_host_recv[0].tlast),
    .probe3(axis_host_send[0].tvalid),
    .probe4(axis_host_send[0].tready),
    .probe5(axis_host_send[0].tlast)
);
<<<<<<<< HEAD:examples_hw/apps/perf_counter/vfpga_top.svh

========
>>>>>>>> 01e1c11a:examples_hw/apps/perf_local/vfpga_top.svh
`endif

`ifdef EN_MEM
perf_counter inst_card_link (
    .axis_sink          (axis_card_recv[0]),
    .axis_src           (axis_card_send[0]),
    // .last_idle_cycles   (user_data),

    .aclk               (aclk),
    .aresetn            (aresetn)
);

// ILA
<<<<<<<< HEAD:examples_hw/apps/perf_counter/vfpga_top.svh
ila_perf_host inst_ila_perf_card_c1 (
    .clk(aclk),
    .probe0(axis_card_recv[0].tvalid),
    .probe1(axis_card_recv[0].tready),
    .probe2(axis_card_recv[0].tlast),
    .probe3(axis_card_send[0].tvalid),
    .probe4(axis_card_send[0].tready),
    .probe5(axis_card_send[0].tlast)
========
ila_perf_host_2 inst_ila_perf_host_c1 (
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
>>>>>>>> 01e1c11a:examples_hw/apps/perf_local/vfpga_top.svh
);
`endif

// Tie-off unused
always_comb axi_ctrl.tie_off_s();
always_comb notify.tie_off_m();
always_comb sq_rd.tie_off_m();
always_comb sq_wr.tie_off_m();
always_comb cq_rd.tie_off_s();
always_comb cq_wr.tie_off_s();
<<<<<<<< HEAD:examples_hw/apps/perf_counter/vfpga_top.svh

========
>>>>>>>> 01e1c11a:examples_hw/apps/perf_local/vfpga_top.svh

/**
 * VFPGA Top Module
 * 
 */
import lynxTypes::*;
     
`ifdef EN_STRM
r_top inst_host_r (
    .axis_sink          (axis_host_recv[0]),
    .axis_src           (axis_host_send[0]),

    .aclk               (aclk),
    .aresetn            (aresetn)
);
`endif

`ifdef EN_MEM
r_top inst_card_r (
    .axis_sink          (axis_card_recv[0]),
    .axis_src           (axis_card_send[0]),

    .aclk               (aclk),
    .aresetn            (aresetn)
);
`endif
    
// Tie off unused interfaces
always_comb axi_ctrl.tie_off_s();
always_comb notify.tie_off_m();
always_comb sq_rd.tie_off_m();
always_comb sq_wr.tie_off_m();
always_comb cq_rd.tie_off_s();
always_comb cq_wr.tie_off_s();

assign user_data = 0;

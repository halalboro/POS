/**
 * VFPGA Top Module
 * 
 */
import lynxTypes::*;
     
`ifdef EN_STRM
r_top inst_host_rsa (
    .axis_sink          (axis_host_recv[0]),
    .axis_src           (axis_host_send[0]),

    .aclk               (aclk),
    .aresetn            (aresetn)
);

// Transaction handshake detection
logic start_signal, end_signal;

assign start_signal = axis_host_recv[0].tvalid && axis_host_recv[0].tready;
assign end_signal = axis_host_send[0].tvalid && axis_host_send[0].tready;
`endif

`ifdef EN_MEM
r_top inst_card_rsa (
    .axis_sink          (axis_card_recv[0]),
    .axis_src           (axis_card_send[0]),

    .aclk               (aclk),
    .aresetn            (aresetn)
);
`endif

// ============================================================================
// Cycle Counter Integration
// ============================================================================

logic [31:0]                     cycle_count;
logic                            count_valid;

// Benchmark slave
logic [1:0] bench_ctrl;
logic [31:0] bench_done;
logic [63:0] bench_timer;
logic [PID_BITS-1:0] bench_pid;
logic [DEST_BITS-1:0] bench_dest;
logic [VADDR_BITS-1:0] bench_vaddr;
logic [LEN_BITS-1:0] bench_len;
logic [31:0] bench_n_reps;
logic [63:0] bench_n_beats;

assign bench_timer[31:0] = cycle_count;
assign bench_timer[63:32] = 32'h0;
assign bench_done = count_valid;

// Instantiate cycle counter
cycle_counter u_cycle_counter (
    .clk          (aclk),
    .resetn       (aresetn),
    .start        (start_signal),
    .stop         (end_signal),
    .cycle_count  (cycle_count),
    .count_valid  (count_valid)
);

ctrl_slv inst_slave (
    .aclk(aclk),
    .aresetn(aresetn),
    .axi_ctrl(axi_ctrl),
    .bench_ctrl(bench_ctrl),
    .bench_done(bench_done),
    .bench_timer(bench_timer),
    .bench_pid(bench_pid),
    .bench_dest(bench_dest),
    .bench_vaddr(bench_vaddr),
    .bench_len(bench_len),
    .bench_n_reps(bench_n_reps),
    .bench_n_beats(bench_n_beats)
);


ila_ctrl_fpga inst_ila_ctrl_fpga (
    .clk(aclk),
    .probe0(bench_ctrl), // 2
    .probe1(bench_done), // 32
    .probe2(bench_timer), // 64
    .probe3(start_signal), // 1
    .probe4(end_signal), // 1
    .probe5(cycle_count), // 32
    .probe6(count_valid) // 1
);

// Tie off unused interfaces
// always_comb axi_ctrl.tie_off_s();
always_comb notify.tie_off_m();
always_comb sq_rd.tie_off_m();
always_comb sq_wr.tie_off_m();
always_comb cq_rd.tie_off_s();
always_comb cq_wr.tie_off_s();


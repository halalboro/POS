/**
 * VFPGA TOP
 *
 * 
 */
import lynxTypes::*;

module vfpga_top (
    axi_intf.s          axi_ctrl,
    axis_intf.s         axis_host_recv [N_STRM_AXI],
    axis_intf.m         axis_host_send [N_STRM_AXI],
    axis_intf.s         axis_card_recv [N_STRM_AXI],
    axis_intf.m         axis_card_send [N_STRM_AXI],
    metaIntf.m          notify,
    metaIntf.m          sq_rd,
    metaIntf.m          sq_wr,
    metaIntf.s          cq_rd,
    metaIntf.s          cq_wr,
    
    input  logic        aclk,
    input  logic        aresetn
);

    // Clock and reset signals
    logic clk_100M;
    logic clk_10M;
    logic clk_locked;
    logic rst_n_100M;
    logic rst_n_10M;

    // SVM interface signals
    logic [31:0] input_tdata;
    logic        input_tvalid;
    logic        input_tready;
    logic [31:0] output_tdata;
    logic        output_tvalid;
    logic        output_tready;
    
    // Clock wizard instance
    clk_wiz_0 inst_clk_wiz (
        .clk_in1_p       (sysclk2_clk_p),
        .clk_in1_n       (sysclk2_clk_n),
        .clk_out1        (clk_100M),
        .clk_out2        (clk_10M),
        .reset           (!aresetn),
        .locked          (clk_locked)
    );

    // Reset modules
    proc_sys_reset_1 inst_rst_100M (
        .slowest_sync_clk (clk_100M),
        .ext_reset_in     (!aresetn),
        .aux_reset_in     (1'b1),
        .mb_debug_sys_rst (1'b0),
        .dcm_locked       (clk_locked),
        .peripheral_aresetn(rst_n_100M)
    );

    proc_sys_reset_0 inst_rst_10M (
        .slowest_sync_clk (clk_10M),
        .ext_reset_in     (!aresetn),
        .aux_reset_in     (1'b1),
        .mb_debug_sys_rst (1'b0),
        .dcm_locked       (clk_locked),
        .peripheral_aresetn(rst_n_10M)
    );

    // AXI Interconnect 
    axi_interconnect_0 inst_axi_interconnect (
        .ACLK            (clk_100M),
        .ARESETN         (rst_n_100M),
        .S00_ACLK        (clk_100M),
        .S00_ARESETN     (rst_n_100M),
        .M00_ACLK        (clk_10M),
        .M00_ARESETN     (rst_n_10M),
        .S00_AXI_*       (axi_ctrl.*),
        .M00_AXI_*       (svm_control.*)
    );

    // SVM IP
    svm_speech_30_0 inst_svm (
        .ap_clk                 (clk_wiz_0_clk_out2),
        .ap_rst_n              (proc_sys_reset_0_peripheral_aresetn),
        .input_r_TDATA         (input_r_0_1_TDATA),
        .input_r_TREADY        (input_r_0_1_TREADY),
        .input_r_TVALID        (input_r_0_1_TVALID),
        .interrupt             (svm_speech_30_0_interrupt),
        .output_r_TDATA        (svm_speech_30_0_output_r_TDATA),
        .output_r_TREADY       (svm_speech_30_0_output_r_TREADY),
        .output_r_TVALID       (svm_speech_30_0_output_r_TVALID),
        .s_axi_control_ARADDR  (s_axi_control_0_1_ARADDR),
        .s_axi_control_ARREADY (s_axi_control_0_1_ARREADY),
        .s_axi_control_ARVALID (s_axi_control_0_1_ARVALID),
        .s_axi_control_AWADDR  (s_axi_control_0_1_AWADDR),
        .s_axi_control_AWREADY (s_axi_control_0_1_AWREADY),
        .s_axi_control_AWVALID (s_axi_control_0_1_AWVALID),
        .s_axi_control_BREADY  (s_axi_control_0_1_BREADY),
        .s_axi_control_BRESP   (s_axi_control_0_1_BRESP),
        .s_axi_control_BVALID  (s_axi_control_0_1_BVALID),
        .s_axi_control_RDATA   (s_axi_control_0_1_RDATA),
        .s_axi_control_RREADY  (s_axi_control_0_1_RREADY),
        .s_axi_control_RRESP   (s_axi_control_0_1_RRESP),
        .s_axi_control_RVALID  (s_axi_control_0_1_RVALID),
        .s_axi_control_WDATA   (s_axi_control_0_1_WDATA),
        .s_axi_control_WREADY  (s_axi_control_0_1_WREADY),
        .s_axi_control_WSTRB   (s_axi_control_0_1_WSTRB),
        .s_axi_control_WVALID  (s_axi_control_0_1_WVALID)
    );

    // AXI-Stream Clock Converter
    axis_clock_converter_0 inst_axis_clock_converter (
        .s_axis_aclk     (clk_10M),
        .s_axis_aresetn  (rst_n_10M),
        .s_axis_tdata    (output_tdata),
        .s_axis_tvalid   (output_tvalid),
        .s_axis_tready   (output_tready),
        .m_axis_aclk     (clk_100M),
        .m_axis_aresetn  (rst_n_100M),
        .m_axis_tdata    (axis_host_send[0].tdata),
        .m_axis_tvalid   (axis_host_send[0].tvalid),
        .m_axis_tready   (axis_host_send[0].tready)
    );
    
   // Tie off unused interfaces
    always_comb begin
        notify.tie_off_m();
        sq_rd.tie_off_m();
        sq_wr.tie_off_m();
        cq_rd.tie_off_s();
        cq_wr.tie_off_s();
        
        for(int i = 1; i < N_STRM_AXI; i++) begin
            axis_host_send[i].tie_off_m();
            axis_card_send[i].tie_off_m();
        end
    end

    // DBG
    ila_0 inst_ila_0 (
        .clk            (aclk),
        .probe0         (axis_host_recv[0].tvalid),    // Host input valid
        .probe1         (axis_host_recv[0].tready),    // Host input ready
        .probe2         (axis_host_recv[0].tdata),     // Host input data
        .probe3         (axis_host_recv[0].tlast),     // Host input last
        .probe4         (inst_svm.input_r_TVALID),     // SVM input valid
        .probe5         (inst_svm.input_r_TREADY),     // SVM input ready
        .probe6         (inst_svm.input_r_TDATA),      // SVM input data
        .probe7         (inst_svm.output_r_TVALID),    // SVM output valid
        .probe8         (inst_svm.output_r_TREADY),    // SVM output ready
        .probe9         (inst_svm.output_r_TDATA),     // SVM output data
        .probe10        (axis_host_send[0].tvalid),    // Host output valid
        .probe11        (axis_host_send[0].tready),    // Host output ready
        .probe12        (axis_host_send[0].tdata),     // Host output data
        .probe13        (axis_host_send[0].tlast),     // Host output last
        .probe14        (clk_locked),                  // Clock wizard locked
        .probe15        (rst_n_100M),                  // 100MHz reset
        .probe16        (rst_n_10M)                    // 10MHz reset
    );

endmodule
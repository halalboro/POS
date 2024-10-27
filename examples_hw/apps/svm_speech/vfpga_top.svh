/**
 * VFPGA Top Module
 * 
 * This module implements the top level of the VFPGA design, containing:
 * - FFT processing pipeline
 * - SVM classification
 * - Clock domain crossing infrastructure
 * - Debug logic
 *
 * Clock Domains:
 * - aclk: Input clock
 * - clk_100M: Main processing clock (100 MHz)
 * - clk_10M: SVM processing clock (10 MHz)
 */
import lynxTypes::*;

module vfpga_top (
    input  logic        aclk,
    input  logic        aresetn
);

    // Clock and reset signals
    logic clk_100M;
    logic clk_10M;
    logic clk_locked;
    logic rst_n_100M;
    logic rst_n_10M;
    
    // Internal AXI Stream interfaces
    AXI4SR axis_sink [N_STRM_AXI] ();
    AXI4SR axis_src [N_STRM_AXI] ();

    // Register slices for host interfaces
    for (genvar i = 0; i < N_STRM_AXI; i++) begin
        axisr_reg inst_reg_sink (.aclk(aclk), .aresetn(aresetn), .s_axis(axis_host_recv[i]), .m_axis(axis_sink[i]));
        axisr_reg inst_reg_src (.aclk(aclk), .aresetn(aresetn), .s_axis(axis_src[i]), .m_axis(axis_host_send[i]));
    end
    
    //Dwidth to FFT    
    AXI4SR axis_host_recv_512 ();
    
    //Clock Converter to Dwidth
    AXI4SR axis_host_send_512 ();
    
    // FFT to FFT2SVM signals (100MHz domain)
    AXI4SR fft ();
    
    // FFT2SVM to Clock Converter signals (100MHz domain)
    AXI4SR fft2svm ();
    
    // Clock Converter to SVM signals (10MHz domain)
    AXI4SR svm_in ();
   
    // SVM to final Clock Converter signals (10MHz domain)
    AXI4SR svm_out ();
    
    
    // Add constant signals for FFT
    logic [15:0] xlconstant_0_dout;
    logic        xlconstant_1_dout;

    assign xlconstant_0_dout = 16'h1;
    assign xlconstant_1_dout = 1'b1;
    
    // Initialize tkeep signals to all valid
    assign fft.tkeep = 8'hFF;      // 64-bit data path
    assign fft2svm.tkeep = 4'hF;   // 32-bit data path
    assign svm_in.tkeep = 4'hF;    // 32-bit data path
    assign svm_out.tkeep = 4'hF;   // 32-bit data path
	
    
    // Clock wizard instance
    clk_wiz_0 inst_clk_wiz (
        .clk_in1          (aclk),              
        .reset           (!aresetn),           
        .clk_out1        (clk_100M),
        .clk_out2        (clk_10M),
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
    
    // I/O 
    for(genvar i = 0; i < N_STRM_AXI; i++) begin
    dwidth_converter_512_32 inst_dwidth_recv (
        .aclk(clk_100M),
        .aresetn(rst_n_100M),
        .s_axis_tvalid(axis_sink[i].tvalid),
        .s_axis_tready(axis_sink[i].tready),
        .s_axis_tdata (axis_sink[i].tdata),
        .s_axis_tkeep (axis_sink[i].tkeep),
        .s_axis_tlast (axis_sink[i].tlast),
        .s_axis_tid   (axis_sink[i].tid),
        .m_axis_tvalid(axis_host_recv_512.tvalid),
        .m_axis_tready(axis_host_recv_512.tready),
        .m_axis_tdata (axis_host_recv_512.tdata),
        .m_axis_tkeep (axis_host_recv_512.tkeep),
        .m_axis_tlast (axis_host_recv_512.tlast),
        .m_axis_tid   (axis_host_recv_512.tid)
    );

    dwidth_converter_32_512 inst_dwidth_send (
        .aclk(clk_100M),
        .aresetn(rst_n_100M),
        .s_axis_tvalid(axis_host_send_512.tvalid),
        .s_axis_tready(axis_host_send_512.tready),
        .s_axis_tdata (axis_host_send_512.tdata),
        .s_axis_tkeep (axis_host_send_512.tkeep),
        .s_axis_tlast (axis_host_send_512.tlast),
        .s_axis_tid   (axis_host_send_512.tid),
        .m_axis_tvalid(axis_src[i].tvalid),
        .m_axis_tready(axis_src[i].tready),
        .m_axis_tdata (axis_src[i].tdata),
        .m_axis_tkeep (axis_src[i].tkeep),
        .m_axis_tlast (axis_src[i].tlast),
        .m_axis_tid   (axis_src[i].tid)
    );
    end
    
    // AXI Crossbar instance
    axi_crossbar_0 inst_axi_crossbar (
        .aclk                   (clk_100M),
        .aresetn                (rst_n_100M),
        // Slave interface (from host)
        .s_axi_awaddr           (axi_ctrl.awaddr),
        .s_axi_awprot           (3'b000),
        .s_axi_awvalid          (axi_ctrl.awvalid),
        .s_axi_awready          (axi_ctrl.awready),
        .s_axi_wdata            (axi_ctrl.wdata),
        .s_axi_wstrb            (axi_ctrl.wstrb),
        .s_axi_wvalid           (axi_ctrl.wvalid),
        .s_axi_wready           (axi_ctrl.wready),
        .s_axi_bresp            (axi_ctrl.bresp),
        .s_axi_bvalid           (axi_ctrl.bvalid),
        .s_axi_bready           (axi_ctrl.bready),
        .s_axi_araddr           (axi_ctrl.araddr),
        .s_axi_arprot           (3'b000),
        .s_axi_arvalid          (axi_ctrl.arvalid),
        .s_axi_arready          (axi_ctrl.arready),
        .s_axi_rdata            (axi_ctrl.rdata),
        .s_axi_rresp            (axi_ctrl.rresp),
        .s_axi_rvalid           (axi_ctrl.rvalid),
        .s_axi_rready           (axi_ctrl.rready),
        // Master interface (to SVM control)
        .m_axi_awaddr           (inst_svm.s_axi_control_AWADDR),
        .m_axi_awvalid          (inst_svm.s_axi_control_AWVALID),
        .m_axi_awready          (inst_svm.s_axi_control_AWREADY),
        .m_axi_wdata            (inst_svm.s_axi_control_WDATA),
        .m_axi_wstrb            (inst_svm.s_axi_control_WSTRB),
        .m_axi_wvalid           (inst_svm.s_axi_control_WVALID),
        .m_axi_wready           (inst_svm.s_axi_control_WREADY),
        .m_axi_bresp            (inst_svm.s_axi_control_BRESP),
        .m_axi_bvalid           (inst_svm.s_axi_control_BVALID),
        .m_axi_bready           (inst_svm.s_axi_control_BREADY),
        .m_axi_araddr           (inst_svm.s_axi_control_ARADDR),
        .m_axi_arvalid          (inst_svm.s_axi_control_ARVALID),
        .m_axi_arready          (inst_svm.s_axi_control_ARREADY),
        .m_axi_rdata            (inst_svm.s_axi_control_RDATA),
        .m_axi_rresp            (inst_svm.s_axi_control_RRESP),
        .m_axi_rvalid           (inst_svm.s_axi_control_RVALID),
        .m_axi_rready           (inst_svm.s_axi_control_RREADY)
    );
    
    // SVM IP
    svm_speech_30_0 inst_svm (
        .ap_clk                (clk_10M),
        .ap_rst_n              (rst_n_10M),
        .input_r_TDATA         (svm_in.tdata),
        .input_r_TREADY        (svm_in.tready),
        .input_r_TVALID        (svm_in.tvalid),
        .input_r_TLAST         (svm_in.tlast),     
        .input_r_TKEEP         (svm_in.tkeep),
        .output_r_TDATA        (svm_out.tdata),
        .output_r_TREADY       (svm_out.tready),
        .output_r_TVALID       (svm_out.tvalid),
        .output_r_TLAST        (svm_out.tlast),    
        .output_r_TKEEP        (svm_out.tkeep),  
        .s_axi_control_ARADDR  (inst_axi_crossbar.m_axi_ARADDR),
        .s_axi_control_ARREADY (inst_axi_crossbar.m_axi_ARREADY),
        .s_axi_control_ARVALID (inst_axi_crossbar.m_axi_ARVALID),
        .s_axi_control_AWADDR  (inst_axi_crossbar.m_axi_AWADDR),
        .s_axi_control_AWREADY (inst_axi_crossbar.m_axi_AWREADY),
        .s_axi_control_AWVALID (inst_axi_crossbar.m_axi_AWVALID),
        .s_axi_control_BREADY  (inst_axi_crossbar.m_axi_BREADY),
        .s_axi_control_BRESP   (inst_axi_crossbar.m_axi_BRESP),
        .s_axi_control_BVALID  (inst_axi_crossbar.m_axi_BVALID),
        .s_axi_control_RDATA   (inst_axi_crossbar.m_axi_RDATA),
        .s_axi_control_RREADY  (inst_axi_crossbar.m_axi_RREADY),
        .s_axi_control_RRESP   (inst_axi_crossbar.m_axi_RRESP),
        .s_axi_control_RVALID  (inst_axi_crossbar.m_axi_RVALID),
        .s_axi_control_WDATA   (inst_axi_crossbar.m_axi_WDATA),
        .s_axi_control_WREADY  (inst_axi_crossbar.m_axi_WREADY),
        .s_axi_control_WSTRB   (inst_axi_crossbar.m_axi_WSTRB),
        .s_axi_control_WVALID  (inst_axi_crossbar.m_axi_WVALID)
    );

    // FFT instance (100MHz)
    xfft_0 inst_xfft (
        .aclk              (clk_100M),
        .aresetn           (rst_n_100M),
        .s_axis_data_tdata     (axis_host_recv_512.tdata),  
        .s_axis_data_tvalid    (axis_host_recv_512.tvalid),
        .s_axis_data_tready    (axis_host_recv_512.tready),
        .s_axis_data_tlast     (axis_host_recv_512.tlast),
        .s_axis_data_tkeep     (axis_host_recv_512.tkeep),
        .m_axis_data_tid       (axis_host_recv_512.tid),
        .m_axis_data_tdata     (fft.tdata),
        .m_axis_data_tvalid    (fft.tvalid),
        .m_axis_data_tready    (fft.tready),
        .m_axis_data_tlast     (fft.tlast),
        .m_axis_data_tkeep     (fft.tkeep),
        .m_axis_data_tid       (fft.tid),        
    	.s_axis_config_tdata   (xlconstant_0_dout),     // 16-bit config from constant
    	.s_axis_config_tvalid  (xlconstant_1_dout),     // Valid signal always high
    	.s_axis_config_tready  (),                      // Not used
    	.s_axis_data_tuser     (1'b0),                  // Not used
    	.m_axis_data_tuser     (),                      // Not used
    	.m_axis_status_tdata   (),                      // Not used
    	.m_axis_status_tvalid  (),                      // Not used
    	.m_axis_status_tready  (1'b1)                   // Always ready
    );

    // FFT2SVM instance (100MHz)
    fft2svm_0 inst_fft2svm (
        .ap_clk           (clk_100M),
        .ap_rst_n         (rst_n_100M),
        .s_axis_fft_tdata (fft.tdata),
        .s_axis_fft_tvalid(fft.tvalid),
        .s_axis_fft_tready(fft.tready),
        .s_axis_fft_tlast (fft.tlast),
        .s_axis_fft_tkeep (fft.tkeep),
        .s_axis_fft_tid   (fft.tid),
        .m_axis_svm_tdata (fft2svm.tdata),
        .m_axis_svm_tvalid(fft2svm.tvalid),
        .m_axis_svm_tready(fft2svm.tready),
        .m_axis_svm_tlast (fft2svm.tlast),
        .m_axis_svm_tkeep (fft2svm.tkeep),
        .m_axis_svm_tid   (fft2svm.tid)
    );

    // Clock Converter FFT2SVM -> SVM (100MHz -> 10MHz)
    axis_clock_converter_1 inst_cc_fft2svm (
        .s_axis_aclk     (clk_100M),
        .s_axis_aresetn  (rst_n_100M),
        .s_axis_tdata    (fft2svm.tdata),
        .s_axis_tvalid   (fft2svm.tvalid),
        .s_axis_tready   (fft2svm.tready),
        .s_axis_tlast    (fft2svm.tlast),
        .s_axis_tkeep    (fft2svm.tkeep),
        .s_axis_tid      (fft2svm.tid),
        .m_axis_aclk     (clk_10M),
        .m_axis_aresetn  (rst_n_10M),
        .m_axis_tdata    (svm_in.tdata),
        .m_axis_tvalid   (svm_in.tvalid),
        .m_axis_tready   (svm_in.tready),
        .m_axis_tlast    (svm_in.tlast),
        .m_axis_tkeep    (svm_in.tkeep),
        .m_axis_tid      (svm_in.tid)
    );

    // Clock Converter SVM -> DWidth Converter (10MHz -> 100MHz)
    axis_clock_converter_0 inst_cc_svm2host (
        .s_axis_aclk     (clk_10M),
        .s_axis_aresetn  (rst_n_10M),
        .s_axis_tdata    (svm_out.tdata),
        .s_axis_tvalid   (svm_out.tvalid),
        .s_axis_tready   (svm_out.tready),
        .s_axis_tlast    (svm_out.tlast),
        .s_axis_tkeep    (svm_out.tkeep),
        .s_axis_tid      (svm_out.tid),
        .m_axis_aclk     (clk_100M),
        .m_axis_aresetn  (rst_n_100M),
        .m_axis_tdata    (axis_host_send_512.tdata),    
        .m_axis_tkeep    (axis_host_send_512.tkeep),
        .m_axis_tvalid   (axis_host_send_512.tvalid),
        .m_axis_tready   (axis_host_send_512.tready),
        .m_axis_tlast    (axis_host_send_512.tlast),
        .m_axis_tid      (axis_host_send_512.tid)
    );
    
    
    // Tie off unused interfaces
    always_comb begin
        notify.tie_off_m();
        sq_rd.tie_off_m();
        sq_wr.tie_off_m();
        cq_rd.tie_off_s();
        cq_wr.tie_off_s();
    end

   
  // DBG
  ila_0 inst_ila_0 (
      .clk            (aclk),
      .probe0         (axis_sink[0].tvalid),     // Internal host input valid
      .probe1         (axis_sink[0].tready),     // Internal host input ready
      .probe2         (axis_sink[0].tdata),      // Internal host input data
      .probe3         (axis_sink[0].tlast),      // Internal host input last
      .probe4         (fft.tvalid),                  // FFT output valid
      .probe5         (fft.tready),                  // FFT output ready
      .probe6         (fft.tdata),                   // FFT output data
      .probe7         (fft.tlast),                   // FFT output last
      .probe8         (fft2svm.tvalid),              // FFT2SVM output valid
      .probe9         (fft2svm.tready),              // FFT2SVM output ready
      .probe10        (fft2svm.tdata),               // FFT2SVM output data
      .probe11        (fft2svm.tlast),               // FFT2SVM output last
      .probe12        (svm_in.tvalid),               // SVM input valid
      .probe13        (svm_in.tready),               // SVM input ready
      .probe14        (svm_in.tdata),                // SVM input data
      .probe15        (svm_in.tlast),                // SVM input last
      .probe16        (svm_out.tvalid),              // SVM output valid
      .probe17        (svm_out.tready),              // SVM output ready
      .probe18        (svm_out.tdata),               // SVM output data
      .probe19        (svm_out.tlast),               // SVM output last
      .probe20        (axis_src[0].tvalid),      // Internal host output valid
      .probe21        (axis_src[0].tready),      // Internal host output ready
      .probe22        (axis_src[0].tdata),       // Internal host output data
      .probe23        (axis_src[0].tlast),       // Internal host output last
      .probe24        (clk_locked),                  // Clock wizard locked
      .probe25        (rst_n_100M),                  // 100MHz reset
      .probe26        (rst_n_10M)                    // 10MHz reset
  );
  
endmodule

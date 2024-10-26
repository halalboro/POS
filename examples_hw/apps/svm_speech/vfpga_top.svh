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
    
    // Internal AXI Stream interfaces
    AXI4SR axis_sink_int[N_STRM_AXI]();
    AXI4SR axis_src_int[N_STRM_AXI]();

    // Register slices for host interfaces
    for (genvar i = 0; i < N_STRM_AXI; i++) begin
        axisr_reg inst_reg_sink_0 (.aclk(aclk), .aresetn(aresetn), .s_axis(axis_host_recv[i]), .m_axis(axis_sink_int[i]));
        axisr_reg inst_reg_src_0 (.aclk(aclk), .aresetn(aresetn), .s_axis(axis_src_int[i]), .m_axis(axis_host_send[i]));
    end
    
    // FFT to FFT2SVM signals (100MHz domain)
    logic [63:0] fft_tdata;
    logic [7:0]  fft_tkeep;
    logic        fft_tvalid;
    logic        fft_tready;
    logic        fft_tlast;
    
    // FFT2SVM to Clock Converter signals (100MHz domain)
    logic [31:0] fft2svm_tdata;
    logic [3:0]  fft2svm_tkeep;
    logic        fft2svm_tvalid;
    logic        fft2svm_tready;
    logic        fft2svm_tlast;
    
    // Clock Converter to SVM signals (10MHz domain)
    logic [31:0] svm_in_tdata;
    logic [3:0]  svm_in_tkeep;
    logic        svm_in_tvalid;
    logic        svm_in_tready;
    logic        svm_in_tlast;
    
    // SVM to final Clock Converter signals (10MHz domain)
    logic [31:0] svm_out_tdata;
    logic [3:0]  svm_out_tkeep; 
    logic        svm_out_tvalid;
    logic        svm_out_tready;
    logic        svm_out_tlast;
    
    
    // Add constant signals for FFT
    logic [15:0] xlconstant_0_dout;
    logic        xlconstant_1_dout;

    assign xlconstant_0_dout = 16'h1;
    assign xlconstant_1_dout = 1'b1;
    
    // Initialize tkeep signals to all valid
    assign fft_tkeep = 8'hFF;      // 64-bit data path
    assign fft2svm_tkeep = 4'hF;   // 32-bit data path
    assign svm_in_tkeep = 4'hF;    // 32-bit data path
    assign svm_out_tkeep = 4'hF;   // 32-bit data path
    
    logic        axis_host_recv_512_tvalid;
    logic        axis_host_recv_512_tready;
    logic [511:0] axis_host_recv_512_tdata;
    logic [63:0]  axis_host_recv_512_tkeep;
    logic        axis_host_recv_512_tlast;

    logic        axis_host_send_512_tvalid;
    logic        axis_host_send_512_tready;
    logic [511:0] axis_host_send_512_tdata;
    logic [63:0]  axis_host_send_512_tkeep;
    logic        axis_host_send_512_tlast;
    
    
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
    
    // Width converter: 512->32 for FFT input
    dwidth_converter_512_32 inst_dwidth_recv (
        .aclk(clk_100M),
        .aresetn(rst_n_100M),
        
        // From host (512-bit)
        .s_axis_tvalid(axis_sink_int[0].tvalid),
        .s_axis_tready(axis_sink_int[0].tready),
        .s_axis_tdata(axis_sink_int[0].tdata),
        .s_axis_tkeep(axis_sink_int[0].tkeep),
        .s_axis_tlast(axis_sink_int[0].tlast),
        
        // To FFT (32-bit)
        .m_axis_tvalid(axis_host_recv_32_tvalid),
        .m_axis_tready(axis_host_recv_32_tready),
        .m_axis_tdata(axis_host_recv_32_tdata),
        .m_axis_tkeep(axis_host_recv_32_tkeep),
        .m_axis_tlast(axis_host_recv_32_tlast)
    );

    // Width converter: 32->512 for host output
    dwidth_converter_32_512 inst_dwidth_send (
        .aclk(clk_100M),
        .aresetn(rst_n_100M),
        
        // From SVM (32-bit)
        .s_axis_tvalid(svm_out_tvalid),
        .s_axis_tready(svm_out_tready),
        .s_axis_tdata(svm_out_tdata),
        .s_axis_tkeep(svm_out_tkeep),
        .s_axis_tlast(svm_out_tlast),
        
        // To host (512-bit)
        .m_axis_tvalid(axis_src_int[0].tvalid),
        .m_axis_tready(axis_src_int[0].tready),
        .m_axis_tdata(axis_src_int[0].tdata),
        .m_axis_tkeep(axis_src_int[0].tkeep),
        .m_axis_tlast(axis_src_int[0].tlast)
    );
    
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
        .m_axi_awprot           (),  // Not used
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
        .m_axi_arprot           (),  // Not used
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
        .input_r_TDATA         (svm_in_tdata),
        .input_r_TREADY        (svm_in_tready),
        .input_r_TVALID        (svm_in_tvalid),
        .output_r_TDATA        (svm_out_tdata),
        .output_r_TREADY       (svm_out_tready),
        .output_r_TVALID       (svm_out_tvalid),
        .s_axi_control_ARADDR  (m_axi_ARADDR),
        .s_axi_control_ARREADY (m_axi_ARREADY),
        .s_axi_control_ARVALID (m_axi_ARVALID),
        .s_axi_control_AWADDR  (m_axi_AWADDR),
        .s_axi_control_AWREADY (m_axi_AWREADY),
        .s_axi_control_AWVALID (m_axi_AWVALID),
        .s_axi_control_BREADY  (m_axi_BREADY),
        .s_axi_control_BRESP   (m_axi_BRESP),
        .s_axi_control_BVALID  (m_axi_BVALID),
        .s_axi_control_RDATA   (m_axi_RDATA),
        .s_axi_control_RREADY  (m_axi_RREADY),
        .s_axi_control_RRESP   (m_axi_RRESP),
        .s_axi_control_RVALID  (m_axi_RVALID),
        .s_axi_control_WDATA   (m_axi_WDATA),
        .s_axi_control_WREADY  (m_axi_WREADY),
        .s_axi_control_WSTRB   (m_axi_WSTRB),
        .s_axi_control_WVALID  (m_axi_WVALID)
    );

    // FFT instance (100MHz)
    xfft_0 inst_xfft (
        .aclk              (clk_100M),
        .aresetn           (rst_n_100M),
        .s_axis_data_tdata     (axis_sink_int[0].tdata),  // Changed to use internal interface
        .s_axis_data_tvalid    (axis_sink_int[0].tvalid),
        .s_axis_data_tready    (axis_sink_int[0].tready),
        .s_axis_data_tlast     (axis_sink_int[0].tlast),
        .m_axis_data_tdata (fft_tdata),
        .m_axis_data_tvalid(fft_tvalid),
        .m_axis_data_tready(fft_tready),
        .m_axis_data_tlast (fft_tlast),
    	.s_axis_config_tdata   (xlconstant_0_dout),    // 16-bit config from constant
    	.s_axis_config_tvalid  (xlconstant_1_dout),    // Valid signal always high
    	.s_axis_config_tready  (),                      // Not used
    	.s_axis_data_tuser    (1'b0),                  // Not used
    	.m_axis_data_tuser    (),                      // Not used
    	.m_axis_status_tdata  (),                      // Not used
    	.m_axis_status_tvalid (),                      // Not used
    	.m_axis_status_tready (1'b1)                   // Always ready
    );

    // FFT2SVM instance (100MHz)
    fft2svm_0 inst_fft2svm (
        .ap_clk            (clk_100M),
        .ap_rst_n         (rst_n_100M),
        .s_axis_fft_tdata (fft_tdata),
        .s_axis_fft_tvalid(fft_tvalid),
        .s_axis_fft_tready(fft_tready),
        .s_axis_fft_tlast (fft_tlast),
        .m_axis_svm_tdata (fft2svm_tdata),
        .m_axis_svm_tvalid(fft2svm_tvalid),
        .m_axis_svm_tready(fft2svm_tready),
        .m_axis_svm_tlast (fft2svm_tlast)
    );

    // Clock Converter FFT2SVM -> SVM (100MHz -> 10MHz)
    axis_clock_converter_1 inst_cc_fft2svm (
        .s_axis_aclk     (clk_100M),
        .s_axis_aresetn  (rst_n_100M),
        .s_axis_tdata    (fft2svm_tdata),
        .s_axis_tvalid   (fft2svm_tvalid),
        .s_axis_tready   (fft2svm_tready),
        .s_axis_tlast    (fft2svm_tlast),
        .m_axis_aclk     (clk_10M),
        .m_axis_aresetn  (rst_n_10M),
        .m_axis_tdata    (svm_in_tdata),
        .m_axis_tvalid   (svm_in_tvalid),
        .m_axis_tready   (svm_in_tready),
        .m_axis_tlast    (svm_in_tlast)
    );

    // Clock Converter SVM -> Host (10MHz -> 100MHz)
    axis_clock_converter_0 inst_cc_svm2host (
        .s_axis_aclk     (clk_10M),
        .s_axis_aresetn  (rst_n_10M),
        .s_axis_tdata    (svm_out_tdata),
        .s_axis_tvalid   (svm_out_tvalid),
        .s_axis_tready   (svm_out_tready),
        .s_axis_tlast    (svm_out_tlast),
        .m_axis_aclk     (clk_100M),
        .m_axis_aresetn  (rst_n_100M),
        .m_axis_tdata    (axis_src_int[0].tdata),    // Changed to use internal interface
        .m_axis_tkeep    (axis_src_int[0].tkeep),
        .m_axis_tvalid   (axis_src_int[0].tvalid),
        .m_axis_tready   (axis_src_int[0].tready),
        .m_axis_tlast    (axis_src_int[0].tlast)
    );
    
    // Tie off unused interfaces
    always_comb begin
        notify.tie_off_m();
        sq_rd.tie_off_m();
        sq_wr.tie_off_m();
        cq_rd.tie_off_s();
        cq_wr.tie_off_s();
    end

    // Generate block for array indexing
    generate
        for(genvar i = 1; i < N_STRM_AXI; i++) begin
            always_comb begin
                axis_host_send[i].tie_off_m();
                axis_card_send[i].tie_off_m();
            end
        end
    endgenerate
   
  // DBG
  ila_0 inst_ila_0 (
      .clk            (aclk),
      .probe0         (axis_sink_int[0].tvalid),     // Internal host input valid
      .probe1         (axis_sink_int[0].tready),     // Internal host input ready
      .probe2         (axis_sink_int[0].tdata),      // Internal host input data
      .probe3         (axis_sink_int[0].tlast),      // Internal host input last
      .probe4         (fft_tvalid),                  // FFT output valid
      .probe5         (fft_tready),                  // FFT output ready
      .probe6         (fft_tdata),                   // FFT output data
      .probe7         (fft_tlast),                   // FFT output last
      .probe8         (fft2svm_tvalid),              // FFT2SVM output valid
      .probe9         (fft2svm_tready),              // FFT2SVM output ready
      .probe10        (fft2svm_tdata),               // FFT2SVM output data
      .probe11        (fft2svm_tlast),               // FFT2SVM output last
      .probe12        (svm_in_tvalid),               // SVM input valid
      .probe13        (svm_in_tready),               // SVM input ready
      .probe14        (svm_in_tdata),                // SVM input data
      .probe15        (svm_in_tlast),                // SVM input last
      .probe16        (svm_out_tvalid),              // SVM output valid
      .probe17        (svm_out_tready),              // SVM output ready
      .probe18        (svm_out_tdata),               // SVM output data
      .probe19        (svm_out_tlast),               // SVM output last
      .probe20        (axis_src_int[0].tvalid),      // Internal host output valid
      .probe21        (axis_src_int[0].tready),      // Internal host output ready
      .probe22        (axis_src_int[0].tdata),       // Internal host output data
      .probe23        (axis_src_int[0].tlast),       // Internal host output last
      .probe24        (clk_locked),                  // Clock wizard locked
      .probe25        (rst_n_100M),                  // 100MHz reset
      .probe26        (rst_n_10M)                    // 10MHz reset
  );
  
endmodule

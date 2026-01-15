/**
 * VFPGA Top Module - FFT2SVM Only
 */
import lynxTypes::*;

    // Interface signals for width conversion
    logic        dwidth_in_tvalid;
    logic        dwidth_in_tready;
    logic [63:0] dwidth_in_tdata;
    logic        dwidth_in_tlast;
    logic [15:0]  dwidth_in_tid;
    logic [7:0]  dwidth_in_tkeep;
    
    logic        dwidth_out_tvalid;
    logic        dwidth_out_tready;
    logic [31:0] dwidth_out_tdata;
    logic        dwidth_out_tlast;
    logic [15:0]  dwidth_out_tid;
    logic [3:0]  dwidth_out_tkeep;
    
    // Stream interface connections
    AXI4SR axis_sink_int ();
    AXI4SR axis_src_int ();
    
    // Stream routing with proper interface connections
    axisr_reg inst_reg_sink (.aclk(aclk), .aresetn(aresetn), .s_axis(axis_host_recv[0]), .m_axis(axis_sink_int));
    axisr_reg inst_reg_src (.aclk(aclk), .aresetn(aresetn), .s_axis(axis_src_int), .m_axis(axis_host_send[0]));
        
    // Input width converter (512->64)
    dwidth_converter_512_64_2 inst_dwidth_recv (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(axis_sink_int.tvalid),
        .s_axis_tready(axis_sink_int.tready),
        .s_axis_tdata (axis_sink_int.tdata),
        .s_axis_tlast (axis_sink_int.tlast),
        .s_axis_tid   (axis_sink_int.tid),  
        .m_axis_tvalid(dwidth_in_tvalid),
        .m_axis_tready(dwidth_in_tready),
        .m_axis_tdata (dwidth_in_tdata),
        .m_axis_tlast (dwidth_in_tlast),
        .m_axis_tid   (dwidth_in_tid) 
    );

    // Output width converter (32->512)
    dwidth_converter_32_512_2 inst_dwidth_send (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(dwidth_out_tvalid),
        .s_axis_tready(dwidth_out_tready),
        .s_axis_tdata (dwidth_out_tdata),
        .s_axis_tlast (dwidth_out_tlast),
        .s_axis_tid   (dwidth_out_tid), 
        .m_axis_tvalid(axis_src_int.tvalid),
        .m_axis_tready(axis_src_int.tready),
        .m_axis_tdata (axis_src_int.tdata),
        .m_axis_tlast (axis_src_int.tlast),
        .m_axis_tid   (axis_src_int.tid) 
    );

    // Static assignments
    always_comb begin
        dwidth_in_tkeep = 4'hF;
        dwidth_out_tkeep = 4'hF;

        dwidth_in_tid = 0;
        dwidth_out_tid = 0;
    end
    
    // FFT2SVM instance
    fft2svm_0 inst_fft2svm (
        .ap_clk             (aclk),
        .ap_rst_n           (aresetn),
        .s_axis_fft_TDATA   (dwidth_in_tdata),
        .s_axis_fft_TVALID  (dwidth_in_tvalid),
        .s_axis_fft_TREADY  (dwidth_in_tready),
        .s_axis_fft_TKEEP   (dwidth_in_tkeep),
        .s_axis_fft_TSTRB   (4'hF),        
        .s_axis_fft_TLAST   (dwidth_in_tlast),
        .m_axis_svm_TDATA   (dwidth_out_tdata),
        .m_axis_svm_TVALID  (dwidth_out_tvalid),
        .m_axis_svm_TREADY  (dwidth_out_tready),
        .m_axis_svm_TKEEP   (dwidth_out_tkeep),
        .m_axis_svm_TSTRB   (),           
        .m_axis_svm_TLAST   (dwidth_out_tlast)
    );
    
    // Tie off unused interfaces
    always_comb axi_ctrl.tie_off_s();
    always_comb notify.tie_off_m();
    always_comb sq_rd.tie_off_m();
    always_comb sq_wr.tie_off_m();
    always_comb cq_rd.tie_off_s();
    always_comb cq_wr.tie_off_s();

ila_fft2svm inst_ila_fft2svm_c1 (
    .clk(aclk),
    .probe0(axis_sink_int.tvalid),
    .probe1(dwidth_in_tvalid),
    .probe2(dwidth_out_tvalid),
    .probe3(axis_src_int.tvalid),
    .probe4(axis_host_send[0].tvalid),
    .probe5(axis_sink_int.tready),
    .probe6(dwidth_in_tready),
    .probe7(dwidth_out_tready),
    .probe8(axis_src_int.tready),
    .probe9(axis_host_send[0].tready),
    .probe10(axis_sink_int.tlast),
    .probe11(dwidth_in_tlast),
    .probe12(dwidth_out_tlast),
    .probe13(axis_src_int.tlast),
    .probe14(axis_host_send[0].tlast)
);

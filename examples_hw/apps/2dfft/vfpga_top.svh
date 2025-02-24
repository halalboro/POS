/**
 * VFPGA Top Module
 * 
 */
import lynxTypes::*;
    
    logic        axis_sink_tvalid;
    logic        axis_sink_tready;
    logic [511:0] axis_sink_tdata;  
    logic        axis_sink_tlast;
    logic        axis_sink_tkeep;
    logic        axis_sink_tid;
    
    logic        axis_src_tvalid;
    logic        axis_src_tready;
    logic [511:0] axis_src_tdata;
    logic        axis_src_tlast;
    logic        axis_src_tkeep;
    logic        axis_src_tid;
	
    AXI4SR axis_sink_int ();
    AXI4SR axis_src_int ();
	
    // Stream routing with proper interface connections
    axisr_reg inst_reg_sink (.aclk(aclk),.aresetn(aresetn),.s_axis(axis_host_recv[0]),.m_axis(axis_sink_int));
    axisr_reg inst_reg_src (.aclk(aclk),.aresetn(aresetn),.s_axis(axis_src_int),.m_axis(axis_host_send[0]));
                
    always_comb begin
        axis_sink_int.tkeep = 1;
        axis_src_int.tkeep = 1;
        axis_sink_int.tid = 0;
        axis_src_int.tid = 0;
    end	
    
    // Convert interface to signals
    assign axis_sink_tvalid = axis_sink_int.tvalid;
    assign axis_sink_int.tready = axis_sink_tready;
    assign axis_sink_tdata = axis_sink_int.tdata;
    assign axis_sink_tlast = axis_sink_int.tlast;
    
    assign axis_src_int.tvalid = axis_src_tvalid;
    assign axis_src_tready = axis_src_int.tready;
    assign axis_src_int.tdata = axis_src_tdata;
    assign axis_src_int.tlast = axis_src_tlast;

    // 2D FFT module 
    top_fft2d inst_fft2d (
        .ap_clk(aclk),
        .ap_rst_n(aresetn),
        
        // Input stream
        .p_inStream_TVALID(axis_sink_tvalid),
        .p_inStream_TREADY(axis_sink_tready),
        .p_inStream_TDATA(axis_sink_tdata),
        //.p_inStream_TLAST(axis_sink_tlast),
        
        // Output stream
        .p_outStream_TVALID(axis_src_tvalid),
        .p_outStream_TREADY(axis_src_tready),
        .p_outStream_TDATA(axis_src_tdata)
        //.p_outStream_TLAST(axis_src_tlast)
    );
        
    // Tie off unused interfaces
    always_comb axi_ctrl.tie_off_s();
    always_comb notify.tie_off_m();
    always_comb sq_rd.tie_off_m();
    always_comb sq_wr.tie_off_m();
    always_comb cq_rd.tie_off_s();
    always_comb cq_wr.tie_off_s();

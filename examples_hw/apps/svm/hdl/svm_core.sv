`timescale 1ns / 1ps
`include "lynxTypes.svh"

module svm_core (
    input  logic         ap_clk,
    input  logic         ap_rst_n,
    
    // AXI-Stream slave interface for input
    input  logic [31:0]  input_r_TDATA,
    input  logic         input_r_TVALID,
    output logic         input_r_TREADY,
    
    // AXI-Stream master interface for output
    output logic [31:0]  output_r_TDATA,
    output logic         output_r_TVALID,
    input  logic         output_r_TREADY,
    
    // Interrupt
    output logic         interrupt,
    
    // AXI-Lite slave interface for control
    AXI_LITE.slave      s_axi_control
);

    // Instantiate SVM IP (from DCP)
    design_1_svm_speech_30_0_0 svm_speech_30_inst (
        .ap_clk                 (ap_clk),
        .ap_rst_n              (ap_rst_n),
        
        // AXI-Stream input
        .input_r_TDATA         (input_r_TDATA),
        .input_r_TVALID        (input_r_TVALID),
        .input_r_TREADY        (input_r_TREADY),
        
        // AXI-Stream output
        .output_r_TDATA        (output_r_TDATA),
        .output_r_TVALID       (output_r_TVALID),
        .output_r_TREADY       (output_r_TREADY),
        
        // Interrupt
        .interrupt             (interrupt),
        
        // AXI-Lite control interface
        .s_axi_control_ARADDR  (s_axi_control.araddr),
        .s_axi_control_ARVALID (s_axi_control.arvalid),
        .s_axi_control_ARREADY (s_axi_control.arready),
        .s_axi_control_AWADDR  (s_axi_control.awaddr),
        .s_axi_control_AWVALID (s_axi_control.awvalid),
        .s_axi_control_AWREADY (s_axi_control.awready),
        .s_axi_control_WDATA   (s_axi_control.wdata),
        .s_axi_control_WSTRB   (s_axi_control.wstrb),
        .s_axi_control_WVALID  (s_axi_control.wvalid),
        .s_axi_control_WREADY  (s_axi_control.wready),
        .s_axi_control_BRESP   (s_axi_control.bresp),
        .s_axi_control_BVALID  (s_axi_control.bvalid),
        .s_axi_control_BREADY  (s_axi_control.bready),
        .s_axi_control_RDATA   (s_axi_control.rdata),
        .s_axi_control_RRESP   (s_axi_control.rresp),
        .s_axi_control_RVALID  (s_axi_control.rvalid),
        .s_axi_control_RREADY  (s_axi_control.rready)
    );

endmodule
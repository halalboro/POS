`timescale 1ns / 1ps
`include "lynxTypes.svh"

module svm_top (
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

    // Instantiate SVM core
    svm_core core (
        .ap_clk            (ap_clk),
        .ap_rst_n          (ap_rst_n),
        .input_r_TDATA     (input_r_TDATA),
        .input_r_TVALID    (input_r_TVALID),
        .input_r_TREADY    (input_r_TREADY),
        .output_r_TDATA    (output_r_TDATA),
        .output_r_TVALID   (output_r_TVALID),
        .output_r_TREADY   (output_r_TREADY),
        .interrupt         (interrupt),
        .s_axi_control     (s_axi_control)
    );

endmodule
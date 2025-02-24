/* refer to sha.v file in the same directory for the rest of the code (design_1 module) */

`timescale 1ns / 1ps

module design_1_tb;
    // Clock and reset
    reg clk = 0;
    reg rst_n = 0;

    // Instantiate AXI4SR interfaces
    AXI4SR #(.AXI4S_DATA_BITS(512)) axis_sink_int (.aclk(clk));
    AXI4SR #(.AXI4S_DATA_BITS(512)) axis_src_int (.aclk(clk));

    // Test data storage
    reg [511:0] input_data;
    integer i;

    // Clock generation (100MHz)
    always #5 clk = ~clk;

    // DUT instance
    design_1 dut (
        .aclk(clk),
        .aresetn(rst_n),
        .rd_tdata(axis_sink_int.tdata),
        .rd_tvalid(axis_sink_int.tvalid),
        .rd_tready(axis_sink_int.tready),
        .rd_tlast(axis_sink_int.tlast),
        .wr_tdata(axis_src_int.tdata),
        .wr_tvalid(axis_src_int.tvalid),
        .wr_tready(axis_src_int.tready),
        .wr_tlast(axis_src_int.tlast)
    );

    // Main test sequence
    initial begin
        // Initialize signals
        rst_n = 0;
        axis_sink_int.tvalid = 0;
        axis_sink_int.tdata = 0;
        axis_sink_int.tlast = 0;
        axis_src_int.tready = 1;

        // Reset sequence
        #100;
        rst_n = 1;
        #100;

        // Test case 1: Simple pattern
        $display("Starting SHA256 test...");

        // Create test pattern (similar to C++ code)
        for(i = 0; i < 512/32; i = i + 1) begin
            input_data[i*32 +: 32] = i;  // Simple incrementing pattern
        end

        // Send data
        @(posedge clk);
        axis_sink_int.tvalid = 1'b1;
        axis_sink_int.tdata = input_data;
        axis_sink_int.tlast = 1'b1;

        while (!axis_sink_int.tready) @(posedge clk);

        $display("Input data sent: %h", input_data);

        @(posedge clk);
        axis_sink_int.tvalid = 1'b0;
        axis_sink_int.tlast = 1'b0;

        // Wait for hash result
        $display("\nWaiting for SHA256 hash...");
        while(!axis_src_int.tvalid) @(posedge clk);

        if (axis_src_int.tvalid) begin
            $display("SHA256 Hash: %h", axis_src_int.tdata[255:0]);
        end

        #1000;
        $finish;
    end

    // Monitor for protocol violations
    always @(posedge clk) begin
        if(axis_sink_int.tvalid && !axis_sink_int.tready) begin
            $display("Cycle %0t: Waiting for TREADY", $time);
        end
    end

    // Timeout watchdog
    initial begin
        #100000; // 1ms timeout
        $display("Timeout - Test Failed");
        $finish;
    end
endmodule
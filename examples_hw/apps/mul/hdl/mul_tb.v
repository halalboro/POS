`timescale 1ns / 1ps
module multiplier_tb();
    // Testbench signals
    reg aclk;
    reg aresetn;
    reg s_axis_tvalid;
    wire s_axis_tready;
    reg [63:0] s_axis_tdata;
    reg s_axis_tlast;
    wire m_axis_tvalid;
    reg m_axis_tready;
    wire [63:0] m_axis_tdata;
    wire m_axis_tlast;
    wire [6:0] dbg_row_cnt;
    wire [6:0] dbg_col_cnt;
    wire [13:0] dbg_dist;

    // Instantiate the Unit Under Test (UUT)
    multiplier uut (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tdata(s_axis_tdata),
        .s_axis_tlast(s_axis_tlast),
        .m_axis_tvalid(m_axis_tvalid),
        .m_axis_tready(m_axis_tready),
        .m_axis_tdata(m_axis_tdata),
        .m_axis_tlast(m_axis_tlast),
        .dbg_row_cnt(dbg_row_cnt),
        .dbg_col_cnt(dbg_col_cnt),
        .dbg_dist(dbg_dist)
    );

    // Clock generation
    initial begin
        aclk = 0;
        forever #5 aclk = ~aclk;
    end

    // Task to apply test data and wait for result
    task apply_test;
        input [31:0] real_in;
        input [31:0] imag_in;
        input [6:0] row;
        input [6:0] col;
        begin
            // Wait for ready
            @(posedge aclk);
            while (!s_axis_tready) @(posedge aclk);
            
            // Apply stimulus
            s_axis_tvalid <= 1'b1;
            s_axis_tdata <= {real_in, imag_in};
            s_axis_tlast <= (row == 7'd127 && col == 7'd127);
            
            // Wait one clock cycle
            @(posedge aclk);
            
            // Deassert valid
            s_axis_tvalid <= 1'b0;
            
            // Wait for valid output
            while (!m_axis_tvalid) @(posedge aclk);
            
            // Log results
            $display("Test at [%0d,%0d] - Distance: %0d", row, col, dbg_dist);
            $display("Input: %h + %hi", real_in, imag_in);
            $display("Output: %h + %hi", m_axis_tdata[63:32], m_axis_tdata[31:0]);
            $display("--------------------");
            
            // Add delay between tests
            repeat(5) @(posedge aclk);
        end
    endtask

    // Main test process
    initial begin
        // Initialize
        aresetn = 0;
        s_axis_tvalid = 0;
        s_axis_tdata = 0;
        s_axis_tlast = 0;
        m_axis_tready = 1;  // Keep ready high for simplicity

        // Reset sequence
        repeat(10) @(posedge aclk);
        aresetn = 1;
        repeat(10) @(posedge aclk);

        // Test sequence
        
        // Test 1: DC component (center)
        $display("\nTest 1: DC Component");
        apply_test(32'h3F800000, 32'h3F800000, 7'd64, 7'd64);

        // Test 2: Low frequency
        $display("\nTest 2: Low Frequency");
        apply_test(32'h3F800000, 32'h3F800000, 7'd32, 7'd32);

        // Test 3: Mid frequency
        $display("\nTest 3: Mid Frequency");
        apply_test(32'h3F800000, 32'h3F800000, 7'd16, 7'd16);

        // Test 4: High frequency (corner)
        $display("\nTest 4: High Frequency");
        apply_test(32'h3F800000, 32'h3F800000, 7'd0, 7'd0);

        // Test 5: Negative value test
        $display("\nTest 5: Negative Value");
        apply_test(32'hBF800000, 32'h3F800000, 7'd127, 7'd127);

        // End simulation
        repeat(10) @(posedge aclk);
        $display("\nSimulation completed");
        $finish;
    end

    // Monitoring process
    initial begin
        $monitor("Time=%0t row=%0d col=%0d dist=%0d valid_in=%b valid_out=%b",
                 $time, dbg_row_cnt, dbg_col_cnt, dbg_dist,
                 s_axis_tvalid, m_axis_tvalid);
    end

endmodule
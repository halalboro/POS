module design_1_tb;
    // Clock and reset
    reg clk = 0;
    reg rst_n = 0;
    
    // Instantiate AXI4SR interfaces   
    AXI4SR #(.AXI4S_DATA_BITS(512)) s_axis_input (.aclk(clk));
    AXI4SR #(.AXI4S_DATA_BITS(512)) m_axis_output (.aclk(clk));
    
    // Test data storage
    reg [255:0] message_data;
    reg [255:0] rsa_modulus = 256'hF4F5E3D2C1B0A9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA98;
    reg [19:0] rsa_exponent = 20'h10001;
    
    // Clock generation (100MHz)
    always #5 clk = ~clk;
    
    // DUT instance
    design_1_wrapper dut (
        .ap_clk_0(clk),
        .ap_rst_n_0(rst_n),
        // Message input interface
        .S_AXIS_0_tdata(s_axis_input.tdata),
        .S_AXIS_0_tvalid(s_axis_input.tvalid),
        .S_AXIS_0_tready(s_axis_input.tready),
        .S_AXIS_0_tlast(s_axis_input.tlast),
        .S_AXIS_0_tid(s_axis_input.tid),
        .S_AXIS_0_tkeep(s_axis_input.tkeep),
        // RSA parameters
        .modulus_0(rsa_modulus),
        .exponent_0(rsa_exponent),
        // Result output interface
        .M_AXIS_0_tdata(m_axis_output.tdata),
        .M_AXIS_0_tvalid(m_axis_output.tvalid),
        .M_AXIS_0_tready(m_axis_output.tready),
        .M_AXIS_0_tlast(m_axis_output.tlast),
        .M_AXIS_0_tid(m_axis_output.tid),
        .M_AXIS_0_tkeep(m_axis_output.tkeep)
    );

    // Main test sequence
    initial begin
        // Initialize signals
        rst_n = 0;
        s_axis_input.tvalid = 0;
        s_axis_input.tdata = 0;
        s_axis_input.tkeep = 32'hFFFFFFFF;
        m_axis_output.tkeep = 32'hFFFFFFFF;
        s_axis_input.tlast = 0;
        m_axis_output.tready = 1;

        // Reset sequence
        #100;
        rst_n = 1;
        #100;

        // Send test message
        $display("Starting RSA encryption test...");
        
        // Test message (can be modified as needed)
        message_data = 256'hA5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5;
        
        // Send message
        @(posedge clk);
        wait(s_axis_input.tready);
        s_axis_input.tvalid = 1'b1;
        s_axis_input.tdata = message_data;
        s_axis_input.tlast = 1'b1;
        
        // Wait for transfer completion
        @(posedge clk);
        while (!s_axis_input.tready) @(posedge clk);
        s_axis_input.tvalid = 1'b0;
        s_axis_input.tlast = 1'b0;
        
        $display("Message sent: %h", message_data);

        // Wait for result
        $display("\nWaiting for RSA result...");
        @(posedge clk);
        while(!m_axis_output.tvalid) @(posedge clk);
        
        if (m_axis_output.tvalid) begin
            $display("RSA Result: %h", m_axis_output.tdata);
        end

        #1000;
        $finish;
    end

    // Timeout watchdog
    initial begin
        #100000; // 1ms timeout
        $display("Timeout - Test Failed");
        $finish;
    end
endmodule
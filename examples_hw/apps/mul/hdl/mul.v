module multiplier (
    input wire        aclk,  
    input wire        aresetn,  
    
    // Input interface
    input wire        s_axis_tvalid,  
    output wire       s_axis_tready,  
    input wire [63:0] s_axis_tdata,      // [63:32]=real, [31:0]=imag from FFT
    input wire        s_axis_tlast,  
    
    // Output interface
    output wire       m_axis_tvalid,  
    input wire        m_axis_tready,  
    output wire [63:0] m_axis_tdata,      // [63:32]=real, [31:0]=imag
    output wire       m_axis_tlast
);
    // Position tracking counters for frequency domain
    reg [6:0] row_cnt, col_cnt;
    
    // Counter logic 
    always @(posedge aclk) begin
        if (!aresetn) begin
            row_cnt <= 7'd0;
            col_cnt <= 7'd0;
        end 
        else if (s_axis_tlast) begin
            row_cnt <= 7'd0;
            col_cnt <= 7'd0;
        end
        else if (s_axis_tvalid && s_axis_tready) begin
            if (row_cnt == 7'd127) begin
                row_cnt <= 7'd0;
                if (col_cnt == 7'd127)
                    col_cnt <= 7'd0;
                else
                    col_cnt <= col_cnt + 1;
            end
            else begin
                row_cnt <= row_cnt + 1;
            end
        end
    end

    // Calculate distance from DC (center point)
    wire [6:0] row_diff = (row_cnt >= 7'd64) ? (row_cnt - 7'd64) : (7'd64 - row_cnt);
    wire [6:0] col_diff = (col_cnt >= 7'd64) ? (col_cnt - 7'd64) : (7'd64 - col_cnt);
    wire [13:0] dist_from_dc = (row_diff * row_diff) + (col_diff * col_diff);

    // Register distance calculation for timing
    reg [13:0] dist_from_dc_reg;
    always @(posedge aclk) begin
        if (!aresetn)
            dist_from_dc_reg <= 14'd0;
        else if (s_axis_tvalid && s_axis_tready)
            dist_from_dc_reg <= dist_from_dc;
    end

    // IEEE-754 components for inputs
    wire fft_real_sign = s_axis_tdata[63];
    wire fft_imag_sign = s_axis_tdata[31];
    wire [7:0] fft_real_exp = s_axis_tdata[62:55];
    wire [7:0] fft_imag_exp = s_axis_tdata[30:23];
    wire [22:0] fft_real_mant = s_axis_tdata[54:32];
    wire [22:0] fft_imag_mant = s_axis_tdata[22:0];

    // High-pass filter coefficient selection based on frequency position
    reg [31:0] filter_coeff;
    
    always @(*) begin
        if (dist_from_dc_reg <= 500) begin            // DC and very low frequencies
            filter_coeff = 32'h3F000000;              // 0.5 in float32
        end
        else if (dist_from_dc_reg <= 2000) begin      // Low frequencies
            filter_coeff = 32'h3F400000;              // 0.75 in float32
        end
        else if (dist_from_dc_reg <= 4000) begin      // Mid frequencies
            filter_coeff = 32'h3F800000;              // 1.0 in float32
        end
        else begin                                    // High frequencies
            filter_coeff = 32'h40000000;              // 2.0 in float32
        end
    end

    // Filter coefficient components
    wire filt_coeff_sign = filter_coeff[31];
    wire [7:0] filt_coeff_exp = filter_coeff[30:23];
    wire [22:0] filt_coeff_mant = filter_coeff[22:0];
    
    // Floating point multiplication logic
    wire [24:0] real_mant_temp = ({1'b1, fft_real_mant} * {1'b1, filt_coeff_mant});
    wire [24:0] imag_mant_temp = ({1'b1, fft_imag_mant} * {1'b1, filt_coeff_mant});
    
    wire real_sign = fft_real_sign ^ filt_coeff_sign;
    wire imag_sign = fft_imag_sign ^ filt_coeff_sign;
    
    wire [8:0] real_exp_pre = fft_real_exp + filt_coeff_exp - 8'd127;
    wire [8:0] imag_exp_pre = fft_imag_exp + filt_coeff_exp - 8'd127;
    
    wire [7:0] real_exp = real_mant_temp[24] ? real_exp_pre + 1 : real_exp_pre;
    wire [7:0] imag_exp = imag_mant_temp[24] ? imag_exp_pre + 1 : imag_exp_pre;
    
    wire [22:0] real_mant = real_mant_temp[24] ? real_mant_temp[23:1] : real_mant_temp[22:0];
    wire [22:0] imag_mant = imag_mant_temp[24] ? imag_mant_temp[23:1] : imag_mant_temp[22:0];
    
    // Output registers
    reg [63:0] result_data;
    reg        result_valid;
    reg        result_last;
    
    // Register outputs
    always @(posedge aclk) begin
        if(!aresetn) begin
            result_data <= 64'h0;
            result_valid <= 1'b0;
            result_last <= 1'b0;
        end
        else if(s_axis_tvalid && s_axis_tready) begin
            // Compose final results
            result_data <= {real_sign, real_exp, real_mant,
                          imag_sign, imag_exp, imag_mant};
            result_valid <= 1'b1;
            result_last <= s_axis_tlast;
        end
        else if(m_axis_tready && result_valid) begin
            result_valid <= 1'b0;
        end
    end

    // Output assignments
    assign m_axis_tdata = result_data;
    assign m_axis_tvalid = result_valid;
    assign m_axis_tlast = result_last;
    
    // Ready when not processing or output is being accepted
    assign s_axis_tready = (!result_valid || m_axis_tready);

endmodule
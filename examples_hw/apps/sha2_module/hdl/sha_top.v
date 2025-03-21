module sha2 (
    // Clock and reset
    input  wire         aclk,
    input  wire         aresetn,
    
    // Input AXI4-Stream interface (512-bit width)
    input  wire [511:0] axis_sink_tdata,
    input  wire         axis_sink_tvalid,
    input  wire         axis_sink_tlast,
    output wire         axis_sink_tready,
    
    // Output AXI4-Stream interface
    output wire [511:0] axis_src_tdata,
    // output wire [255:0] axis_src_tdata,
    output wire [63:0]  axis_src_tkeep,
    output wire [7:0]   axis_src_tid,
    output wire         axis_src_tvalid,
    output wire         axis_src_tlast,
    input  wire         axis_src_tready
);

    // Internal signals for byte-wise SHA-256 module
    wire         sha_tready;
    wire         sha_tvalid;
    wire         sha_tlast;
    wire [31:0]  sha_tid;
    wire [7:0]   sha_tdata;
    wire         sha_ovalid;
    wire [31:0]  sha_oid;
    wire [60:0]  sha_olen;
    wire [255:0] sha_osha;
    
    // Serialization state machine
    localparam IDLE = 2'b00,
               SENDING = 2'b01,
               WAITING = 2'b10;
    
    reg [1:0]   state;
    reg [6:0]   byte_counter;    // Counts bytes within 512-bit block (0-63)
    reg         final_chunk;     // Indicates if current chunk is the final one
    reg [511:0] data_buffer;     // Buffer to hold the 512-bit data
    reg [7:0]   byte_array[0:63]; // Array to hold bytes for better control
    
    // Control signals
    wire        start_serialization;
    
    // Connect SHA256 outputs to AXI4-Stream output interface
    assign axis_src_tdata  = {256'b0, sha_osha};
    // assign axis_src_tdata  = sha_osha;
    assign axis_src_tkeep  = 64'hffffffffffffffff; // All bytes valid
    assign axis_src_tid    = sha_oid[7:0];         // Use lower 8 bits of ID
    assign axis_src_tvalid = sha_ovalid;
    assign axis_src_tlast  = sha_ovalid;           // Last flag same as valid
    
    // Serialization control
    assign start_serialization = (state == IDLE) && axis_sink_tvalid;
    
    // Input flow control
    assign axis_sink_tready = (state == IDLE);
    
    // Set valid when in SENDING state
    assign sha_tvalid = (state == SENDING);
    
    // Only assert tlast on the final byte of the final chunk
    assign sha_tlast = (state == SENDING) && (byte_counter == 7'd63) && final_chunk;
    
    // Select current byte to send to SHA256 module
    assign sha_tdata = byte_array[byte_counter];
    
    // Fixed ID value (can be modified if needed)
    assign sha_tid = 32'h0;
    
    integer i;
    
    // State machine and serialization logic
    always @(posedge aclk or negedge aresetn) begin
        if (~aresetn) begin
            state <= IDLE;
            byte_counter <= 7'd0;
            final_chunk <= 1'b0;
            data_buffer <= 512'd0;
            
            // Clear byte array
            for (i = 0; i < 64; i = i + 1) begin
                byte_array[i] <= 8'h0;
            end
        end else begin
            case (state)
                IDLE: begin
                    if (start_serialization) begin
                        state <= SENDING;
                        byte_counter <= 7'd0;
                        final_chunk <= axis_sink_tlast;
                        data_buffer <= axis_sink_tdata;
                        
                        // Convert 512-bit word to byte array for consistent processing
                        for (i = 0; i < 64; i = i + 1) begin
                            byte_array[i] <= axis_sink_tdata[8*(63-i) +: 8];
                        end
                    end
                end
                
                SENDING: begin
                    if (sha_tready) begin
                        // Increment byte counter
                        if (byte_counter == 7'd63) begin
                            byte_counter <= 7'd0;
                            
                            if (final_chunk) begin
                                // If this was the final chunk, go to WAITING state
                                state <= WAITING;
                            end else begin
                                // Otherwise, go back to IDLE to get next chunk
                                state <= IDLE;
                            end
                        end else begin
                            byte_counter <= byte_counter + 7'd1;
                        end
                    end
                end
                
                WAITING: begin
                    // Wait for SHA output to be ready before accepting new input
                    if (sha_ovalid) begin
                        state <= IDLE;
                        final_chunk <= 1'b0; // Reset for next message
                    end
                end
                
                default: state <= IDLE;
            endcase
        end
    end
    
    // Debug logic - uncomment for simulation debugging
    /*
    always @(posedge aclk) begin
        if (sha_tvalid && sha_tready) begin
            $display("Sending byte: %h, byte_counter: %d, final_chunk: %b, sha_tlast: %b", 
                     sha_tdata, byte_counter, final_chunk, sha_tlast);
        end
        if (sha_ovalid) begin
            $display("SHA output valid! Hash: %h", sha_osha);
        end
    end
    */
    
    // Instantiate the original SHA-256 module
    sha256 sha256_inst (
        .rstn(aresetn),
        .clk(aclk),
        // Input interface
        .tready(sha_tready),
        .tvalid(sha_tvalid),
        .tlast(sha_tlast),
        .tid(sha_tid),
        .tdata(sha_tdata),
        // Output interface
        .ovalid(sha_ovalid),
        .oid(sha_oid),
        .olen(sha_olen),
        .osha(sha_osha)
    );

    ila_sha2_ul inst_ila_sha2_ul (
        .clk(aclk),
        .probe0(start_serialization),
        .probe1(sha_ovalid),
        .probe2(sha_tlast),
        .probe3(final_chunk),
        .probe4(state),
        .probe5(axis_src_tdata)
    );

endmodule
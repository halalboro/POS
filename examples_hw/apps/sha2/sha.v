module design_1(
    input        aclk,
    input        aresetn,
    input        rd_tvalid,
    input        rd_tlast,
    output       rd_tready,
    input [511:0] rd_tdata,
    output       wr_tvalid,
    input        wr_tready,
    output       wr_tlast,
    output [511:0] wr_tdata
);

wire rd_tvalid;
wire rd_tlast;
wire rd_tready;
wire [511:0] rd_tdata;

wire wr_tvalid;
wire wr_tready;
wire wr_tlast;
wire [511:0] wr_tdata;

// FIFO signals
wire idf_wrreq;
wire [511:0] idf_din;
wire idf_full;
wire idf_rdreq;
wire idf_valid;
wire [511:0] idf_dout;
wire idf_empty;

assign idf_wrreq = rd_tvalid;
assign idf_din = rd_tdata;

quick_fifo #(.FIFO_WIDTH(512), .FIFO_DEPTH_BITS(9), .FIFO_ALMOSTFULL_THRESHOLD(508))
InDataFIFO
(
    .clk        (aclk),
    .reset_n    (aresetn),
    .din        (rd_tdata),
    .we         (idf_wrreq),
    .re         (idf_rdreq),
    .dout       (idf_dout),
    .empty      (idf_empty),
    .valid      (idf_valid),
    .full       (idf_full),
    .count      (),
    .almostfull ()
);

//// SHA256 core
// state and signals
reg [63:0] sha_words;
reg [64:0] sha_valid;
wire sha_in_valid = idf_valid;
wire sha_out_valid = sha_valid[64];
wire sha_out_valid_prev = sha_valid[63];
reg sha_done;

reg [31:0] sha_a_reg, sha_b_reg, sha_c_reg, sha_d_reg, sha_e_reg, sha_f_reg, sha_g_reg, sha_h_reg;
wire [255:0] sha_hash;
wire [31:0] sha_a = sha_hash[31:0];
wire [31:0] sha_b = sha_hash[63:32];
wire [31:0] sha_c = sha_hash[95:64];
wire [31:0] sha_d = sha_hash[127:96];
wire [31:0] sha_e = sha_hash[159:128];
wire [31:0] sha_f = sha_hash[191:160];
wire [31:0] sha_g = sha_hash[223:192];
wire [31:0] sha_h = sha_hash[255:224];
wire [511:0] sha_chunk = idf_dout;

// Output data is valid only if core is done and wr_ready is high
wire [255:0] sha_out;
assign sha_out = {sha_h_reg, sha_g_reg, sha_f_reg, sha_e_reg, sha_d_reg, sha_c_reg, sha_b_reg, sha_a_reg};
assign wr_tdata = sha_out;
assign wr_tvalid = sha_done && wr_tready;
assign wr_tlast = sha_done && wr_tready;

// logic
assign idf_rdreq = 1;
always @(posedge aclk) begin
    if (~aresetn) begin
        sha_valid <= 0;
        sha_words <= 0;
        sha_done <= 0;
        sha_a_reg <= 0;
        sha_b_reg <= 0;
        sha_c_reg <= 0;
        sha_d_reg <= 0;
        sha_e_reg <= 0;
        sha_f_reg <= 0;
        sha_g_reg <= 0;
        sha_h_reg <= 0;
    end
    else begin
        sha_valid <= {sha_valid[63:0], sha_in_valid};
        if (sha_out_valid) begin
            sha_a_reg <= sha_a_reg + sha_a;
            sha_b_reg <= sha_b_reg + sha_b;
            sha_c_reg <= sha_c_reg + sha_c;
            sha_d_reg <= sha_d_reg + sha_d;
            sha_e_reg <= sha_e_reg + sha_e;
            sha_f_reg <= sha_f_reg + sha_f;
            sha_g_reg <= sha_g_reg + sha_g;
            sha_h_reg <= sha_h_reg + sha_h;
            sha_words <= sha_words + 1;
            
            if(!sha_out_valid_prev) begin
                sha_done <= 1;
            end
        end
    end
end

// instantiation
sha256_transform #(
    .LOOP(1)
) sha (
    .clk(aclk),
    .rst_n(aresetn),
    .feedback(0),
    .cnt(0),
    .rx_state(256'h5be0cd191f83d9ab9b05688c510e527fa54ff53a3c6ef372bb67ae856a09e667),
    .rx_input(sha_chunk),
    .tx_hash(sha_hash)
);

endmodule
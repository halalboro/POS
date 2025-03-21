/**
* VFPGA Top Module - Digital Signature Pipeline
*/
import lynxTypes::*;

logic        axis_host_recv_tvalid;
logic        axis_host_recv_tready;
logic [511:0] axis_host_recv_tdata;
logic [31:0]  axis_host_recv_tkeep;
logic [15:0] axis_host_recv_tid;
logic        axis_host_recv_tlast;

logic        axis_host_send_tvalid;
logic        axis_host_send_tready;
logic [255:0] axis_host_send_tdata;
logic [31:0]  axis_host_send_tkeep;
logic [15:0] axis_host_send_tid;
logic        axis_host_send_tlast;

// Interface declarations
AXI4SR axis_sink_int ();
AXI4SR axis_src_int (); 

// Host stream routing
axisr_reg inst_reg_sink (.aclk(aclk),.aresetn(aresetn),.s_axis(axis_host_recv[0]),.m_axis(axis_sink_int));
axisr_reg inst_reg_src (.aclk(aclk),.aresetn(aresetn),.s_axis(axis_src_int),.m_axis(axis_host_send[0]));

always_comb begin
	axis_host_recv_tkeep = 1;
	axis_host_send_tkeep = 1;
	axis_host_recv_tid = 0;
	axis_host_send_tid = 0;
end	

// Input assignments
assign axis_host_recv_tdata  = axis_sink_int.tdata;
assign axis_host_recv_tvalid = axis_sink_int.tvalid;
assign axis_host_recv_tlast  = axis_sink_int.tlast;

/////////////////////// FIFO signals ///////////////////////
wire idf_wrreq;
wire [511:0] idf_din;
wire idf_full;
wire idf_rdreq;
wire idf_valid;
wire [511:0] idf_dout;
wire idf_empty;

assign idf_wrreq = axis_host_recv_tvalid;
assign idf_din   = axis_host_recv_tdata;

// Can't read more data if the queue is full
assign axis_sink_int.tready = !idf_full;

quick_fifo #(.FIFO_WIDTH(512),.FIFO_DEPTH_BITS(9),.FIFO_ALMOSTFULL_THRESHOLD(508)) 
InDataFIFO 
(
  .clk                (aclk),
  .reset_n            (aresetn),
  .din                (axis_host_recv_tdata),
  .we                 (idf_wrreq),
  .re                 (idf_rdreq),
  .dout               (idf_dout),
  .empty              (idf_empty),
  .valid              (idf_valid),
  .full               (idf_full),
  .count              (),
  .almostfull         ()
);

/////////////////////// SHA256 core ///////////////////////
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

// Output data is valid only if core is done and wr_tready is high
wire [255:0] sha_out;
assign sha_out = {sha_h_reg, sha_g_reg, sha_f_reg, sha_e_reg, sha_d_reg, sha_c_reg, sha_b_reg, sha_a_reg};

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



/////////////////////// RSA core ///////////////////////
// state and signals
wire sha_to_rsa_tready;
logic [255:0] rsa_modulus;
logic [19:0] rsa_exponent;

// Default RSA key assignments
assign rsa_modulus = 256'hF4F5E3D2C1B0A9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA98;
assign rsa_exponent = 20'h10001;

// RSA instance
rsa inst_rsa (
   .ap_clk(aclk),
   .ap_rst_n(aresetn),
   // Connect to SHA-256 output
   .message_TVALID(sha_done),
   .message_TREADY(sha_to_rsa_tready),
   .message_TDATA(sha_out),
   .message_TLAST(sha_done),
   // Static inputs
   .modulus(rsa_modulus),
   .exponent(rsa_exponent),
   // Output to width converter
   .result_TVALID(axis_host_send_tvalid),
   .result_TREADY(axis_host_send_tready),
   .result_TDATA(axis_host_send_tdata),
   .result_TLAST(axis_host_send_tlast)
);



// Width converter for final output (256->512)
dwidth_converter_256_512 inst_dwidth_send (
   .aclk(aclk),
   .aresetn(aresetn),
   .s_axis_tvalid(axis_host_send_tvalid),
   .s_axis_tready(axis_host_send_tready),
   .s_axis_tdata(axis_host_send_tdata),
   .s_axis_tlast(axis_host_send_tlast),
   .s_axis_tid(axis_host_send_tid),
   .m_axis_tvalid(axis_src_int.tvalid),
   .m_axis_tready(axis_src_int.tready),
   .m_axis_tdata(axis_src_int.tdata),
   .m_axis_tlast(axis_src_int.tlast),
   .m_axis_tid(axis_src_int.tid)
);

// Tie-off unused
always_comb axi_ctrl.tie_off_s();
always_comb notify.tie_off_m();
always_comb sq_rd.tie_off_m();
always_comb sq_wr.tie_off_m();
always_comb cq_rd.tie_off_s();
always_comb cq_wr.tie_off_s();

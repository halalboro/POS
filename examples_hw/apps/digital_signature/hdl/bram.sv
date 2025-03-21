module bram #(
    parameter DATA_WIDTH = 32,
    parameter ADDR_WIDTH = 8
) (
    input  wire                     clk,
    input  wire                     we,
    input  wire                     re,  
    input  wire [ADDR_WIDTH-1:0]    raddr,
    input  wire [ADDR_WIDTH-1:0]    waddr,  
    input  wire [DATA_WIDTH-1:0]    din,
    output reg  [DATA_WIDTH-1:0]    dout
);

`ifdef VENDOR_XILINX
(* ram_extract = "yes", ram_style = "block" *) reg [DATA_WIDTH-1:0] mem[0:2**ADDR_WIDTH-1];
`else
(* ramstyle = "no_rw_check" *) reg [DATA_WIDTH-1:0] mem[0:2**ADDR_WIDTH-1];
`endif

    always @(posedge clk) begin
        if (we)
            mem[waddr] <= din;		
        if (re)
	    dout <= mem[raddr];
    end
			
endmodule

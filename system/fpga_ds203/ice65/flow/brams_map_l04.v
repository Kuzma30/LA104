// Yosys techmap for the iCE65 L04 block RAM (256x16, SB_RAM40_4K in mode 0).
module $__ICE65_RAM4K_ (...);

parameter INIT = 0;
parameter PORT_R_CLK_POL = 1;
parameter PORT_W_CLK_POL = 1;

input PORT_R_CLK;
input PORT_R_RD_EN;
input [7:0] PORT_R_ADDR;
output [15:0] PORT_R_RD_DATA;

input PORT_W_CLK;
input PORT_W_WR_EN;
input [15:0] PORT_W_WR_BE;
input [7:0] PORT_W_ADDR;
input [15:0] PORT_W_WR_DATA;

localparam [4095:0] INIT_P = INIT;

`define INSTANCE(type, rclk, wclk) \
	type #( \
		.INIT_0(INIT_P[255:0]), \
		.INIT_1(INIT_P[511:256]), \
		.INIT_2(INIT_P[767:512]), \
		.INIT_3(INIT_P[1023:768]), \
		.INIT_4(INIT_P[1279:1024]), \
		.INIT_5(INIT_P[1535:1280]), \
		.INIT_6(INIT_P[1791:1536]), \
		.INIT_7(INIT_P[2047:1792]), \
		.INIT_8(INIT_P[2303:2048]), \
		.INIT_9(INIT_P[2559:2304]), \
		.INIT_A(INIT_P[2815:2560]), \
		.INIT_B(INIT_P[3071:2816]), \
		.INIT_C(INIT_P[3327:3072]), \
		.INIT_D(INIT_P[3583:3328]), \
		.INIT_E(INIT_P[3839:3584]), \
		.INIT_F(INIT_P[4095:3840]), \
		.READ_MODE(0), \
		.WRITE_MODE(0) \
	) _TECHMAP_REPLACE_ ( \
		.RDATA(PORT_R_RD_DATA), \
		.rclk(PORT_R_CLK), \
		.RCLKE(PORT_R_RD_EN), \
		.RE(1'b1), \
		.RADDR({3'b000, PORT_R_ADDR}), \
		.WDATA(PORT_W_WR_DATA), \
		.wclk(PORT_W_CLK), \
		.WCLKE(PORT_W_WR_EN), \
		.WE(1'b1), \
		.WADDR({3'b000, PORT_W_ADDR}), \
		.MASK(~PORT_W_WR_BE) \
	);

generate
case ({PORT_R_CLK_POL, PORT_W_CLK_POL})
	2'b11: `INSTANCE(SB_RAM40_4K, RCLK, WCLK)
	2'b10: `INSTANCE(SB_RAM40_4KNW, RCLK, WCLKN)
	2'b01: `INSTANCE(SB_RAM40_4KNR, RCLKN, WCLK)
	2'b00: `INSTANCE(SB_RAM40_4KNRNW, RCLKN, WCLKN)
endcase
endgenerate

endmodule

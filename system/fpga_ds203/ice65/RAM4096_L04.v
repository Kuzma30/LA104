// 4096 x WIDTH dual-port memory for iCE65 L04.
//
// The L04 block RAM is 256x16 only. Each data bit gets one block used as
// 4096x1: the low 8 address bits select the word, the high 4 select the bit
// inside the word (per-bit write mask on write, 16:1 mux on read). This is
// the same structure the vendor tools produced for the stock DS203 FPGA.
//
// Write: rising edge of Wclk when We is high. Read: falling edge of nRclk.
module RAM4096_L04 #(parameter WIDTH = 18) (
  input              Wclk,
  input              We,
  input  [11:0]      Waddr,
  input  [WIDTH-1:0] Din,
  input              nRclk,
  input  [11:0]      Raddr,
  output [WIDTH-1:0] Dout
);
  wire [15:0] wmask = ~(16'd1 << Waddr[11:8]);   // 0 = write this bit
  reg  [3:0]  rsel = 0;
  always @(negedge nRclk) rsel <= Raddr[11:8];

  genvar i;
  generate
    for (i = 0; i < WIDTH; i = i + 1) begin : g_bit
      wire [15:0] rdata;
      SB_RAM40_4KNR #(.READ_MODE(0), .WRITE_MODE(0)) ram (
        .WCLK(Wclk), .WCLKE(1'b1), .WE(We),
        .WADDR({3'b000, Waddr[7:0]}), .WDATA({16{Din[i]}}), .MASK(wmask),
        .RCLKN(nRclk), .RCLKE(1'b1), .RE(1'b1),
        .RADDR({3'b000, Raddr[7:0]}), .RDATA(rdata)
      );
      assign Dout[i] = rdata[rsel];
    end
  endgenerate
endmodule

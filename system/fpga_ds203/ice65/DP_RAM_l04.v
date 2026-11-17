// DP_RAM for the iCE65L04 built with the open tools (yosys / nextpnr / icestorm).
// Same ports and behaviour as scope/DP_RAM.v (4096 x 18, write on the rising
// edge of Wclk, read on the falling edge of nRclk). The block RAM of the
// iCE65 is 256 x 16 only and the open flow does not build deeper memories
// from it, so the memory is made of 18 blocks used as 4096 x 1 (RAM4096_L04),
// the same structure as in the vendor-built V2.61 image.
module DP_RAM ( Din, Wclk, nRclk, Waddr, Raddr, Dout );

  input  Wclk;                 // Wr clock       -> rising edge
  input  nRclk;                // Rd clock       -> falling edge
  input  [11:0]Waddr;          // written address
  input  [11:0]Raddr;          // read address
  input  [17:0]Din;            // input data
  output [17:0]Dout;           // output data

  RAM4096_L04 #(.WIDTH(18)) Mem (
    .Wclk(Wclk), .We(1'b1), .Waddr(Waddr), .Din(Din),
    .nRclk(nRclk), .Raddr(Raddr), .Dout(Dout)
  );

endmodule

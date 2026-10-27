/*******************************************************************************
                 32 bit edge counters of a digital input (CH_C / CH_D)

 Added to the e-Design FPGA V2.5 for the LA104 OS (DS203).

 The counters are clocked by the input pin itself: they count every edge
 independently of the sampling clock (MCI is 60 Hz at 1 s/div), up to the
 speed of the input path. Rising edges are counted on the posedge, falling
 edges on the negedge, both edges = rising + falling.

 The value is kept in Gray code as well (registered in the same clock as the
 binary value): it changes in one bit per edge, so the MCU side (IO_Ctrl,
 latched by nRD) always reads the old or the new value, never a mix.

 Clr is asynchronous (from the serial register 0x0E of the MCU).
*******************************************************************************/

module EdgeCnt( Pin, Clr, GrayR, GrayF );

  input         Pin;            // digital input
  input         Clr;            // clear              -> active high
  output [31:0] GrayR;          // rising edges, Gray code
  output [31:0] GrayF;          // falling edges, Gray code

  reg    [31:0] BinR, BinF;
  reg    [31:0] GrayR, GrayF;

  wire   [31:0] NextR = BinR + 1;
  wire   [31:0] NextF = BinF + 1;

  initial begin
    BinR = 0; BinF = 0; GrayR = 0; GrayF = 0;
  end

  always @( posedge Pin or posedge Clr ) begin
    if ( Clr ) begin
      BinR  <= 0;
      GrayR <= 0;
    end else begin
      BinR  <= NextR;
      GrayR <= NextR ^ ( NextR >> 1 );
    end
  end

  always @( negedge Pin or posedge Clr ) begin
    if ( Clr ) begin
      BinF  <= 0;
      GrayF <= 0;
    end else begin
      BinF  <= NextF;
      GrayF <= NextF ^ ( NextF >> 1 );
    end
  end

endmodule

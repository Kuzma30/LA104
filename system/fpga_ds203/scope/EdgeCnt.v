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

/*******************************************************************************
      32 bit edge counters of the analog inputs CH_A / CH_B (FPGA 2.66)

 CH_A and CH_B come through the ADC, so their edges exist only in the
 sampling clock MCI: the level is the output of the comparator with
 hysteresis of QuadSync (registers 0x10 / 0x11 threshold / hysteresis of
 CH_A, 0x12 / 0x13 of CH_B, raw sample values as in the FIFO), an edge is a
 change of this level between two samples. A signal is counted exactly up to
 half the sampling rate (each level must last at least one sample); at a
 slow time base (MCI 60 Hz at 1 s/div) only slow signals are counted.

 One counter per channel counts both edges (the logic cells of the L04 are
 short). The edges of a comparator output alternate, so with the level
 before the first edge (Init, taken while the counter is 0) the MCU gets
 rising = (E + !Init) / 2 and falling = E - rising.

 As EdgeCnt the value is kept in binary and in Gray code, the MCU (nRD)
 reads the Gray value. Clr (register 0x0E bit 0, together with CH_C / CH_D)
 is asynchronous, so it works without MCI; when it is released only bit 0
 can change at the next count, a Gray value is never torn. Init changes
 only while the counter is 0.
*******************************************************************************/

module EdgeSync( Mclk, LvA, LvB, Clr, GrayA, GrayB, InitA, InitB );

  input         Mclk;           // sampling clock
  input         LvA;            // level of CH_A (comparator, in Mclk)
  input         LvB;            // level of CH_B
  input         Clr;            // clear              -> active high
  output [31:0] GrayA;          // edges of CH_A (both), Gray code
  output [31:0] GrayB;          // edges of CH_B
  output        InitA;          // level of CH_A before the first edge
  output        InitB;

  reg    [31:0] BinA, BinB, GrayA, GrayB;
  reg           PrevA, PrevB, InitA, InitB;

  wire   [31:0] NextA = BinA + 1;
  wire   [31:0] NextB = BinB + 1;

  initial begin
    BinA = 0; BinB = 0; GrayA = 0; GrayB = 0;
    PrevA = 0; PrevB = 0; InitA = 0; InitB = 0;
  end

  always @( posedge Mclk ) begin
    PrevA <= LvA;
    PrevB <= LvB;
    if ( BinA == 0 ) InitA <= PrevA;
    if ( BinB == 0 ) InitB <= PrevB;
  end

  always @( posedge Mclk or posedge Clr ) begin
    if ( Clr ) begin
      BinA <= 0; GrayA <= 0;
      BinB <= 0; GrayB <= 0;
    end else begin
      if ( LvA != PrevA ) begin
        BinA  <= NextA;
        GrayA <= NextA ^ ( NextA >> 1 );
      end
      if ( LvB != PrevB ) begin
        BinB  <= NextB;
        GrayB <= NextB ^ ( NextB >> 1 );
      end
    end
  end

endmodule

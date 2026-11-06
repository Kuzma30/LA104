/*******************************************************************************
          Quadrature decoder of an incremental encoder (A = CH_C, B = CH_D)

 Added to the e-Design FPGA V2.5 for the LA104 OS (DS203), FPGA 2.63.

 Every edge of A and B counts one step (x4), the direction is given by the
 level of the other input at the edge:

        forward (A leads B): 00 -> 10 -> 11 -> 01 -> 00   (A, B)

        A rising : B = 0 -> +1, B = 1 -> -1
        A falling: B = 1 -> +1, B = 0 -> -1
        B rising : A = 1 -> +1, A = 0 -> -1
        B falling: A = 0 -> +1, A = 1 -> -1

 As the edge counters (EdgeCnt.v) the four up/down counters are clocked by
 the input pins, so they do not depend on the sampling clock MCI. The
 position is the sum of the four counters (done by the MCU, modulo 2^32).
 A glitch or a bouncing edge on one input while the other one is stable
 counts +1 and -1 and cancels out.

 The values are kept in Gray code (one bit changes per step, up or down), a
 value captured in another clock domain is always the old or the new one.
 The counters have no clear: the MCU keeps the start value.
*******************************************************************************/

module QuadCnt( A, B, GrayAR, GrayAF, GrayBR, GrayBF );

  input         A;              // CH_C
  input         B;              // CH_D
  output [31:0] GrayAR;         // steps at the rising edges of A, Gray code
  output [31:0] GrayAF;         // falling edges of A
  output [31:0] GrayBR;         // rising edges of B
  output [31:0] GrayBF;         // falling edges of B

  reg    [31:0] BinAR, BinAF, BinBR, BinBF;
  reg    [31:0] GrayAR, GrayAF, GrayBR, GrayBF;

  wire   [31:0] NextAR = B ? BinAR - 1 : BinAR + 1;
  wire   [31:0] NextAF = B ? BinAF + 1 : BinAF - 1;
  wire   [31:0] NextBR = A ? BinBR + 1 : BinBR - 1;
  wire   [31:0] NextBF = A ? BinBF - 1 : BinBF + 1;

  initial begin
    BinAR = 0; BinAF = 0; BinBR = 0; BinBF = 0;
    GrayAR = 0; GrayAF = 0; GrayBR = 0; GrayBF = 0;
  end

  always @( posedge A ) begin
    BinAR  <= NextAR;
    GrayAR <= NextAR ^ ( NextAR >> 1 );
  end

  always @( negedge A ) begin
    BinAF  <= NextAF;
    GrayAF <= NextAF ^ ( NextAF >> 1 );
  end

  always @( posedge B ) begin
    BinBR  <= NextBR;
    GrayBR <= NextBR ^ ( NextBR >> 1 );
  end

  always @( negedge B ) begin
    BinBF  <= NextBF;
    GrayBF <= NextBF ^ ( NextBF >> 1 );
  end

endmodule

/*******************************************************************************
               Index (Z) of the encoder from an analog channel

 The DS203 has only two digital inputs, the index comes to CH_A or CH_B and
 is compared with a threshold (with hysteresis) in the sampling clock MCI:
 the Z pulse must be longer than one sample period.

 At the active edge of Z the four quadrature counters are captured (Gray
 code, so every one is the old or the new value) and the Z counter is
 incremented. The MCU computes the position within the revolution as
 position - position at the last Z, the total position is not changed.

 ZCtrl: bit 1..0 source (0 off, 1 CH_A, 2 CH_B), bit 2 active low
 ZThr : threshold (raw sample value as in the FIFO), ZHys: hysteresis
*******************************************************************************/

module QuadZ( Mclk, Din, ZCtrl, ZThr, ZHys, QAR, QAF, QBR, QBF,
              LAR, LAF, LBR, LBF, ZGray );

  input         Mclk;
  input  [15:0] Din;            // 0~7: CH_A, 8~15: CH_B (as written to the FIFO)
  input  [ 7:0] ZCtrl;
  input  [ 7:0] ZThr;
  input  [ 7:0] ZHys;
  input  [31:0] QAR, QAF, QBR, QBF;
  output [31:0] LAR, LAF, LBR, LBF;
  output [15:0] ZGray;          // number of Z pulses, Gray code

  reg    [31:0] LAR, LAF, LBR, LBF;
  reg    [15:0] ZBin, ZGray;
  reg           Level, Active;

  wire   [ 7:0] V     = ( ZCtrl[1:0] == 2'b10 ) ? Din[15:8] : Din[7:0];
  wire   [ 8:0] High  = { 1'b0, ZThr } + { 1'b0, ZHys };
  wire   [ 8:0] Low   = { 1'b0, V } + { 1'b0, ZHys };
  wire          On    = ( ZCtrl[1:0] == 2'b01 ) || ( ZCtrl[1:0] == 2'b10 );
  wire          Z     = Level ^ ZCtrl[2];
  wire   [15:0] ZNext = ZBin + 1;

  initial begin
    LAR = 0; LAF = 0; LBR = 0; LBF = 0;
    ZBin = 0; ZGray = 0; Level = 0; Active = 0;
  end

  always @( posedge Mclk ) begin
    if ( { 1'b0, V } >= High )
      Level <= 1;
    else if ( Low <= { 1'b0, ZThr } )
      Level <= 0;

    Active <= On && Z;
    if ( On && Z && !Active ) begin
      LAR   <= QAR;
      LAF   <= QAF;
      LBR   <= QBR;
      LBF   <= QBF;
      ZBin  <= ZNext;
      ZGray <= ZNext ^ ( ZNext >> 1 );
    end
  end

endmodule

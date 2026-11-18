/*******************************************************************************
          Quadrature decoder of an incremental encoder (A = CH_C, B = CH_D)

 Added to the e-Design FPGA V2.5 for the LA104 OS (DS203), FPGA 2.64.

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
 The counters have no clear: the MCU keeps the start value. They have 24
 bits (the logic cells of the iCE65L04 are short), the MCU reads them every
 100 ms and extends them.
*******************************************************************************/

module QuadCnt( A, B, GrayAR, GrayAF, GrayBR, GrayBF );

  input         A;              // CH_C
  input         B;              // CH_D
  output [23:0] GrayAR;         // steps at the rising edges of A, Gray code
  output [23:0] GrayAF;         // falling edges of A
  output [23:0] GrayBR;         // rising edges of B
  output [23:0] GrayBF;         // falling edges of B

  reg    [23:0] BinAR, BinAF, BinBR, BinBF;
  reg    [23:0] GrayAR, GrayAF, GrayBR, GrayBF;

  wire   [23:0] NextAR = B ? BinAR - 1 : BinAR + 1;
  wire   [23:0] NextAF = B ? BinAF + 1 : BinAF - 1;
  wire   [23:0] NextBR = A ? BinBR + 1 : BinBR - 1;
  wire   [23:0] NextBF = A ? BinBF - 1 : BinBF + 1;

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
     Quadrature decoder in the sampling clock: A, B and Z from any input

 A, B and the index Z can come to any of the four inputs (FPGA 2.64):
 CH_A / CH_B (analog, compared with a threshold with hysteresis, one per
 channel) or CH_C / CH_D (digital, synchronized to MCI). The decoder samples
 them with MCI, so the steps must be slower than the sampling clock (two
 changes in one sample period are counted as errors, the direction is not
 known then). A = CH_C and B = CH_D are counted by QuadCnt as well, without
 this limit.

 At the active edge of Z both the position of this decoder and the four
 counters of QuadCnt are captured and the Z counter is incremented. The
 total position is never cleared by Z.

 Ctrl: bit 1..0 source of A, bit 3..2 B, bit 5..4 Z (0 CH_A, 1 CH_B,
       2 CH_C, 3 CH_D), bit 6 Z on, bit 7 Z active low
 AThr / AHys, BThr / BHys: threshold and hysteresis of CH_A / CH_B (raw
       sample value as in the FIFO)
*******************************************************************************/

module QuadSync( Mclk, Din, DinP, Ctrl, AThr, AHys, BThr, BHys,
                 QAR, QAF, QBR, QBF, LAR, LAF, LBR, LBF, ZGray,
                 SGray, EGray, LSync, LevA, LevB );

  input         Mclk;
  input  [17:0] Din;            // 0~7 CH_A, 8~15 CH_B (as written to the FIFO), 16 CH_C, 17 CH_D
  input  [17:0] DinP;           // Din one sample before (FPGA 2.67)
  input  [ 7:0] Ctrl;
  input  [ 7:0] AThr, AHys, BThr, BHys;
  input  [23:0] QAR, QAF, QBR, QBF;
  output [23:0] LAR, LAF, LBR, LBF;
  output [15:0] ZGray;          // number of Z pulses
  output [23:0] SGray;          // position of this decoder
  output [ 7:0] EGray;          // errors (A and B changed in one sample)
  output [23:0] LSync;          // SGray captured at the last Z
  output        LevA, LevB;     // comparator levels of CH_A / CH_B (EdgeSync)

  reg    [23:0] LAR, LAF, LBR, LBF, LSync;
  reg    [15:0] ZBin, ZGray;
  reg    [ 7:0] EBin, EGray;
  reg    [23:0] SBin, SGray;
  reg           LvA, LvB, C1, C2, D1, D2;
  reg    [ 1:0] Phase;
  reg           Active;

  wire   [ 8:0] AHigh = { 1'b0, AThr } + { 1'b0, AHys };
  // FPGA 2.67: compared one sample before (DinP) and registered, the
  // levels follow from the registered results as before from Din
  wire   [ 8:0] ALow  = { 1'b0, DinP[7:0] } + { 1'b0, AHys };
  wire   [ 8:0] BHigh = { 1'b0, BThr } + { 1'b0, BHys };
  wire   [ 8:0] BLow  = { 1'b0, DinP[15:8] } + { 1'b0, BHys };
  reg           AHi, ALo, BHi, BLo;
  initial begin AHi = 0; ALo = 0; BHi = 0; BLo = 0; end

  wire   [ 3:0] Lv = { D2, C2, LvB, LvA };
  assign        LevA = LvA;
  assign        LevB = LvB;
  // the selected inputs are registered once more (FPGA 2.66): A, B and Z
  // one sample later together, a shorter path to the counters
  reg           A, B, Z;
  wire          ZOn = Ctrl[6];
  // (A, B) 00 10 11 01 = phase 0 1 2 3, forward = A leads B
  wire   [ 1:0] NewPhase = A ? ( B ? 2'd2 : 2'd1 ) : ( B ? 2'd3 : 2'd0 );
  wire   [ 1:0] Delta = NewPhase - Phase;
  wire   [23:0] SNext = ( Delta == 2'd1 ) ? SBin + 1 : SBin - 1;
  wire   [ 7:0] ENext = EBin + 1;
  wire   [15:0] ZNext = ZBin + 1;

  initial begin
    LAR = 0; LAF = 0; LBR = 0; LBF = 0; LSync = 0;
    ZBin = 0; ZGray = 0; EBin = 0; EGray = 0; SBin = 0; SGray = 0;
    LvA = 0; LvB = 0; A = 0; B = 0; Z = 0; C1 = 0; C2 = 0; D1 = 0; D2 = 0; Phase = 0; Active = 0;
  end

  always @( posedge Mclk ) begin
    // levels of the four inputs
    AHi <= { 1'b0, DinP[7:0] } >= AHigh;
    ALo <= ALow <= { 1'b0, AThr };
    BHi <= { 1'b0, DinP[15:8] } >= BHigh;
    BLo <= BLow <= { 1'b0, BThr };
    if ( AHi )        LvA <= 1;
    else if ( ALo )   LvA <= 0;
    if ( BHi )        LvB <= 1;
    else if ( BLo )   LvB <= 0;
    C1 <= Din[16]; C2 <= C1;
    D1 <= Din[17]; D2 <= D1;
    A <= Lv[ Ctrl[1:0] ];
    B <= Lv[ Ctrl[3:2] ];
    Z <= Lv[ Ctrl[5:4] ] ^ Ctrl[7];

    // steps
    Phase <= NewPhase;
    if ( Delta == 2'd1 || Delta == 2'd3 ) begin
      SBin  <= SNext;
      SGray <= SNext ^ ( SNext >> 1 );
    end else if ( Delta == 2'd2 ) begin
      EBin  <= ENext;
      EGray <= ENext ^ ( ENext >> 1 );
    end

    // index
    Active <= ZOn && Z;
    if ( ZOn && Z && !Active ) begin
      LAR   <= QAR;
      LAF   <= QAF;
      LBR   <= QBR;
      LBF   <= QBF;
      LSync <= SGray;
      ZBin  <= ZNext;
      ZGray <= ZNext ^ ( ZNext >> 1 );
    end
  end

endmodule

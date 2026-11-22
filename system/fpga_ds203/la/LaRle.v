/*******************************************************************************
 LA104 logic analyzer image: run length coding of the four channels.

 State S = { B, A, D, C }: C, D the digital inputs (CH_C = Din[16],
 CH_D = Din[17]), A, B the analog channels against a threshold with
 hysteresis (registers 0x10..0x13 as the comparators of FPGA 2.66).

 A word is { S[3:0], R[13:0] }: the state and its duration - 1 in samples
 (MCI). It is written in every MCI at the write pointer (AddrCtrlLa); Adv
 moves the pointer when the state changes, when R is full (16384 samples)
 or at the trigger (Mark), so the word of the finished run stays in the RAM.
 Word = { S[3:0], R[13:0] } -> DP_RAM Din[17:0]: bit 14 C, 15 D, 16 A, 17 B.

 Timing: the compare results of A / B are registered from DinP as Din_Bus,
 the level one MCI later; C / D are delayed by one MCI to match.
*******************************************************************************/

module LaRle ( Mclk, Din, DinP, Mark, AThr, AHys, BThr, BHys, Word, Adv, State );

  input   Mclk;
  input   [17:0]Din;       // samples (Din_Bus)
  input   [17:0]DinP;      // the samples one MCI earlier (Din_Pre)
  input   Mark;            // trigger: start a new word
  input   [ 7:0]AThr;
  input   [ 7:0]AHys;
  input   [ 7:0]BThr;
  input   [ 7:0]BHys;
  output  [17:0]Word;
  output  Adv;
  output  [ 3:0]State;

  reg     AHi, ALo, BHi, BLo;
  reg     LvA, LvB;
  reg     [1:0]CD;
  reg     [3:0]WS;          // state of the word
  reg     [13:0]WR;         // its length - 1

  initial begin
    AHi = 0; ALo = 0; BHi = 0; BLo = 0; LvA = 0; LvB = 0; CD = 0; WS = 0; WR = 0;
  end

  wire    [8:0]AUp = { 1'b0, AThr } + { 1'b0, AHys };
  wire    [8:0]ADn = { 1'b0, AThr } - { 1'b0, AHys };
  wire    [8:0]BUp = { 1'b0, BThr } + { 1'b0, BHys };
  wire    [8:0]BDn = { 1'b0, BThr } - { 1'b0, BHys };

  always @( posedge Mclk ) begin
    AHi <= { 1'b0, DinP[ 7:0] } > AUp;
    ALo <= ( { 1'b0, DinP[ 7:0] } < ADn ) && !ADn[8];
    BHi <= { 1'b0, DinP[15:8] } > BUp;
    BLo <= ( { 1'b0, DinP[15:8] } < BDn ) && !BDn[8];
    if ( AHi ) LvA <= 1; else if ( ALo ) LvA <= 0;
    if ( BHi ) LvB <= 1; else if ( BLo ) LvB <= 0;
    CD <= Din[17:16];
  end

  assign  State = { LvB, LvA, CD[1], CD[0] };
  assign  Adv = ( State != WS ) || ( WR == 14'h3FFF ) || Mark;
  assign  Word = { WS, WR };

  always @( posedge Mclk ) begin
    if ( Adv ) begin
      WS <= State;
      WR <= 0;
    end else
      WR <= WR + 1;
  end

endmodule

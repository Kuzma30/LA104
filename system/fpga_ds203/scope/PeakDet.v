/*******************************************************************************
                 Peak detection (min / max) of CH_A and CH_B

 Added to the e-Design FPGA V2.5 for the LA104 OS (DS203), FPGA 2.68. The
 idea and the data format come from the "oversampling" mode of the Wildcat
 FPGA W1.1 (DS203 HW 2.81, github.com/MotoMaxis/DS203-DSOQuad), rewritten
 for this design.

 OsSize = 0: off, the samples go to the FIFO as before (Din unchanged).
 OsSize = K - 1 > 0: the ADC is sampled K times faster than the time base
 needs; every K samples give two FIFO words, the minimum and the maximum of
 CH_A and CH_B over these K samples:
   word 0: bits 7..0 min CH_A, 15..8 min CH_B, 17..16 = 00
   word 1: bits 7..0 max CH_A, 15..8 max CH_B, 17..16 = 01
 (CH_C / CH_D are not stored in this mode, bits 17..16 mark the pair.)

 Cnt counts the samples of a period 0..OsSize. Ph0: first sample of a
 period (always 1 when off), Adv: a FIFO word is written (Cnt 0 and 1),
 PEnd: last sample of a period (always 1 when off). The words written at
 Cnt 0 / 1 are the minimum / maximum of the previous period.
*******************************************************************************/

module PeakDet( Mclk, OsSize, Din, OsOn, Ph0, Adv, PEnd, WData );

  input         Mclk;
  input  [15:0] OsSize;         // K - 1, 0 = off
  input  [17:0] Din;            // samples (as written to the FIFO)
  output        OsOn;           // peak detection on
  output        Ph0;            // first sample of a period
  output        Adv;            // FIFO word written in this sample
  output        PEnd;           // last sample of a period
  output [17:0] WData;          // data for the FIFO

  reg    [15:0] Cnt;
  reg    [ 7:0] MinA, MaxA, MinB, MaxB;

  initial begin
    Cnt = 0; MinA = 0; MaxA = 0; MinB = 0; MaxB = 0;
  end

  wire   [7:0] A = Din[ 7:0];
  wire   [7:0] B = Din[15:8];

  assign OsOn  = ( OsSize != 16'd0 );
  assign Ph0   = ( Cnt == 16'd0 );
  assign Adv   = ( Cnt[15:1] == 15'd0 );
  wire   Last  = ( Cnt == OsSize );
  assign PEnd  = !OsOn || Last;
  assign WData = OsOn ? { 1'b0, Cnt[0], ( Cnt[0] ? MaxB : MinB ), ( Cnt[0] ? MaxA : MinA ) }
                      : Din;

  always @( posedge Mclk ) begin
    if ( !OsOn || Last )  Cnt <= 0;
    else                  Cnt <= Cnt + 1;

    if ( Ph0 ) begin
      MinA <= A;                                // new period
      MinB <= B;
    end else begin
      if ( Cnt == 16'd1 ) begin
        MaxA <= ( A > MinA ) ? A : MinA;        // the higher of the first two
        MaxB <= ( B > MinB ) ? B : MinB;
      end else begin
        if ( A > MaxA ) MaxA <= A;
        if ( B > MaxB ) MaxB <= B;
      end
      if ( A < MinA ) MinA <= A;
      if ( B < MinB ) MinB <= B;
    end
  end

endmodule

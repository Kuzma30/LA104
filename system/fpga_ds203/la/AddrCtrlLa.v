/*******************************************************************************
 LA104 logic analyzer image: write / read address control of the FIFO RAM.
 Derived from AddrCtrl.v (e-Design FPGA V2.x) for the run length capture:

 - the write pointer advances only with Adv (a new run starts, LaRle.v); the
   RAM is written in every MCI at the pointer, so the word of the current
   run grows in place and stays when the pointer moves on
 - Pcnt counts written words: PerCnt words before the trigger, the buffer is
   full when Depth words are counted (as the oscilloscope, in words)
 - the trigger is accepted (Sampled) when PerCnt words are written or after
   ArmTime * 1024 MCI (a quiet line gives few words; ArmTime 0: at once);
   Wptr starts at 0 with ClrW and Wrapped tells whether the words before
   the trigger are all from this capture
 - Mark: the MCI of the trigger (Ready set), LaRle starts a new word there,
   Bptr is the address of the last word before it
 - reading starts at Bptr - PerCnt (the oscilloscope: Bptr - 151 with
   PerCnt = 150), so word PerCnt + 1 of the read starts at the trigger
*******************************************************************************/

module AddrCtrlLa ( ClrW, Wclk, Start, Adv, nRclk, RE, H_L, Depth, PerCnt, Delay, ArmTime,
                    Ready, Sampled, Full, Empty, Mark, Wrapped, Wptr, Rptr, Bptr );

  input   ClrW;         // Wr addr cnt rst      -> Active high
  input   Wclk;         // Wr clock             -> rising edge
  input   Start;        // Sync start           -> active high
  input   Adv;          // a new word starts    -> active high
  input   nRclk;        // Rd clock             -> falling edge
  input   RE;           // Rd cnt enable        -> active high
  input   H_L;          // Data output select
  input   [11:0]Depth;  // words of the capture - 1
  input   [11:0]PerCnt; // words before the trigger
  input   [31:0]Delay;  // MCI from the trigger to Ready
  input   [15:0]ArmTime;// accept the trigger after ArmTime * 1024 MCI at the latest

  output  Sampled;
  output  Ready;
  output  Full;
  output  Empty;
  output  Mark;         // MCI of the trigger
  output  Wrapped;      // Wptr went around since ClrW
  output  [11:0]Wptr;
  output  [11:0]Rptr;
  output  [11:0]Bptr;

  reg     Full;
  reg     Ready;
  reg     Loaded;
  reg     Wrapped;
  reg     [11:0]Wptr;
  reg     [11:0]Rptr;
  reg     [12:0]Pcnt;
  reg     [11:0]Bptr;
  reg     [31:0]DelayCnt;
  reg     Sampled;
  reg     [25:0]ArmCnt;
  assign  Mark = ( DelayCnt == Delay ) && !Ready;

  always@ ( posedge Wclk or posedge ClrW ) begin
    if ( ClrW ) begin
      Full     <= 0;
      Pcnt     <= 0;
      DelayCnt <= 0;
      Ready    <= 0;
      Wptr     <= 0;
      Wrapped  <= 0;
      Bptr     <= 0;
      Sampled  <= 0;
      ArmCnt   <= 0;
    end else begin
      if ( !ArmCnt[25] )                ArmCnt   <= ArmCnt + 1;
      if (( Pcnt >= PerCnt )||( ArmCnt[25:10] >= { 1'b0, ArmTime[14:0] } ))
                                        Sampled  <= 1;
      if ( Start )                      DelayCnt <= DelayCnt + 1;
      if ( !Full && Adv ) begin
        Wptr <= Wptr + 1;
        if ( Wptr == 12'hFFF )          Wrapped <= 1;
      end
      if ( Pcnt >= Depth )              Full     <= Ready;
      else if ( Adv )                   Pcnt     <= Pcnt + 1;
      if(( !Start )&&( Pcnt >= PerCnt)) Pcnt     <= PerCnt;
      if ( Mark ) begin
        Ready <= 1;
        Bptr  <= Wptr;
        Pcnt  <= PerCnt;
      end
    end
  end

  assign Empty = ( Rptr == Wptr ) ? 1'b1 : 1'b0 ;

  always @( posedge nRclk or posedge ClrW ) begin
    if ( ClrW ) begin
      Loaded <= 0;
      Rptr   <= 0;
    end else begin
      if ( H_L && RE )  Rptr <= Rptr + 1;
      if (( H_L )&& RE &&( ~Loaded )&& Start ) begin
        Loaded <= 1;
        Rptr   <= Bptr - PerCnt;
      end
    end
  end

endmodule

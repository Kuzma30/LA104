/********************* (C) COPYRIGHT 2011 e-Design Co.,Ltd. ********************
                     Sync signal select and control module  
 Version   : FPGA CFG Ver 2.x                                     Author : bure
*******************************************************************************/

module Signal( Reset, Mclk, Trigg_Mode, Vthreshold, Tthreshold, Din, DinP,
               Sampled, CtrlReg,
               Start, ClkA, ClkB );

  input   Reset;              // Trigger status rst    -> Active high 
  input   Mclk;               // main clock
  input   [ 7:0]Trigg_Mode;   // Trigger Mode 
  input   [ 7:0]Vthreshold;   // Trigger voltage threshold 
  input   [31:0]Tthreshold;   // Trigger time threshold (LA104 2.68: 32 bits)
  input   [17:0]Din;          // 0~7:CH_A, 8~15:CH_B, 16:CH_C, 17:CH_D
  input   [17:0]DinP;         // LA104: Din one sample before (Din = DinP delayed)
  input   Sampled;
  input   [ 7:0]CtrlReg;      // bit0=nPD, bit1=Mode, 

  output  Start;              // Sync start            -> Active high 
  output  ClkA;               // Clock_A for ADC CH_A
  output  ClkB;               // Clock_B for ADC CH_B
  
  
  
  
  
  // LA104 (FPGA 2.68): the measurement registers of the original (edges and
  // sum of the widths of every channel, Select 0x00..0x0E) are removed, no
  // program of the LA104 OS reads them (the edge counters of EdgeCnt.v are
  // exact); the counters of the pulse triggers have 32 bits as in the Wildcat
  // FPGA W1.1 (12 bits wrapped after 4096 samples, wrong pulse triggers for
  // long pulses and with peak detection, where they count the fast samples)
  // The pulse counters count down from Tthreshold - 1 (loaded at every
  // edge) and stop below 0: bit 32 = 0 at the next edge is the original
  // "count < Tthreshold" (a 32 bit compare was the longest path)
  wire    [32:0]TLoad = { 1'b0, Tthreshold } - 33'd1;

  reg     Start;
  wire    [ 8:0]Delta_V1;
  wire    [ 8:0]Delta_V2;
  wire    [ 8:0]Delta_V3;
  wire    [ 8:0]Delta_V4;

  reg     A_Flag;
  reg     A_Status;
  reg     A_Dt_L;
  reg     A_Dt_H;
  reg     A_Ut_L;
  reg     A_Ut_H;
  reg     [32:0]A_Cnt;
   
  reg     B_Flag;
  reg     B_Status;
  reg     B_Dt_L;
  reg     B_Dt_H;
  reg     B_Ut_L;
  reg     B_Ut_H;
  reg     [32:0]B_Cnt;
  
  reg     C_Flag;
  reg     C_Dt_L;
  reg     C_Dt_H;
  reg     C_Ut_L;
  reg     C_Ut_H;
  reg     [32:0]C_Cnt;
  
  reg     D_Flag;
  reg     D_Dt_L;
  reg     D_Dt_H;
  reg     D_Ut_L;
  reg     D_Ut_H;
  reg     [32:0]D_Cnt;
  
  assign ClkA = Mclk; 
  assign ClkB = CtrlReg[1] ? ( ~Mclk ) : ( Mclk );
  
  assign  Delta_V1 = Vthreshold + 12;
  assign  Delta_V2 = Vthreshold - 12;
  assign  Delta_V3 = Vthreshold + 1;
  assign  Delta_V4 = Vthreshold - 1;

  // LA104 (FPGA 2.67): the comparisons of CH_A / CH_B with the levels are
  // made one sample before, from DinP, and registered: the same results
  // for Din, the path from the comparators to the flags is cut in two.
  reg     A_Gt1, A_Lt2, A_Gt3, A_Lt4, A_GtV, A_LtV;
  reg     B_Gt1, B_Lt2, B_Gt3, B_Lt4, B_GtV, B_LtV;
  always @( posedge Mclk ) begin
    A_Gt1 <= DinP[ 7:0] > Delta_V1;   B_Gt1 <= DinP[15:8] > Delta_V1;
    A_Lt2 <= DinP[ 7:0] < Delta_V2;   B_Lt2 <= DinP[15:8] < Delta_V2;
    A_Gt3 <= DinP[ 7:0] > Delta_V3;   B_Gt3 <= DinP[15:8] > Delta_V3;
    A_Lt4 <= DinP[ 7:0] < Delta_V4;   B_Lt4 <= DinP[15:8] < Delta_V4;
    A_GtV <= DinP[ 7:0] > Vthreshold; B_GtV <= DinP[15:8] > Vthreshold;
    A_LtV <= DinP[ 7:0] < Vthreshold; B_LtV <= DinP[15:8] < Vthreshold;
  end
  
  always @( posedge Mclk or posedge Reset ) begin

    if ( Reset ) begin 
      Start    <= 0;
      A_Dt_L   <= 0;
      A_Dt_H   <= 0;
      A_Ut_L   <= 0;
      A_Ut_H   <= 0;
      B_Dt_L   <= 0;
      B_Dt_H   <= 0;
      B_Ut_L   <= 0;
      B_Ut_H   <= 0;
      C_Dt_L   <= 0;
      C_Dt_H   <= 0;
      C_Ut_L   <= 0;
      C_Ut_H   <= 0;
      D_Dt_L   <= 0;
      D_Dt_H   <= 0;
      D_Ut_L   <= 0;
      D_Ut_H   <= 0;
    end else begin
      // For CH_A Trigger
      if ( A_Gt3 )  A_Status <= 1;
      if ( A_Lt4 )  A_Status <= 0;
      if (( A_Gt1 )&&( ~A_Flag )) begin
        if ( !A_Cnt[32] )  A_Dt_L <= Sampled; 
        else                       A_Ut_L <= Sampled; 
        A_Flag <= 1;
        A_Cnt  <= TLoad;
      end else
      if (( A_Lt2 )&&(  A_Flag )) begin
        if ( !A_Cnt[32] )  A_Dt_H <= Sampled; 
        else                       A_Ut_H <= Sampled; 
        A_Flag <= 0;
        A_Cnt  <= TLoad;
      end  else if ( !A_Cnt[32] ) A_Cnt <= A_Cnt - 1; 
      
      // For CH_B Trigger
      if ( B_Gt3 )  B_Status <= 1;
      if ( B_Lt4 )  B_Status <= 0;
      if (( B_Gt1 )&&( ~B_Flag )) begin
        if ( !B_Cnt[32] )  B_Dt_L <= Sampled;
        else                       B_Ut_L <= Sampled;
        B_Flag <= 1;
        B_Cnt  <= TLoad;
      end else
      if (( B_Lt2 )&&(  B_Flag )) begin
        if ( !B_Cnt[32] )  B_Dt_H <= Sampled;
        else                       B_Ut_H <= Sampled;
        B_Flag <= 0;
        B_Cnt  <= TLoad;
      end  else if ( !B_Cnt[32] ) B_Cnt <= B_Cnt - 1; 
    
      // For CH_C Trigger
      if ( Din[16] != C_Flag ) begin
        if ( !C_Cnt[32] ) begin
          if ( Din[16] )  C_Dt_L <= Sampled;
          else            C_Dt_H <= Sampled;
        end else begin
          if ( Din[16] )  C_Ut_L <= Sampled;
          else            C_Ut_H <= Sampled;
        end 
        C_Cnt <= TLoad;
      end else if ( !C_Cnt[32] ) C_Cnt <= C_Cnt - 1; 
      C_Flag <= Din[16]; 
       
      // For CH_D Trigger
      if ( Din[17] != D_Flag ) begin
        if ( !D_Cnt[32] ) begin
          if ( Din[17] )  D_Dt_L <= Sampled;
          else            D_Dt_H <= Sampled;
        end else begin
          if ( Din[17] )  D_Ut_L <= Sampled;
          else            D_Ut_H <= Sampled;
        end  
        D_Cnt <= TLoad;
      end else if ( !D_Cnt[32] ) D_Cnt <= D_Cnt - 1; 
      D_Flag <= Din[17]; 
    
      case( Trigg_Mode )
        // For CH_A Trigger
        8'h00: if (( A_LtV )&&(  A_Flag ))   Start <= Sampled; // Negedge
        8'h01: if (( A_GtV )&&( ~A_Flag ))   Start <= Sampled; // Posedge
        8'h02: if (( A_LtV )&&(  A_Status )) Start <= Sampled; // L Level
        8'h03: if (( A_GtV )&&( ~A_Status )) Start <= Sampled; // H Level
        8'h04: Start <= A_Dt_L; // Pusle 0 < ConfigDt
        8'h05: Start <= A_Ut_L; // Pusle 0 > ConfigDt
        8'h06: Start <= A_Dt_H; // Pusle 1 < ConfigDt
        8'h07: Start <= A_Ut_H; // Pusle 1 > ConfigDt
        // For CH_B Trigger
        8'h08: if (( B_LtV )&&(  B_Flag ))   Start <= Sampled; // Negedge
        8'h09: if (( B_GtV )&&( ~B_Flag ))   Start <= Sampled; // Posedge
        8'h0A: if (( B_LtV )&&(  B_Status )) Start <= Sampled; // L Level
        8'h0B: if (( B_GtV )&&( ~B_Status )) Start <= Sampled; // H Level
        8'h0C: Start <= B_Dt_L; // Pusle 0 < ConfigDt
        8'h0D: Start <= B_Ut_L; // Pusle 0 > ConfigDt
        8'h0E: Start <= B_Dt_H; // Pusle 1 < ConfigDt
        8'h0F: Start <= B_Ut_H; // Pusle 1 > ConfigDt
        // For CH_C Trigger
        8'h10: if (( ~Din[16] )&&(  C_Flag ))  Start <= Sampled; // Negedge
        8'h11: if ((  Din[16] )&&( ~C_Flag ))  Start <= Sampled; // Posedge
        8'h12: if (( ~Din[16] )&&(  C_Flag ))  Start <= Sampled; // L Level
        8'h13: if ((  Din[16] )&&( ~C_Flag ))  Start <= Sampled; // H Level
        8'h14: Start <= C_Dt_L; // Pusle 0 < ConfigDt
        8'h15: Start <= C_Ut_L; // Pusle 0 > ConfigDt
        8'h16: Start <= C_Dt_H; // Pusle 1 < ConfigDt
        8'h17: Start <= C_Ut_H; // Pusle 1 > ConfigDt
        // For CH_D Trigger
        8'h18: if (( ~Din[17] )&&(  D_Flag ))  Start <= Sampled; // Negedge
        8'h19: if ((  Din[17] )&&( ~D_Flag ))  Start <= Sampled; // Posedge
        8'h1A: if (( ~Din[17] )&&(  D_Flag ))  Start <= Sampled; // L Level
        8'h1B: if ((  Din[17] )&&( ~D_Flag ))  Start <= Sampled; // H Level
        8'h1C: Start <= D_Dt_L; // Pusle 0 < ConfigDt
        8'h1D: Start <= D_Ut_L; // Pusle 0 > ConfigDt
        8'h1E: Start <= D_Dt_H; // Pusle 1 < ConfigDt
        8'h1F: Start <= D_Ut_H; // Pusle 1 > ConfigDt
        default:                                          Start <= 1;
      endcase
    end
  end
endmodule
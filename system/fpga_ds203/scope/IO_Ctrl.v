/********************* (C) COPYRIGHT 2011 e-Design Co.,Ltd. ********************
                          Input & output control module  
 Version   : FPGA CFG Ver 2.x                                     Author : bure
*******************************************************************************/

module IO_Ctrl( CE, nRD, SCK, SDA, Dout, Start, Full, Empty, H_L, C_D, Ready,  
                A_Edge, A_TL, A_TH, B_Edge, B_TL, B_TH, 
                C_Edge, C_TL, C_TH, D_Edge, D_TL, D_TH, 
                Depth, PerCnt, Delay,
                nPD, Trigg_Mode, Vthreshold, Tthreshold, CtrlReg, 
                C_Rise, C_Fall, D_Rise, D_Fall, CntClr,
                QAR, QAF, QBR, QBF, LAR, LAF, LBR, LBF, ZGray,
                ZCtrl, ZThr, ZHys, DB ); 

  input     CE;                 // Databus selece enable -> Active high
  input     nRD;                // Databus read enable   -> Active low
  input     SCK;                // Serial input clock    -> rising edge 
  input     SDA;                // Serial input data 
  input     [17:0]Dout;         // Output data                     
  input     Start;              // Sync start            -> active high
  input     Ready;              // Sampling start        -> active high
  input     Full;               // FIFO RAM is full      -> active high
  input     Empty;              // FIFO RAM is empty     -> active high
  input     H_L;                // 0/1 = Status/[15:0]Din -> [15:0]DB
  input     C_D;                // 1 = [15:0]Data        -> [15:0]DB
                                
  input     [15:0]A_Edge;       // CH_A edge counter
  input     [15:0]A_TL;         // CH_A 
  input     [15:0]A_TH;         // CH_A 
  
  input     [15:0]B_Edge;       // CH_B edge counter
  input     [15:0]B_TL;         // CH_B 
  input     [15:0]B_TH;         // CH_B 
  
  input     [15:0]C_Edge;       // CH_C edge counter
  input     [15:0]C_TL;         // CH_C 
  input     [15:0]C_TH;         // CH_C 
  
  input     [15:0]D_Edge;       // CH_D edge counter
  input     [15:0]D_TL;         // CH_D 
  input     [15:0]D_TH;         // CH_D 
  
  output    [11:0]Depth;        // Sampling depth
  output    [11:0]PerCnt;       // Per-sampling counter                        
  output    [31:0]Delay;        // Sampling delay

  output    nPD;                // ADC power down        -> Active low
  output    [ 7:0]Trigg_Mode;   // Trigger Mode 
  output    [ 7:0]Vthreshold;   // Trigger voltage threshold 
  output    [15:0]Tthreshold;   // Trigger time threshold 
  output    [ 7:0]CtrlReg;      // bit0=nPD, bit1=Mode, 

  // LA104: 32 bit edge counters of CH_C and CH_D (EdgeCnt.v, Gray code)
  //   register 0x0E (serial): bit0 = clear the counters of CH_C and CH_D
  //   (held while 1; one clear signal, the clocks of the counters already
  //   use 7 of the 8 global buffers)
  //   Select 0x10/0x11: CH_C rising [15:0]/[31:16], 0x12/0x13 CH_C falling,
  //          0x14/0x15: CH_D rising, 0x16/0x17 CH_D falling, 0x18 ID 0xC0D2
  //   reading an even select (low word) latches the whole 32 bit value,
  //   the following odd select (high word) returns the latched upper half
  input     [31:0]C_Rise;
  input     [31:0]C_Fall;
  input     [31:0]D_Rise;
  input     [31:0]D_Fall;
  output    CntClr;

  // LA104: quadrature decoder (QuadCnt.v, FPGA 2.63), Gray code
  //   registers (serial): 0x0F Z control (bit1..0 source 0 off / 1 CH_A /
  //   2 CH_B, bit2 active low), 0x10 Z threshold, 0x11 Z hysteresis
  //   Select 0x20: reading latches the four quadrature counters, word 0 is
  //          returned, 0x21..0x27 the other words of the latch
  //          (AR lo/hi, AF lo/hi, BR lo/hi, BF lo/hi)
  //   Select 0x28: the same for the counters captured at the last Z, and
  //          the Z counter, 0x29..0x2F the other words, 0x30 the Z counter
  //   Select 0x19: ID 0xC0D3 (quadrature decoder present)
  input     [31:0]QAR;
  input     [31:0]QAF;
  input     [31:0]QBR;
  input     [31:0]QBF;
  input     [31:0]LAR;
  input     [31:0]LAF;
  input     [31:0]LBR;
  input     [31:0]LBF;
  input     [15:0]ZGray;
  output    [ 7:0]ZCtrl;
  output    [ 7:0]ZThr;
  output    [ 7:0]ZHys;

  inout     [15:0]DB;           // Data bus to MCU

  reg       [ 7:0]Trigg_Mode;   // Trigger Mode 
  reg       [ 7:0]Vthreshold;   // Trigger voltage threshold 
  reg       [15:0]Tthreshold;   // Trigger time threshold 
  reg       [ 7:0]CtrlReg;
  reg       [ 7:0]RegAddr;
  reg       [ 7:0]DataBuff;
  wire      [15:0]DB_Mux ; 

  reg       [15:0]Data;
  reg       [ 7:0]Select;
  wire      [15:0]CD_Mux ; 

  reg       [11:0]Depth;       
  reg       [11:0]PerCnt;                         
  reg       [31:0]Delay;      

  reg       [ 7:0]CntCtrl;
  reg       [31:0]Hold;
  wire      [31:0]CntMux;
  reg       [ 7:0]ZCtrl;
  reg       [ 7:0]ZThr;
  reg       [ 7:0]ZHys;
  reg      [127:0]HoldQ;
  reg       [15:0]HoldZ;
  wire      [15:0]HoldW;
  // version in the status word (SYS: FPGAVER = status >> 6): "2.63"
  wire      [ 9:0]Version = 10'd263;

  initial begin
    CntCtrl = 0;
    ZCtrl = 0; ZThr = 128; ZHys = 8;
    HoldQ = 0; HoldZ = 0;
  end

  assign HoldW = ( Select[2:0] == 3'd0 ) ? HoldQ[ 15:  0] :
                 ( Select[2:0] == 3'd1 ) ? HoldQ[ 31: 16] :
                 ( Select[2:0] == 3'd2 ) ? HoldQ[ 47: 32] :
                 ( Select[2:0] == 3'd3 ) ? HoldQ[ 63: 48] :
                 ( Select[2:0] == 3'd4 ) ? HoldQ[ 79: 64] :
                 ( Select[2:0] == 3'd5 ) ? HoldQ[ 95: 80] :
                 ( Select[2:0] == 3'd6 ) ? HoldQ[111: 96] : HoldQ[127:112];

  assign CntClr = CntCtrl[0];
  assign CntMux = ( Select[2:1] == 2'b00 ) ? C_Rise :
                  ( Select[2:1] == 2'b01 ) ? C_Fall :
                  ( Select[2:1] == 2'b10 ) ? D_Rise : D_Fall;
   
  assign nPD = CtrlReg[0];
        
  assign CD_Mux = C_D ? Data[15:0] : { Version, Start, Empty, Full, Ready, Dout[17:16] };  
  assign DB_Mux = H_L ? Dout[15:0] : CD_Mux;  
   
  assign DB = ( CE && !nRD ) ? DB_Mux : 16'hzzzz ;

  always @(posedge SCK) begin
      
    DataBuff <= { DataBuff[6:0], SDA };
    
  end
  
  always @( negedge nRD ) begin
   if (( Select[7:3] == 5'b00010 )&&( !Select[0] ))
     Hold <= CntMux;                  // whole 32 bit value with the low word
  end

  // quadrature: the whole set is latched at once with word 0
  always @( negedge nRD ) begin
   if ( Select == 8'h20 )
     HoldQ <= { QBF, QBR, QAF, QAR };
   else if ( Select == 8'h28 ) begin
     HoldQ <= { LBF, LBR, LAF, LAR };
     HoldZ <= ZGray;
   end
  end

  always @( negedge nRD ) begin
   if ( Select[7:4] == 4'h2 || Select == 8'h30 ) begin
     case( Select )
       8'h20:   Data <= QAR[15:0];
       8'h28:   Data <= LAR[15:0];
       8'h30:   Data <= HoldZ;
       default: Data <= HoldW;
     endcase
   end else
   case( Select )
     4'b0000: Data <= A_Edge;
     4'b0001: Data <= A_TL;
     4'b0010: Data <= A_TH;
     4'b0100: Data <= B_Edge;
     4'b0101: Data <= B_TL;
     4'b0110: Data <= B_TH;
     4'b1000: Data <= C_Edge;
     4'b1001: Data <= C_TL;
     4'b1010: Data <= C_TH;
     4'b1100: Data <= D_Edge;
     4'b1101: Data <= D_TL;
     4'b1110: Data <= D_TH;
     8'h10, 8'h12, 8'h14, 8'h16: Data <= CntMux[15:0];
     8'h11, 8'h13, 8'h15, 8'h17: Data <= Hold[31:16];
     8'h18:   Data <= 16'hC0D2;
     8'h19:   Data <= 16'hC0D3;
     default: Data <= 0;
   endcase 
  end

  always @( posedge SDA ) begin
    if ( !SCK ) begin
      if ( H_L ) begin
        RegAddr <= DataBuff;
      end else begin
        case( RegAddr )
          8'h00:  begin 
            Trigg_Mode <= DataBuff;    
            Depth      <= 4095;
            PerCnt     <= 150;
            Delay      <= 1;
          end
          8'h01:  Vthreshold       <= DataBuff;    
          8'h02:  Tthreshold[ 7:0] <= DataBuff;    
          8'h03:  Tthreshold[15:8] <= DataBuff;    
          8'h04:  CtrlReg   [ 7:0] <= DataBuff;    
          8'h05:  Select    [ 7:0] <= DataBuff;    
          8'h06:  Depth     [ 7:0] <= DataBuff;       
          8'h07:  Depth     [11:8] <= DataBuff;      
          8'h08:  PerCnt    [ 7:0] <= DataBuff;                         
          8'h09:  PerCnt    [11:8] <= DataBuff;                         
          8'h0A:  Delay     [ 7:0] <= DataBuff;      
          8'h0B:  Delay     [15:8] <= DataBuff;      
          8'h0C:  Delay    [23:16] <= DataBuff;      
          8'h0D:  Delay    [31:24] <= DataBuff;      
          8'h0E:  CntCtrl   [ 7:0] <= DataBuff;
          8'h0F:  ZCtrl     [ 7:0] <= DataBuff;
          8'h10:  ZThr      [ 7:0] <= DataBuff;
          8'h11:  ZHys      [ 7:0] <= DataBuff;
        endcase 
      end
    end 
  end

endmodule
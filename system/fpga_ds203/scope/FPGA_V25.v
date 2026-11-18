/******************** (C) COPYRIGHT 2011 e-Design Co.,Ltd. *********************

                          DS203 FIFO main control module  

 Version   : FPGA CFG Ver 2.x                                     Author : bure

********************************************************************************
版本修改说明：
 FPGA_V2.3: 1. 修改了同步触发模块的逻辑结构 
            2. 优化了FIFO读写地址控制的逻辑结构
 FPGA_V2.4: 1. 增加了4个通道独立的频率和脉宽计量
            2. 修改了脉宽触发的1个BUG
 FPGA_V2.5: 1. 增加了采样深度可设置的功能
            2. 增加了预采样深度可设置的功能
            3. 增加了延迟触发采样可设置的功能
*******************************************************************************/

module FIFO( ClrW, MCI, nRD, CE, H_L, C_D, SCK, SDA, Din, 
             nPD, CKA, CKB, DB);

  input	  ClrW;               // FIFO status reset       -> Active high 
  input	  MCI;                // Main clock input        -> rising edge 
  input	  nRD;                // FIFO read clock         -> falling edge
  input	  CE;                 // FIFO bus enable         -> Active high
  input	  H_L;                // 1 = [17:16]Dout->[ 1:0]DB, 0 = [15:0]Dout->[15:0]DB
  input	  C_D;                // 1 = [15:0]Data->[15:0]DB
  input	  SCK;                // Serial input clock    -> rising edge 
  input	  SDA;                // Serial input data 
  input	  [17:0]Din;          // FIFO write data

  output  nPD;                // ADC Power down Output
  output  CKA;                // Clock_A
  output  CKB;                // Clock_B

  inout   [15:0]DB;           // FIFO output data bus
  
  wire	  [11:0]R_Addr_Bus;   // current read address
  wire	  [11:0]W_Addr_Bus;   // current Writen address 
  wire	  [17:0]Dout_Bus;     // Internal data output Bus 
  wire	  [17:0]Din_Bus;      // Internal data input Bus 
  wire	  [ 7:0]Tm_Link;  
  wire	  [ 7:0]Vt_Link;      
  wire	  [15:0]Tt_Link;     
  wire	  [ 7:0]Ctrl_Link;
  wire	  Start_Link;
  wire	  Full_Link;
  wire	  Empty_Link;
  wire	  Sampled_Link;
  wire	  Ready_Link;
  wire	  [15:0]A_Edge_Link;
  wire	  [15:0]A_TL_Link;
  wire	  [15:0]A_TH_Link;   
  wire	  [15:0]B_Edge_Link;
  wire	  [15:0]B_TL_Link;
  wire	  [15:0]B_TH_Link;  
  wire	  [15:0]C_Edge_Link;
  wire	  [15:0]C_TL_Link;
  wire	  [15:0]C_TH_Link; 
  wire	  [15:0]D_Edge_Link;
  wire	  [15:0]D_TL_Link;
  wire	  [15:0]D_TH_Link; 
  wire	  [11:0]Depth_Link; 
  wire	  [11:0]PerCnt_Link; 
  wire	  [31:0]Delay_Link;
  // LA104: edge counters of CH_C and CH_D
  wire	  CntClr_Link;
  wire	  [31:0]CR_Link;
  wire	  [31:0]CF_Link;
  wire	  [31:0]DR_Link;
  wire	  [31:0]DF_Link;
  // LA104: quadrature decoder, A = CH_C, B = CH_D by the pins, any inputs in MCI
  wire	  [23:0]QAR_Link, QAF_Link, QBR_Link, QBF_Link;
  wire	  [23:0]LAR_Link, LAF_Link, LBR_Link, LBF_Link;
  wire	  [15:0]ZGray_Link;
  wire	  [ 7:0]QCtrl_Link, AThr_Link, AHys_Link, BThr_Link, BHys_Link;
  wire	  [23:0]SGray_Link, LSync_Link;
  wire	  [ 7:0]EGray_Link;
  // LA104: edge counters of CH_A and CH_B (comparators, in MCI), FPGA 2.66
  wire	  LevA_Link, LevB_Link;
  wire	  [31:0]EA_Link, EB_Link;
  wire	  InitA_Link, InitB_Link;

  // LA104 (FPGA 2.67): the inputs are registered twice in MCI. Without it
  // the paths from the pins (comparators of the trigger, RAM data, decoder)
  // are not timed by nextpnr (no input delays) and were up to 30 ns long:
  // triggers failed at 36 and 72 MHz on the device. The first register
  // (Din_Raw, the pins as they are) is placed next to the pads on the
  // iCE65L04 (same short path for every bit); it feeds the comparators of
  // the trigger and of the decoder (registered compare results), the second
  // (Din_Bus) everything else, so all users see the samples two MCI later
  // together. The oscilloscope is the same.
  wire    [17:0]Din_Raw;
`ifdef ICE65_L04
  // pads: Din[0..15] top edge (y 21), Din[16..17] left edge (x 0, y 2)
  (* BEL = "X23/Y20/lc0" *) SB_DFF Din_FF0  ( .C( MCI ), .D( Din[0] ), .Q( Din_Raw[0] ) );
  (* BEL = "X23/Y20/lc1" *) SB_DFF Din_FF1  ( .C( MCI ), .D( Din[1] ), .Q( Din_Raw[1] ) );
  (* BEL = "X24/Y20/lc0" *) SB_DFF Din_FF2  ( .C( MCI ), .D( Din[2] ), .Q( Din_Raw[2] ) );
  (* BEL = "X18/Y20/lc0" *) SB_DFF Din_FF3  ( .C( MCI ), .D( Din[3] ), .Q( Din_Raw[3] ) );
  (* BEL = "X20/Y20/lc0" *) SB_DFF Din_FF4  ( .C( MCI ), .D( Din[4] ), .Q( Din_Raw[4] ) );
  (* BEL = "X21/Y20/lc0" *) SB_DFF Din_FF5  ( .C( MCI ), .D( Din[5] ), .Q( Din_Raw[5] ) );
  (* BEL = "X20/Y20/lc1" *) SB_DFF Din_FF6  ( .C( MCI ), .D( Din[6] ), .Q( Din_Raw[6] ) );
  (* BEL = "X21/Y20/lc1" *) SB_DFF Din_FF7  ( .C( MCI ), .D( Din[7] ), .Q( Din_Raw[7] ) );
  (* BEL = "X12/Y20/lc0" *) SB_DFF Din_FF8  ( .C( MCI ), .D( Din[8] ), .Q( Din_Raw[8] ) );
  (* BEL = "X7/Y20/lc0" *) SB_DFF Din_FF9  ( .C( MCI ), .D( Din[9] ), .Q( Din_Raw[9] ) );
  (* BEL = "X7/Y20/lc1" *) SB_DFF Din_FF10 ( .C( MCI ), .D( Din[10] ), .Q( Din_Raw[10] ) );
  (* BEL = "X5/Y20/lc0" *) SB_DFF Din_FF11 ( .C( MCI ), .D( Din[11] ), .Q( Din_Raw[11] ) );
  (* BEL = "X5/Y20/lc1" *) SB_DFF Din_FF12 ( .C( MCI ), .D( Din[12] ), .Q( Din_Raw[12] ) );
  (* BEL = "X4/Y20/lc0" *) SB_DFF Din_FF13 ( .C( MCI ), .D( Din[13] ), .Q( Din_Raw[13] ) );
  (* BEL = "X4/Y20/lc1" *) SB_DFF Din_FF14 ( .C( MCI ), .D( Din[14] ), .Q( Din_Raw[14] ) );
  (* BEL = "X3/Y20/lc0" *) SB_DFF Din_FF15 ( .C( MCI ), .D( Din[15] ), .Q( Din_Raw[15] ) );
  (* BEL = "X1/Y2/lc0" *) SB_DFF Din_FF16 ( .C( MCI ), .D( Din[16] ), .Q( Din_Raw[16] ) );
  (* BEL = "X1/Y2/lc1" *) SB_DFF Din_FF17 ( .C( MCI ), .D( Din[17] ), .Q( Din_Raw[17] ) );
`else
  reg     [17:0]Din_RawR;
  initial Din_RawR = 0;
  always @( posedge MCI ) Din_RawR <= Din;
  assign  Din_Raw = Din_RawR;
`endif
  wire    [17:0]Din_Pre = { Din_Raw[17:16], ~Din_Raw[15:8], ~Din_Raw[7:0] };
  reg     [17:0]Din_Reg;
  initial Din_Reg = 0;
  always @( posedge MCI ) Din_Reg <= Din_Pre;
  assign Din_Bus = Din_Reg;

  AddrCtrl	U1(
    .ClrW( ClrW ),
    .Wclk( MCI ),
    .Start( Start_Link ),
    .nRclk( nRD ),
    .RE( CE ),
    .H_L( H_L ),
    .Depth( Depth_Link ), 
    .PerCnt( PerCnt_Link ), 
    .Delay( Delay_Link ),
    .Ready( Ready_Link ), 
    .Sampled(Sampled_Link),
    .Full( Full_Link ),
    .Empty( Empty_Link ),
    .Rptr( R_Addr_Bus ),
    .Wptr( W_Addr_Bus )
  );
  
  DP_RAM	U2(
    .Wclk( MCI ),
    .nRclk( nRD ),
    .Din( Din_Bus ),
    .Raddr( R_Addr_Bus ),
    .Waddr( W_Addr_Bus ),
    .Dout( Dout_Bus )
  );
                   
  Signal U3(
    .Reset( ClrW ),
    .Mclk( MCI ),         
    .Trigg_Mode( Tm_Link ), 
    .Vthreshold( Vt_Link ), 
    .Tthreshold( Tt_Link ), 
    .Din( Din_Bus ),
    .DinP( Din_Pre ),
    .Sampled(Sampled_Link),
    .CtrlReg( Ctrl_Link ),
    .Start( Start_Link ),
    .ClkA( CKA ),
    .ClkB( CKB ),
    .A_Edge( A_Edge_Link ),
    .A_TL( A_TL_Link ),
    .A_TH( A_TH_Link ),   
    .B_Edge( B_Edge_Link ),
    .B_TL( B_TL_Link ),
    .B_TH( B_TH_Link ),   
    .C_Edge( C_Edge_Link ),
    .C_TL( C_TL_Link ),
    .C_TH( C_TH_Link ),   
    .D_Edge( D_Edge_Link ),
    .D_TL( D_TL_Link ),
    .D_TH( D_TH_Link )   
  );

  EdgeCnt U5( .Pin( Din[16] ), .Clr( CntClr_Link ), .GrayR( CR_Link ), .GrayF( CF_Link ) );
  EdgeCnt U6( .Pin( Din[17] ), .Clr( CntClr_Link ), .GrayR( DR_Link ), .GrayF( DF_Link ) );

  QuadCnt U7( .A( Din[16] ), .B( Din[17] ),
    .GrayAR( QAR_Link ), .GrayAF( QAF_Link ), .GrayBR( QBR_Link ), .GrayBF( QBF_Link ) );

  QuadSync U8( .Mclk( MCI ), .Din( Din_Bus ), .DinP( Din_Pre ),
    .Ctrl( QCtrl_Link ), .AThr( AThr_Link ), .AHys( AHys_Link ),
    .BThr( BThr_Link ), .BHys( BHys_Link ),
    .QAR( QAR_Link ), .QAF( QAF_Link ), .QBR( QBR_Link ), .QBF( QBF_Link ),
    .LAR( LAR_Link ), .LAF( LAF_Link ), .LBR( LBR_Link ), .LBF( LBF_Link ),
    .ZGray( ZGray_Link ), .SGray( SGray_Link ), .EGray( EGray_Link ),
    .LSync( LSync_Link ), .LevA( LevA_Link ), .LevB( LevB_Link ) );

  EdgeSync U9( .Mclk( MCI ), .LvA( LevA_Link ), .LvB( LevB_Link ), .Clr( CntClr_Link ),
    .GrayA( EA_Link ), .GrayB( EB_Link ), .InitA( InitA_Link ), .InitB( InitB_Link ) );

  IO_Ctrl U4( 
    .CE( CE ), 
    .nRD( nRD ), 
    .SCK( SCK ), 
    .SDA( SDA ), 
    .Dout( Dout_Bus ),  
    .Start( Start_Link ),
    .Full( Full_Link ),
    .Empty( Empty_Link ),
    .H_L( H_L ), 
    .C_D( C_D ), 
    .Ready( Ready_Link ), 
    .A_Edge( A_Edge_Link ),
    .A_TL( A_TL_Link ),
    .A_TH( A_TH_Link ),   
    .B_Edge( B_Edge_Link ),
    .B_TL( B_TL_Link ),
    .B_TH( B_TH_Link ),   
    .C_Edge( C_Edge_Link ),
    .C_TL( C_TL_Link ),
    .C_TH( C_TH_Link ),   
    .D_Edge( D_Edge_Link ),
    .D_TL( D_TL_Link ),
    .D_TH( D_TH_Link ),   
    .Depth( Depth_Link ), 
    .PerCnt( PerCnt_Link ), 
    .Delay( Delay_Link ),
    .nPD( nPD ),
    .Trigg_Mode( Tm_Link ), 
    .Vthreshold( Vt_Link ), 
    .Tthreshold( Tt_Link ), 
    .CtrlReg( Ctrl_Link ),
    .C_Rise( CR_Link ),
    .C_Fall( CF_Link ),
    .D_Rise( DR_Link ),
    .D_Fall( DF_Link ),
    .CntClr( CntClr_Link ),
    .A_Edges( EA_Link ), .B_Edges( EB_Link ), .InitA( InitA_Link ), .InitB( InitB_Link ),
    .QAR( QAR_Link ), .QAF( QAF_Link ), .QBR( QBR_Link ), .QBF( QBF_Link ),
    .LAR( LAR_Link ), .LAF( LAF_Link ), .LBR( LBR_Link ), .LBF( LBF_Link ),
    .ZGray( ZGray_Link ), .SGray( SGray_Link ), .EGray( EGray_Link ), .LSync( LSync_Link ),
    .QCtrl( QCtrl_Link ), .AThr( AThr_Link ), .AHys( AHys_Link ),
    .BThr( BThr_Link ), .BHys( BHys_Link ),
    .DB( DB ) 
  );
  
endmodule
 
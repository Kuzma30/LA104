/*******************************************************************************
 LA104 logic analyzer image for the DS203 (iCE65L04), built from the
 oscilloscope FPGA 2.67 (../scope): same pins, bus, registers, trigger and
 input registers; the FIFO RAM holds run length words of the four channels
 instead of the samples (LaRle.v, AddrCtrlLa.v, IO_CtrlLa.v). Loaded into
 the FPGA by the application 66_logic for the time it runs (not flashed).
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
  // logic analyzer: run length words, pointers
  wire	  Adv_Link, Mark_Link, Wrapped_Link;
  wire	  [11:0]Bptr_Link;
  wire	  [17:0]Word_Link;
  wire	  [ 3:0]State_Link;
  wire	  [ 7:0]AThr_Link, AHys_Link, BThr_Link, BHys_Link;
  wire    [ 3:0]LaMask_Link;
  wire	  [15:0]ArmTime_Link;

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

  AddrCtrlLa	U1(
    .ClrW( ClrW ),
    .Wclk( MCI ),
    .Start( Start_Link ),
    .Adv( Adv_Link ),
    .ArmTime( ArmTime_Link ),
    .Mark( Mark_Link ),
    .Wrapped( Wrapped_Link ),
    .Bptr( Bptr_Link ),
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
    .Din( Word_Link ),
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

  LaRle U5( .Mclk( MCI ), .Din( Din_Bus ), .DinP( Din_Pre ), .Mark( Mark_Link ),
    .AThr( AThr_Link ), .AHys( AHys_Link ), .BThr( BThr_Link ), .BHys( BHys_Link ),
    .Mask( LaMask_Link ), .Word( Word_Link ), .Adv( Adv_Link ), .State( State_Link ) );

  IO_CtrlLa U4( 
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
    .Bptr( Bptr_Link ), .Wptr( W_Addr_Bus ), .Wrapped( Wrapped_Link ),
    .AThr( AThr_Link ), .AHys( AHys_Link ), .BThr( BThr_Link ), .BHys( BHys_Link ),
    .ArmTime( ArmTime_Link ), .LaMask( LaMask_Link ),
    .DB( DB ) 
  );
  
endmodule
 
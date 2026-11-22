/*******************************************************************************
 LA104 logic analyzer image: bus and register interface.
 IO_Ctrl.v (e-Design FPGA V2.x) without the counters of FPGA 2.6x, plus:
   serial registers 0x10 / 0x11 CH_A threshold / hysteresis, 0x12 / 0x13 CH_B
   (the comparators of LaRle, as in FPGA 2.66); 0x14 / 0x15 ArmTime: the
   trigger is accepted after PerCnt words or ArmTime * 1024 samples
   Select 0x50: { Wrapped, 3'b0, Bptr[11:0] }  address of the last word before
                the trigger, Wrapped: the write pointer went around
   Select 0x51: { Full, Ready, 2'b0, Wptr[11:0] }  write pointer (stable when
                Full; read twice while capturing)
   Select 0x52: ID 0x1A01 (logic analyzer, run length words)
 Version in the status word: 901 ("9.01").
*******************************************************************************/
module IO_CtrlLa( CE, nRD, SCK, SDA, Dout, Start, Full, Empty, H_L, C_D, Ready,
                A_Edge, A_TL, A_TH, B_Edge, B_TL, B_TH,
                C_Edge, C_TL, C_TH, D_Edge, D_TL, D_TH,
                Depth, PerCnt, Delay,
                nPD, Trigg_Mode, Vthreshold, Tthreshold, CtrlReg,
                Bptr, Wptr, Wrapped, AThr, AHys, BThr, BHys, ArmTime, DB );

  input     CE, nRD, SCK, SDA;
  input     [17:0]Dout;
  input     Start, Ready, Full, Empty, H_L, C_D;
  input     [15:0]A_Edge, A_TL, A_TH, B_Edge, B_TL, B_TH;
  input     [15:0]C_Edge, C_TL, C_TH, D_Edge, D_TL, D_TH;
  output    [11:0]Depth;
  output    [11:0]PerCnt;
  output    [31:0]Delay;
  output    nPD;
  output    [ 7:0]Trigg_Mode;
  output    [ 7:0]Vthreshold;
  output    [15:0]Tthreshold;
  output    [ 7:0]CtrlReg;
  input     [11:0]Bptr;
  input     [11:0]Wptr;
  input     Wrapped;
  output    [ 7:0]AThr, AHys, BThr, BHys;
  output    [15:0]ArmTime;
  inout     [15:0]DB;

  reg       [ 7:0]Trigg_Mode;
  reg       [ 7:0]Vthreshold;
  reg       [15:0]Tthreshold;
  reg       [ 7:0]CtrlReg;
  reg       [ 7:0]RegAddr;
  reg       [ 7:0]DataBuff;
  wire      [15:0]DB_Mux;
  reg       [15:0]Data;
  reg       [ 7:0]Select;
  wire      [15:0]CD_Mux;
  reg       [11:0]Depth;
  reg       [11:0]PerCnt;
  reg       [31:0]Delay;
  reg       [ 7:0]AThr, AHys, BThr, BHys;
  reg       [15:0]ArmTime;
  wire      [ 9:0]Version = 10'd901;

  initial begin
    AThr = 128; AHys = 8; BThr = 128; BHys = 8; ArmTime = 0;
  end

  assign nPD = CtrlReg[0];
  assign CD_Mux = C_D ? Data[15:0] : { Version, Start, Empty, Full, Ready, Dout[17:16] };
  assign DB_Mux = H_L ? Dout[15:0] : CD_Mux;
  assign DB = ( CE && !nRD ) ? DB_Mux : 16'hzzzz ;

  always @(posedge SCK) begin
    DataBuff <= { DataBuff[6:0], SDA };
  end

  always @( negedge nRD ) begin
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
     8'h50:   Data <= { Wrapped, 3'b0, Bptr };
     8'h51:   Data <= { Full, Ready, 2'b0, Wptr };
     8'h52:   Data <= 16'h1A01;
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
          8'h10:  AThr      [ 7:0] <= DataBuff;
          8'h11:  AHys      [ 7:0] <= DataBuff;
          8'h12:  BThr      [ 7:0] <= DataBuff;
          8'h13:  BHys      [ 7:0] <= DataBuff;
          8'h14:  ArmTime   [ 7:0] <= DataBuff;
          8'h15:  ArmTime  [15:8] <= DataBuff;
        endcase
      end
    end
  end

endmodule

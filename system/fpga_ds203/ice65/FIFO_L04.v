// Top level for the iCE65L04-VQ100 of the DS203, open tools.
// The sampling clock MCI is on a global buffer pin (15): it is taken to its
// global network directly from the pad (SB_GB_IO), as the vendor-built image
// does. Everything else is the unchanged module FIFO.
module FIFO_L04( ClrW, MCI, nRD, CE, H_L, C_D, SCK, SDA, Din, nPD, CKA, CKB, DB);

  input   ClrW, MCI, nRD, CE, H_L, C_D, SCK, SDA;
  input   [17:0]Din;
  output  nPD, CKA, CKB;
  inout   [15:0]DB;

  wire    MCI_g;
  SB_GB_IO #(.PIN_TYPE(6'b000001), .PULLUP(1'b0)) MCI_pad (
    .PACKAGE_PIN(MCI), .GLOBAL_BUFFER_OUTPUT(MCI_g)
  );

  FIFO U( .ClrW(ClrW), .MCI(MCI_g), .nRD(nRD), .CE(CE), .H_L(H_L), .C_D(C_D),
          .SCK(SCK), .SDA(SDA), .Din(Din), .nPD(nPD), .CKA(CKA), .CKB(CKB), .DB(DB) );

endmodule

// Testbench of the DS203 FPGA (module FIFO), the MCU side is driven as the
// SYS (Set_Param, Get) and the LA104 OS (BIOS::ADC) do it.
//
// Every bus read is printed as "R <tag> <value>". The same file is built with
// the original e-Design V2.5 sources and with the version with counters,
// the outputs must be equal (except the version bits of the status word),
// see run.sh. With -DCOUNTERS the counters are tested as well.
`timescale 1ps/1ps

module tb_scope;
    // ---------------------------------------------------------------- DUT
    reg  ClrW = 0, MCI = 0, nRD = 1, CE = 0, H_L = 1, C_D = 0, SCK = 1, SDA = 1;
    reg  [17:0] Din = 0;
    wire nPD, CKA, CKB;
    wire [15:0] DB;

    FIFO dut( .ClrW(ClrW), .MCI(MCI), .nRD(nRD), .CE(CE), .H_L(H_L), .C_D(C_D),
        .SCK(SCK), .SDA(SDA), .Din(Din), .nPD(nPD), .CKA(CKA), .CKB(CKB), .DB(DB) );

    // iCE65 flip-flops and RAM are 0 after the configuration, the original
    // design relies on it (Wptr, flags and counters have no reset)
    integer k;
    initial begin
        dut.U1.Full = 0;
        dut.U1.Loaded = 0;
        dut.U1.Ready = 0;
        dut.U1.Sampled = 0;
        dut.U1.Bptr = 0;
        dut.U1.Rptr = 0;
        dut.U1.Wptr = 0;
        dut.U1.Pcnt = 0;
        dut.U1.DelayCnt = 0;
        dut.U2.Dout = 0;
        dut.U3.Start = 0;
        dut.U3.A_Dt_H = 0;
        dut.U3.A_Dt_L = 0;
        dut.U3.A_Flag = 0;
        dut.U3.A_Status = 0;
        dut.U3.A_Ut_H = 0;
        dut.U3.A_Ut_L = 0;
        dut.U3.B_Dt_H = 0;
        dut.U3.B_Dt_L = 0;
        dut.U3.B_Flag = 0;
        dut.U3.B_Status = 0;
        dut.U3.B_Ut_H = 0;
        dut.U3.B_Ut_L = 0;
        dut.U3.C_Dt_H = 0;
        dut.U3.C_Dt_L = 0;
        dut.U3.C_Flag = 0;
        dut.U3.C_Ut_H = 0;
        dut.U3.C_Ut_L = 0;
        dut.U3.D_Dt_H = 0;
        dut.U3.D_Dt_L = 0;
        dut.U3.D_Flag = 0;
        dut.U3.D_Ut_H = 0;
        dut.U3.D_Ut_L = 0;
        dut.U3.A_Cnt = 0;
        dut.U3.B_Cnt = 0;
        dut.U3.C_Cnt = 0;
        dut.U3.D_Cnt = 0;
        dut.U3.A_Edge = 0;
        dut.U3.A_TH = 0;
        dut.U3.A_TL = 0;
        dut.U3.B_Edge = 0;
        dut.U3.B_TH = 0;
        dut.U3.B_TL = 0;
        dut.U3.C_Edge = 0;
        dut.U3.C_TH = 0;
        dut.U3.C_TL = 0;
        dut.U3.D_Edge = 0;
        dut.U3.D_TH = 0;
        dut.U3.D_TL = 0;
        dut.U4.CtrlReg = 0;
        dut.U4.DataBuff = 0;
        dut.U4.RegAddr = 0;
        dut.U4.Select = 0;
        dut.U4.Trigg_Mode = 0;
        dut.U4.Vthreshold = 0;
        dut.U4.Depth = 0;
        dut.U4.PerCnt = 0;
        dut.U4.Data = 0;
        dut.U4.Tthreshold = 0;
        dut.U4.Delay = 0;
        for (k = 0; k < 4096; k = k + 1) dut.U2.Mem_data[k] = 0;
    end

    // sample clock: 72 MHz or slower (TIM1 / MCO of the timebase)
    integer mci_half = 6944;
    always begin #(mci_half) MCI = ~MCI; end

    // ADC data: two different saw waves (inverted on the pins, as the ADC is)
    integer phase = 0;
    always @(negedge MCI) begin
        phase = phase + 1;
        Din[7:0]  <= ~((phase * 3) & 8'hff);
        Din[15:8] <= ~(((phase * 5) + 100) & 8'hff);
    end

    // digital inputs: independent generators
    integer half_c = 0, half_d = 0;
    always begin if (half_c > 0) begin #(half_c) Din[16] = ~Din[16]; end else #5000; end
    always begin if (half_d > 0) begin #(half_d) Din[17] = ~Din[17]; end else #5000; end

    // ---------------------------------------------------------------- MCU
    localparam HCLK = 13889;
    localparam GPIO = 28000;            // GPIO toggle by BSRR, ~2 HCLK

    // SYS SendByte / Set_Param
    task send_byte(input [7:0] d);
        integer i;
        begin
            SCK = 1; #GPIO;
            for (i = 0; i < 8; i = i + 1) begin
                SDA = (d << i) & 8'h80 ? 1 : 0; #GPIO;
                SCK = 0; #GPIO;
                SCK = 1; #GPIO;
            end
            SDA = 0; #GPIO;
            SCK = 0; #GPIO;
            SDA = 1; #GPIO;
        end
    endtask
    task set_param(input [7:0] addr, input [7:0] value);
        begin
            H_L = 1; #GPIO;
            send_byte(addr);
            H_L = 0; #GPIO;
            send_byte(value);
        end
    endtask

    // FSMC read of the FPGA bank
    task bus_read(output [15:0] v);
        begin
            #(HCLK * 2);
            CE = 1; nRD = 0;
            #(HCLK * 6);
            v = DB;
            nRD = 1;                    // NOE rises before NE (address hold)
            #(HCLK);
            CE = 0;
            #(HCLK);
        end
    endtask

    reg [15:0] w;
    task log_read(input [63:0] tag, input [15:0] mask);
        begin
            bus_read(w);
            $display("R %0s %04h", tag, w & mask);
        end
    endtask

    // BIOS::ADC::ReadFifo of the LA104 OS: H_L low -> status word, high -> data
    reg [15:0] hi, lo;
    task read_fifo;
        begin
            H_L = 0; #GPIO;
            bus_read(hi);
            H_L = 1; #GPIO;
            bus_read(lo);
            // status bits 15..6 are the version, not compared
            $display("R fifo %04h %04h", hi & 16'h003f, lo);
        end
    endtask
    task clr_w;
        begin
            ClrW = 1; #GPIO; ClrW = 0; #GPIO;
        end
    endtask
    task status;
        begin
            H_L = 0; #GPIO;
            log_read("status", 16'h003f);
        end
    endtask
    // CH_x edge / width registers of the original design, C_D high
    task read_meas(input [7:0] sel);
        begin
            set_param(5, sel);
            C_D = 1; H_L = 0; #GPIO;
            log_read("meas", 16'hffff);
            C_D = 0; #GPIO;
        end
    endtask

    // one acquisition as the oscilloscope application does it
    task acquire(input [7:0] trig, input [7:0] vt, input [15:0] tt, input integer nread);
        integer i, guard;
        begin
            set_param(2, tt[7:0]);
            set_param(3, tt[15:8]);
            set_param(1, vt);
            set_param(0, trig);
            clr_w;
            guard = 0;
            // wait for full (status bit 3)
            H_L = 0; #GPIO;
            bus_read(w);
            while (!(w & 16'h0008) && guard < 20000) begin
                #(1000000);
                bus_read(w);
                guard = guard + 1;
            end
            $display("R full %0d", guard > 0 ? 1 : 0);
            for (i = 0; i < nread; i = i + 1)
                read_fifo;
        end
    endtask

    // ---------------------------------------------------------------- counters
`ifdef COUNTERS
    integer failures = 0;
    task check(input [255:0] name, input ok);
        begin
            if (!ok) failures = failures + 1;
            $display("%0s %0s", ok ? "OK  " : "FAIL", name);
        end
    endtask

    // model: edges on the pins
    integer m_cr = 0, m_cf = 0, m_dr = 0, m_df = 0;
    always @(posedge Din[16]) m_cr = m_cr + 1;
    always @(negedge Din[16]) m_cf = m_cf + 1;
    always @(posedge Din[17]) m_dr = m_dr + 1;
    always @(negedge Din[17]) m_df = m_df + 1;

    function [31:0] gray2bin(input [31:0] g);
        integer i;
        begin
            gray2bin[31] = g[31];
            for (i = 30; i >= 0; i = i - 1)
                gray2bin[i] = gray2bin[i + 1] ^ g[i];
        end
    endfunction

    // driver of the LA104 application: low word latches, then high word
    task read_cnt(input [7:0] sel, output [31:0] v);
        reg [15:0] l, h;
        begin
            set_param(5, sel);
            C_D = 1; H_L = 0; #GPIO;
            bus_read(l);
            C_D = 0; #GPIO;
            set_param(5, sel + 1);
            C_D = 1; #GPIO;
            bus_read(h);
            C_D = 0; #GPIO;
            v = gray2bin({h, l});
        end
    endtask
    task clear(input [1:0] which);
        begin
            set_param(8'h0e, which);
            set_param(8'h0e, 0);
        end
    endtask

    reg [31:0] cr, cf, dr, df, prev;
    integer ok, i, e0;
`endif

    // ---------------------------------------------------------------- run
    initial begin
        #500000;
        set_param(4, 8'h01);              // ADC on, separate mode
        status;
        // regression: triggers on all channels, a few modes
        acquire(8'h20, 8'd100, 16'd10, 40);   // unconditional
        half_c = 500000; half_d = 1300000;
        acquire(8'h01, 8'd128, 16'd10, 40);   // CH_A rising
        acquire(8'h09, 8'd60,  16'd10, 40);   // CH_B rising
        acquire(8'h11, 8'd0,   16'd10, 40);   // CH_C rising
        acquire(8'h18, 8'd0,   16'd10, 40);   // CH_D falling
        acquire(8'h1d, 8'd0,   16'd50, 20);   // CH_D pulse > Tt
        read_meas(8'h08);                     // CH_C edges
        read_meas(8'h0c);                     // CH_D edges
        read_meas(8'h00);                     // CH_A edges
        // slower sampling clock
        mci_half = 6944 * 12;
        acquire(8'h11, 8'd0, 16'd10, 30);
        set_param(4, 8'h03);                  // interlace
        acquire(8'h20, 8'd0, 16'd10, 10);
        set_param(4, 8'h01);
        mci_half = 6944;
        status;

`ifdef COUNTERS
        $display("---- counters");
        half_c = 0; half_d = 0;
        #1000000;
        read_cnt(8'h18, cr);
        set_param(5, 8'h18); C_D = 1; H_L = 0; #GPIO; bus_read(w); C_D = 0;
        check("id 0xC0D2", w == 16'hC0D2);
        H_L = 0; #GPIO; bus_read(w);
        check("version 2.62 in status", (w >> 6) == 262);

        // counts since power up equal the model
        read_cnt(8'h10, cr); read_cnt(8'h12, cf); read_cnt(8'h14, dr); read_cnt(8'h16, df);
        $display("     C %0d/%0d D %0d/%0d model %0d/%0d %0d/%0d", cr, cf, dr, df, m_cr, m_cf, m_dr, m_df);
        check("counts since start", cr == m_cr && cf == m_cf && dr == m_dr && df == m_df);

        // clear: all four counters are 0
        clear(2'b01);
        m_cr = 0; m_cf = 0; m_dr = 0; m_df = 0;
        read_cnt(8'h10, cr); read_cnt(8'h12, cf); read_cnt(8'h14, dr); read_cnt(8'h16, df);
        check("clear", cr == 0 && cf == 0 && dr == 0 && df == 0);

        // 20 MHz on C and 7 MHz on D with a slow sampling clock (1 s/div
        // would be 60 Hz, here 600 kHz to keep the simulation short)
        mci_half = 833333;
        half_c = 25000; half_d = 71429;
        ok = 1; prev = 0;
        for (i = 0; i < 20; i = i + 1) begin
            // reading while the counters run: monotonic, plausible steps
            read_cnt(8'h10, cr);
            if (cr < prev || cr - prev > 32'd200) ok = 0;
            prev = cr;
        end
        check("reading while counting", ok);
        half_c = 0; half_d = 0;
        #1000000;
        read_cnt(8'h10, cr); read_cnt(8'h12, cf); read_cnt(8'h14, dr); read_cnt(8'h16, df);
        $display("     C %0d/%0d D %0d/%0d model %0d/%0d %0d/%0d", cr, cf, dr, df, m_cr, m_cf, m_dr, m_df);
        check("20 MHz / 7 MHz exact", cr == m_cr && cf == m_cf && dr == m_dr && df == m_df);

        // 32 bit wrap of the Gray code counter
        dut.U5.BinR = 32'hffff_fff0;
        dut.U5.GrayR = 32'hffff_fff0 ^ (32'hffff_fff0 >> 1);
        half_c = 50000;
        ok = 1; prev = 32'hffff_fff0;
        for (i = 0; i < 10; i = i + 1) begin
            read_cnt(8'h10, cr);
            if ((cr - prev) > 32'd200) ok = 0;
            prev = cr;
        end
        $display("     after wrap %0d", cr);
        check("wrap of 32 bits", ok && cr < 32'd1000);
        half_c = 0;

        // the FIFO still works after the counter accesses
        mci_half = 6944;
        acquire(8'h20, 8'd100, 16'd10, 4);

        if (failures == 0) $display("\nall passed"); else $display("\n%0d FAILED", failures);
`endif
        $finish;
    end
endmodule

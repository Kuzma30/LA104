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

`ifdef STOCK_DIN_REG
    // the original with the input registers of FPGA 2.67 in front of it
    // (two registers; they start with 0 after the inversion)
    reg [17:0] DinP = 18'h00000, DinR = 18'h0ffff;
    always @(posedge MCI) begin DinP <= Din; DinR <= DinP; end
    FIFO dut( .ClrW(ClrW), .MCI(MCI), .nRD(nRD), .CE(CE), .H_L(H_L), .C_D(C_D),
        .SCK(SCK), .SDA(SDA), .Din(DinR), .nPD(nPD), .CKA(CKA), .CKB(CKB), .DB(DB) );
`else
    FIFO dut( .ClrW(ClrW), .MCI(MCI), .nRD(nRD), .CE(CE), .H_L(H_L), .C_D(C_D),
        .SCK(SCK), .SDA(SDA), .Din(Din), .nPD(nPD), .CKA(CKA), .CKB(CKB), .DB(DB) );
`endif

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
`ifdef STOCK_DIN_REG
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
`endif
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
`ifdef STOCK_DIN_REG
        dut.U4.Delay = 0;
`endif
        for (k = 0; k < 4096; k = k + 1) dut.U2.Mem_data[k] = 0;
`ifdef COUNTERS
        // Din = 0 at time 0 is an x -> 0 edge for the simulator
        #1;
        dut.U7.BinAR = 0; dut.U7.BinAF = 0; dut.U7.BinBR = 0; dut.U7.BinBF = 0;
        dut.U7.GrayAR = 0; dut.U7.GrayAF = 0; dut.U7.GrayBR = 0; dut.U7.GrayBF = 0;
`endif
    end

    // sample clock: 72 MHz or slower (TIM1 / MCO of the timebase)
    integer mci_half = 6944;
    always begin #(mci_half) MCI = ~MCI; end

    // ADC data: two different saw waves (inverted on the pins, as the ADC is)
    integer phase = 0;
    reg zdrive = 0;                     // quadrature test: CH_A carries Z
    reg adrive = 0;                     // or CH_A carries A and CH_B carries B
    reg [7:0] zval = 0, aval = 0, bval = 0;
    // square waves on CH_A / CH_B (edge counters in MCI, FPGA 2.66):
    // a level change every agen / bgen samples, levels with noise
    integer agen = 0, bgen = 0, acnt = 0, bcnt = 0;
    reg sa = 0, sb = 0;
    integer m_ar = 0, m_af = 0, m_br = 0, m_bf = 0;
    // (one process with the ADC data below, the order is fixed)
    task gen_ab;
      begin
        if (agen > 0) begin
            acnt = acnt + 1;
            if (acnt >= agen) begin
                acnt = 0; sa = ~sa;
                if (sa) m_ar = m_ar + 1; else m_af = m_af + 1;
            end
            aval = sa ? 8'd200 + ($random & 7) : 8'd40 + ($random & 7);
        end
        if (bgen > 0) begin
            bcnt = bcnt + 1;
            if (bcnt >= bgen) begin
                bcnt = 0; sb = ~sb;
                if (sb) m_br = m_br + 1; else m_bf = m_bf + 1;
            end
            bval = sb ? 8'd180 + ($random & 7) : 8'd60 + ($random & 7);
        end
      end
    endtask
    always @(negedge MCI) begin
        gen_ab;
        phase = phase + 1;
        Din[7:0]  <= adrive ? ~aval : zdrive ? ~zval : ~((phase * 3) & 8'hff);
        Din[15:8] <= adrive ? ~bval : ~(((phase * 5) + 100) & 8'hff);
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

    task skip_meas(input [7:0] sel);
        begin
            set_param(5, sel);
            C_D = 1; H_L = 0; #GPIO;
            bus_read(w);
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
    // CH_A / CH_B: edges and the level before the first edge
    reg [31:0] ea, eb, ar, af, br, bf;
    reg [15:0] lv;
    task read_ab;
        begin
            read_cnt(8'h40, ea);
            read_cnt(8'h42, eb);
            set_param(5, 8'h44); C_D = 1; H_L = 0; #GPIO; bus_read(lv); C_D = 0; #GPIO;
            ar = (ea + !lv[0]) >> 1; af = ea - ar;
            br = (eb + !lv[1]) >> 1; bf = eb - br;
        end
    endtask
    integer ok, i, e0;

    // ---- quadrature encoder on C (A) and D (B), Z on CH_A
    integer qpos = 0;                   // model position (x4 steps)
    integer qstep_t = 200000;           // time between steps
    integer zwidth = 2;                 // Z high for this many steps
    integer ppr4 = 400;                 // steps per revolution
    integer qmode = 0;                  // 0: A, B on C, D, Z on CH_A; 1: A, B on CH_A, CH_B, Z on C
    // forward: (A,B) 00 -> 10 -> 11 -> 01
    task qstep(input integer dir);
        reg [1:0] st;
        reg za, zb, zz;
        begin
            qpos = qpos + dir;
            st = qpos & 3;
            za = (st == 1 || st == 2);
            zb = (st == 2 || st == 3);
            // Z: high in the first zwidth steps of every revolution
            zz = (((qpos % ppr4) + ppr4) % ppr4) < zwidth;
            if (qmode == 0) begin
                Din[16] = za;
                Din[17] = zb;
                zval = zz ? 8'd220 : 8'd30;
            end else begin
                // analog levels with some noise within the hysteresis
                aval = za ? 8'd200 + ($random & 7) : 8'd40 + ($random & 7);
                bval = zb ? 8'd180 + ($random & 7) : 8'd60 + ($random & 7);
                Din[16] = zz;
            end
            #(qstep_t);
        end
    endtask
    task qmove(input integer n);
        integer j;
        begin
            for (j = 0; j < (n < 0 ? -n : n); j = j + 1)
                qstep(n < 0 ? -1 : 1);
        end
    endtask

    // reads a latched set of four counters (select 0x20 or 0x28), returns the sum
    reg [15:0] qw [0:7];
    task read_quad(input [7:0] base, output [31:0] pos, output [15:0] zc);
        integer j;
        begin
            for (j = 0; j < 8; j = j + 1) begin
                set_param(5, base + j);
                C_D = 1; H_L = 0; #GPIO;
                bus_read(w);
                qw[j] = w;
                C_D = 0; #GPIO;
            end
            pos = gray2bin({qw[1], qw[0]}) + gray2bin({qw[3], qw[2]}) +
                  gray2bin({qw[5], qw[4]}) + gray2bin({qw[7], qw[6]});
            pos = {{8{pos[23]}}, pos[23:0]};    // 24 bit counters
            zc = 0;
            if (base == 8'h28) begin
                set_param(5, 8'h30);
                C_D = 1; H_L = 0; #GPIO;
                bus_read(w);
                C_D = 0; #GPIO;
                zc = gray2bin({16'h0, w});
            end
        end
    endtask

    // decoder in MCI: position, errors, position at Z, Z counter
    reg [31:0] s_pos, s_atz;
    reg [15:0] s_err, s_z;
    task read_sync;
        begin
            for (k = 0; k < 6; k = k + 1) begin
                set_param(5, 8'h38 + k);
                C_D = 1; H_L = 0; #GPIO;
                bus_read(w);
                qw[k] = w;
                C_D = 0; #GPIO;
            end
            s_pos = gray2bin({qw[1], qw[0]});
            s_pos = {{8{s_pos[23]}}, s_pos[23:0]};
            s_err = gray2bin({16'h0, qw[2]});
            s_atz = gray2bin({qw[4], qw[3]});
            s_atz = {{8{s_atz[23]}}, s_atz[23:0]};
            s_z   = gray2bin({16'h0, qw[5]});
        end
    endtask
    reg [31:0] q0, q1, qz, qz0, sp0;
    reg [15:0] w2;
    integer guard, npair, nstep, prevmin;
    reg [15:0] se0, sz0;
    reg [15:0] zc, zc0;
    integer qbase;
`endif

    // ---------------------------------------------------------------- run
    initial begin
        #500000;
        set_param(4, 8'h01);              // ADC on, separate mode
        status;
        // regression: triggers on all channels, a few modes
        acquire(8'h20, 8'd100, 16'd10, 40);   // unconditional
        // (no edges of C and D at the same instant: in a zero-delay gate
        // netlist the order of such events is arbitrary)
        half_c = 500000; half_d = 1300001;
        acquire(8'h01, 8'd128, 16'd10, 40);   // CH_A rising
        acquire(8'h09, 8'd60,  16'd10, 40);   // CH_B rising
        acquire(8'h11, 8'd0,   16'd10, 40);   // CH_C rising
        acquire(8'h18, 8'd0,   16'd10, 40);   // CH_D falling
        acquire(8'h1d, 8'd0,   16'd50, 20);   // CH_D pulse > Tt
        // the measurement registers of the original (Select 0x00..0x0E) are
        // removed in FPGA 2.68: the same bus accesses, not compared (the
        // timing of the tests that follow stays the same)
        skip_meas(8'h08);
        skip_meas(8'h0c);
        skip_meas(8'h00);
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
        check("version 2.68 in status", (w >> 6) == 268);

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

        // ---------------------------------------------------- quadrature
        $display("---- quadrature");
        set_param(5, 8'h19); C_D = 1; H_L = 0; #GPIO; bus_read(w); C_D = 0; #GPIO;
        check("id 0xC0D4", w == 16'hC0D4);
        mci_half = 6944;
        // start from A = B = 0, the counters already hold the edges of the
        // tests above: positions are compared as differences
        Din[16] = 0; #1000; Din[17] = 0; #999000;
        qpos = 0;
        read_quad(8'h20, q0, zc);
        qmove(1000);
        read_quad(8'h20, q1, zc);
        $display("     forward 1000: %0d", $signed(q1 - q0));
        check("forward 1000 steps", q1 - q0 == 1000);
        qmove(-1500);
        read_quad(8'h20, q1, zc);
        $display("     back 1500: %0d", $signed(q1 - q0));
        check("backward 1500 (negative)", $signed(q1 - q0) == -500);

        // bouncing edge: A toggles 5 times while B is stable
        qbase = qpos;
        for (i = 0; i < 5; i = i + 1) begin
            Din[16] = ~Din[16]; #30000;
        end
        Din[16] = ~Din[16]; #30000;     // even number of toggles: back to the level
        read_quad(8'h20, q1, zc);
        check("bouncing edge cancels", $signed(q1 - q0) == -500);

        // Z on CH_A: threshold 128, hysteresis 8, active high
        zval = 30; zdrive = 1; #1000000;
        set_param(8'h10, 128);
        set_param(8'h11, 8);
        set_param(8'h0f, 8'h4e);       // A = C, B = D, Z = CH_A on
        read_quad(8'h28, qz0, zc0);
        // to a revolution boundary, then 3 revolutions forward
        qmove(500 + 10);                // position 10 (model)
        read_quad(8'h28, qz, zc);
        // from -500 to 10: Z at -400 and at 0, latched at the first Z step
        $display("     Z from -500 to 10: count %0d, latched %0d", zc - zc0, $signed(qz - q0));
        check("2 Z, latched at 0", zc - zc0 == 2 && $signed(qz - q0) == 0);
        zc0 = zc;
        qmove(3 * 400);
        read_quad(8'h28, qz, zc);
        read_quad(8'h20, q1, w);
        $display("     3 revolutions: Z count %0d, latched %0d, now %0d", zc - zc0, $signed(qz - q0), $signed(q1 - q0));
        check("3 revs: 3 Z, at 1200, pos 1210", zc - zc0 == 3 && $signed(qz - q0) == 1200 && $signed(q1 - q0) == 1210);
        check("in the revolution: 10", $signed(q1 - qz) == 10);

        // backward: Z high at 1200 and 1201, going down from 1210 the
        // first position with Z is 1201
        qmove(-20);
        read_quad(8'h28, qz, zc);
        $display("     backward over Z: latched %0d", $signed(qz - q0));
        check("backward: Z at 1201", $signed(qz - q0) == 1201);

        // noise around the threshold (within the hysteresis): no Z
        read_quad(8'h28, qz0, zc0);
        for (i = 0; i < 40; i = i + 1) begin
            zval = (i & 1) ? 8'd133 : 8'd123; #200000;
        end
        zval = 30; #200000;
        read_quad(8'h28, qz, zc);
        check("noise within hysteresis: no Z", zc == zc0);
        // active low: the edge to low counts
        set_param(8'h0f, 8'hce);       // active low
        read_quad(8'h28, qz0, zc0);
        zval = 220; #500000; zval = 30; #500000;
        read_quad(8'h28, qz, zc);
        check("active low", zc - zc0 == 1);
        zval = 220; #500000;            // inactive again (no count)
        read_quad(8'h28, qz, zc);
        check("active low: high not counted", zc - zc0 == 1);

        // fast encoder: 10 MHz steps (each edge 100 ns apart), slow MCI,
        // reading while it runs, then exact
        mci_half = 833333;
        zdrive = 0;
        set_param(8'h0f, 8'h0e);
        qstep_t = 100000;
        read_quad(8'h20, q0, zc);
        fork
            qmove(30000);
            begin
                ok = 1; prev = q0;
                for (i = 0; i < 6; i = i + 1) begin
                    read_quad(8'h20, q1, zc);
                    if ($signed(q1 - prev) < 0 || q1 - prev > 32'd20000) ok = 0;
                    prev = q1;
                end
            end
        join
        read_quad(8'h20, q1, zc);
        $display("     10 MHz steps: %0d", q1 - q0);
        check("reading while it runs", ok);
        check("10 MHz steps exact", q1 - q0 == 30000);

        // ---- decoder in MCI: A on CH_A, B on CH_B (analog), Z on CH_C
        $display("---- quadrature from any input");
        mci_half = 6944;                // 72 MHz
        qmode = 1; qstep_t = 200000; adrive = 1; Din[16] = 0;
        qmove(1);                       // levels of the analog inputs
        set_param(8'h10, 120); set_param(8'h11, 10);   // CH_A
        set_param(8'h12, 120); set_param(8'h13, 10);   // CH_B
        set_param(8'h0f, 8'h64);        // A = CH_A, B = CH_B, Z = CH_C on
        #1000000;
        read_sync; sp0 = s_pos; se0 = s_err; sz0 = s_z;
        // to the next revolution boundary, then 2 revolutions and back
        qbase = qpos;
        qmove(((ppr4 - (qpos % ppr4)) % ppr4) + 5);   // just after a Z
        qz0 = qpos;                     // model position after the Z step
        read_sync;
        $display("     after Z: pos %0d (model %0d), Z %0d, at Z %0d", $signed(s_pos - sp0), qpos - qbase, s_z - sz0, $signed(s_atz - sp0));
        check("analog A/B steps", $signed(s_pos - sp0) == qpos - qbase && s_err == se0);
        check("Z on CH_C latched", s_z - sz0 == 1 && $signed(s_atz - sp0) == qz0 - 5 - qbase);
        qmove(2 * 400);
        qmove(-123);
        read_sync;
        $display("     +800 -123: pos %0d, Z %0d, at Z %0d, in rev %0d (qbase %0d)", $signed(s_pos - sp0), s_z - sz0, $signed(s_atz - sp0), $signed(s_pos - s_atz), qbase);
        check("2 revolutions and back", $signed(s_pos - sp0) == qpos - qbase && s_z - sz0 == 4 && s_err == se0);
        // backward the Z is met at 801 of the revolution; the analog inputs
        // come one sample later than the digital Z (in the DS203 the ADC
        // adds its own latency), a step at the same moment as Z may be
        // captured before or after it
        check("backward Z at 801 +-1",
            $signed(s_atz - sp0) >= 809 && $signed(s_atz - sp0) <= 811);
        // A on CH_D, B on CH_A (mixed): only the source bits change
        // steps faster than the sampling clock: errors are counted
        mci_half = 833333;              // 600 kHz
        qstep_t = 300000;               // 3.3 MHz steps
        qmove(200);
        #2000000;
        read_sync;
        $display("     too fast: errors %0d", s_err - se0);
        check("too fast: errors counted", ((s_err - se0) & 8'hff) > 10);
        adrive = 0; qmode = 0;
        // C and D in MCI as well (A = D, B = C: reversed), with the pins
        mci_half = 6944; qstep_t = 200000;
        qstep(0);                       // C, D to the state of the position
        set_param(8'h0f, 8'h0b);        // A = CH_D (3), B = CH_C (2)
        #1000000;
        read_sync; sp0 = s_pos;
        read_quad(8'h20, q0, zc);
        qmove(333);
        read_sync;
        read_quad(8'h20, q1, zc);
        $display("     A = D, B = C: MCI %0d, pins %0d", $signed(s_pos - sp0), $signed(q1 - q0));
        check("A = D, B = C reversed", $signed(s_pos - sp0) == -333 && $signed(q1 - q0) == 333);
        set_param(8'h0f, 8'h0e);

        // ---- edge counters of CH_A / CH_B (comparators, in MCI)
        $display("---- edge counters of CH_A / CH_B");
        set_param(5, 8'h1A); C_D = 1; H_L = 0; #GPIO; bus_read(w); C_D = 0; #GPIO;
        check("id 0xC0D6", w == 16'hC0D6);
        mci_half = 6944;
        adrive = 1; qmode = 1;
        set_param(8'h10, 120); set_param(8'h11, 10);
        set_param(8'h12, 120); set_param(8'h13, 10);
        // A starts low, B starts high
        sa = 0; sb = 1; aval = 40; bval = 180; agen = 0; bgen = 0; #1000000;
        clear(2'b01);
        m_ar = 0; m_af = 0; m_br = 0; m_bf = 0; acnt = 0; bcnt = 0;
        // A: a change every sample (36 MHz at 72 MHz), B every 3 samples
        agen = 1; bgen = 3;
        ok = 1; prev = 0;
        for (i = 0; i < 8; i = i + 1) begin
            read_cnt(8'h40, cr);
            if (cr < prev || cr - prev > 32'd2000) ok = 0;
            prev = cr;
        end
        check("A/B: reading while counting", ok);
        agen = 0; bgen = 0; #1000000;
        read_ab;
        $display("     A %0d/%0d B %0d/%0d model %0d/%0d %0d/%0d", ar, af, br, bf, m_ar, m_af, m_br, m_bf);
        check("A/B: fs/2 exact", ar == m_ar && af == m_af && br == m_br && bf == m_bf && m_ar > 1000);
        // an odd number of edges on both (A ends high, B ends low)
        sa = ~sa; aval = sa ? 8'd200 : 8'd40; if (sa) m_ar = m_ar + 1; else m_af = m_af + 1;
        sb = ~sb; bval = sb ? 8'd180 : 8'd60; if (sb) m_br = m_br + 1; else m_bf = m_bf + 1;
        #1000000;
        $display("     A %0d/%0d B %0d/%0d model %0d/%0d %0d/%0d", ar, af, br, bf, m_ar, m_af, m_br, m_bf);
        read_ab;
        $display("     A %0d/%0d B %0d/%0d model %0d/%0d %0d/%0d", ar, af, br, bf, m_ar, m_af, m_br, m_bf);
        check("A/B: odd edges, levels", ar == m_ar && af == m_af && br == m_br && bf == m_bf);
        // noise around the threshold within the hysteresis: no edges
        for (i = 0; i < 40; i = i + 1) begin
            aval = (i & 1) ? 8'd127 : 8'd113; bval = (i & 1) ? 8'd113 : 8'd127; #200000;
        end
        aval = sa ? 8'd200 : 8'd40; bval = sb ? 8'd180 : 8'd60; #200000;
        read_ab;
        check("A/B: noise within hysteresis", ar == m_ar && af == m_af && br == m_br && bf == m_bf);
        // clear (asynchronous) with a slow sampling clock
        mci_half = 833333;
        clear(2'b01);
        read_cnt(8'h40, cr); read_cnt(8'h42, cf);
        check("A/B: clear", cr == 0 && cf == 0);
        mci_half = 6944;
        adrive = 0; qmode = 0;

        // ---- peak detection and 32 bit pulse triggers (FPGA 2.68)
        $display("---- peak detection, pulse triggers of 32 bits");
        set_param(5, 8'h1B); C_D = 1; H_L = 0; #GPIO; bus_read(w); C_D = 0; #GPIO;
        check("id 0xC0D8", w == 16'hC0D8);
        mci_half = 6944;
        // K = 8: CH_A rises by 3 per sample, CH_B by 5 (saw waves)
        set_param(1, 128);
        set_param(0, 8'h01);                      // CH_A rising: pretrigger part filled
        set_param(8'h0a, 7); set_param(8'h0b, 0);
        clr_w;
        guard = 0;
        H_L = 0; #GPIO; bus_read(w);
        while (!(w & 16'h0008) && guard < 2000) begin #(1000000); bus_read(w); guard = guard + 1; end
        check("peak: full", (w & 16'h0008) != 0);
        ok = 1; npair = 0; nstep = 0; prevmin = -1;
        // the first read is the old read pointer (as in the original)
        H_L = 0; #GPIO; bus_read(hi); H_L = 1; #GPIO; bus_read(lo);
        for (i = 0; i < 60; i = i + 1) begin
            H_L = 0; #GPIO; bus_read(hi); H_L = 1; #GPIO; bus_read(lo);
            H_L = 0; #GPIO; bus_read(w);  H_L = 1; #GPIO; bus_read(w2);

            if ((hi & 3) != 0 || (w & 3) != 1) ok = 0;        // pair: min, max
            if (lo[7:0] > w2[7:0] || lo[15:8] > w2[15:8]) ok = 0;
            if (w2[7:0] - lo[7:0] == 21 && w2[15:8] - lo[15:8] == 35) begin
                npair = npair + 1;
                if (prevmin >= 0 && ((lo[7:0] - prevmin) & 8'hff) == 24) nstep = nstep + 1;
                prevmin = lo[7:0];
            end else prevmin = -1;
        end
        $display("     pairs %0d of 60 with max - min = 7 steps, %0d periods of 8 samples", npair, nstep);
        check("peak: min / max of 8 samples, words 00 / 01", ok && npair >= 40 && nstep >= 30);
        // off again with the trigger mode: samples as before
        set_param(0, 8'h01);
        clr_w; #(1000000);
        H_L = 0; #GPIO; bus_read(w); while (!(w & 16'h0008)) begin #(1000000); bus_read(w); end
        H_L = 0; #GPIO; bus_read(hi); H_L = 1; #GPIO; bus_read(lo);
        H_L = 0; #GPIO; bus_read(hi); H_L = 1; #GPIO; bus_read(lo);
        H_L = 0; #GPIO; bus_read(w);  H_L = 1; #GPIO; bus_read(w2);
        check("peak: off after the trigger mode", ((w2[7:0] - lo[7:0]) & 8'hff) == 3 && ((w2[15:8] - lo[15:8]) & 8'hff) == 5);
        // pulse trigger of CH_C: low longer than 70000 samples (0x011170)
        half_c = 0; #100000; Din[16] = 1; #100000;
        set_param(2, 8'h70); set_param(3, 8'h11); set_param(1, 0);
        set_param(0, 8'h15);                      // CH_C pulse 0 > Tt
        set_param(8'h0c, 8'h01); set_param(8'h0d, 0);
        clr_w; #(300 * 13888);
        Din[16] = 0; #(60000 * 13888); Din[16] = 1; #(100 * 13888);
        H_L = 0; #GPIO; bus_read(w);
        check("pulse 60000 < 70000: no trigger", (w & 16'h0020) == 0);
        Din[16] = 0; #(80000 * 13888); Din[16] = 1; #(100 * 13888);
        H_L = 0; #GPIO; bus_read(w);
        check("pulse 80000 > 70000: trigger", (w & 16'h0020) != 0);

        // the FIFO still works after the counter accesses
        mci_half = 6944;
        acquire(8'h20, 8'd100, 16'd10, 4);

        if (failures == 0) $display("\nall passed"); else $display("\n%0d FAILED", failures);
`endif
        $finish;
    end
endmodule

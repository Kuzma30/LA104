// Testbench of the logic analyzer image (../la, module FIFO): run length
// words of CH_C, CH_D and the comparators of CH_A, CH_B.
//
// A reference of the state is taken from the pins in every MCI (the image
// sees the pins three MCI later: input register, Din_Reg / compare, level).
// After a capture the words are read as the application does it (BIOS
// ReadFifo pairs, the first pair is the stale output register), expanded to
// samples and compared with the reference around the trigger, sample by
// sample. Prints OK / FAIL per test and "all passed".
`timescale 1ps/1ps

module tb_la;
    reg  ClrW = 0, MCI = 0, nRD = 1, CE = 0, H_L = 1, C_D = 0, SCK = 1, SDA = 1;
    reg  [17:0] Din = 18'h0ffff;        // ADC data inverted on the pins: 0 = 0xff
    wire nPD, CKA, CKB;
    wire [15:0] DB;

    FIFO dut( .ClrW(ClrW), .MCI(MCI), .nRD(nRD), .CE(CE), .H_L(H_L), .C_D(C_D),
        .SCK(SCK), .SDA(SDA), .Din(Din), .nPD(nPD), .CKA(CKA), .CKB(CKB), .DB(DB) );

    // configuration state of the iCE65: flip-flops and RAM 0
    integer k;
    initial begin
        dut.U1.Full = 0; dut.U1.Loaded = 0; dut.U1.Ready = 0; dut.U1.Bptr = 0;
        dut.U1.Rptr = 0; dut.U1.Wptr = 0; dut.U1.Pcnt = 0; dut.U1.DelayCnt = 0;
        dut.U1.Wrapped = 0; dut.U1.Sampled = 0; dut.U1.ArmCnt = 0; dut.U4.ArmTime = 0;
        dut.U2.Dout = 0;
        dut.U3.Start = 0;
        dut.U3.A_Dt_H = 0; dut.U3.A_Dt_L = 0; dut.U3.A_Flag = 0; dut.U3.A_Status = 0;
        dut.U3.A_Ut_H = 0; dut.U3.A_Ut_L = 0; dut.U3.B_Dt_H = 0; dut.U3.B_Dt_L = 0;
        dut.U3.B_Flag = 0; dut.U3.B_Status = 0; dut.U3.B_Ut_H = 0; dut.U3.B_Ut_L = 0;
        dut.U3.C_Dt_H = 0; dut.U3.C_Dt_L = 0; dut.U3.C_Flag = 0; dut.U3.C_Ut_H = 0;
        dut.U3.C_Ut_L = 0; dut.U3.D_Dt_H = 0; dut.U3.D_Dt_L = 0; dut.U3.D_Flag = 0;
        dut.U3.D_Ut_H = 0; dut.U3.D_Ut_L = 0;
        dut.U3.A_Cnt = 0; dut.U3.B_Cnt = 0; dut.U3.C_Cnt = 0; dut.U3.D_Cnt = 0;
        dut.U3.A_Edge = 0; dut.U3.A_TH = 0; dut.U3.A_TL = 0; dut.U3.B_Edge = 0;
        dut.U3.B_TH = 0; dut.U3.B_TL = 0; dut.U3.C_Edge = 0; dut.U3.C_TH = 0;
        dut.U3.C_TL = 0; dut.U3.D_Edge = 0; dut.U3.D_TH = 0; dut.U3.D_TL = 0;
        dut.U4.CtrlReg = 0; dut.U4.DataBuff = 0; dut.U4.RegAddr = 0; dut.U4.Select = 0;
        dut.U4.Trigg_Mode = 0; dut.U4.Vthreshold = 0; dut.U4.Depth = 0; dut.U4.PerCnt = 0;
        dut.U4.Data = 0; dut.U4.Tthreshold = 0; dut.U4.Delay = 0;
        for (k = 0; k < 4096; k = k + 1) dut.U2.Mem_data[k] = 0;
    end

    // 72 MHz sample clock
    integer mci_half = 6944;
    always begin #(mci_half) MCI = ~MCI; end

    // ------------------------------------------------------------ stimulus
    // levels change on negedge MCI after a random number of samples; mode 0:
    // fast random (1..maxrun samples), the quiet phase holds every channel
    integer maxrun = 20, quiet = 0, cyc = 0;
    integer cntc = 0, cntd = 0, cnta = 0, cntb = 0;
    reg lc = 0, ld = 0, la = 0, lb = 0;
    reg tog_c = 0;                      // toggle request of CH_C (long runs)
    reg [7:0] av, bv;
    always @(negedge MCI) begin
        if (tog_c) begin lc = ~lc; tog_c = 0; end
        if (!quiet) begin
            cntc = cntc - 1; if (cntc <= 0) begin lc = ~lc; cntc = 1 + {$random} % maxrun; end
            cntd = cntd - 1; if (cntd <= 0) begin ld = ~ld; cntd = 1 + {$random} % (maxrun * 3); end
            cnta = cnta - 1; if (cnta <= 0) begin la = ~la; cnta = 2 + {$random} % (maxrun * 2); end
            cntb = cntb - 1; if (cntb <= 0) begin lb = ~lb; cntb = 2 + {$random} % (maxrun * 5); end
        end
        // analog: far from the thresholds (128 +- 8), with noise
        av = la ? 8'd200 + ({$random} & 15) : 8'd40 + ({$random} & 15);
        bv = lb ? 8'd190 + ({$random} & 15) : 8'd50 + ({$random} & 15);
        Din[7:0]  <= ~av;
        Din[15:8] <= ~bv;
        Din[16]   <= lc;
        Din[17]   <= ld;
    end

    // reference: state at every MCI as the image sees it (3 MCI later)
    localparam LAT = 3;
    localparam NREF = 1 << 22;
    reg [3:0] ref [0:NREF-1];
    reg [3:0] pin_state;
    // MCI of the trigger (Mark: the edge where its word starts) as cycle number
    integer mark_cyc = -1;
    always @(posedge MCI) begin
        if (dut.U1.Mark && mark_cyc < 0) mark_cyc = cyc;
        pin_state = { lb, la, ld, lc };
        ref[(cyc + LAT) % NREF] = pin_state;
        cyc = cyc + 1;
    end

    // ------------------------------------------------------------ MCU
    localparam HCLK = 13889;
    localparam GPIO = 28000;
    task send_byte(input [7:0] d);
        integer i;
        begin
            SCK = 1; #GPIO;
            for (i = 0; i < 8; i = i + 1) begin
                SDA = (d << i) & 8'h80 ? 1 : 0; #GPIO;
                SCK = 0; #GPIO;
                SCK = 1; #GPIO;
            end
            SDA = 0; #GPIO; SCK = 0; #GPIO; SDA = 1; #GPIO;
        end
    endtask
    task set_param(input [7:0] addr, input [7:0] value);
        begin
            H_L = 1; #GPIO; send_byte(addr);
            H_L = 0; #GPIO; send_byte(value);
        end
    endtask
    task bus_read(output [15:0] v);
        begin
            #(HCLK * 2); CE = 1; nRD = 0; #(HCLK * 6);
            v = DB; nRD = 1; #(HCLK); CE = 0; #(HCLK);
        end
    endtask
    reg [15:0] hi, lo, w;
    task read_fifo(output [17:0] v);
        begin
            H_L = 0; #GPIO; bus_read(hi);
            H_L = 1; #GPIO; bus_read(lo);
            v = { hi[1:0], lo };
        end
    endtask
    task read_reg(input [7:0] sel, output [15:0] v);
        begin
            set_param(5, sel);
            C_D = 1; H_L = 0; #GPIO; bus_read(v); C_D = 0; #GPIO;
        end
    endtask
    task clr_w; begin ClrW = 1; #GPIO; ClrW = 0; #GPIO; end endtask

    // ------------------------------------------------------------ checks
    integer failures = 0;
    task check(input [319:0] name, input ok);
        begin
            if (!ok) failures = failures + 1;
            $display("%0s %0s", ok ? "OK  " : "FAIL", name);
        end
    endtask

    reg [17:0] words [0:4095];
    integer nwords;
    reg [15:0] r50, r51;

    // capture: trigger mode, pretrigger words; waits for Full (or timeout),
    // reads the pointers and 1 + nread pairs (the first is stale)
    task capture(input [7:0] trig, input integer pre, input integer timeout_us, input [15:0] arm);
        integer i, guard;
        reg [17:0] v;
        begin
            mark_cyc = -1;
            set_param(1, 8'd128);               // trigger threshold A / B
            set_param(0, trig);                 // resets Depth / PerCnt / Delay
            set_param(6, 8'hFE); set_param(7, 8'h0F);   // Depth 4094
            set_param(8, pre & 255); set_param(9, pre >> 8);
            set_param(8'h14, arm[7:0]); set_param(8'h15, arm[15:8]);
            clr_w;
            guard = 0;
            H_L = 0; #GPIO; bus_read(w);
            while (!(w & 16'h0008) && guard < timeout_us) begin
                #(1000000); bus_read(w); guard = guard + 1;
            end
            read_reg(8'h50, r50);
            read_reg(8'h51, r51);
            // not full (stopped): the words up to the write pointer
            nwords = r51[15] ? 4095 : ((r51[11:0] - (r50[11:0] - pre)) & 12'hfff);
            read_fifo(v);                       // stale output register
            for (i = 0; i < nwords; i = i + 1) begin
                read_fifo(v);
                words[i] = v;
            end
        end
    endtask

    // expands the words read and compares with the reference: word pre + 1
    // starts at mark_cyc. first: index of the first valid word
    task verify(input [319:0] name, input integer pre, input integer first);
        integer i, j, t, len, bad, samples, merged;
        reg [3:0] s;
        begin
            bad = 0; samples = 0;
            // backwards from the trigger word: words pre .. first
            t = mark_cyc;
            for (i = pre; i >= first && bad == 0; i = i - 1) begin
                s = words[i][17:14]; len = words[i][13:0] + 1;
                for (j = 1; j <= len; j = j + 1) begin
                    if (ref[(t - j) % NREF] !== s) begin
                        if (bad == 0) $display("  pre word %0d sample -%0d: %h ref %h", i, j, s, ref[(t - j) % NREF]);
                        bad = bad + 1;
                    end
                    samples = samples + 1;
                end
                t = t - len;
            end
            // forwards: words pre + 1 .. end
            t = mark_cyc;
            for (i = pre + 1; i < nwords && bad == 0; i = i + 1) begin
                s = words[i][17:14]; len = words[i][13:0] + 1;
                for (j = 0; j < len; j = j + 1) begin
                    if (ref[(t + j) % NREF] !== s) begin
                        if (bad == 0) $display("  post word %0d sample %0d: %h ref %h", i, j, s, ref[(t + j) % NREF]);
                        bad = bad + 1;
                    end
                    samples = samples + 1;
                end
                t = t + len;
            end
            $display("  %0d words checked from %0d, %0d samples", nwords - first, first, samples);
            check(name, bad == 0 && samples > 0);
        end
    endtask

    // state at the trigger: the triggering edge must be just before mark_cyc
    task check_edge(input [319:0] name, input integer bit_, input rising);
        integer d, found;
        begin
            found = -1;
            for (d = 0; d < 8 && found < 0; d = d + 1)
                if (ref[(mark_cyc - d) % NREF][bit_] == rising && ref[(mark_cyc - d - 1) % NREF][bit_] == !rising)
                    found = d;
            $display("  trigger edge %0d samples before the trigger word", found);
            check(name, found >= 0 && found <= 4);
        end
    endtask

    initial begin
        #(200000);
        set_param(4, 8'h01);                    // nPD
        // thresholds (registers 0x10..0x13 default 128 / 8)

        // 1. busy signals, trigger CH_C rising, 2048 words before it
        maxrun = 20; quiet = 0;
        #(5000000);
        capture(8'h11, 2048, 2000, 16'h7fff);
        $display("  Bptr %0d wrapped %0d, Wptr %0d full %0d", r50[11:0], r50[15], r51[11:0], r51[15]);
        check("C rising: full, triggered", r51[15] && r51[14] && mark_cyc > 0);
        check("C rising: 2048 words before the trigger", r50[15] || r50[11:0] >= 2048);
        verify("C rising: words = pins", 2048, 0);
        check_edge("C rising: edge at the trigger", 0, 1);

        // 2. trigger CH_D falling, 100 words before
        capture(8'h18, 100, 2000, 16'h7fff);
        verify("D falling: words = pins", 100, 0);
        check_edge("D falling: edge at the trigger", 1, 0);

        // 3. trigger CH_A rising (analog, threshold 128)
        capture(8'h01, 1000, 2000, 16'h7fff);
        verify("A rising: words = pins", 1000, 0);
        check_edge("A rising: edge at the trigger", 2, 1);

        // 4. quiet line: few words before the trigger, not wrapped
        quiet = 1;
        lc = 0; ld = 0; la = 0; lb = 0;
        #(2000000);
        fork
            capture(8'h11, 2048, 4000, 16'd1);
            begin
                #(60000000);                    // 60 us quiet, then C rises
                quiet = 0; maxrun = 40;
            end
        join
        $display("  Bptr %0d wrapped %0d", r50[11:0], r50[15]);
        // valid words: written since ClrW, the read starts at Bptr - 2048
        check("quiet: not wrapped, Bptr < pretrigger", !r50[15] && r50[11:0] < 2048);
        verify("quiet: valid words = pins", 2048, 2048 - r50[11:0]);

        // 5. long runs: one level change every 40000 samples (> 16384): the
        //    runs are split in full words of the same state
        quiet = 1; lc = 0; ld = 0; la = 0; lb = 0;
        fork
            capture(8'h11, 4, 30000, 16'd1);
            begin : long_gen
                integer n;
                for (n = 0; n < 40; n = n + 1) begin
                    #(40000 * 2 * 6944); tog_c = 1;
                end
            end
        join
        $display("  Bptr %0d wrapped %0d, Wptr %0d full %0d", r50[11:0], r50[15], r51[11:0], r51[15]);
        verify("long runs: split words = pins", 4, r50[11:0] >= 4 ? 0 : 4 - r50[11:0]);
        begin : long_chk
            integer n, full_words;
            full_words = 0;
            for (n = 0; n < nwords; n = n + 1) if (words[n][13:0] == 14'h3FFF) full_words = full_words + 1;
            $display("  %0d words, %0d of 16384 samples", nwords, full_words);
            check("long runs: words of 16384 samples", full_words > 10);
        end

        // 6. no trigger (UNCONDITION): the capture starts at once
        quiet = 0; maxrun = 10;
        capture(8'h20, 0, 2000, 16'd0);
        check("unconditional: full", r51[15]);
        verify("unconditional: words = pins", 0, 1);

        if (failures == 0) $display("all passed");
        else $display("%0d FAILED", failures);
        $finish;
    end
endmodule

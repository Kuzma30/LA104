// Testbench of counter_top: bus accesses with the timing of the STM32 FSMC
// (asynchronous to the FPGA clock), counted edges compared with a model.
//   iverilog -g2012 -o tb tb_counter.v counter_top.v counter32.v && vvp tb
`timescale 1ps/1ps

module tb_counter;
    reg clk = 0;
    always #6944 clk = ~clk;                   // 72 MHz

    reg ce = 0, nwr = 1, nrd = 1;
    reg [15:0] db_out = 0;
    reg db_drive = 0;
    wire [15:0] db = db_drive ? db_out : 16'hzzzz;
    reg chc = 0, chd = 0;
    wire adc_sleep, cha_clk, chb_clk;

    counter_top dut (.clk(clk), .fsmc_ce(ce), .fsmc_nwr(nwr), .fsmc_nrd(nrd), .fsmc_db(db),
        .chc_din(chc), .chd_din(chd), .adc_sleep(adc_sleep), .cha_clk(cha_clk), .chb_clk(chb_clk));

    integer failures = 0;
    task check(input [255:0] name, input ok);
        begin
            if (!ok) begin
                failures = failures + 1;
                $display("FAIL %0s", name);
            end else
                $display("OK   %0s", name);
        end
    endtask

    // FSMC: HCLK 13.889 ns, phase not related to the FPGA clock
    localparam HCLK = 13889;
    integer datast = 8;                         // write / read strobe in HCLK
    task bus_write(input [15:0] v);
        begin
            #(HCLK * 2);                        // address setup
            ce = 1; db_out = v; db_drive = 1;
            #(HCLK);
            nwr = 0;
            #(HCLK * datast);
            nwr = 1;
            #(HCLK);                            // data hold
            db_drive = 0; ce = 0;
            #(HCLK * 2);
        end
    endtask
    task bus_read(output [15:0] v);
        begin
            #(HCLK * 2);
            ce = 1; nrd = 0;
            #(HCLK * datast);
            v = db;
            nrd = 1; ce = 0;
            #(HCLK * 2);
        end
    endtask
    // driver: command, wait, read
    reg [15:0] modes = 0;
    task read32(input [2:0] sel, output [31:0] v);
        reg [15:0] lo, hi;
        begin
            bus_write(modes | sel); #100000; bus_read(lo);
            bus_write(modes | (sel + 1)); #100000; bus_read(hi);
            v = {hi, lo};
        end
    endtask
    task snapshot(input [15:0] extra);
        begin
            bus_write(modes | 16'h0008 | extra);
            #100000;
        end
    endtask

    // reference model: edges seen by sampling with the FPGA clock
    integer ref_c_rise = 0, ref_c_fall = 0, ref_d_rise = 0, ref_d_fall = 0;
    reg pc = 0, pd = 0;
    always @(posedge clk) begin
        if (chc & ~pc) ref_c_rise = ref_c_rise + 1;
        if (~chc & pc) ref_c_fall = ref_c_fall + 1;
        if (chd & ~pd) ref_d_rise = ref_d_rise + 1;
        if (~chd & pd) ref_d_fall = ref_d_fall + 1;
        pc = chc; pd = chd;
    end

    // signal generators
    integer half_c = 0, half_d = 0;            // half period in ps, 0 = stopped
    always begin
        if (half_c > 0) begin #(half_c) chc = ~chc; end else #10000;
    end
    always begin
        if (half_d > 0) begin #(half_d) chd = ~chd; end else #10000;
    end

    reg [15:0] w;
    reg [31:0] c, d, t, c2, t2;
    integer r0, f0, rd0, ok, i;
    initial begin
        #200000;
        bus_read(w);
        check("id 0xC0D1", w == 16'hC0D1);

        // clear all, count 1 MHz on C (rising), 3.3 MHz on D (both edges)
        modes = (0 << 7) | (2 << 9);
        snapshot(16'h0070);
        r0 = ref_c_rise; rd0 = ref_d_rise + ref_d_fall;
        half_c = 500000; half_d = 151515;
        #(1000 * 1000000);                      // 1 ms
        snapshot(0);
        read32(1, c); read32(3, d); read32(5, t);
        $display("     C %0d (ref %0d)  D %0d (ref %0d)  T %0d", c, ref_c_rise - r0, d,
            ref_d_rise + ref_d_fall - rd0, t);
        // the snapshot is taken a few clocks after the model value was read
        check("C 1 MHz rising", c >= 999 && c <= 1001);
        check("D 3.3 MHz both", d >= 6590 && d <= 6610);
        check("T ~ 72000 clocks", t > 71900 && t < 72400);

        // exact against the model while the signal is stopped
        half_c = 0; half_d = 0;
        #1000000;
        snapshot(16'h0070);                     // read and clear
        r0 = ref_c_rise; f0 = ref_c_fall;
        half_c = 33333;                         // 15 MHz
        #(200 * 1000000);
        half_c = 0;
        #1000000;
        snapshot(0);
        read32(1, c);
        $display("     C 15 MHz %0d ref %0d", c, ref_c_rise - r0);
        check("C 15 MHz exact", c == ref_c_rise - r0);

        // falling edges, 27 MHz with uneven duty is still seen
        modes = (1 << 7) | (2 << 9);
        snapshot(16'h0010);
        f0 = ref_c_fall;
        half_c = 18518;
        #(100 * 1000000);
        half_c = 0;
        #1000000;
        snapshot(0);
        read32(1, c);
        $display("     C 27 MHz falling %0d ref %0d", c, ref_c_fall - f0);
        check("C 27 MHz falling exact", c == ref_c_fall - f0);

        // 32 bit wrap: preset the counter just below 2^32
        dut.counter_c.part[0] = 8'hf0; dut.counter_c.part[1] = 8'hff;
        dut.counter_c.part[2] = 8'hff; dut.counter_c.part[3] = 8'hff;
        dut.counter_c.ones = 4'b1110;
        modes = (2 << 7) | (2 << 9);
        half_c = 100000;
        ok = 1;
        snapshot(0); read32(1, c);
        for (i = 0; i < 20; i = i + 1) begin
            #(3000000);
            snapshot(0); read32(1, c2);
            // monotonic modulo 2^32, never jumps by 2^16 or 2^24
            if ((c2 - c) > 32'd200) ok = 0;
            c = c2;
        end
        $display("     after wrap C = %0d", c);
        check("wrap without jumps", ok && c < 32'd1000);

        // snapshot is consistent: T read twice between snapshots equal
        snapshot(0); read32(5, t); read32(5, t2);
        check("snapshot holds", t == t2);

        // status
        half_c = 0; half_d = 0;
        #1000000;
        chc = 1; chd = 0;
        #100000;
        bus_write(modes | 7); #100000; bus_read(w);
        check("status C=1 D=0", w[1:0] == 2'b01);

        // short strobes still work (3 HCLK)
        datast = 3;
        bus_write(0); #100000; bus_read(w);
        check("short strobe", w == 16'hC0D1);

        if (failures == 0) $display("\nall passed"); else $display("\n%0d FAILED", failures);
        $finish;
    end
endmodule

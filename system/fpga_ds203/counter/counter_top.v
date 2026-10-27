// DS203 (HW 2.6x, Lattice iCE65L04 VQ100): 32 bit pulse counters on the
// digital inputs C and D, read over the FSMC bus by the STM32.
//
// The image replaces the oscilloscope FPGA image while the counters are used:
// the application loads it from the disk and loads the original image from
// the flash (0x0802C000) afterwards. Nothing is sampled meanwhile.
//
// Clock: 72 MHz (PA8 = MCO = SYSCLK, set by the application). The inputs are
// sampled with this clock, so the input frequency must stay below 36 MHz
// (high and low level at least one clock each), in practice ~25 MHz.
//
// Bus: the FPGA is at 0x64000000, all accesses are 16 bit.
//
//   write  command word
//      bits 2..0  read select: 0 id 0xC0D1, 1 C[15:0], 2 C[31:16],
//                 3 D[15:0], 4 D[31:16], 5 T[15:0], 6 T[31:16],
//                 7 status (bit 0 level of C, bit 1 level of D)
//      bit 3      snapshot: copies C, D and T to the read registers in the
//                 same clock, so all 32 bit values are consistent
//      bit 4      clear C       (after the snapshot of the same command)
//      bit 5      clear D
//      bit 6      clear T
//      bits 8..7  edges of C: 0 rising, 1 falling, 2 both
//      bits 10..9 edges of D
//   read   the selected 16 bit word
//
// T counts the clock: dC * 72 MHz / dT is the frequency of C measured by the
// FPGA itself, and T not changing means that the clock does not run.
//
// The command is taken at the end of the write strobe (synchronized to the
// clock), the strobe must be at least 3 clocks (42 ns) long and the next read
// must come at least 4 clocks after it. The driver slows down the bus timing
// of the FPGA bank while the image is loaded.
//
// Pin numbers of the board are taken from the logic analyzer project of
// Petteri Aimonen (github.com/PetteriAimonen/dso-quad-logic, fpga branch).

module counter_top (
    input  wire        clk,

    input  wire        fsmc_ce,
    input  wire        fsmc_nwr,
    input  wire        fsmc_nrd,
    inout  wire [15:0] fsmc_db,

    input  wire        chc_din,
    input  wire        chd_din,

    output wire        adc_sleep,
    output wire        cha_clk,
    output wire        chb_clk
);
    localparam [15:0] ID = 16'hC0D1;

    // ADC is not used, keep it as the original logic analyzer design did
    assign adc_sleep = 1'b1;
    assign cha_clk = 1'b0;
    assign chb_clk = 1'b0;

    // ------------------------------------------------------------------
    // inputs: two synchronizer stages and the previous level

    reg [2:0] c_r = 0, d_r = 0;
    always @(posedge clk) begin
        c_r <= {c_r[1:0], chc_din};
        d_r <= {d_r[1:0], chd_din};
    end

    reg [1:0] edge_c = 0, edge_d = 0;
    wire c_rise = c_r[1] & ~c_r[2], c_fall = ~c_r[1] & c_r[2];
    wire d_rise = d_r[1] & ~d_r[2], d_fall = ~d_r[1] & d_r[2];
    wire c_inc = edge_c == 2'd0 ? c_rise : edge_c == 2'd1 ? c_fall : (c_rise | c_fall);
    wire d_inc = edge_d == 2'd0 ? d_rise : edge_d == 2'd1 ? d_fall : (d_rise | d_fall);

    // ------------------------------------------------------------------
    // bus: write strobe synchronized, data captured while it is active

    wire wr_raw = fsmc_ce & ~fsmc_nwr;
    reg [2:0] wr_s = 0;
    reg [15:0] wr_data = 0;
    always @(posedge clk) begin
        wr_s <= {wr_s[1:0], wr_raw};
        if (wr_s[0])
            wr_data <= fsmc_db;
    end
    wire cmd = ~wr_s[1] & wr_s[2];          // end of the write strobe

    // ------------------------------------------------------------------
    // counters

    wire clr_c = cmd & wr_data[4];
    wire clr_d = cmd & wr_data[5];
    wire clr_t = cmd & wr_data[6];
    wire [31:0] cnt_c, cnt_d, cnt_t;
    counter32 counter_c (.clk(clk), .clear(clr_c), .inc(c_inc), .value(cnt_c));
    counter32 counter_d (.clk(clk), .clear(clr_d), .inc(d_inc), .value(cnt_d));
    counter32 counter_t (.clk(clk), .clear(clr_t), .inc(1'b1), .value(cnt_t));

    reg [31:0] snap_c = 0, snap_d = 0, snap_t = 0;
    reg [2:0] sel = 0;
    always @(posedge clk) begin
        if (cmd) begin
            sel <= wr_data[2:0];
            edge_c <= wr_data[8:7];
            edge_d <= wr_data[10:9];
            if (wr_data[3]) begin
                snap_c <= cnt_c;
                snap_d <= cnt_d;
                snap_t <= cnt_t;
            end
        end
    end

    // ------------------------------------------------------------------
    // read: registered word, driven to the bus during the read strobe only

    reg [15:0] rd_data = ID;
    always @(posedge clk) begin
        case (sel)
            3'd0: rd_data <= ID;
            3'd1: rd_data <= snap_c[15:0];
            3'd2: rd_data <= snap_c[31:16];
            3'd3: rd_data <= snap_d[15:0];
            3'd4: rd_data <= snap_d[31:16];
            3'd5: rd_data <= snap_t[15:0];
            3'd6: rd_data <= snap_t[31:16];
            3'd7: rd_data <= {14'd0, d_r[2], c_r[2]};
        endcase
    end

    assign fsmc_db = (fsmc_ce & ~fsmc_nrd) ? rd_data : 16'hzzzz;
endmodule

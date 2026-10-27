// 32 bit synchronous counter made of four 8 bit parts.
//
// A part is incremented in the same clock as the parts below it wrap, its
// carry is the AND of the registered "all ones" flags of the lower parts, so
// the value is exact in every clock and the longest path is one 8 bit adder.
//
// clear has priority over the count, an increment in the same clock is kept
// (the counter is set to 1), so no edge is lost by clearing.

module counter32 (
    input  wire        clk,
    input  wire        clear,
    input  wire        inc,
    output wire [31:0] value
);
    reg [7:0] part [0:3];
    reg [3:0] ones;     // part == 8'hff

    initial begin
        part[0] = 0; part[1] = 0; part[2] = 0; part[3] = 0;
        ones = 0;
    end

    assign value = {part[3], part[2], part[1], part[0]};

    wire [3:0] carry;
    assign carry[0] = inc;
    assign carry[1] = inc & ones[0];
    assign carry[2] = inc & ones[0] & ones[1];
    assign carry[3] = inc & ones[0] & ones[1] & ones[2];

    integer i;
    always @(posedge clk) begin
        if (clear) begin
            part[0] <= inc ? 8'd1 : 8'd0;
            part[1] <= 0;
            part[2] <= 0;
            part[3] <= 0;
            ones <= 0;
        end else begin
            for (i = 0; i < 4; i = i + 1)
                if (carry[i]) begin
                    part[i] <= part[i] + 8'd1;
                    ones[i] <= part[i] == 8'hfe;
                end
        end
    end
endmodule

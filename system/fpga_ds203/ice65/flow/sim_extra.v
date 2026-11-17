// Simulation model for the tri-state buffer cell Yosys leaves in the netlist
module \$_TBUF_ (input A, input E, output Y);
  assign Y = E ? A : 1'bz;
endmodule

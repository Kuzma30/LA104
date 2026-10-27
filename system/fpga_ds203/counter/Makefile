# Simulation and a synthesis check with the open tools.
#
# The bitstream for the iCE65L04 of the DS203 can be built only with Lattice
# iCEcube2 (see readme.md), the open tools (yosys / nextpnr) support iCE40
# only. The iCE40 HX4K result is only an estimate of the size and speed.

SRC = counter_top.v counter32.v

all: sim check

sim: $(SRC) tb_counter.v
	iverilog -g2012 -Wall -o tb_counter tb_counter.v $(SRC)
	vvp tb_counter

check: $(SRC)
	yosys -q -p "read_verilog $(SRC); synth_ice40 -top counter_top -json counter_ice40.json"
	nextpnr-ice40 --hx4k --package tq144 --json counter_ice40.json --freq 72 \
		--pcf-allow-unconstrained 2>&1 | grep -E "ICESTORM_LC:|Max frequency"

clean:
	rm -f tb_counter counter_ice40.json

.PHONY: all sim check clean

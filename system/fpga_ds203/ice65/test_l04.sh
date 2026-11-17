#!/bin/bash
# Tests of the iCE65L04 build (after build_l04.sh), with the testbench of ../tb:
#  1. the sources with the memory of 256 x 16 blocks (RTL)
#  2. the netlist after the synthesis
#  3. the netlist recovered from the bitstream build/V264L04.BIN
#     (iceunpack -6, icebox_vlog): checks place and route and the bitstream
# Needs iverilog, the patched icestorm (ICESTORM = its source folder with
# icepack/ and icebox/) and cells_sim.v of yosys (CS).
set -e
cd "$(dirname "$0")"
: "${ICESTORM:?set ICESTORM to the icestorm source folder}" "${CS:?set CS to share/ice40/cells_sim.v of yosys}"
export CS SIM_EXTRA=$PWD/flow/sim_extra.v
W=build/test
mkdir -p $W

iverilog -g2005 -o $W/stock.vvp ../tb/tb_scope.v ../stock_v25/*.v
vvp -n $W/stock.vvp | grep '^R ' > $W/stock.txt

SRC="../scope/AddrCtrl.v ../scope/Signal.v ../scope/IO_Ctrl.v ../scope/FPGA_V25.v ../scope/EdgeCnt.v ../scope/QuadCnt.v DP_RAM_l04.v RAM4096_L04.v"
python3 mk_tb.py ram ../tb/tb_scope.v $W/tb_ram.v
iverilog -g2005 -DNO_ICE40_DEFAULT_ASSIGNMENTS -s tb_scope -o $W/rtl.vvp $W/tb_ram.v $SRC $CS
vvp -n $W/rtl.vvp | grep '^R ' > $W/rtl.txt
if diff -q $W/stock.txt $W/rtl.txt > /dev/null; then echo "OK   rtl: oscilloscope function equal to the original V2.5 ($(wc -l < $W/rtl.txt) bus reads)"
else echo "FAIL rtl: differs from the original V2.5"; exit 1; fi
iverilog -g2005 -DCOUNTERS -DNO_ICE40_DEFAULT_ASSIGNMENTS -s tb_scope -o $W/rtl.vvp $W/tb_ram.v $SRC $CS
vvp -n $W/rtl.vvp | grep -v '^R ' > $W/rtl_cnt.txt
echo "     rtl counters / quadrature: $(grep -c '^OK' $W/rtl_cnt.txt) OK, $(grep -c '^FA' $W/rtl_cnt.txt) failed"

python3 mk_shim.py syn FIFO_L04 > $W/shim_syn.v
./gate_test.sh $W syn syn build/FIFO_L04_syn.v $W/shim_syn.v

$ICESTORM/icepack/iceunpack -6 build/V264L04.BIN $W/rec.asc
echo "     bits of the bitstream not in the database: $(python3 $ICESTORM/icebox/icebox_explain.py $W/rec.asc | grep -c '?' || true)"
python3 $ICESTORM/icebox/icebox_vlog.py -n FIFO_REC -p Pins_l04.pcf -s $W/rec.asc > $W/rec_raw.v 2> /dev/null
python3 flow/fix_vlog.py $W/rec_raw.v > $W/rec.v
python3 mk_shim.py rec $W/rec_raw.v FIFO_REC > $W/shim_rec.v
./gate_test.sh $W rec rec $W/rec.v $W/shim_rec.v

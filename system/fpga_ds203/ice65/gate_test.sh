#!/bin/bash
# Run the regression of ../tb on a netlist of the iCE65 build.
# usage: gate_test.sh <workdir> <name> <kind: syn|rec> <netlist.v> <shim.v>
# needs: $W/stock.txt (bus reads of the original V2.5), $CS (yosys ice40 cells_sim.v), $SIM_EXTRA (flow/sim_extra.v)
W=$1; n=$2; kind=$3; net=$4; shim=$5
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p $W/inc_$n
python3 $HERE/mk_init.py $kind $net dut.U > $W/inc_$n/gate_init.vh
python3 $HERE/mk_tb.py gate $HERE/../tb/tb_scope.v $W/tb_gate.v
for v in "" "-DCOUNTERS"; do
  iverilog -g2005 $v -DNO_ICE40_DEFAULT_ASSIGNMENTS -I $W/inc_$n -s tb_scope -o $W/$n.vvp $W/tb_gate.v $net $shim $SIM_EXTRA $CS 2>&1 | grep -v "sorry\|warning" | head -5
  if [ -z "$v" ]; then
    vvp -n $W/$n.vvp | grep '^R ' > $W/$n.txt
    if diff -q $W/stock.txt $W/$n.txt > /dev/null; then echo "OK   $n: oscilloscope function equal to the original V2.5 ($(wc -l < $W/$n.txt) bus reads)"
    else echo "FAIL $n: differs from the original V2.5"; diff $W/stock.txt $W/$n.txt | head -6; fi
  else
    vvp -n $W/$n.vvp | grep -v '^R ' > $W/${n}_cnt.txt
    echo "     $n counters / quadrature: $(grep -c '^OK' $W/${n}_cnt.txt) OK, $(grep -c '^FA' $W/${n}_cnt.txt) failed"
    grep '^FA' $W/${n}_cnt.txt | head -8
  fi
done

#!/bin/bash
# Regression of the FPGA with counters against the original e-Design V2.5:
# the same stimulus must give the same bus reads (the original gets the
# input register of 2.67 in front of it), then the counter tests.
set -e
cd "$(dirname "$0")"
OUT=${TMPDIR:-/tmp}/fpga_ds203_tb
mkdir -p $OUT

iverilog -g2005 -DSTOCK_DIN_REG -o $OUT/stock tb_scope.v ../stock_v25/*.v
iverilog -g2005 -o $OUT/scope tb_scope.v ../scope/*.v
iverilog -g2005 -DCOUNTERS -o $OUT/counters tb_scope.v ../scope/*.v

vvp -n $OUT/stock | grep '^R ' > $OUT/stock.txt
vvp -n $OUT/scope | grep '^R ' > $OUT/scope.txt
echo "bus reads: $(wc -l < $OUT/stock.txt)"
if diff -q $OUT/stock.txt $OUT/scope.txt > /dev/null; then
    echo "OK   oscilloscope function equal to the original V2.5"
else
    echo "FAIL oscilloscope function differs from the original V2.5"
    diff $OUT/stock.txt $OUT/scope.txt | head -20
    exit 1
fi

vvp -n $OUT/counters | grep -v '^R '

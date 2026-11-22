#!/bin/bash
# Testbench of the logic analyzer image (../la): run length words against a
# reference taken from the pins, triggers, pretrigger, long runs.
set -e
cd "$(dirname "$0")"
OUT=${TMPDIR:-/tmp}/fpga_ds203_tb
mkdir -p $OUT
iverilog -g2005 -o $OUT/la tb_la.v ../la/FPGA_La.v ../la/AddrCtrlLa.v ../la/IO_CtrlLa.v \
    ../la/LaRle.v ../scope/Signal.v ../scope/DP_RAM.v
vvp -n $OUT/la | grep -E "^(OK|FAIL)|passed|FAILED"

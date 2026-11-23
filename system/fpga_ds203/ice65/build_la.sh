#!/bin/bash
# Logic analyzer image of the DS203 (iCE65L04-VQ100) with the open tools, as
# build_l04.sh: the oscilloscope design with the run length capture of ../la
# instead of the counters. Output: build/LAFPGA.BIN (68088 bytes), loaded by
# the application 66_logic into the FPGA while it runs (never flashed).
#   SEED=n  placement seed          YOSYS / NEXTPNR / ICEPACK  tool names
set -e
cd "$(dirname "$0")"
OUT=build
mkdir -p $OUT
SEED=${SEED:-3}
: "${YOSYS:=yosys}" "${NEXTPNR:=nextpnr-ice40}" "${ICEPACK:=icepack}"
SRC="FIFO_L04.v ../la/AddrCtrlLa.v ../scope/Signal.v ../la/IO_CtrlLa.v ../la/FPGA_La.v \
     ../la/LaRle.v DP_RAM_l04.v RAM4096_L04.v"

sed -e "s|@SYNTH_OPTS@||g" -e "s|@TOP@|FIFO_L04|g" -e "s|@FLOW@|flow|g" -e "s|@OUT@|$OUT/LA_L04|g" \
    flow/synth_l04.ys.in > $OUT/synth_la.ys
READ=""; for f in $SRC; do READ="$READ read_verilog -DICE65_L04 $f;"; done
$YOSYS -q -l $OUT/yosys_la.log -p "$READ script $OUT/synth_la.ys"
# the timing data of the l04 device is an estimate, the report is not a sign-off
$NEXTPNR --l04 --package vq100 --json $OUT/LA_L04.json --pcf Pins_l04.pcf \
    --asc $OUT/LA_L04.asc --freq 72 --timing-allow-fail --seed $SEED -l $OUT/nextpnr_la.log --quiet
grep -E "(ICESTORM_LC|ICESTORM_RAM|SB_GB|SB_IO): " $OUT/nextpnr_la.log | grep -v iteration | sort -u
grep -E "Max frequency for clock" $OUT/nextpnr_la.log | tail -6
$ICEPACK $OUT/LA_L04.asc $OUT/LA_L04.pack.bin
# icepack writes a 4 byte comment header, the vendor image starts with the sync word
tail -c +5 $OUT/LA_L04.pack.bin > $OUT/LAFPGA.BIN
rm $OUT/LA_L04.pack.bin
ls -l $OUT/LAFPGA.BIN

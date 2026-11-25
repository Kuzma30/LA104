#!/bin/bash
# iCE65L04-VQ100 of the DS203 (HW 2.6x / 2.7x) with the open tools: yosys,
# nextpnr-ice40 and icestorm with the iCE65 L04 patches (device "l04", see
# the readme). The sources are ../scope unchanged, plus DP_RAM_l04.v
# (memory from 256 x 16 blocks) and FIFO_L04.v (clock pad) of this folder.
# Output: build/V268L04.BIN (68088 bytes, flashed as V261FPGA.BIN).
#   SEED=n  placement seed          YOSYS / NEXTPNR / ICEPACK  tool names
set -e
cd "$(dirname "$0")"
OUT=build
mkdir -p $OUT
SEED=${SEED:-3}
: "${YOSYS:=yosys}" "${NEXTPNR:=nextpnr-ice40}" "${ICEPACK:=icepack}"
SRC="FIFO_L04.v ../scope/AddrCtrl.v ../scope/Signal.v ../scope/IO_Ctrl.v ../scope/FPGA_V25.v \
     ../scope/EdgeCnt.v ../scope/QuadCnt.v ../scope/PeakDet.v DP_RAM_l04.v RAM4096_L04.v"

sed -e "s|@SYNTH_OPTS@||g" -e "s|@TOP@|FIFO_L04|g" -e "s|@FLOW@|flow|g" -e "s|@OUT@|$OUT/FIFO_L04|g" \
    flow/synth_l04.ys.in > $OUT/synth.ys
READ=""; for f in $SRC; do READ="$READ read_verilog -DICE65_L04 $f;"; done
$YOSYS -q -l $OUT/yosys.log -p "$READ script $OUT/synth.ys"
# the timing data of the l04 device is an estimate, the report is not a sign-off
$NEXTPNR --l04 --package vq100 --json $OUT/FIFO_L04.json --pcf Pins_l04.pcf \
    --asc $OUT/FIFO_L04.asc --freq 72 --timing-allow-fail --seed $SEED -l $OUT/nextpnr.log --quiet
grep -E "(ICESTORM_LC|ICESTORM_RAM|SB_GB|SB_IO): " $OUT/nextpnr.log | grep -v iteration | sort -u
grep -E "Max frequency for clock" $OUT/nextpnr.log | tail -6
$ICEPACK $OUT/FIFO_L04.asc $OUT/FIFO_L04.pack.bin
# icepack writes a 4 byte comment header, the vendor image starts with the sync word
tail -c +5 $OUT/FIFO_L04.pack.bin > $OUT/V268L04.BIN
rm $OUT/FIFO_L04.pack.bin
ls -l $OUT/V268L04.BIN

# Logic analyzer FPGA image for the DS203 HW 2.6x / 2.7x (iCE65L04)

`LAFPGA.BIN` (68088 bytes, sha256
`8b535e555e000e47ec70fb20d50f9520d081b0cca8e0c4bcae8fb8389ade40c1`) is the
FPGA image of the logic analyzer `66_logic`. Copy it to the root of the disk;
the application loads it into the FPGA when it starts and loads the
oscilloscope image back from the flash when it exits. It is **not** flashed
(do not write it with DFU or fpgaload as the boot image).

Built with the open tools from `system/fpga_ds203/la` (`ice65/build_la.sh`):
the oscilloscope FPGA 2.67 (same pins, bus, registers and trigger) with run
length capture instead of the counters. Status word version 901, Select
0x52 = 0x1A01.

Capture: 4095 words of `{B, A, D, C, length - 1 (14 bits)}`, a word for every
change of the four channels (C, D digital inputs, A, B analog against a
threshold with hysteresis), at the sample rate of the time base up to
72 MS/s; quiet lines take no space. Testbench: `system/fpga_ds203/tb/run_la.sh`.

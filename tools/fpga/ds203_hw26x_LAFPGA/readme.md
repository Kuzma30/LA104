# Logic analyzer FPGA image for the DS203 HW 2.6x / 2.7x (iCE65L04)

`LAFPGA.BIN` (68088 bytes, sha256
`dd32ba1727075a144921ede12ff10e0ae513cb5c5baf23a6073544b8c02f6d77`) is the
FPGA image of the logic analyzer `66_logic`. Copy it to the root of the disk;
the application loads it into the FPGA when it starts and loads the
oscilloscope image back from the flash when it exits. It is **not** flashed
(do not write it with DFU or fpgaload as the boot image).

Built with the open tools from `system/fpga_ds203/la` (`ice65/build_la.sh`):
the oscilloscope FPGA 2.67 (same pins, bus, registers and trigger) with run
length capture instead of the counters. Status word version 902, Select
0x52 = 0x1A01 (seed 3, nextpnr estimate of the sample clock 48 MHz, as
9.01 which runs at 72 MHz on the device).

Capture: 4095 words of `{B, A, D, C, length - 1 (14 bits)}`, a word for every
change of the four channels (C, D digital inputs, A, B analog against a
threshold with hysteresis), at the sample rate of the time base up to
72 MS/s; quiet lines take no space. Register 0x16 (9.02): the channels
that are on; a channel that is off is 0 in the words and its changes take
no space, so the 4095 words hold the changes of the channels that are on. Testbench: `system/fpga_ds203/tb/run_la.sh`.

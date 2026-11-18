# DS203 FPGA 2.66 (HW 2.6x / 2.7x, iCE65L04)

Oscilloscope of the original FPGA 2.5 / 2.61 with 32 bit edge counters of
CH_C / CH_D (by the pins) and of CH_A / CH_B (comparators in the sampling
clock, new in 2.66), and two quadrature decoders, built with the open tools
(yosys, nextpnr and icestorm with the iCE65 L04 patches). Sources and
build: `system/fpga_ds203`. Tester: `system/apps_ds213/64_fpgatest`.

Flashing in the DFU mode (the DFU disk of the device): copy `CFG_FPGA.ADR`
(address 0x0802C000), then `V266FPGA.BIN`.
SYS shows the FPGA version 2.66. The original image is flashed back the
same way from `../ds203_8mb_hwV272-V261FPGA`, the previous version from
`../ds203_hw26x_V264FPGA`.

`V266FPGA.BIN`: 68088 bytes, sha256
2145eeeae39a8da29317a12961b67dbf91136acfee8ec4eca249b58d1032abb7

Superseded by `../ds203_hw26x_V267FPGA`: in 2.66 the trigger fails at the
fast time bases (sampling 36 / 72 MHz).

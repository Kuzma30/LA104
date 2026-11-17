# DS203 FPGA 2.64 (HW 2.6x / 2.7x, iCE65L04)

Oscilloscope of the original FPGA 2.5 / 2.61 with 32 bit edge counters of
CH_C / CH_D and two quadrature decoders, built with the open tools
(yosys, nextpnr and icestorm with the iCE65 L04 patches). Sources and
build: `system/fpga_ds203`. Tester: `system/apps_ds213/64_fpgatest`.

Flashing in the DFU mode (the DFU disk of the device): copy `CFG_FPGA.ADR`
(address 0x0802C000), then `V264FPGA.BIN`.
SYS shows the FPGA version 2.64. The original image is flashed back the
same way from `../ds203_8mb_hwV272-V261FPGA`.

`V264FPGA.BIN`: 68088 bytes, sha256
6c3af127cf138bcd4fef94ed0b8e6ac217bcdfff6a3c5182c3a62eb6a46fcc76

Superseded by `../ds203_hw26x_V266FPGA` (2.66: edge counters of CH_A /
CH_B as well, same registers otherwise). This image was built from commit
b426d5a of `system/fpga_ds203`.

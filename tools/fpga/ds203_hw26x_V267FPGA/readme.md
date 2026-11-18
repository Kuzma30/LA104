# DS203 FPGA 2.67 (HW 2.6x / 2.7x, iCE65L04)

Oscilloscope of the original FPGA 2.5 / 2.61 with 32 bit edge counters of
CH_C / CH_D (by the pins) and of CH_A / CH_B (comparators in the sampling
clock), and two quadrature decoders, built with the open tools (yosys,
nextpnr and icestorm with the iCE65 L04 patches). Sources and build:
`system/fpga_ds203`. Tester: `system/apps_ds213/64_fpgatest`.

2.67 fixes the trigger of 2.66 at fast time bases (36 / 72 MHz sampling):
the ADC and digital inputs are now registered next to the pins, the
comparators of the trigger and of the decoder are pipelined. The data and
the trigger point are the same as with the original FPGA.

Flashing in the DFU mode (the DFU disk of the device): copy `CFG_FPGA.ADR`
(address 0x0802C000), then `V267FPGA.BIN`.
SYS shows the FPGA version 2.67. The original image is flashed back the
same way from `../ds203_8mb_hwV272-V261FPGA`.

`V267FPGA.BIN`: 68088 bytes, sha256
4dc4d2ee4dee2fe2a0195859178b524eb29718bb0495df41c149478d0ac73e19

# DS203 FPGA 2.68 (HW 2.6x / 2.7x, iCE65L04)

FPGA 2.67 (oscilloscope of the original 2.5 / 2.61, edge counters,
quadrature decoders) with the useful parts of the Wildcat FPGA W1.1 (written
for HW 2.81, github.com/MotoMaxis/DS203-DSOQuad), adapted to this design:

- peak detection: the ADC runs K times faster, every K samples give the
  minimum and the maximum of CH_A / CH_B (two FIFO words). Registers
  0x0A / 0x0B = K - 1 (0 = off).
- pulse triggers with 32 bit counters: time threshold 0x02 / 0x03 and
  0x0C / 0x0D (bits 16..31). The counters count down (no 32 bit compare).
- writing the trigger mode (0x00) clears both, SYS and older programs work
  as before. ID 0xC0D8 at Select 0x1B.
- removed (used by no program of the LA104 OS): the measurement registers
  (Select 0x00..0x0E) and the trigger delay (0x0A..0x0D of the original).

The free-run mode and the trigger buffer of W1.1 are not taken: the
oscilloscope uses the unconditional trigger without a trigger condition, and
2.67 already registers the inputs before the trigger logic.

Sources and build: `system/fpga_ds203` (`scope/PeakDet.v`). Tester:
`system/apps_ds213/64_fpgatest`. Peak detection in the oscilloscope:
Input, Buf:Peak.

Flashing in the DFU mode (the DFU disk of the device): copy `CFG_FPGA.ADR`
(address 0x0802C000), then `V268FPGA.BIN`.
SYS shows the FPGA version 2.68. The original image is flashed back the
same way from `../ds203_8mb_hwV272-V261FPGA`.

`V268FPGA.BIN`: 68088 bytes, sha256
bc5d0fe8d7a0fd3086ac9278e82e5e255a92d74f253cc740e31c6bb0c19f483c

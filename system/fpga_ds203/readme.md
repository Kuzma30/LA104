# DS203 FPGA (HW 2.6x, iCE65L04) with edge counters and a quadrature decoder (FPGA 2.67)

- `stock_v25/` original FPGA source of e-Design (FPGA CFG Ver 2.5, MIT
  license, from [matthiasbock/dso203](https://github.com/matthiasbock/dso203),
  commit 9114997), unchanged, used as the reference of the tests
- `scope/` the same design with the counters, the oscilloscope part is
  not changed (see the regression test)
- `tb/` testbench (Icarus Verilog): `./run.sh`
- `ice65/` build for the iCE65L04-VQ100 with the open tools and the
  experimental iCE65 L04 patches (see below)

## What was added (`scope/EdgeCnt.v`, `scope/IO_Ctrl.v`)

Four 32 bit counters: rising and falling edges of CH_C and CH_D (both edges
= rising + falling). They are clocked by the input pins, so they count every
edge independently of the timebase (the sampling clock MCI is 60 Hz at 1 s/div
and 72 MHz at 0.1 us/div) while the oscilloscope samples as before.

Access from the MCU, with the same signals as SYS uses:

| what | how |
|------|-----|
| write register | `Set_Param(addr, value)`: H_L = 1, serial byte addr (PA6 SCK, PA7 SDA), H_L = 0, serial byte value |
| clear | register `0x0E` = 1 (held), then 0: all four counters are 0 |
| select | register `0x05` (Select): `0x10`/`0x11` CH_C rising low/high word, `0x12`/`0x13` CH_C falling, `0x14`/`0x15` CH_D rising, `0x16`/`0x17` CH_D falling, `0x18` ID `0xC0D2` |
| read | C_D (PC4) = 1, H_L (PC5) = 0, read 16 bits from 0x64000000 |

Reading the low word latches the whole 32 bit value, the high word comes
from the latch. The values are in Gray code (converted by the MCU), so a
value read while the input toggles is always the old or the new one.

The status word (H_L = 0, C_D = 0) has the version 267 in bits 15..6,
SYS `__Get(FPGAVER)` shows "2.67". The other bits are as in the original.
(2.64: without the counters of CH_A / CH_B; 2.65 was a variant with a
smaller buffer, not part of this line; 2.66: the trigger fails at 36 / 72
MHz sampling, see below.)

## Input registers (FPGA 2.67)

The ADC data and CH_C / CH_D go through two registers in MCI before
anything uses them (`FPGA_V25.v`): the first one (`Din_Raw`) is placed next
to its pad on the iCE65L04 (`BEL` attributes, `ICE65_L04`), so every bit
has the same short path from the pin; the comparisons with the trigger
level (`Signal.v`) and with the thresholds of the decoder (`QuadSync`) are
made from it and registered; the second register (`Din_Bus`) feeds the
FIFO, the trigger logic and the decoder. All of them see the samples two
MCI later together, so the data in the FIFO and the trigger point are the
same as with the original (the testbench compares with the original with
two registers in front of it).

Why: nextpnr does not time the paths from input pins (no input delays).
In 2.66 the path from the ADC pins through the comparators of the trigger
to its flags was about 30 ns (estimate) and placed anywhere; on the
device the trigger failed at 36 and 72 MHz sampling (tester groups T-A /
T-B: the original V2.61 passes all, 2.66 failed level and pulse modes).

## Edge counters of CH_A / CH_B (FPGA 2.66, `EdgeSync` in `scope/EdgeCnt.v`)

CH_A and CH_B come through the ADC, their edges exist only in the sampling
clock MCI. The level of a channel is the comparator with hysteresis of the
quadrature decoder (registers `0x10`..`0x13`, see below; the thresholds are
shared), an edge is a change of this level between two samples. A signal is
counted exactly while every level lasts at least one sample, that is up to
half the sampling rate of the timebase (MCI 36 MHz at 0.5 us/div, 6 MHz at
5 us/div, 60 Hz at 1 s/div), and while the input stage passes it.

One counter of both edges per channel (the logic cells are short). The
edges of a comparator alternate, so the rising and falling edges follow from
the count and the level before the first edge: rising = (edges + !level) / 2,
falling = edges - rising.

| what | how |
|------|-----|
| clear | register `0x0E` bit 0 (with CH_C / CH_D), asynchronous: works without MCI |
| count | Select `0x40` / `0x41` CH_A edges low / high word, `0x42` / `0x43` CH_B (Gray code, the low word latches the whole value, as `0x10`..`0x17`) |
| level | Select `0x44`: bit 0 / bit 1 level of CH_A / CH_B before the first edge (changes only while the counter is 0) |
| ID | Select `0x1A` returns `0xC0D6` |

To fit (3431 of 3520 logic cells), the selected inputs of the MCI decoder
got one more register: A, B and Z one sample later together, the decoding is
the same.

## Quadrature decoder (FPGA 2.64, `scope/QuadCnt.v`)

Incremental encoder: A, B and the index Z on any of the four inputs. CH_A
and CH_B are compared with a threshold with hysteresis (one per channel),
CH_C and CH_D are digital.

- `QuadSync`: the decoder in the sampling clock MCI, for any inputs. The
  steps must be slower than the sample rate of the timebase, two changes in
  one sample are counted as errors (the direction is not known then).
- `QuadCnt`: A = CH_C and B = CH_D are counted by the pins as well, without
  this limit (independently of the timebase): four up/down counters, one for
  every edge of A and B, the direction from the level of the other input;
  position = sum of the four (by the MCU). A bouncing edge counts +1 / -1.
- x4, forward when A leads B. Counters 24 bits (the MCU reads them every
  100 ms and extends them), Gray code, no clear (the MCU keeps the start).
- Z: sampled in MCI, the pulse must be longer than one sample period. At its
  active edge both positions are captured and a 16 bit Z counter is
  incremented. The total position is never cleared by Z; position in the
  revolution = position - captured position (MCU). The analog inputs come
  later than the digital ones (ADC latency), with analog A / B and digital Z
  a step at the same moment as Z may be captured before or after it.

| what | how |
|------|-----|
| control | register `0x0F`: bit 1..0 A, 3..2 B, 5..4 Z (0 CH_A, 1 CH_B, 2 CH_C, 3 CH_D), bit 6 Z on, bit 7 Z active low; power up `0x0E` (A = C, B = D, Z off) |
| thresholds | `0x10` / `0x11` CH_A threshold / hysteresis, `0x12` / `0x13` CH_B (raw sample values, default 128 / 8) |
| pins: position | Select `0x20` latches the four counters and returns word 0, `0x21`..`0x27` the other words (AR lo/hi, AF, BR, BF) |
| pins: position at Z | Select `0x28`: the same for the captured set (latches the Z counter too), `0x29`..`0x2F`, `0x30` Z counter |
| MCI decoder | Select `0x38` latches: `0x38` / `0x39` position, `0x3A` errors (8 bit), `0x3B` / `0x3C` position at Z, `0x3D` Z counter |
| ID | Select `0x19` returns `0xC0D4` |

Version 267 in the status word ("2.67").

## Tests (`tb/run.sh`)

1. The same stimulus (Set_Param of the triggers, ClrW, reading the FIFO as
   BIOS::ADC of the LA104 OS does, measurement registers, slow clock,
   interlace mode) on the original V2.5 and on the version with counters:
   all 273 bus reads are equal (version bits masked).
2. Counters: ID, version, counts equal to the edges of the stimulus, clear,
   reading while the inputs toggle (20 MHz / 7 MHz) with a slow sampling
   clock, wrap of 32 bits.
3. Quadrature decoders (pins and MCI, Z, hysteresis, errors).
4. Counters of CH_A / CH_B: ID, square waves at half the sampling rate
   (exact), an odd number of edges (rising / falling from the level),
   noise within the hysteresis (no edges), reading while counting, clear.

Size estimate with yosys / nextpnr for iCE40 (the unpatched open tools do
not support iCE65): original 1120 logic cells, with counters 1625, with the quadrature
decoder 2902 (iCE65L04: 3520), 18 RAM blocks (20), 8 global buffers (8),
all clocks above 100 MHz. If iCEcube2 runs out of global buffers, the clock
of SCK (the slowest, serial register writes) can be left on local routing.

## Building the bitstream

The vendor tool for iCE65 bitstreams is **Lattice iCEcube2** (the source
states that the 2011-12 version was used; newer versions with iCE65 support
should work). An experimental build with the open tools is in `ice65/`.

1. New project: family iCE65, device iCE65L04, package VQ100.
2. Design files: all `scope/*.v`, top module `FIFO`, constraints `scope/Time.sdc`.
3. Synthesis, then P&R with `scope/Pins.pcf`, Generate Bitmap.
4. `sbt/outputs/bitmap/FIFO_bitmap.bin` must have 68088 bytes (as the
   original `V261FPGA.BIN`).

Flashing: the FPGA image is in the STM32 flash at 0x0802C000 and is loaded by
SYS at power up. It is flashed in the DFU mode like the original package
(`CFG_FPGA.ADR` with `0x0802C000`, then `V261FPGA.BIN`): copy `CFG_FPGA.ADR`,
then the new image renamed for example to `V264FPGA.BIN`. The original
`V261FPGA.BIN` can be flashed back the same way
(`tools/fpga/ds203_8mb_hwV272-V261FPGA`).

The image built with the open tools (`ice65/`) is in
`tools/fpga/ds203_hw26x_V267FPGA` (`CFG_FPGA.ADR` + `V267FPGA.BIN`); the
previous versions in `tools/fpga/ds203_hw26x_V264FPGA` and `..._V266FPGA`.
The functions are tested on the device with `apps_ds213/64_fpgatest`.

Note: the published source is version 2.5, the image of HW 2.60 is 2.61.
The changes of 2.61 are not known, the interface used by SYS 1.5x and by
the LA104 OS is the same.

## iCE65L04-VQ100 with the open tools (`ice65/`)

Experimental: yosys, nextpnr-ice40 and icestorm with the iCE65 L04 patches
of `ice65/toolchain/` (icestorm 00da9c3, nextpnr 5e2718b; device `l04`,
`nextpnr-ice40 --l04 --package vq100`, `iceunpack -6`). The device data was
derived from the vendor-built V2.61 image and the board schematic, without
iCEcube.

On the device (DS203 HW 2.7, SYS 1.52, LA104 OS): the image is loaded, SYS
shows the FPGA version 2.64, the oscilloscope shows the signal of Wave Out
on CH2, the counters of CH_C are exact up to 6 MHz
(`apps_ds213/64_fpgatest`). 2.66 on the device: the trigger fails at
36 / 72 MHz sampling (fixed in 2.67, not yet tested on the device).

The sources of `scope/` are used unchanged (FIFO of 4096 samples). Two
files replace or wrap them:
- `DP_RAM_l04.v` / `RAM4096_L04.v`: the block RAM of the iCE65 is 256 x 16
  only (bit write mask) and the open flow does not build deeper memories
  from it. The 4096 x 18 memory is made of 18 blocks used as 4096 x 1, the
  structure of the vendor-built image.
- `FIFO_L04.v`: top level, MCI enters its global network from the pad of
  pin 15 (`SB_GB_IO`).

`./build_l04.sh` writes `build/V267L04.BIN` (seed 4, the image in
`tools/fpga/ds203_hw26x_V267FPGA`; the best of seeds 1..10 for MCI).
Result: 3450 of 3520 logic cells (2.64: 3122), 18 of 20 RAM blocks, 45 pins.

`./test_l04.sh` runs the testbench of `tb/` on three levels: the sources
with the new memory, the netlist after the synthesis, and the netlist
recovered from the bitstream itself (`iceunpack -6`, `icebox_vlog`). On all
three the oscilloscope function equals the original V2.5 (273 bus reads) and
the counter and quadrature tests pass. The wrap of 32 bits is checked on the
sources only (it forces the counter). The netlists get the same stimulus
timing as the sources: the test "Z on CH_C latched" depends on the phase of
the steps to MCI (a step at the moment of Z is captured before or after it).

Limits:
- **Timing is not verified.** There is no timing data for the iCE65; the
  `l04` device uses an estimate (iCE40LP8K x 1.3). With it nextpnr reports
  for 2.67 48 MHz for the sampling clock (72 MHz needed; 43..48 MHz with
  other seeds), 50 MHz for nRD, 58 MHz for the pin counters; the paths
  from the input pins are short now (2.66: 52 MHz, but 30 ns from the ADC
  pins, not counted by nextpnr). Paths from input pins are not timed by
  nextpnr at all: keep them register-to-pin short as in 2.67. The unchanged
  original V2.5, which runs at 72 MHz on this chip when built by the vendor
  tool, gets 51 MHz from the same flow and estimate: the estimate is
  pessimistic, and this design is not slower than the original by it.
- **Global buffers**: only the locations confirmed in the vendor image are
  used: the pad of MCI and three more, taken by CH_C, CH_D and nRD. SDA and
  SCK and the resets are on local routing (the vendor image has four
  global networks as well).
- The pads of the pins not used by the design are not known, the pin file
  is `Pins_l04.pcf` (the pins of `scope/Pins.pcf`).
- The check through the recovered netlist uses the same device database
  as the tools, it does not find errors of the database itself.

# DS203 FPGA (HW 2.6x, iCE65L04) with counters and quadrature decoder of CH_C / CH_D

- `stock_v25/` original FPGA source of e-Design (FPGA CFG Ver 2.5, MIT
  license, from [matthiasbock/dso203](https://github.com/matthiasbock/dso203),
  commit 9114997), unchanged, used as the reference of the tests
- `scope/` the same design with the counters, the oscilloscope part is
  not changed (see the regression test)
- `tb/` testbench (Icarus Verilog): `./run.sh`

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

The status word (H_L = 0, C_D = 0) has the version 263 in bits 15..6,
SYS `__Get(FPGAVER)` shows "2.63". The other bits are as in the original.

## Quadrature decoder (FPGA 2.63, `scope/QuadCnt.v`)

Incremental encoder: A = CH_C, B = CH_D, index Z = CH_A or CH_B (the DS203
has only two digital inputs, Z is compared with a threshold).

- four 32 bit up/down counters, one for every edge of A and B (x4), the
  direction from the level of the other input, clocked by the pins like the
  edge counters; position = sum of the four (modulo 2^32, by the MCU);
  forward when A leads B. A bouncing edge counts +1 / -1 and cancels out.
- Z: the sample of CH_A / CH_B (as written to the FIFO) compared with a
  threshold with hysteresis in the sampling clock MCI, so the Z pulse must
  be longer than one sample period. At the active edge of Z the four
  counters are captured and a 16 bit Z counter is incremented. The total
  position is never cleared by Z; position in the revolution = position -
  captured position (MCU).
- Gray code everywhere, the counters have no clear (the MCU keeps the start).

| what | how |
|------|-----|
| Z control | register `0x0F`: bit 1..0 source (0 off, 1 CH_A, 2 CH_B), bit 2 active low |
| Z threshold / hysteresis | registers `0x10` / `0x11` (default 128 / 8) |
| position | Select `0x20` latches the four counters and returns word 0, `0x21`..`0x27` the other words (AR lo/hi, AF, BR, BF) |
| position at the last Z | Select `0x28`: the same for the captured set (latches the Z counter too), `0x29`..`0x2F`, `0x30` Z counter |
| ID | Select `0x19` returns `0xC0D3` |

Version 263 in the status word ("2.63").

## Tests (`tb/run.sh`)

1. The same stimulus (Set_Param of the triggers, ClrW, reading the FIFO as
   BIOS::ADC of the LA104 OS does, measurement registers, slow clock,
   interlace mode) on the original V2.5 and on the version with counters:
   all 273 bus reads are equal (version bits masked).
2. Counters: ID, version, counts equal to the edges of the stimulus, clear,
   reading while the inputs toggle (20 MHz / 7 MHz) with a slow sampling
   clock, wrap of 32 bits.

Size estimate with yosys / nextpnr for iCE40 (the open tools do not support
iCE65): original 1120 logic cells, with counters 1625, with the quadrature
decoder 2781 (iCE65L04: 3520), 18 RAM blocks (20), 8 global buffers (8),
all clocks above 100 MHz. If iCEcube2 runs out of global buffers, the clock
of SCK (the slowest, serial register writes) can be left on local routing.

## Building the bitstream

Only **Lattice iCEcube2** builds iCE65 bitstreams (the source states that the
2011-12 version was used; newer versions with iCE65 support should work):

1. New project: family iCE65, device iCE65L04, package VQ100.
2. Design files: all `scope/*.v`, top module `FIFO`, constraints `scope/Time.sdc`.
3. Synthesis, then P&R with `scope/Pins.pcf`, Generate Bitmap.
4. `sbt/outputs/bitmap/FIFO_bitmap.bin` must have 68088 bytes (as the
   original `V261FPGA.BIN`).

Flashing: the FPGA image is in the STM32 flash at 0x0802C000 and is loaded by
SYS at power up. It is flashed in the DFU mode like the original package
(`CFG_FPGA.ADR` with `0x0802C000`, then `V261FPGA.BIN`): copy `CFG_FPGA.ADR`,
then the new image renamed for example to `V263FPGA.BIN`. The original
`V261FPGA.BIN` can be flashed back the same way.

Note: the published source is version 2.5, the image of HW 2.60 is 2.61.
The changes of 2.61 are not known, the interface used by SYS 1.5x and by
the LA104 OS is the same.

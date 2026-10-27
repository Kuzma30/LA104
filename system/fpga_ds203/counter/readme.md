# DS203: 32 bit counters on the digital inputs C and D

FPGA image for the DS203 HW 2.6x (Lattice iCE65L04, VQ100) with three
32 bit counters:

| counter | counts |
|---------|--------|
| C       | edges of input C (rising, falling or both) |
| D       | edges of input D |
| T       | FPGA clock (72 MHz), reference for the frequency |

The STM32 reads and clears them over the FSMC bus (0x64000000). The register
map is described in [counter_top.v](counter_top.v).

The image does not sample anything for the oscilloscope. The application
`58_gabmini` (Decode > Counter > Source: FPGA C/D) loads it from the disk
when the counter is switched on and loads the original image from the flash
(0x0802C000) when it is switched off or the application exits.

## Files

- `counter_top.v`, `counter32.v` - the design
- `counter.pcf` - pins of the board (taken from
  [dso-quad-logic](https://github.com/PetteriAimonen/dso-quad-logic/tree/fpga_support/fpga)
  by Petteri Aimonen)
- `counter.sdc` - 72 MHz clock constraint
- `tb_counter.v` - testbench (Icarus Verilog), `make sim`
- `make check` - synthesis for iCE40 HX4K with yosys / nextpnr as an estimate
  (355 logic cells, 140 MHz on iCE40; iCE65L04 has 3520 cells)

## Building the bitstream

The open tools do not support iCE65, the bitstream is built with
**Lattice iCEcube2** (free license from latticesemi.com, Windows or Linux):

1. New project, device family **iCE65**, device **iCE65L04**, package
   **VQ100**, (the DS203 has the T power grade, if it is offered).
2. Add `counter_top.v` and `counter32.v` as design files, top module
   `counter_top`, `counter.sdc` as the constraint file for synthesis.
3. Run Synthesis (Synplify Pro or LSE).
4. Add `counter.pcf` as the physical constraint file for P&R.
5. Run P&R and Generate Bitmap. Check the timing report (72 MHz).
6. Copy `<project>/sbt/outputs/bitmap/counter_top_bitmap.bin` to the root of
   the DS203 disk as **`CNT_FPGA.BIN`** (about 68 KB, the same size as the
   original image `V261FPGA.BIN`).

The original FPGA image in the flash is never changed. When the custom image
does not configure (DONE stays low), the application reloads the original
one, a reboot of the device does the same.

## Hardware notes

- Clock: PA8 of the STM32. SYS drives it with MCO (72 or 36 MHz) or TIM1 by
  the timebase. The application switches MCO to SYSCLK (72 MHz) while the
  image is loaded.
- Configuration (the same as QuadPawn `fpga.c`): SS PB10, SCK PB11, SI PB12,
  CRESET PB14, CDONE PB15, bitbanged SPI.
- Inputs are sampled at 72 MHz: up to ~25 MHz with a 50 % duty cycle,
  both levels must last at least 14 ns.

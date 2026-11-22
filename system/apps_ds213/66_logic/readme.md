# 66_logic: logic analyzer for the DS203 (LA104 OS)

Four channels with run length capture in the FPGA, after the logic analyzer
of Petteri Aimonen (essentialscrap.com/dsoquad/logic.html), which sampled in
software at 500 kHz. Here the FPGA writes a word only when a channel
changes, at the sample rate of the time base up to **72 MS/s**:

- 4095 changes per capture; a quiet line takes no space (one word covers
  up to 16384 samples, longer runs take more words)
- channels **A, B**: the analog inputs against a threshold (hysteresis about
  0.1 V), **C, D**: the digital inputs
- trigger on a rising or falling edge of any channel, or none; pretrigger
  10 .. 90 % of the words; the trigger is accepted when the pretrigger
  words are written or after 0.2 s (quiet lines)
- single or repeated capture, stop at any time (the words up to then)
- zoom down to 8 columns per sample, scroll, cursors X1 / X2 with dX and
  1/dX, jump to the next edge
- **VCD** files (GTKWave, PulseView: `LA000.VCD` ..) and screenshots
  (`LA000.BMP` ..)

## Files

- `66logic3.elf` (build: `./build_ds203.sh`)
- `LAFPGA.BIN`, the FPGA image (`tools/fpga/ds203_hw26x_LAFPGA`, sources in
  `system/fpga_ds203/la`), in the root of the disk. The application loads it
  into the FPGA at the start and loads the oscilloscope image back from the
  flash at the exit (F2). It is never written to the flash. If the device is
  switched off while the logic analyzer runs, the oscilloscope image is
  there again at the next power up anyway.
- `MINICAL.CFG` of the oscilloscope / MiniCal (optional): the calibration
  used for the thresholds of A and B.

Needs the STM32F103VE (512 KB flash, 64 KB RAM) of the DS203 HW 2.6x / 2.7x:
code at 0x08068000, the capture (16 KB) in the RAM at 0x2000C000.

## Screen and keys

```
Logic 24.0MS/s  C rise  1834 ch 1.20ms           [STOP]
   -500us      0s        500us      1.00ms              time from the trigger
A  ___|^^^^|______|^^^^^^|___
B  ...
C  ...                                                 T (red), X1, X2 dashed
D  ...
X1 0s  X2 41.7us  dX 41.7us 24.0kHz
24.0MS/s C rise Pre 25% A 1.40V B 1.40V Single         settings
View Zoom X1 X2 BMP                       status        view
```

| Key | |
|---|---|
| Up / Down | menu item |
| Left / Right | change the item; View: scroll, Zoom: zoom out / in, X1 / X2: move (held: faster) |
| F1 | run / stop |
| F2 | exit |
| F3 | X1 / X2: next edge of any channel; otherwise the whole capture |
| F4 | save VCD; on the item BMP a screenshot |

Settings: sample rate 72 MS/s .. 30 S/s (right: faster), trigger, pretrigger,
thresholds of A and B (-0.5 .. 30 V; 1, 2 or 5 V/div is chosen for the
threshold), single / repeat.

## Notes

- C and D are the logic inputs of the DS203 (about 1.6 V threshold); their
  protection is weak: keep them below 5 V. A and B take up to 80 V (x1).
- The time resolution is one sample; an edge is seen one sample after it
  happens (the trigger time is corrected for it).
- VCD time unit is 1 ps (the sample period rounded to ps), the trigger time
  is written as a comment.

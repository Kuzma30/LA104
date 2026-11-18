# 65_minical: manual calibration of CH1 / CH2 for 58_gabmini (DS203)

A separate application that edits `MINICAL.CFG`, the calibration file of the
oscilloscope 58_gabmini: the zero (0 V level) and the gain of every V/div
range of both analog channels. The oscilloscope reads the file at start.

Build: `./build_ds203.sh` (needs `system/os_library/build/libbios_ds203.so`
and `tools/elfstrip/elfstrip`), copy `build/65mcal03.elf` to the device
(for example as `minical.elf`).

## Screen

```
Range  CH1 zero  gain     CH2 zero  gain
50mV   127.62    100.00%  126.95    100.00%
...
10V    128.10    100.00%  127.40     99.85%
Position  128
Ref       1.000 V              for the gain (F1)
File      Save   Reload   Defaults
CH1 1V pos 128  ADC 128.37 (127-130)
Vavg 8.1 mV   zero 128.33
<status>
F3 column  F1 auto/do  F4 save  F2 exit
```

- **zero**: ADC value (0..255, in hundredths) of 0 V at the Position row,
  the same value the oscilloscope uses for its zero mark.
- **gain**: correction of the range, 100 % = nominal (32 ADC steps per
  division).
- The last two lines measure live: the channel of the selected column at
  the selected range and position (DC, mean of 60 ms = whole periods of
  50 Hz and 60 Hz), and the voltage computed with the calibration shown.

## Keys

| Key | |
|---|---|
| Up / Down | row (a range row also selects the range of the live measurement) |
| F3 | next column: CH1 zero, CH1 gain, CH2 zero, CH2 gain; on File: next action |
| Left / Right | change the value; held, the step grows |
| F1 | zero column: automatic zero; gain column: automatic gain; File: the action |
| F4 | save |
| F2 | exit (twice when not saved) |

Steps: zero 1/16 of an ADC step, gain 1/2048 (0.05 %), position 1, Ref 1 mV.
Changing the zero moves the 0 V level at every position of the range;
changing the gain keeps the zero.

## Calibration

Zero, per channel and range:
1. Input of the channel to ground (probe tip on its ground clip).
2. Select the range row and the zero column of the channel, F1.
   Three positions (40, 128, 216) are measured; a clipped one is moved
   towards the middle.
3. Check: Vavg about 0 at any Position. Fine tune with Left / Right.

Gain, per channel and range, after its zero:
1. A known DC voltage on the input (a battery or a reference measured with a
   good multimeter), 2..4 divisions of the range: for 1 V/div 2..4 V.
2. Set Ref to that voltage (Left / Right on the Ref row).
3. Select the range row and the gain column of the channel, F1.
4. Check: Vavg = Ref. With a negative voltage set Ref negative.

The probe setting of the oscilloscope is not used here: calibrate with the
voltage on the input (x1).

F4 saves. Zero cal in the Tools of the oscilloscope recalibrates the zero of
all ranges and keeps the gains.

## File

One sector, little endian: `u32 "GMCA"`, `u32 3` (version), `u32` checksum
(djb2, `*33`, of the data), then for CH1 and CH2, ranges 50 mV..10 V:
`s16 qin[3]`, `s32 qout[3]`, `s32 K`. The arithmetic is that of the
oscilloscope: `Q` is the line through `(qin, qout)` at the position,
`Q + raw * K = 0` at 0 V, `volts = (Q + raw * K) * V/div / 65536`,
`K = 2048` is the gain 1. Version 2 files (without K) are read with gain 1.
`source/calib.h` holds the format and the arithmetic.

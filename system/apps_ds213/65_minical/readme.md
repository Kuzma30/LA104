# 65_minical: manual calibration of CH1 / CH2 for 58_gabmini (DS203)

A separate application that edits `MINICAL.CFG`, the calibration file of the
oscilloscope 58_gabmini: the zero (0 V level) and the gain of every V/div
range of both analog channels, and the calibration of the DAC (Wave Out
voltage). The oscilloscope reads the file at start.

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
File      Save   Reload   Defaults   DAC
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

## DAC (Wave Out)

File row, action `DAC` (F3 / Left / Right to select, F1 to open): a page that
calibrates the voltage of the generator output against an external voltmeter.
The DAC puts a constant level on the Wave Out (12 bit DAC with the output
buffer off, reference and the supply of the output amplifier 2.8 V, the sample
is repeated by the DMA as in the generator); F2 returns to the ADC page and
releases the output (the registers are restored; the DS203 BIOS has no DAC off
mode).

```
Code     2048
Vmeas    1123.4 mV       F1 edit
Point 1  300 = 0.205 V   F1: set
Point 2  3500 = 2.395 V  F1: set
Target   1000.0 mV       F1 edit
Offset (code 0): 0.0 mV
Gain: 0.6875 mV/code
Output now: code 2048 = 1.408 V
```

| Row | |
|---|---|
| Code | DAC code 0..4095 sent to the output, F3 steps the presets 0, 256, 1024, 2048, 3072, 3840, 4095 |
| Vmeas | the voltage read on the voltmeter in mV, 0.1 mV resolution; set to the calibrated value of the Code after every change of the Code |
| Point 1, Point 2 | the two calibration points (code = measured voltage). F1: the point = (Code, Vmeas); the output at once goes to the Target with the new calibration |
| Target | wanted voltage; the Code is computed from the calibration, the output shows it for the check |
| Offset, Gain | the line through the two points: the voltage at code 0 and mV per code (information) |

The voltage of a code is the straight line through the two points. The points
can be at any codes (in any order), so they are taken on the linear part of
the output: the amplifier on a single supply does not reach the rails, near
code 0 and near the full scale (2.8 V) the output is not linear and above
the supply the codes do not raise the voltage. Good points are about 10 % and
90 % of the linear part, for example 0.3 V and 2.5 V; the voltage in between is
then the best.

Digit entry (as in the generator): on the Code, Vmeas and Target rows (and on
the Position and Ref rows of the ADC page) F1 starts the editing, Left / Right
select the digit (one more step to the left selects the sign, where the value
can be negative), Up / Down change the selected digit from 0 to 9 with carry
(on the DS203 the `>` side of the rocker increases), F1 or F2 finishes. Vmeas
and Target are in mV with 0.1 mV resolution. Without F1 Left / Right still
step the value, held faster.

Calibration:
1. Code row: a code for about 0.3 V (F3 or digit entry). Read the output with
   the voltmeter, set Vmeas to it, Point 1 row, F1.
2. Code row: a code for about 2.5 V (below the clipping). Read the voltmeter,
   set Vmeas, Point 2 row, F1. Both points are needed: a point that is not
   set stays at the default (0 V at code 0, 2.8 V at code 4095).
3. Check: Target row, set the voltage, e.g. 1.000 V; the voltmeter must show
   it. A deviation at a voltage: set the points closer to it (or to the
   range that is used) and check again.
4. F4 saves.

The two points must be at least 256 codes apart and the gain must be within
0.27..4 times the nominal (0.68 mV per code), otherwise the point is rejected.
Defaults (File row): 0 V at code 0 and 2.8 V at code 4095.

## File

One sector, little endian: `u32 "GMCA"`, `u32 3` (version), `u32` checksum
(djb2, `*33`, of the data), then for CH1 and CH2, ranges 50 mV..10 V:
`s16 qin[3]`, `s32 qout[3]`, `s32 K`. The arithmetic is that of the
oscilloscope: `Q` is the line through `(qin, qout)` at the position,
`Q + raw * K = 0` at 0 V, `volts = (Q + raw * K) * V/div / 65536`,
`K = 2048` is the gain 1. Version 2 files (without K) are read with gain 1.
`source/calib.h` holds the format and the arithmetic.

The DAC calibration is a 20 byte trailer right after the ADC data, outside
the checksum of the header: `u32 "GDAC"`, `s16 code[2]`, `s32 uv[2]` (microvolts
measured at the codes), `u32` checksum (djb2 of the code and uv bytes). The
voltage of a code is the line through the two points
(`Cal::DacUv`), the code of a wanted voltage is the inverse (`Cal::DacCode`).
The oscilloscope checks the checksum of the ADC data only, so the trailer
does not disturb it, and its Zero cal keeps the trailer when it saves the
file. A file without the trailer (or with a damaged one) has the default DAC
calibration.

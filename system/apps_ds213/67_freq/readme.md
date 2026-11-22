# 67_freq: frequency response analyzer for the DS203 (LA104 OS)

Bode plot (gain and phase over a logarithmic frequency sweep) with the
generator of the DS203 as the source, after the analyzer of Petteri Aimonen
(essentialscrap.com/dsoquad/freq.html).

## How it measures

- **Source**: Wave Out (DAC) gives a sine from a table with a whole number
  of periods (up to 2000 samples, DAC rate 72 MHz / D, at most 1 MS/s), so
  the frequency is exact: 1 Hz .. 200 kHz (5 samples per period at 200 kHz).
- **Inputs**: CH A and CH B sampled together over a whole number of periods
  (3500 .. 3800 samples, at most 0.7 s at low frequencies). The component at
  the generator frequency is taken by a single bin DFT with a Hann window:
  harmonics of the generator and noise do not count.
- **V/div and position** are chosen for every point and every channel (no
  clipping, at least a quarter of the screen); the calibration of the
  oscilloscope (`MINICAL.CFG`) gives the volts of every range.
- Host test of the arithmetic: two 8 bit channels with noise, gain -6.02 dB
  and phase -30 deg are measured within 0.006 dB / 0.05 deg from 10 Hz to
  200 kHz.

## Wiring

**Mode A/B** (gain and phase, recommended):

```
Wave Out --+--> circuit input            circuit output --> CH A
           +--> CH B
```

Gain = A / B, phase = phase of A - phase of B: the generator level and its
fall at high frequencies do not matter.

**Mode A**: only CH A on the circuit output, the result is the level of A in
dBV (no phase: nothing to compare with). After a calibration sweep it is
the gain relative to the generator.

**Calibration (F3)**: a sweep with the inputs on Wave Out directly (A/B: A
and B both on Wave Out; A: A on Wave Out). Later sweeps with the same
frequencies and mode are divided by it ("cal" in the title): it removes the
difference of the two channels (ranges, input capacitance) and, in mode A,
the generator response.

## Screen and keys

```
Freq response A/B cal                                   [SWEEP]
 10 +--------------------------------------------------+ 180   gain dB (yellow)
  0 |~~~~~~~~~~~~~~~~~~~~~~~~\                         |       phase deg (cyan)
-10 |                         \_                       | 90
 ...                             1k     10k    100k
1.00kHz -3.01dB -45.0deg A 0.71V B 1.00V                       cursor
10Hz 200kHz 50 pts A/B AC Out 100% Cursor                      settings
F1 sweep  F3 cal  F4 CSV  F2 exit                    status
```

| Key | |
|---|---|
| Up / Down | item |
| Left / Right | change: start / stop frequency (1 Hz .. 200 kHz), points (10 / 20 / 50 / 100), mode A/B or A, coupling AC / DC, generator level 25 .. 100 %, Cursor: over the points |
| F1 | sweep (again: stop) |
| F3 | calibration sweep |
| F4 | save `FR000.CSV` .. (f, gain, phase, A, B) |
| F2 | exit (the generator is switched off) |

Coupling AC (default) removes the DC level of Wave Out (the DAC output is
0 .. about 3 V); its high pass filter of the inputs affects both channels
alike in mode A/B. Use DC for circuits that pass DC down to the lowest
frequency in mode A.

Build: `./build_ds203.sh` -> `build/67freq3.elf`. Code at 0x08070000, the
samples in the RAM at 0x2000C000 (STM32F103VE of the DS203 HW 2.6x / 2.7x).

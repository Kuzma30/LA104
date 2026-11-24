# gabmini (58gmin03.elf): oscilloscope + generator for the DS203 / DS213

Keys: F1 run / stop, F2 back, F3 automatic setup (oscilloscope tabs), F4 enter.

## Oscilloscope tabs

- **Input**: channels, time base (up to 1 s/div), window, trigger (F3 on
  the trigger item changes its source), Buf: Full (4096 samples) or Scr (one
  screen, frames come much faster at slow time bases). Trigger modes:
  Auto (triggered, without a trigger the screen is updated anyway after a
  timeout), Norm (only triggered), Single (one capture, then stop), Scan
  (no trigger; at 50 ms/div and slower a roll display), None (no trigger
  condition, every full buffer is shown: free running). Norm / Single show
  TRIG (green) or WAIT (yellow) at the top right of the graph. Channel list (Enter on CH1 /
  CH2): coupling, V/div, probe, position, Beam (trace width 1..3 px, like
  the focus of an analog scope), color.
- **V/div and position while stopped**: a new V/div or position of CH1 /
  CH2 shows the stored samples at that V/div and position (the ADC values
  are converted with the calibration of both settings): the trace grows,
  shrinks and moves as it would with a new capture; values outside the
  range stored by the capture stay at its limits.
- **Time base while stopped** (F1 stop, Single): a new time base shows
  the stored samples at that time base (horizontal zoom around the trigger
  position): faster stretches them (CH1 / CH2 interpolated), slower
  compresses them (the part outside the stored samples stays empty). The
  cursors, measurements, FFT, CSV and the decoded frames follow; the
  decoders keep the rate the samples were taken at. The next capture uses
  the new time base.
- **Cursor**: X1 / X2 / Y1 / Y2, dX and dY. Units follow the time base
  (ns, us, ms, s). Y1 / Y2 in mode A follow the signal at X1 / X2.
- **Disp**: T-Y or X-Y (Lissajous), Dots / Lines / Fill (the area between
  the trace and its 0 V level), averaging, Persist No / Yes / Phosph, grid.
- **Decode**: UART, SPI, I2C, CAN, LIN, 1-Wire, WS2812, NEC, PS/2, pulses,
  pulse counter, quadrature encoder (FPGA) ... With Counter and with
  Encoder on the hardware counter the item List becomes Reset (the counted
  value back to 0).
- **Meas**: four automatic measurements and statistics.
  Up / Down selects M1..M4 or Stat, Left / Right the type, F4 the source.
  Types: Vpp, Vmax, Vmin, Vavg, Vrms (CH1 / CH2), Freq, Period, Duty,
  Wid+ / Wid- (also CH3 / CH4), Rise / Fall (10..90 %), Delay and Phase
  (CH1 -> CH2), FreqHW (edge counters of the FPGA 2.66+: CH3 / CH4 exact up
  to 6 MHz, CH1 / CH2 up to half the sampling rate; 1 s gate). The values
  come from the whole sample buffer. Stat: Left / Right on / off, F4
  reset; shows count, min, max, mean of the selected measurement.
- **Tools**:
  - Math: A+B, A-B, B-A, A*B of CH1 and CH2 in volts, magenta trace,
    scale "Math V/d", zero in the middle of the screen
  - FFT: spectrum of CH1 or CH2 (1024 samples from the screen start, 256
    with Buf:Scr; Hann window, bins to fs/2, 10 dB grid, 0 dB = full scale
    sine), peak frequency and level interpolated between the bins, the bin
    width ("bin") shown. Harmonics H2..H9 of the peak: numbers above their
    bars, list with the level relative to the fundamental (dBc) and the THD
    (floor about 0.2 %, quantization of the 8 bit ADC). Cursors X1 / X2 of
    the Cursor tab are frequency lines: frequency and level of each, the
    difference (Hz, dB) on the graph and in the Cursor tab
  - Ref: F4 stores the CH1 trace as a gray reference, Left / Right on / off
  - Save CSV (WAVnnn.CSV: sample, CH1 V, CH2 V, CH3, CH4) and Save BMP
    (SCRnnn.BMP, the screen, 16 bit) on the disk of the device
  - Zero cal: zero of CH1 and CH2 for every V/div. Disconnect the probes
    (or short them to ground), F4, F4 again to confirm. For each range the
    ADC value of 0 V is measured at three positions (DC coupling, mean of
    60 ms); the result is stored in MINICAL.CFG and loaded at start, the
    gains in the file stay. Afterwards the channel markers on the left show
    the real 0 V and the voltages (cursors, measurements, math, CSV) are
    relative to it. Zero and gain of every range can be edited by hand
    with the separate application 65_minical (same file).

## Generator

Tabs Wave (waveform, frequency, amplitude, offset / level, duty), Mod
(AM, FM, sweep), Edit (custom wave: Copy CH1 = one period of the captured
CH1, Normalize, Invert, Edit point by point, Save / Load of the custom wave
in GENWAVE.DAT on the disk, the label shows the result), PID.

F3 automatic setup: V/div and position of CH1 / CH2 (the signal takes 4..6
divisions in the middle), time base for 2.5..6 periods, rising edge
trigger in the middle of the larger signal (Auto).

Settings are kept in MINIOSC.CFG (format 7; format 6 loads, older files are
ignored once).

## Build

`./build_ds203.sh` (or `build_ds213.sh`), needs `arm-none-eabi-gcc` and
`system/os_library/build/libbios_ds203.so`. Memory on the DS203
(`source/app_ds203.lds`), needs the STM32F103VE (512 KB flash, 64 KB RAM) of
HW 2.6x / 2.7x:
- code at 0x08040000, 144 KB (then 16 KB for the translation), above the factory data at 0x0802C000..0x0803FFFF
  (FPGA bitstream, calibration; the loader refuses to write there)
- RAM 0x20004400, 0x6200 bytes, the stack of the OS below it
- the sample buffer (12 KB) in the section `.bss_hi` at 0x2000C000, RAM the OS
  does not use (it ends at 0x2000B800); the loader zeroes it like `.bss`

## Language

`LANG.CFG` with `UK` on the disk and `LANG/UK/OSC.TXT`: Ukrainian texts (see
`../lang/README.md`; needs the OS with UTF-8 text). All texts drawn through
`UI::Print` / `UI::Printf` (Framework/UiText.h) and the labels of the OS
controls (gui/Controls.h, `GUI_TEXT`) are translated as whole strings.
Values keep the international units; protocol names and decoded frames stay
as they are. Flash: the program up to 144 KB at 0x08040000, the compiled
translation 16 KB at 0x08064000.

## Trigger

The edge trigger of the FPGA has a hysteresis of 12 ADC steps (about half a
division at 25 steps / div): the level must be at least that far from the
top and the bottom of the signal, otherwise the trigger fires anywhere on
the flat part and the trace moves. After a triggered capture the crossing of
the level nearest to the trigger position (at most 24 samples away) is moved
to the trigger position, so the trace does not jitter at the fastest time
bases (the FPGA mark moves by some samples at 72 MS/s).

## Battery

An icon left of the first item of the toolbar shows the battery, read every
5 s: 4 segments from 3.40 V (empty, a red line) to 4.10 V (full); green 3-4
segments, yellow 2, red 0-1. The voltage comes from PC2 (divider 1:2) with
ADC2, channel 12, against VDDA 3.3 V (common/battery.h); the SYS attribute
V_BATTERY gives nothing under this OS. The System tab shows the voltage.

## System

Tab `System` (after Tools; `common/system.h`, the logic analyzer uses the
same settings):

- Language: EN or a directory of `LANG/` (`LANG.CFG`); the texts change at
  once (the acquisition starts again)
- Bright: `SYS` (the level the SYS set at power up) or 10..100 % of the
  backlight
- Volume: Off or 10..100 % of the beeper (a test beep at a change)
- Sleep: Off, 1, 2, 5, 10, 30 minutes (default 5): when no key was pressed
  and no signal was seen for that time, the backlight is switched off; any
  key switches it on again and does nothing else. A signal is CH1 / CH2 (on)
  varying by more than half a division or CH3 / CH4 (on) changing (checked
  every acquisition and every 2 s); the acquisition, the generator and the
  PID keep running.

Bright, Volume and Sleep are kept in `SYSTEM.CFG` (`sleep=5`, `bright=60`,
`volume=50`).

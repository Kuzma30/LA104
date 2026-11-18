# gabmini (58gmin03.elf): oscilloscope + generator for the DS203 / DS213

Keys: F1 run / stop, F2 back, F3 automatic setup (oscilloscope tabs), F4 enter.

## Oscilloscope tabs

- **Input**: channels, time base, window, trigger (F3 on the trigger item
  changes its source).
- **Cursor**: X1 / X2 / Y1 / Y2, dX and dY. Units follow the time base
  (ns, us, ms, s). Y1 / Y2 in mode A follow the signal at X1 / X2.
- **Disp**: drawing, averaging, persistence, grid.
- **Decode**: UART, SPI, I2C, CAN, pulses ...
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
  - FFT: spectrum of CH1 or CH2 (256 samples from the screen start, Hann
    window, 128 bins to fs/2, 10 dB grid, 0 dB = full scale sine), peak
    frequency and level
  - Ref: F4 stores the CH1 trace as a gray reference, Left / Right on / off
  - Save CSV (WAVnnn.CSV: sample, CH1 V, CH2 V, CH3, CH4) and Save BMP
    (SCRnnn.BMP, the screen, 16 bit) on the disk of the device
  - Zero cal: zero of CH1 and CH2 for every V/div. Disconnect the probes
    (or short them to ground), F4, F4 again to confirm. For each range the
    ADC value of 0 V is measured at two positions (DC coupling); the
    result is stored in MINICAL.CFG and loaded at start. Afterwards the
    channel markers on the left show the real 0 V and the voltages
    (cursors, measurements, math, CSV) are relative to it.

F3 automatic setup: V/div and position of CH1 / CH2 (the signal takes 4..6
divisions in the middle), time base for 2.5..6 periods, rising edge
trigger in the middle of the larger signal (Auto).

Settings are kept in MINIOSC.CFG (format 3: older files are ignored once).

## Build

`./build_ds203.sh` (or `build_ds213.sh`), needs `arm-none-eabi-gcc` and
`system/os_library/build/libbios_ds203.so`. Limits on the DS203: ROM 100 KB,
RAM 0x6200 bytes (see `source/app_ds203.lds`).

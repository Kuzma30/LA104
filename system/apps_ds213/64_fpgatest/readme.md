# 64_fpgatest: tester of the DS203 FPGA 2.67 / 2.68

DS203 only, needs the FPGA image 2.68 (`tools/fpga/ds203_hw26x_V268FPGA`;
2.67 passes all groups except P-A,
sources in `system/fpga_ds203`). Build: `./build_ds203.sh` (needs
`system/os_library/build/libbios_ds203.so` and `tools/elfstrip/elfstrip`),
copy `build/64fpgtst.elf` to the device.

The registers are accessed directly: `Set_Param` by PA6 (SCK), PA7 (SDA)
and PC5 (H_L), reads with PC4 (C_D, ClrR on the schematic) = 1, H_L = 0
from the FPGA bank 0x64000000. The OS is not changed. The test signal is
made on Wave Out (PB6) by TIM4 or by the CPU (exact number of pulses).

Keys: Left / Right page, F2 exit.

## Page Tests

Up / Down chooses the group (A-C, A-D, A-A, A-B, B, T-A, T-B, P-A), F1 runs it. Every
line is OK, FAIL or info.

Groups A-C and A-D, edge counters of one channel. Wiring: Wave Out -> CH_C
(group A-C) or Wave Out -> CH_D (group A-D), one connection each.
- version 267 in the status word, IDs 0xC0D2 / 0xC0D4 / 0xC0D6
- clear: the four counters are 0
- 100000 pulses of the CPU (~1 MHz): rising and falling exactly 100000
- square bursts of 0.5 s at 1 kHz, 100 kHz, 1 MHz, 6 MHz, 9 MHz: rising =
  falling, count = f * t within 0.5 % (the expected count in brackets);
  the counts are ~0.1 % below f * t, the error of the 1 ms tick that
  measures the burst
- 12 MHz: information only, the output stage of Wave Out does not pass it
  (0 edges on HW 2.7 while 6 MHz is exact)
- 2000 reads while the counter runs at 6 MHz: it moves, never back, no jump

Groups A-A and A-B, edge counters of CH_A / CH_B (comparators counted in
the sampling clock). Wiring: Wave Out -> CH_A (group A-A) or Wave Out ->
CH_B (group A-B), one connection each.
- version and IDs as above
- levels of the channel at 1 V/div with a 1 kHz square, threshold =
  middle, hysteresis = 1/8 of the swing; the other channel gets a threshold
  above its range
- sampling 36 MHz (0.5 us/div): clear, then 100000 pulses of the CPU:
  rising and falling exactly 100000
- square bursts of 0.5 s at 1 MHz and 3 MHz (exact within 0.5 %), 6 MHz
  information only (the analog input stage)
- the other channel: 0 edges
- sampling 6 MHz (5 us/div): 2 MHz exact; 4 MHz information only, it is
  above half the sampling rate and is counted wrong (aliasing)
- 2000 reads while the counter runs at 1 MHz: it moves, never back, no jump

Group B, quadrature. Wiring:

```
Wave Out ---+------------------ CH_A (CH1)
            +------------------ CH_C
            +--[ 10k ]--+------ CH_B (CH2)
                        +------ CH_D
                       ===  33 nF
                        |
                       GND
```

B is A delayed by ~0.23 ms: at 1 kHz that is a quadrature signal, A leads B.
- levels of CH_A / CH_B (1 V/div), thresholds and hysteresis set from them
- decoder in MCI, A = CH_A, B = CH_B, Z = CH_C: position +4 per period of
  CH_C exactly, no errors, Z counter = periods
- the same with A and B swapped: -4 per period
- decoder of the pins (A = CH_C, B = CH_D): +4 per period exactly (a slow
  edge on CH_D may bounce, the counters cancel it)

Groups T-A and T-B, triggers of the oscilloscope (the logic of the
original FPGA, `Signal.v` / `AddrCtrl.v`). Wiring: Wave Out -> CH_A (T-A)
or Wave Out -> CH_B (T-B), one connection each. The registers are written
directly: 0x00 mode, 0x01 level, 0x02 / 0x03 pulse time; ClrW starts an
acquisition, the status word shows Start (bit 5) and Full (bit 3), the
FIFO is read as the oscilloscope does.
- levels at 1 V/div, trigger level = middle (the swing must be > 40: the
  trigger uses the level +-12)
- no signal: a rising edge trigger does not fire
- for the sampling rates 150 kHz (200 us/div), 3, 6, 14.4, 24, 36 and
  72 MHz (200 ns/div), with a square of fs / 200 (a level lasts 100
  samples):
  - unconditional acquisition: the real half period, shown as the real
    sampling rate (`fs 72.0M`)
  - 12 cases: edge rising / falling, level high / low, the four pulse
    modes each with a time of 0.5 and 1.5 levels (fires / does not fire);
    a trigger that fires must have the edge in the right direction at
    sample ~150 (`at 151`: the position of the rising edge)
  - FAIL lists the cases that went wrong
- the same rising trigger at 3 MHz through BIOS::ADC::ConfigureTrigger
  (the path of the oscilloscope app through SYS)

Group P-A, peak detection and pulse triggers of 32 bits (FPGA 2.68).
Wiring: Wave Out -> CH_A. The first line also checks the ID 0xC0D8.
- levels of CH_A (swing > 40)
- 1 ms/div with the ADC at 36 MHz, K = 1200 (registers 0x0A / 0x0B): a
  1 MHz square, at least 95 % of the pairs (min, max) have 3/4 of the
  swing; the marks of the words (bits 16, 17 = 0 / 1) are checked
- no signal: at least 95 % of the pairs are narrow (max - min <= 10)
- the trigger mode (0x00) switches the peak detection off
- pulse trigger "0 > t" of CH_A at 36 MHz, 200 Hz square (a low level of
  90000 samples): time 60000 fires, 120000 does not (with 16 bits only it
  would be 54464 and fire)

If the cases fail only at the fast rates, the FPGA does not meet the
timing there; if the direct registers work and the BIOS line fails, the
problem is in SYS / the app.

## Page Edges

The eight counters (C, D by the pins; A, B by the comparators at the
sampling rate of the page, 3 MHz: exact up to 1.5 MHz) and their rate per
second. Up / Down: square on Wave Out (off, 1 Hz ... 12 MHz), F3 clears
the counters, F4 measures the thresholds of CH_A / CH_B (Wave Out at 1 kHz
for a moment, connected to the channels to count).

## Page Quad

Both decoders: position, position at the last Z, Z counter, position in
the turn, errors of the decoder in MCI. Up / Down: control register
(sources of A, B, Z), F3 sets the zero of the display, F4 measures the
thresholds of CH_A / CH_B (needs a signal on them, e.g. group B wiring and
the generator of the page Edges at 1 kHz). For a real encoder: A on CH_C,
B on CH_D, index on CH_A with the control "A=C B=D, Z=CH_A"; measure the
threshold first with Wave Out (1 kHz, 3.3 V) on CH_A and CH_B, then connect
the encoder (CH_A, CH_B are at 1 V/div DC).

# 64_fpgatest: tester of the DS203 FPGA 2.64

DS203 only, needs the FPGA image 2.64 (`tools/fpga/ds203_hw26x_V264FPGA`,
sources in `system/fpga_ds203`). Build: `./build_ds203.sh` (needs
`system/os_library/build/libbios_ds203.so` and `tools/elfstrip/elfstrip`),
copy `build/64fpgtst.elf` to the device.

The registers are accessed directly: `Set_Param` by PA6 (SCK), PA7 (SDA)
and PC5 (H_L), reads with PC4 (C_D, ClrR on the schematic) = 1, H_L = 0
from the FPGA bank 0x64000000. The OS is not changed. The test signal is
made on Wave Out (PB6) by TIM4 or by the CPU (exact number of pulses).

Keys: Left / Right page, F2 exit.

## Page Tests

Up / Down chooses the group (A-C, A-D, B), F1 runs it. Every line is OK
or FAIL.

Groups A-C and A-D, edge counters of one channel. Wiring: Wave Out -> CH_C
(group A-C) or Wave Out -> CH_D (group A-D), one connection each.
- version 264 in the status word, IDs 0xC0D2 / 0xC0D4
- clear: the four counters are 0
- 100000 pulses of the CPU (~1 MHz): rising and falling exactly 100000
- square bursts of 0.5 s at 1 kHz, 100 kHz, 1 MHz, 6 MHz, 9 MHz: rising =
  falling, count = f * t within 0.5 % (the expected count in brackets);
  the counts are ~0.1 % below f * t, the error of the 1 ms tick that
  measures the burst
- 12 MHz: information only, the output stage of Wave Out does not pass it
  (0 edges on HW 2.7 while 6 MHz is exact)
- 2000 reads while the counter runs at 6 MHz: it moves, never back, no jump

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

## Page Edges

The four counters and their rate per second. Up / Down: square on Wave
Out (off, 1 Hz ... 12 MHz), F3 clears the counters.

## Page Quad

Both decoders: position, position at the last Z, Z counter, position in
the turn, errors of the decoder in MCI. Up / Down: control register
(sources of A, B, Z), F3 sets the zero of the display, F4 measures the
thresholds of CH_A / CH_B (needs a signal on them, e.g. group B wiring and
the generator of the page Edges at 1 kHz). For a real encoder: A on CH_C,
B on CH_D, index on CH_A with the control "A=C B=D, Z=CH_A"; measure the
threshold first with Wave Out (1 kHz, 3.3 V) on CH_A and CH_B, then connect
the encoder (CH_A, CH_B are at 1 V/div DC).

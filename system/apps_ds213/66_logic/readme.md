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
  1/dX, jump to the next edge; **the data between X1 and X2 stretched to
  the whole screen** (any zoom factor, not only powers of 2)
- **VCD** files (GTKWave, PulseView: `LA000.VCD` ..) and screenshots
  (`LA000.BMP` ..)
- **protocol decoders** (as in the oscilloscope): UART, LIN, I2C, SPI,
  1-Wire, CAN, WS2812, NEC IR, PS/2, pulse width, edge counter, quadrature
  encoder on any lanes; the frames under the waveform, a list of them, CSV
  files (`LA000.CSV` ..)
- **protocol trigger**: a condition on the decoded frames (value / mask,
  I2C write or read address, SPI command, CAN ID, error, NACK, pulse
  width ...), followed by a value, n-th occurrence; captures again until
  it matches

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
code at 0x08068000 (64 KB), the capture (16 KB) in the RAM at 0x2000C000.

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
View Zoom X1 X2 UART List BMP             status        view
```

| Key | |
|---|---|
| Up / Down | menu item |
| Left / Right | change the item; View: scroll, Zoom: zoom out / in, X1 / X2: move (held: faster) |
| F1 | run / stop |
| F2 | exit |
| F3 | X1 / X2: next edge of any channel; **Zoom: the data between X1 and X2 on the whole screen, again: the whole capture**; Dec: decoder setup; List: list of the frames; otherwise the whole capture |
| F4 | save VCD; BMP: screenshot; Dec / List: the list of the frames; **X1 / X2: X1 .. X2 on the whole screen / the whole capture** |

Settings: sample rate 72 MS/s .. 30 S/s (right: faster), trigger, pretrigger,
thresholds of A and B (-0.5 .. 30 V; 1, 2 or 5 V/div is chosen for the
threshold), single / repeat.

## Protocol decoders

The item **Dec** (row 2, shows the protocol): Left / Right chooses the
protocol; **F3** opens the setup page: Up / Down item, Left / Right change,
F1 the digit of a value, F2 / F3 / F4 back. The setup is kept in
`LOGICDEC.CFG` (written only when it changes). Every capture is decoded
right away; a change of the setup decodes the capture again.

Every signal of a protocol is put on any lane: A, B (the analog inputs
against their thresholds), C, D; optional ones can be "-".

| Protocol | Signals (default) | Settings | Status line |
|---|---|---|---|
| UART | RX (C), TX (D), each optional | baud rate (auto, 300 .. 4 M), 5 .. 9 bits, parity, 1 / 2 stop bits, polarity | baud rate |
| LIN | Bus (C) | baud rate (auto, 2400 .. 19200), polarity; break, sync, PID with parity check, checksum classic / enhanced | baud rate |
| I2C | SCL (C), SDA (D) | 7 bit address, R / W, ACK / NACK, start / repeated start / stop | |
| SPI | CLK (C), MOSI (D), MISO (A), CS (B); MOSI, MISO, CS optional | mode 0 .. 3, 4 .. 32 bits, MSB / LSB first; the first word after CS is marked (command) | |
| 1-Wire | DQ (C) | reset, presence, bytes; ROM and function commands by name | |
| CAN | RX (C) | bit rate (auto, 10 k .. 1 M), polarity; standard / extended ID, RTR, DLC, data, CRC check, ACK, stuff errors | bit rate |
| WS2812 | DIN (C) | GRB / RGB; LED number and color (the frame in that color) | |
| NEC IR | IR (C) | polarity (receiver: idle high); leader, address (also 16 bit), command with inverse check, repeat | |
| PS/2 | CLK (C), DATA (D) | device to host, parity and stop check | |
| Pulse | In (C) | high or low pulses: width, period, frequency, duty cycle | mean frequency, duty cycle |
| Counter | In (C) | rising, falling or both edges, numbered | number of edges |
| Encoder | A (C), B (D), Z (optional) | x4 / x2 / x1, direction, Z polarity: runs of steps, Z with the steps per revolution, A / B errors | steps, per revolution, speed |

All protocols with values: hex, decimal or ASCII (Format).

- A decoded lane shows the waveform in its upper half and the frames below
  it: a box from the first to the last bit with the text inside when it
  fits (zoom in for more text). Address / ID / PID orange, start / break /
  leader / Z green, stop / CRC / checksum yellow, NACK pink with `~`,
  errors red with `!`. The role of every lane is written under its letter
  (RX, TX, CK, MO, MI, CS, CL, DA, QA ...).
- Automatic baud / bit rate: the shortest runs of the line, refined with
  all runs up to 10 bits, rounded to a standard rate within 5 %. At least
  3 samples per bit are needed (status `rate too low`); 8 or more
  are better.
- **List** (F3 on the item List): the frames with the time from the
  trigger, lane, role and the full text (ACK, errors, commands by name),
  starting at X1. Up / Down, Left / Right a page; **F1 puts X1 on the
  start of the selected frame, F3 X2 on its end** (marks X1 / X2 at the
  right); F4 writes `LA000.CSV` (time in ns from the trigger, lane, role,
  frame); F2 back. Back with both set: the frames from X1 to X2 fill the
  screen; with one set: the view is centered on it.
- At most 1024 frames per capture (status `1024 frames max`).
- Host test of every decoder and trigger condition: `test/test_decode.cpp`
  (`g++ -std=c++17 -I../source test_decode.cpp && ./a.out`).

## Protocol trigger

The trigger item (row 1) has the choice **Decoder**; the condition is set on
the setup page of the decoder, part "Trigger":

| Protocol | Conditions |
|---|---|
| UART | data, RX data, TX data (value / mask), break, error (framing, parity) |
| LIN | PID (6 bit id / mask), data, break, error (sync, parity, checksum) |
| I2C | write address, read address, address R/W (value / mask), data, NACK, start |
| SPI | MOSI word, MISO word, command (the first MOSI word after CS), word cut by CS |
| 1-Wire | reset, ROM command, function command, data, error |
| CAN | ID (11 / 29 bit, value / mask), data, error (CRC, ACK, stuffing) |
| WS2812 | color (0xRRGGBB / mask), LED number, error |
| NEC IR | address, command, repeat, error |
| PS/2 | data (scan code), error |
| Pulse | width > t, width < t, period > t (t in us) |
| Counter | edge number n |
| Encoder | index Z, A / B error |

- **Value / Mask**: the bits of the mask are compared (mask 0x7F with an
  I2C address, 0xF0 for a group of values ...).
- **Then next =**: the next frame of the same lane must have this value:
  "I2C write address 0x50, then 0x20" triggers on a write to register 0x20,
  "SPI command 0x03 then 0x00" on a read from address 0x00.., "CAN ID
  0x7DF then 8" on the DLC.
- **Occurrence**: the n-th matching frame of the capture (1 .. 99).

How it works: the FPGA stores the changes of the lines, the decoder runs
on the stored capture. With Trig = Decoder the capture starts at the first
activity of the main signal of the protocol (UART start bit, I2C SDA fall,
SPI CS fall or the first clock edge, CAN SOF ...) with 10 % before it.
After the capture the frames are searched; on a match the trigger mark
(time 0), X1 and the view go to the frame (X2 at its end); without a
match the analyzer captures again (status `no match #n`) until it
matches or F1 stops it. Repeat: after a match it waits for the next one.
The matching frame must be inside one capture (4095 changes), so the
sample rate should not be much higher than needed (8 .. 20 samples per
bit).

## Notes

- C and D are the logic inputs of the DS203 (about 1.6 V threshold); their
  protection is weak: keep them below 5 V. A and B take up to 80 V (x1).
- The time resolution is one sample; an edge is seen one sample after it
  happens (the trigger time is corrected for it).
- VCD time unit is 1 ps (the sample period rounded to ps), the trigger time
  is written as a comment.

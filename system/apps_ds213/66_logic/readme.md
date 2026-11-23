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
- **protocol decoders**: UART, SPI, I2C, 1-Wire on any lanes; the frames
  under the waveform, a list of them, CSV files (`LA000.CSV` ..)

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
View Zoom X1 X2 UART List BMP             status        view
```

| Key | |
|---|---|
| Up / Down | menu item |
| Left / Right | change the item; View: scroll, Zoom: zoom out / in, X1 / X2: move (held: faster) |
| F1 | run / stop |
| F2 | exit |
| F3 | X1 / X2: next edge of any channel; Dec: decoder setup; List: list of the frames; otherwise the whole capture |
| F4 | save VCD; on the item BMP a screenshot; on Dec / List the list of the frames |

Settings: sample rate 72 MS/s .. 30 S/s (right: faster), trigger, pretrigger,
thresholds of A and B (-0.5 .. 30 V; 1, 2 or 5 V/div is chosen for the
threshold), single / repeat.

## Protocol decoders

The item **Dec** (row 2, shows the protocol): Left / Right chooses off,
UART, SPI, I2C, 1-Wire; **F3** opens the setup page (Up / Down item, Left /
Right change, F2 / F3 / F4 back). The setup is kept in `LOGICDEC.CFG` (written
only when it changes). Every capture is decoded right away; a change of the
setup decodes the capture again.

| Protocol | Lanes (default) | Settings |
|---|---|---|
| UART | RX (C), TX (D), each optional | baud rate (auto or 300 .. 3 M), 5 .. 9 data bits, parity none / even / odd, 1 / 2 stop bits, idle high / low (inverted, RS-232 level shifted) |
| SPI | CLK (C), MOSI (D), MISO (A), CS (B); MOSI, MISO, CS optional | mode 0 .. 3 (CPOL / CPHA), 4 .. 16 bits per word, MSB / LSB first; CS active low |
| I2C | SCL (C), SDA (D) | 7 bit address, R / W, ACK / NACK, start / restart / stop |
| 1-Wire | data (C) | standard speed: reset, presence, bytes (LSB first) |

All protocols: values in hex, decimal or ASCII (Format).

- A decoded lane shows the waveform in its upper half and the frames below
  it: a box from the first to the last bit with the value inside when it
  fits (zoom in for the text). Address of I2C orange (`W50`, `R50`), start
  green line (`S`, `Sr`), stop yellow (`P`), NACK pink with `~`, errors
  red (UART framing / parity, SPI word cut by CS). The role of every lane
  of the decoder is written under its letter (RX, TX, CK, MO, MI, CS, CL,
  DA, 1W).
- **Automatic baud rate**: the runs of the line as whole numbers of bits,
  rounded to a standard rate within 8 %; the result is in the status line
  and on the setup page (`auto (115200)`). A lane without traffic gets the
  rate of the other one. At least 3 samples per bit are needed (status
  `rate too low`; 8 or more are better: the sample rate at least 8 x the
  baud rate).
- **List** (F3 on the item List): the frames with the time from the
  trigger, lane, role, value and ACK / errors, starting at X1. Up / Down,
  Left / Right a page; F3 puts X1 on the selected frame and shows it; F4
  writes `LA000.CSV` (time in ns from the trigger, lane, role, frame);
  F2 back.
- At most 1024 frames per capture (status `1024 frames max`).
- SPI without CS: the words count from the first clock edge of the
  capture. I2C: the transfer must start in the capture (the first START).
- Sample rates: SPI and I2C need at least 2 samples per clock phase (I2C
  400 kHz: 4 MS/s or more); 1-Wire: 1 MS/s or more.
- Decoders tested on the host (`test/test_decode.cpp`: UART at 115200 /
  9600 even 2 stop / 921600 odd inverted / 115200 at 1 MS/s with jitter,
  automatic baud rate, parity errors; SPI modes 0 .. 3 with a cut word;
  I2C write, restart, read, NACK; 1-Wire reset, presence, bytes).

## Notes

- C and D are the logic inputs of the DS203 (about 1.6 V threshold); their
  protection is weak: keep them below 5 V. A and B take up to 80 V (x1).
- The time resolution is one sample; an edge is seen one sample after it
  happens (the trigger time is corrected for it).
- VCD time unit is 1 ps (the sample period rounded to ps), the trigger time
  is written as a comment.

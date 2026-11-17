# FPGA loader (DS203)

Application for the LA104 OS on the DS203: loads FPGA images (`*.BIN`)
from the disk of the oscilloscope, without DFU and without a PC.

| menu | what |
|------|------|
| Load file into FPGA (test) | configures the FPGA from the file over SPI slave; the image runs until power off, the flash is not changed |
| Write file to flash (boot image) | writes the file as the boot image at 0x0802C000 (loaded by the DFU bootloader at every power up), verifies it and loads it into the FPGA; asks for a confirmation |
| Reload FPGA from flash | configures the FPGA from the boot image again (after a test) |
| Save flash image to FPGABAK.BIN | copies the boot image (68088 bytes) into a file |

The screen shows CDONE, the version from the status word of the running
image (as SYS `FPGAVER`) and the IDs of the counters / quadrature decoder
of the LA104 images (Select 0x18 / 0x19), and whether the flash holds an
iCE bitstream.

## Details

- Configuration pins (SYS `GPIO_Config`): PB10 C_nSS, PB11 C_SCK, PB12 C_SI,
  PB14 C_RST (CRESET_B), PB15 CDONE. Reset with SS low (as SYS `FPGA_RST`),
  8 clocks with SS high, the image MSB first, sampled at the rising edge,
  then 104 clocks to CDONE and 64 more; afterwards the pins are set back as
  SYS leaves them (SS high = PWRon, SI and C_RST inputs).
- Files up to 68088 bytes with the iCE preamble `7E AA 99 7E` in the first
  512 bytes. A shorter file is padded with zeros to 68088 bytes when
  written to the flash.
- The flash is written page by page (2 KB): pages that do not change are
  skipped, the rest of the last page (0x0803C9F8..) and the following pages
  hold the calibration and device info of the factory system and are kept.
  Only 0x0802C000..0x0803FFFF can be written.
- A broken image in the flash: the oscilloscope does not work after power
  up, the LA104 OS and this application still do; restore with this
  application (`FPGABAK.BIN` or `V261FPGA.BIN`) or with DFU
  (`CFG_FPGA.ADR`, then the `.BIN`).
- A test image that drives the data bus at the wrong time can disturb the
  display (the LCD shares the bus); power off restores the boot image.

Build: `./build_ds203.sh` -> `build/fpgaload.elf`. Not tested on hardware.

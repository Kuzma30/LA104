#pragma once
#include <stdint.h>

// FPGA of the DS203 (iCE65L04, or a compatible chip): configuration over
// SPI slave from the MCU and the boot image in the internal flash.
//
// Configuration pins (SYS GPIO_Config): PB10 C_nSS (after the configuration
// PWRon = 1), PB11 C_SCK, PB12 C_SI (MCU -> FPGA, then an input), PB13 C_SO,
// PB14 C_RST (CRESET_B, then an input with pull-up), PB15 CDONE.
// The DFU bootloader configures the FPGA from the flash at 0x0802C000 at
// every power up; the image there has 68088 bytes, the rest of the last
// page and the following pages hold the calibration and device info.
namespace Fpga
{
    enum {
        FlashBase = 0x0802C000,
        ImageSize = 68088,              // what the bootloader sends
        PageSize = 2048
    };

    // configuration from any source, the bytes in pieces
    void ConfigBegin();
    void ConfigData(const uint8_t* p, int n);
    bool ConfigEnd();                   // true when CDONE is high

    bool Done();                        // CDONE

    // status word and registers of the running image
    uint16_t Status();                  // version in bits 15..6
    uint16_t ReadSelected(uint8_t nSelect);

    // image header: FF 00 <comment> 00 FF ... 7E AA 99 7E
    bool ParseHeader(const uint8_t* p, int n, char* comment, int nMax);

    // flash programming of one page (2048 bytes, aligned)
    bool FlashPage(uint32_t nAddress, const uint8_t* pData);
}

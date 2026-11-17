#include <library.h>
#include "fpga.h"

namespace Fpga
{
    static inline volatile uint32_t& Reg(uint32_t a) { return *(volatile uint32_t*)a; }
    static inline volatile uint16_t& Bus() { return *(volatile uint16_t*)0x64000000; }

    enum : uint32_t {
        GPIOA_BSRR = 0x40010810, GPIOA_BRR = 0x40010814,
        GPIOB_CRH = 0x40010C04, GPIOB_IDR = 0x40010C08,
        GPIOB_BSRR = 0x40010C10, GPIOB_BRR = 0x40010C14,
        GPIOC_BSRR = 0x40011010, GPIOC_BRR = 0x40011014,

        SS = 1 << 10, SCK = 1 << 11, SI = 1 << 12, CRST = 1 << 14, CDONE = 1 << 15,
        BUS_SCK = 1 << 6, BUS_SDA = 1 << 7,     // GPIOA, register writes
        C_D = 1 << 4, H_L = 1 << 5,             // GPIOC

        FLASH_KEYR = 0x40022004, FLASH_SR = 0x4002200C,
        FLASH_CR = 0x40022010, FLASH_AR = 0x40022014,
        SR_BSY = 1 << 0, SR_PGERR = 1 << 2, SR_WRPRTERR = 1 << 4, SR_EOP = 1 << 5,
        CR_PG = 1 << 0, CR_PER = 1 << 1, CR_STRT = 1 << 6, CR_LOCK = 1 << 7
    };

    static uint32_t s_nSavedCrh = 0;

    static void _Delay(int n)
    {
        while ( n-- )
            __asm volatile ("nop");
    }

    // nibble of a pin in GPIOB_CRH
    static uint32_t _Mode(int nPin, uint32_t nMode)
    {
        return nMode << ((nPin - 8) * 4);
    }

    static void _SendByte(uint8_t b)
    {
        for ( int i = 0; i < 8; i++ )
        {
            // SCK low with the data bit, the FPGA samples at the rising edge
            Reg(GPIOB_BSRR) = (b & 0x80) ? (SI | (SCK << 16)) : ((SI | SCK) << 16);
            b <<= 1;
            _Delay(2);
            Reg(GPIOB_BSRR) = SCK;
            _Delay(2);
        }
    }

    bool Done()
    {
        return (Reg(GPIOB_IDR) & CDONE) != 0;
    }

    void ConfigBegin()
    {
        s_nSavedCrh = Reg(GPIOB_CRH);
        // SS, SCK, SI, CRST outputs (push-pull 50 MHz), SO and CDONE inputs
        uint32_t nCrh = s_nSavedCrh & ~(_Mode(10, 0xf) | _Mode(11, 0xf) | _Mode(12, 0xf) | _Mode(14, 0xf));
        Reg(GPIOB_BSRR) = SS | SCK | CRST;
        Reg(GPIOB_CRH) = nCrh | _Mode(10, 3) | _Mode(11, 3) | _Mode(12, 3) | _Mode(14, 3);

        // reset with SS low: SPI slave mode (as SYS FPGA_RST)
        Reg(GPIOB_BRR) = CRST;
        BIOS::SYS::DelayMs(1);
        Reg(GPIOB_BRR) = SS;
        BIOS::SYS::DelayMs(1);
        Reg(GPIOB_BSRR) = CRST;
        BIOS::SYS::DelayMs(3);          // clearing of the configuration memory
        // 8 clocks with SS high, then the image with SS low
        Reg(GPIOB_BSRR) = SS;
        _SendByte(0);
        Reg(GPIOB_BRR) = SS;
    }

    void ConfigData(const uint8_t* p, int n)
    {
        while ( n-- )
            _SendByte(*p++);
    }

    bool ConfigEnd()
    {
        // at least 100 clocks until CDONE, then at least 49 more
        for ( int i = 0; i < 13; i++ )
            _SendByte(0);
        bool bDone = Done();
        for ( int i = 0; i < 8; i++ )
            _SendByte(0);

        // SI becomes an input (Ready), SS high = PWRon, the rest as before;
        // CRST stays high (input with pull-up, ODR bit 14 = 1)
        Reg(GPIOB_BSRR) = SS | CRST;
        Reg(GPIOB_CRH) = s_nSavedCrh;
        BIOS::SYS::DelayMs(2);
        return bDone && Done();
    }

    // ------------------------------------------------------------------
    // the same access as SYS / the oscilloscope application

    static void _BusByte(uint8_t nData)
    {
        Reg(GPIOA_BSRR) = BUS_SCK;
        for ( int i = 0; i < 8; i++ )
        {
            Reg((nData << i) & 0x80 ? GPIOA_BSRR : GPIOA_BRR) = BUS_SDA;
            Reg(GPIOA_BRR) = BUS_SCK;
            Reg(GPIOA_BSRR) = BUS_SCK;
        }
        Reg(GPIOA_BRR) = BUS_SDA;
        Reg(GPIOA_BRR) = BUS_SCK;
        Reg(GPIOA_BSRR) = BUS_SDA;
    }

    static void _SetParam(uint8_t nAddr, uint8_t nValue)
    {
        Reg(GPIOC_BSRR) = H_L;
        _BusByte( nAddr );
        Reg(GPIOC_BRR) = H_L;
        _BusByte( nValue );
    }

    uint16_t Status()
    {
        Reg(GPIOC_BRR) = H_L | C_D;
        __asm volatile ("dsb" ::: "memory");
        return Bus();
    }

    uint16_t ReadSelected(uint8_t nSelect)
    {
        _SetParam( 5, nSelect );
        Reg(GPIOC_BRR) = H_L;
        Reg(GPIOC_BSRR) = C_D;
        __asm volatile ("dsb" ::: "memory");
        uint16_t n = Bus();
        Reg(GPIOC_BRR) = C_D;
        return n;
    }

    // ------------------------------------------------------------------

    bool ParseHeader(const uint8_t* p, int n, char* comment, int nMax)
    {
        comment[0] = 0;
        int i = 0;
        if ( n >= 2 && p[0] == 0xff && p[1] == 0x00 )
        {
            // comment of the tool: text lines separated by 0, ends with 00 FF
            int nLen = 0;
            for ( i = 2; i + 1 < n; i++ )
            {
                if ( p[i] == 0x00 && p[i+1] == 0xff )
                    break;
                char c = (char)p[i];
                if ( c == 0 )
                    c = ' ';
                if ( c >= 32 && c < 127 && nLen < nMax - 1 )
                    comment[nLen++] = c;
            }
            comment[nLen] = 0;
        }
        for ( i = 0; i + 3 < n && i < 512; i++ )
            if ( p[i] == 0x7e && p[i+1] == 0xaa && p[i+2] == 0x99 && p[i+3] == 0x7e )
                return true;
        return false;
    }

    // ------------------------------------------------------------------

    static bool _FlashWait()
    {
        uint32_t t0 = BIOS::SYS::GetTick();
        while ( Reg(FLASH_SR) & SR_BSY )
            if ( BIOS::SYS::GetTick() - t0 > 500 )
                return false;
        uint32_t nSr = Reg(FLASH_SR);
        Reg(FLASH_SR) = SR_EOP | SR_PGERR | SR_WRPRTERR;
        return (nSr & (SR_PGERR | SR_WRPRTERR)) == 0;
    }

    bool FlashPage(uint32_t nAddress, const uint8_t* pData)
    {
        // refuse everything but the FPGA area
        if ( nAddress < FlashBase || nAddress + PageSize > 0x08040000 || (nAddress & (PageSize - 1)) )
            return false;
        if ( memcmp( (const void*)nAddress, pData, PageSize ) == 0 )
            return true;                // nothing to do

        Reg(FLASH_KEYR) = 0x45670123;
        Reg(FLASH_KEYR) = 0xCDEF89AB;
        bool bOk = true;

        Reg(FLASH_CR) = CR_PER;
        Reg(FLASH_AR) = nAddress;
        Reg(FLASH_CR) = CR_PER | CR_STRT;
        bOk = _FlashWait();
        Reg(FLASH_CR) = 0;

        for ( int i = 0; bOk && i < PageSize; i += 2 )
        {
            uint16_t w = pData[i] | (pData[i+1] << 8);
            if ( w == 0xffff )
                continue;               // erased value
            Reg(FLASH_CR) = CR_PG;
            *(volatile uint16_t*)(nAddress + i) = w;
            bOk = _FlashWait();
            Reg(FLASH_CR) = 0;
        }
        Reg(FLASH_CR) = CR_LOCK;
        return bOk && memcmp( (const void*)nAddress, pData, PageSize ) == 0;
    }
}

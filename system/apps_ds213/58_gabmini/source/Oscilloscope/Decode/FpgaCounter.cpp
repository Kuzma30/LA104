#include <library.h>
#include "FpgaCounter.h"
#include <Oscilloscope/Core/CoreOscilloscope.h>

namespace FpgaCounter
{
#if defined(DS203)
    static inline volatile uint32_t& Reg(uint32_t a) { return *(volatile uint32_t*)a; }
    static volatile uint16_t& Bus() { return *(volatile uint16_t*)0x64000000; }

    static const char* const FileName = "CNT_FPGA.BIN";
    enum : uint32_t {
        RomImage = 0x0802C000, RomImageSize = 68088,    // original image (iCE65L04)
        GPIOB_CRH = 0x40010C04, GPIOB_IDR = 0x40010C08, GPIOB_BSRR = 0x40010C10, GPIOB_BRR = 0x40010C14,
        SS = 1 << 10, SCK = 1 << 11, SI = 1 << 12, CRESET = 1 << 14, CDONE = 1 << 15,
        RCC_CFGR = 0x40021004, MCO_MASK = 7u << 24, MCO_SYSCLK = 4u << 24,
        TIM1_CCER = 0x40012C20,
        FSMC_BCR2 = 0xA0000008, FSMC_BTR2 = 0xA000000C, FSMC_BWTR2 = 0xA000010C,
        Id = 0xC0D1,
        // command word (counter_top.v)
        CmdSnapshot = 1 << 3, CmdClearC = 1 << 4, CmdClearD = 1 << 5, CmdClearT = 1 << 6
    };

    static bool s_bActive = false;
    static EStatus s_eStatus = Off;
    static uint16_t s_nModes = 0;       // edge bits of the command word
    static uint32_t s_nSavedMco, s_nSavedCcer, s_nSavedBtr, s_nSavedBwtr;

    // 64 bit values extended from the 32 bit snapshots
    static uint32_t s_nLast[3];
    static uint64_t s_nValue[3];
    static uint64_t s_nBase[3];
    static uint32_t s_nLastTick = 0;

    static uint64_t s_nGate[3];
    static uint32_t s_nGateTick = 0;
    static float s_fRate[2];
    static bool s_bRate = false;

    // ------------------------------------------------------------------
    // configuration over bitbanged SPI (as QuadPawn fpga.c does)

    static inline void _Delay()
    {
        __asm volatile ("nop\n nop\n nop\n nop");
    }

    static void _Send(uint8_t b)
    {
        for ( int i = 0; i < 8; i++, b <<= 1 )
        {
            uint32_t si = (b & 0x80) ? SI : (SI << 16);
            Reg(GPIOB_BSRR) = (SCK << 16) | si;
            _Delay();
            Reg(GPIOB_BSRR) = SCK | si;
            _Delay();
        }
    }

    // bFromFile: CNT_FPGA.BIN, otherwise the original image from the flash
    static bool _Configure(bool bFromFile)
    {
        uint32_t nSize = 0;
        if ( bFromFile )
        {
            if ( BIOS::FAT::Open( FileName, BIOS::FAT::IoRead ) != BIOS::FAT::EOk )
                return false;
            nSize = BIOS::FAT::GetFileSize();
            if ( nSize < 1024 || nSize > 140000 )
            {
                BIOS::FAT::Close();
                return false;
            }
        }

        uint32_t nCrh = Reg(GPIOB_CRH);
        Reg(GPIOB_CRH) = (nCrh & 0x000000ff) | 0x83833300;

        Reg(GPIOB_BRR) = CRESET | SS;
        Reg(GPIOB_BSRR) = SCK;
        BIOS::SYS::DelayMs(2);
        Reg(GPIOB_BSRR) = CRESET;
        BIOS::SYS::DelayMs(2);

        if ( bFromFile )
        {
            uint8_t arrSector[BIOS::FAT::SectorSize];
            for ( uint32_t nPos = 0; nPos < nSize; nPos += BIOS::FAT::SectorSize )
            {
                if ( BIOS::FAT::Read( arrSector ) != BIOS::FAT::EOk )
                    break;
                uint32_t n = nSize - nPos;
                if ( n > BIOS::FAT::SectorSize )
                    n = BIOS::FAT::SectorSize;
                for ( uint32_t i = 0; i < n; i++ )
                    _Send( arrSector[i] );
            }
            BIOS::FAT::Close();
        } else
        {
            const uint8_t* p = (const uint8_t*)RomImage;
            for ( uint32_t i = 0; i < RomImageSize; i++ )
                _Send( p[i] );
        }
        for ( int i = 0; i < 500; i++ )
            _Send( 0 );

        bool bDone = (Reg(GPIOB_IDR) & CDONE) != 0;
        Reg(GPIOB_BSRR) = SS;
        Reg(GPIOB_CRH) = nCrh;
        return bDone;
    }

    // ------------------------------------------------------------------
    // bus access

    static void _Wait()
    {
        // the command is taken a few FPGA clocks after the write
        for ( volatile int i = 0; i < 20; i++ )
            ;
    }

    static void _Command(uint16_t nCmd)
    {
        Bus() = s_nModes | nCmd;
        _Wait();
    }

    static uint16_t _Read(int nSelect)
    {
        _Command( (uint16_t)nSelect );
        return Bus();
    }

    static uint32_t _Read32(int nSelect)
    {
        uint32_t nLo = _Read( nSelect );
        return nLo | ((uint32_t)_Read( nSelect + 1 ) << 16);
    }

    static void _Clock()
    {
        // FPGA clock on PA8: MCO = SYSCLK (72 MHz), TIM1 output off
        Reg(TIM1_CCER) &= ~1u;
        Reg(RCC_CFGR) = (Reg(RCC_CFGR) & ~MCO_MASK) | MCO_SYSCLK;
    }

    // snapshot of all counters, extends them to 64 bits
    static void _Update()
    {
        _Command( CmdSnapshot );
        for ( int i = 0; i < 3; i++ )
        {
            uint32_t n = _Read32( 1 + 2*i );
            s_nValue[i] += (uint32_t)(n - s_nLast[i]);
            s_nLast[i] = n;
        }
    }

    // ------------------------------------------------------------------

    bool Available()
    {
        return true;
    }

    bool Start(int nEdge)
    {
        if ( s_bActive )
            return true;

        s_nSavedMco = Reg(RCC_CFGR) & MCO_MASK;
        s_nSavedCcer = Reg(TIM1_CCER);
        s_nSavedBtr = Reg(FSMC_BTR2);
        s_nSavedBwtr = Reg(FSMC_BWTR2);

        _Clock();
        if ( !_Configure( true ) )
        {
            // missing file or bad image: original image back
            bool bFile = BIOS::FAT::Open( FileName, BIOS::FAT::IoRead ) == BIOS::FAT::EOk;
            if ( bFile )
                BIOS::FAT::Close();
            s_eStatus = bFile ? ConfigError : NoFile;
            s_bActive = true;
            Stop();
            return false;
        }

        // slow bus timing for the FPGA bank: strobes of 10 HCLK
        Reg(FSMC_BTR2) = (s_nSavedBtr & ~0xffffu) | 0x0a02;
        if ( Reg(FSMC_BCR2) & (1 << 14) )
            Reg(FSMC_BWTR2) = (s_nSavedBwtr & ~0xffffu) | 0x0a02;

        s_bActive = true;
        SetEdge( nEdge );
        if ( _Read( 0 ) != Id )
        {
            s_eStatus = NoId;
            Stop();
            return false;
        }
        _Command( CmdSnapshot | CmdClearC | CmdClearD | CmdClearT );
        for ( int i = 0; i < 3; i++ )
        {
            s_nLast[i] = _Read32( 1 + 2*i );
            s_nValue[i] = s_nBase[i] = 0;
            s_nGate[i] = 0;
        }
        s_nLastTick = s_nGateTick = BIOS::SYS::GetTick();
        s_bRate = false;
        s_eStatus = Running;
        return true;
    }

    void SetEdge(int nEdge)
    {
        if ( nEdge < 0 || nEdge > 2 )
            nEdge = 0;
        s_nModes = (uint16_t)((nEdge << 7) | (nEdge << 9));
        if ( s_bActive )
        {
            // new gate with the new edges
            _Command( 0 );
            _Update();
            for ( int i = 0; i < 3; i++ )
                s_nGate[i] = s_nValue[i];
            s_nGateTick = BIOS::SYS::GetTick();
            s_bRate = false;
        }
    }

    void Stop()
    {
        if ( !s_bActive )
            return;
        s_bActive = false;
        s_bRate = false;

        Reg(FSMC_BTR2) = s_nSavedBtr;
        Reg(FSMC_BWTR2) = s_nSavedBwtr;
        _Clock();
        _Configure( false );
        Reg(RCC_CFGR) = (Reg(RCC_CFGR) & ~MCO_MASK) | s_nSavedMco;
        Reg(TIM1_CCER) = s_nSavedCcer;

        // the new oscilloscope image starts with defaults, set everything
        CCoreOscilloscope::ConfigureAdc();
        CCoreOscilloscope::ConfigureTrigger();
        if ( s_eStatus == Running )
            s_eStatus = Off;
    }

    bool Active()
    {
        return s_bActive;
    }

    EStatus Status()
    {
        return s_eStatus;
    }

    void Reset()
    {
        if ( !s_bActive )
            return;
        _Update();
        s_nBase[C] = s_nValue[C];
        s_nBase[D] = s_nValue[D];
    }

    uint64_t Value(int nChannel)
    {
        if ( !s_bActive || nChannel < 0 || nChannel > 2 )
            return 0;
        _Update();
        return s_nValue[nChannel] - s_nBase[nChannel];
    }

    bool Rate(int nChannel, float& fRate)
    {
        fRate = (nChannel == C || nChannel == D) ? s_fRate[nChannel] : 0;
        return s_bActive && s_bRate && (nChannel == C || nChannel == D);
    }

    void Tick()
    {
        if ( !s_bActive )
            return;
        uint32_t nNow = BIOS::SYS::GetTick();
        if ( nNow - s_nLastTick < 50 )
            return;
        s_nLastTick = nNow;

        // the timebase setting of the oscilloscope changes the clock
        if ( (Reg(RCC_CFGR) & MCO_MASK) != MCO_SYSCLK || (Reg(TIM1_CCER) & 1) )
            _Clock();
        _Update();

        if ( nNow - s_nGateTick < 1000 )
            return;
        s_nGateTick = nNow;
        uint64_t dT = s_nValue[T] - s_nGate[T];
        if ( dT == 0 )
        {
            s_eStatus = NoClock;
            s_bRate = false;
        } else
        {
            // FPGA clock is SYSCLK, 72 MHz
            for ( int i = 0; i < 2; i++ )
                s_fRate[i] = (float)(s_nValue[i] - s_nGate[i]) * 72e6f / (float)dT;
            s_bRate = true;
            if ( s_eStatus == NoClock )
                s_eStatus = Running;
        }
        for ( int i = 0; i < 3; i++ )
            s_nGate[i] = s_nValue[i];
    }
#else
    bool Available() { return false; }
    bool Start(int) { return false; }
    void SetEdge(int) {}
    void Stop() {}
    bool Active() { return false; }
    EStatus Status() { return Off; }
    void Reset() {}
    uint64_t Value(int) { return 0; }
    bool Rate(int, float& fRate) { fRate = 0; return false; }
    void Tick() {}
#endif
}

#include <library.h>
#include "FpgaCounter.h"
#include <Oscilloscope/Core/FpgaBus.h>

namespace FpgaCounter
{
#if defined(DS203)
    static inline volatile uint32_t& Reg(uint32_t a) { return *(volatile uint32_t*)a; }

    enum : uint32_t {
        SYST_LOAD = 0xE000E014, SYST_VAL = 0xE000E018, SCB_ICSR = 0xE000ED04,
        PENDSTSET = 1 << 26,

        RegSelect = 0x05, RegCounter = 0x0E,
        SelBase = 0x10,         // + 2*(channel*2 + falling), low word; +1 high word
        SelId = 0x18, Id = 0xC0D2
    };

    static bool s_bActive = false;
    static EStatus s_eStatus = Off;
    static int s_nEdge = 0;

    // raw counters: C rising, C falling, D rising, D falling
    static uint32_t s_nLast[4];
    static uint64_t s_nValue[4];
    static uint64_t s_nBase[2];
    static uint32_t s_nLastTick = 0;

    static uint64_t s_nGate[2];
    static uint64_t s_nGateClocks = 0;
    static uint32_t s_nGateTick = 0;
    static float s_fRate[2];
    static bool s_bRate = false;

    static void _SetParam(uint8_t nAddr, uint8_t nValue) { FpgaBus::SetParam( nAddr, nValue ); }
    static uint16_t _ReadSelected(uint8_t nSelect) { return FpgaBus::ReadSelected( nSelect ); }

    static uint32_t _Gray2Bin(uint32_t g)
    {
        g ^= g >> 16;
        g ^= g >> 8;
        g ^= g >> 4;
        g ^= g >> 2;
        g ^= g >> 1;
        return g;
    }

    // reading the low word latches the whole value in the FPGA
    static uint32_t _Read32(int nRaw)
    {
        uint8_t nSel = (uint8_t)(SelBase + 2 * nRaw);
        uint32_t nLo = _ReadSelected( nSel );
        uint32_t nHi = _ReadSelected( nSel + 1 );
        return _Gray2Bin( nLo | (nHi << 16) );
    }

    static void _Update(int nRaw)
    {
        uint32_t n = _Read32( nRaw );
        s_nValue[nRaw] += (uint32_t)(n - s_nLast[nRaw]);
        s_nLast[nRaw] = n;
    }

    static void _UpdateAll()
    {
        for ( int i = 0; i < 4; i++ )
            _Update( i );
    }

    // edges of a channel in the selected mode
    static uint64_t _Edges(int nChannel)
    {
        uint64_t nRise = s_nValue[nChannel * 2], nFall = s_nValue[nChannel * 2 + 1];
        if ( s_nEdge == 0 )
            return nRise;
        if ( s_nEdge == 1 )
            return nFall;
        return nRise + nFall;
    }

    static uint64_t _Clocks()
    {
        uint32_t nPrimask;
        __asm volatile ("mrs %0, primask\n cpsid i" : "=r"(nPrimask) :: "memory");
        uint32_t nTick = BIOS::SYS::GetTick();
        uint32_t nVal = Reg(SYST_VAL);
        if ( Reg(SCB_ICSR) & PENDSTSET )
        {
            nVal = Reg(SYST_VAL);
            nTick++;
        }
        __asm volatile ("msr primask, %0" :: "r"(nPrimask) : "memory");
        uint32_t nLoad = Reg(SYST_LOAD) & 0xffffff;
        return (uint64_t)nTick * (nLoad + 1) + (nLoad - (nVal & 0xffffff));
    }

    static void _StartGate()
    {
        _UpdateAll();
        s_nGateClocks = _Clocks();
        s_nGate[C] = _Edges(C);
        s_nGate[D] = _Edges(D);
        s_nGateTick = BIOS::SYS::GetTick();
        s_bRate = false;
    }

    // ------------------------------------------------------------------

    bool Available()
    {
        return true;
    }

    int FpgaVersion()
    {
        return FpgaBus::Status() >> 6;
    }

    bool Start(int nEdge)
    {
        if ( s_bActive )
            return true;
        if ( _ReadSelected( SelId ) != Id )
        {
            s_eStatus = NoCounters;
            return false;
        }
        s_nEdge = nEdge;
        for ( int i = 0; i < 4; i++ )
        {
            s_nLast[i] = _Read32( i );
            s_nValue[i] = 0;
        }
        s_nBase[C] = s_nBase[D] = 0;
        s_bActive = true;
        s_eStatus = Running;
        s_nLastTick = BIOS::SYS::GetTick();
        _StartGate();
        return true;
    }

    void SetEdge(int nEdge)
    {
        if ( nEdge < 0 || nEdge > 2 )
            nEdge = 0;
        if ( nEdge == s_nEdge )
            return;
        // the shown values continue from where they were
        uint64_t nOld[2] = {Value(C), Value(D)};
        s_nEdge = nEdge;
        if ( s_bActive )
        {
            s_nBase[C] = _Edges(C) - nOld[C];
            s_nBase[D] = _Edges(D) - nOld[D];
            _StartGate();
        }
    }

    void Stop()
    {
        s_bActive = false;
        s_bRate = false;
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
        _SetParam( RegCounter, 1 );
        _SetParam( RegCounter, 0 );
        for ( int i = 0; i < 4; i++ )
        {
            s_nLast[i] = _Read32( i );
            s_nValue[i] = 0;
        }
        s_nBase[C] = s_nBase[D] = 0;
        _StartGate();
    }

    uint64_t Value(int nChannel)
    {
        if ( !s_bActive || (nChannel != C && nChannel != D) )
            return 0;
        _Update( nChannel * 2 );
        _Update( nChannel * 2 + 1 );
        return _Edges(nChannel) - s_nBase[nChannel];
    }

    bool Rate(int nChannel, float& fRate)
    {
        bool bOk = nChannel == C || nChannel == D;
        fRate = bOk ? s_fRate[nChannel] : 0;
        return s_bActive && s_bRate && bOk;
    }

    void Tick()
    {
        if ( !s_bActive )
            return;
        uint32_t nNow = BIOS::SYS::GetTick();
        if ( nNow - s_nLastTick < 100 )
            return;
        s_nLastTick = nNow;
        if ( nNow - s_nGateTick < 1000 )
        {
            _UpdateAll();   // 32 bit wrap is detected in time
            return;
        }
        _UpdateAll();
        uint64_t nClocks = _Clocks();
        uint64_t dClocks = nClocks - s_nGateClocks;
        uint32_t nLoad = Reg(SYST_LOAD) & 0xffffff;
        if ( dClocks > 0 )
        {
            float fSeconds = (float)dClocks / ((float)(nLoad + 1) * 1000.0f);
            for ( int i = 0; i < 2; i++ )
            {
                uint64_t n = _Edges(i);
                s_fRate[i] = (float)(n - s_nGate[i]) / fSeconds;
                s_nGate[i] = n;
            }
            s_bRate = true;
        }
        s_nGateClocks = nClocks;
        s_nGateTick = nNow;
    }
#else
    bool Available() { return false; }
    bool Start(int) { return false; }
    void SetEdge(int) {}
    void Stop() {}
    bool Active() { return false; }
    EStatus Status() { return Off; }
    int FpgaVersion() { return 0; }
    void Reset() {}
    uint64_t Value(int) { return 0; }
    bool Rate(int, float& fRate) { fRate = 0; return false; }
    void Tick() {}
#endif
}

#include <library.h>
#include "QuadCounter.h"
#include <Oscilloscope/Core/FpgaBus.h>

namespace QuadCounter
{
#if defined(DS203)
    static inline volatile uint32_t& Reg(uint32_t a) { return *(volatile uint32_t*)a; }

    enum : uint32_t {
        SYST_LOAD = 0xE000E014, SYST_VAL = 0xE000E018, SCB_ICSR = 0xE000ED04,
        PENDSTSET = 1 << 26,

        RegZCtrl = 0x0F, RegZThr = 0x10, RegZHys = 0x11,
        SelId = 0x19, Id = 0xC0D3,
        SelNow = 0x20,          // 4 counters, reading word 0 latches them
        SelAtZ = 0x28,          // captured at the last Z, with the Z counter
        SelZCount = 0x30
    };

    static bool s_bActive = false;
    static EStatus s_eStatus = Off;
    static uint8_t s_arrZ[3] = {0xff, 0xff, 0xff};    // written Z registers

    static uint32_t s_nLastRaw = 0;     // last raw position (sum of the counters)
    static int64_t s_nPos = 0;          // extended position
    static int64_t s_nBase = 0;         // position at Reset()
    static uint16_t s_nLastZRaw = 0;
    static uint32_t s_nZ = 0;           // Z pulses since Start()
    static uint32_t s_nZBase = 0;
    static bool s_bHaveZ = false;
    static int64_t s_nZPos = 0;         // extended position at the last Z
    static bool s_bHavePerRev = false;
    static int32_t s_nPerRev = 0;
    static uint32_t s_nLastTick = 0;

    static int64_t s_nGatePos = 0;
    static uint64_t s_nGateClocks = 0;
    static uint32_t s_nGateTick = 0;
    static float s_fRate = 0;
    static bool s_bRate = false;

    static uint32_t _Gray2Bin(uint32_t g)
    {
        g ^= g >> 16;
        g ^= g >> 8;
        g ^= g >> 4;
        g ^= g >> 2;
        g ^= g >> 1;
        return g;
    }

    // sum of the four up/down counters of a latched set
    static uint32_t _ReadSet(uint8_t nBase)
    {
        uint32_t nSum = 0;
        for ( int i = 0; i < 4; i++ )
        {
            uint32_t nLo = FpgaBus::ReadSelected( (uint8_t)(nBase + 2*i) );
            uint32_t nHi = FpgaBus::ReadSelected( (uint8_t)(nBase + 2*i + 1) );
            nSum += _Gray2Bin( nLo | (nHi << 16) );
        }
        return nSum;
    }

    // position at the last Z and the Z counter; the capture in the FPGA runs
    // in the sampling clock, the set is read twice to get a consistent one
    static bool _ReadAtZ(uint32_t& nPos, uint16_t& nZ)
    {
        for ( int nTry = 0; nTry < 4; nTry++ )
        {
            uint32_t nPos1 = _ReadSet( SelAtZ );
            uint16_t nZ1 = (uint16_t)_Gray2Bin( FpgaBus::ReadSelected( SelZCount ) );
            uint32_t nPos2 = _ReadSet( SelAtZ );
            uint16_t nZ2 = (uint16_t)_Gray2Bin( FpgaBus::ReadSelected( SelZCount ) );
            if ( nPos1 == nPos2 && nZ1 == nZ2 )
            {
                nPos = nPos1;
                nZ = nZ1;
                return true;
            }
        }
        return false;
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

    static void _Update()
    {
        uint32_t nRaw = _ReadSet( SelNow );
        s_nPos += (int32_t)(nRaw - s_nLastRaw);
        s_nLastRaw = nRaw;

        // reading word 0 of the set latches the Z counter as well: a new Z?
        FpgaBus::ReadSelected( SelAtZ );
        if ( (uint16_t)_Gray2Bin( FpgaBus::ReadSelected( SelZCount ) ) == s_nLastZRaw )
            return;
        uint32_t nZPosRaw;
        uint16_t nZRaw;
        if ( !_ReadAtZ( nZPosRaw, nZRaw ) )
            return;
        uint16_t nNew = (uint16_t)(nZRaw - s_nLastZRaw);
        if ( nNew == 0 )
            return;
        s_nLastZRaw = nZRaw;
        s_nZ += nNew;
        // the captured position relative to the current one
        int64_t nZPos = s_nPos - (int32_t)(nRaw - nZPosRaw);
        if ( s_bHaveZ )
        {
            // average when more Z came since the last reading; going back
            // and forth over the index is not a revolution
            int64_t nDiff = (nZPos - s_nZPos) / nNew;
            if ( nDiff >= 8 || nDiff <= -8 )
            {
                s_nPerRev = (int32_t)nDiff;
                s_bHavePerRev = true;
            }
        }
        s_nZPos = nZPos;
        s_bHaveZ = true;
    }

    static void _StartGate()
    {
        s_nGatePos = s_nPos;
        s_nGateClocks = _Clocks();
        s_nGateTick = BIOS::SYS::GetTick();
        s_bRate = false;
    }

    static void _WriteZ(int nIndex, uint8_t nAddr, uint8_t nValue)
    {
        if ( s_arrZ[nIndex] == nValue )
            return;
        s_arrZ[nIndex] = nValue;
        FpgaBus::SetParam( nAddr, nValue );
    }

    // ------------------------------------------------------------------

    bool Available()
    {
        return true;
    }

    bool Start()
    {
        if ( s_bActive )
            return true;
        if ( FpgaBus::ReadSelected( SelId ) != Id )
        {
            s_eStatus = NoDecoder;
            return false;
        }
        s_arrZ[0] = s_arrZ[1] = s_arrZ[2] = 0xff;   // written again by SetZ
        s_nLastRaw = _ReadSet( SelNow );
        s_nPos = s_nBase = 0;
        uint32_t nZPos;
        if ( !_ReadAtZ( nZPos, s_nLastZRaw ) )
            s_nLastZRaw = 0;
        s_nZ = s_nZBase = 0;
        s_bHaveZ = s_bHavePerRev = false;
        s_bActive = true;
        s_eStatus = Running;
        s_nLastTick = BIOS::SYS::GetTick();
        _StartGate();
        return true;
    }

    void Stop()
    {
        if ( s_bActive )
            _WriteZ( 0, RegZCtrl, 0 );
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

    void SetZ(int nSource, bool bActiveLow, int nThreshold, int nHysteresis)
    {
        if ( !s_bActive )
            return;
        if ( nThreshold < 0 ) nThreshold = 0;
        if ( nThreshold > 255 ) nThreshold = 255;
        if ( nHysteresis < 1 ) nHysteresis = 1;
        if ( nHysteresis > 64 ) nHysteresis = 64;
        uint8_t nCtrl = (uint8_t)((nSource == ZCh1 ? 1 : nSource == ZCh2 ? 2 : 0) | (bActiveLow ? 4 : 0));
        // threshold first: a new source must not see an old threshold
        _WriteZ( 1, RegZThr, (uint8_t)nThreshold );
        _WriteZ( 2, RegZHys, (uint8_t)nHysteresis );
        _WriteZ( 0, RegZCtrl, nCtrl );
    }

    void Reset()
    {
        if ( !s_bActive )
            return;
        _Update();
        s_nBase = s_nPos;
        s_nZBase = s_nZ;
        _StartGate();
    }

    int64_t Position()
    {
        return s_bActive ? s_nPos - s_nBase : 0;
    }

    uint32_t ZCount()
    {
        return s_bActive ? s_nZ - s_nZBase : 0;
    }

    bool InRev(int64_t& nSteps)
    {
        nSteps = s_nPos - s_nZPos;
        return s_bActive && s_bHaveZ;
    }

    bool PerRev(int32_t& nSteps)
    {
        nSteps = s_nPerRev;
        return s_bActive && s_bHavePerRev;
    }

    bool Rate(float& fSteps)
    {
        fSteps = s_fRate;
        return s_bActive && s_bRate;
    }

    void Tick()
    {
        if ( !s_bActive )
            return;
        uint32_t nNow = BIOS::SYS::GetTick();
        if ( nNow - s_nLastTick < 100 )
            return;
        s_nLastTick = nNow;
        _Update();
        if ( nNow - s_nGateTick < 1000 )
            return;
        uint64_t nClocks = _Clocks();
        uint64_t dClocks = nClocks - s_nGateClocks;
        uint32_t nLoad = Reg(SYST_LOAD) & 0xffffff;
        if ( dClocks > 0 )
        {
            float fSeconds = (float)dClocks / ((float)(nLoad + 1) * 1000.0f);
            s_fRate = (float)(s_nPos - s_nGatePos) / fSeconds;
            s_bRate = true;
        }
        s_nGatePos = s_nPos;
        s_nGateClocks = nClocks;
        s_nGateTick = nNow;
    }
#else
    bool Available() { return false; }
    bool Start() { return false; }
    void Stop() {}
    bool Active() { return false; }
    EStatus Status() { return Off; }
    void SetZ(int, bool, int, int) {}
    void Reset() {}
    void Tick() {}
    int64_t Position() { return 0; }
    uint32_t ZCount() { return 0; }
    bool InRev(int64_t& n) { n = 0; return false; }
    bool PerRev(int32_t& n) { n = 0; return false; }
    bool Rate(float& f) { f = 0; return false; }
#endif
}

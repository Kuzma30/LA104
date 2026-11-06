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

        // registers (serial)
        RegCtrl = 0x0F,         // bit1..0 A, 3..2 B, 5..4 Z, 6 Z on, 7 Z active low
        RegThr1 = 0x10, RegHys1 = 0x11, RegThr2 = 0x12, RegHys2 = 0x13,
        SelId = 0x19, Id = 0xC0D4,
        // A = C, B = D counted by the pins: four counters, word 0 latches them
        SelPins = 0x20, SelPinsAtZ = 0x28, SelPinsZ = 0x30,
        // decoder in the sampling clock: position, errors, position at Z, Z
        SelSync = 0x38,
        Mask = 0xffffff         // the counters have 24 bits
    };

    static bool s_bActive = false;
    static EStatus s_eStatus = Off;
    static uint8_t s_arrReg[5] = {0xff, 0xff, 0xff, 0xff, 0xff};
    static uint8_t s_nInputs = 0xff;    // A | B << 2 | Z << 4 (Z = 4 off)
    static bool s_bPins = true;
    static int s_nPinSign = 1;          // A = D, B = C counted by the pins: reversed

    static uint32_t s_nLastRaw = 0;     // last raw position (24 bit)
    static int64_t s_nPos = 0;          // extended position
    static int64_t s_nBase = 0;         // position at Reset()
    static uint16_t s_nLastZRaw = 0;
    static uint32_t s_nZ = 0, s_nZBase = 0;
    static uint8_t s_nLastErrRaw = 0;
    static uint32_t s_nErr = 0, s_nErrBase = 0, s_nErrTick = 0;
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

    static int32_t _Signed24(uint32_t n)
    {
        n &= Mask;
        return (n & 0x800000) ? (int32_t)(n | 0xff000000) : (int32_t)n;
    }

    static uint32_t _Word32(uint8_t nSel)
    {
        uint32_t nLo = FpgaBus::ReadSelected( nSel );
        uint32_t nHi = FpgaBus::ReadSelected( (uint8_t)(nSel + 1) );
        return _Gray2Bin( nLo | (nHi << 16) );
    }

    // sum of the four up/down counters of a latched set (pins)
    static uint32_t _ReadPins(uint8_t nBase)
    {
        uint32_t nSum = 0;
        for ( int i = 0; i < 4; i++ )
            nSum += _Word32( (uint8_t)(nBase + 2*i) );
        return (uint32_t)(nSum * s_nPinSign) & Mask;
    }

    // one latched reading: position now, position at the last Z, Z counter,
    // errors (sampling clock decoder only)
    struct TSet
    {
        uint32_t nPos, nAtZ;
        uint16_t nZ;
        uint8_t nErr;
    };

    static void _ReadZOnly(TSet& s)
    {
        if ( s_bPins )
        {
            s.nPos = _ReadPins( SelPins );
            FpgaBus::ReadSelected( SelPinsAtZ );           // latches the Z counter
            s.nZ = (uint16_t)_Gray2Bin( FpgaBus::ReadSelected( SelPinsZ ) );
            s.nErr = 0;
        } else
        {
            s.nPos = _Word32( SelSync ) & Mask;            // latches the whole set
            s.nErr = (uint8_t)_Gray2Bin( FpgaBus::ReadSelected( SelSync + 2 ) );
            s.nZ = (uint16_t)_Gray2Bin( FpgaBus::ReadSelected( SelSync + 5 ) );
        }
    }

    static void _ReadAtZ(TSet& s)
    {
        if ( s_bPins )
        {
            s.nAtZ = _ReadPins( SelPinsAtZ );
            s.nZ = (uint16_t)_Gray2Bin( FpgaBus::ReadSelected( SelPinsZ ) );
        } else
        {
            // latch again, the position at Z and Z of the same moment
            FpgaBus::ReadSelected( SelSync );
            s.nAtZ = _Word32( SelSync + 3 ) & Mask;
            s.nZ = (uint16_t)_Gray2Bin( FpgaBus::ReadSelected( SelSync + 5 ) );
        }
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
        s_nGatePos = s_nPos;
        s_nGateClocks = _Clocks();
        s_nGateTick = BIOS::SYS::GetTick();
        s_bRate = false;
    }

    // starts counting from the current state of the FPGA
    static void _Restart()
    {
        TSet s;
        _ReadZOnly( s );
        s_nLastRaw = s.nPos;
        s_nLastZRaw = s.nZ;
        s_nLastErrRaw = s.nErr;
        s_nPos = s_nBase = 0;
        s_nZ = s_nZBase = 0;
        s_nErr = s_nErrBase = 0;
        s_nErrTick = 0;
        s_bHaveZ = s_bHavePerRev = false;
        _StartGate();
    }

    static void _Update()
    {
        TSet s;
        _ReadZOnly( s );
        s_nPos += _Signed24( s.nPos - s_nLastRaw );
        s_nLastRaw = s.nPos;
        uint8_t nNewErr = (uint8_t)(s.nErr - s_nLastErrRaw);
        if ( nNewErr )
        {
            s_nErr += nNewErr;
            s_nLastErrRaw = s.nErr;
            s_nErrTick = BIOS::SYS::GetTick();
        }
        if ( s.nZ == s_nLastZRaw )
            return;

        // a new Z: the captured position, read twice for a consistent set
        TSet z1, z2;
        int nTry;
        for ( nTry = 0; nTry < 4; nTry++ )
        {
            _ReadAtZ( z1 );
            _ReadAtZ( z2 );
            if ( z1.nAtZ == z2.nAtZ && z1.nZ == z2.nZ )
                break;
        }
        if ( nTry == 4 )
            return;
        uint16_t nNew = (uint16_t)(z1.nZ - s_nLastZRaw);
        s_nLastZRaw = z1.nZ;
        s_nZ += nNew;
        // the captured position relative to the current one
        int64_t nZPos = s_nPos - _Signed24( s.nPos - z1.nAtZ );
        if ( s_bHaveZ && nNew )
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

    static void _Write(int nIndex, uint8_t nAddr, uint8_t nValue)
    {
        if ( s_arrReg[nIndex] == nValue )
            return;
        s_arrReg[nIndex] = nValue;
        FpgaBus::SetParam( nAddr, nValue );
    }

    static uint8_t _Clamp(int n, int nMin, int nMax)
    {
        return (uint8_t)(n < nMin ? nMin : n > nMax ? nMax : n);
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
        for ( int i = 0; i < 5; i++ )
            s_arrReg[i] = 0xff;     // written again by Configure
        s_nInputs = 0xff;
        s_bPins = true;
        s_nPinSign = 1;
        s_bActive = true;
        s_eStatus = Running;
        s_nLastTick = BIOS::SYS::GetTick();
        _Restart();
        return true;
    }

    void Stop()
    {
        if ( s_bActive )
            _Write( 0, RegCtrl, 0x0e );     // A = C, B = D, Z off (power up state)
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

    bool ByPins()
    {
        return s_bPins;
    }

    void Configure(int nA, int nB, int nZ, bool bZActiveLow,
                   int nThr1, int nHys1, int nThr2, int nHys2)
    {
        if ( !s_bActive )
            return;
        bool bZ = nZ >= Ch1 && nZ <= Ch4;
        uint8_t nInputs = (uint8_t)((nA & 3) | ((nB & 3) << 2) | ((bZ ? nZ : 4) << 4));
        // thresholds first: a new input must not see an old threshold
        _Write( 1, RegThr1, _Clamp( nThr1, 0, 255 ) );
        _Write( 2, RegHys1, _Clamp( nHys1, 1, 64 ) );
        _Write( 3, RegThr2, _Clamp( nThr2, 0, 255 ) );
        _Write( 4, RegHys2, _Clamp( nHys2, 1, 64 ) );
        uint8_t nCtrl = (uint8_t)((nA & 3) | ((nB & 3) << 2) | ((bZ ? nZ & 3 : 0) << 4) |
            (bZ ? 0x40 : 0) | (bZActiveLow ? 0x80 : 0));
        _Write( 0, RegCtrl, nCtrl );
        if ( nInputs != s_nInputs )
        {
            s_nInputs = nInputs;
            s_bPins = (nA == Ch3 && nB == Ch4) || (nA == Ch4 && nB == Ch3);
            s_nPinSign = nA == Ch4 ? -1 : 1;
            _Restart();
        }
    }

    void Reset()
    {
        if ( !s_bActive )
            return;
        _Update();
        s_nBase = s_nPos;
        s_nZBase = s_nZ;
        s_nErrBase = s_nErr;
        s_nErrTick = 0;
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

    uint32_t Errors()
    {
        return s_bActive ? s_nErr - s_nErrBase : 0;
    }

    uint32_t LastErrorTick()
    {
        return s_nErrTick;
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
    void Configure(int, int, int, bool, int, int, int, int) {}
    bool ByPins() { return false; }
    void Reset() {}
    void Tick() {}
    int64_t Position() { return 0; }
    uint32_t ZCount() { return 0; }
    uint32_t Errors() { return 0; }
    uint32_t LastErrorTick() { return 0; }
    bool InRev(int64_t& n) { n = 0; return false; }
    bool PerRev(int32_t& n) { n = 0; return false; }
    bool Rate(float& f) { f = 0; return false; }
#endif
}

#include "SettingsStore.h"
#include "MainWnd.h"
#include "SettingsIo.h"

namespace SettingsStore
{
    static const char* const FileName = "MINIOSC.CFG";
    enum { Magic = 0x46434d47, Version = 2 };   // "GMCF"
    enum { SectorSize = BIOS::FAT::SectorSize };
    typedef TSettingsIo<SectorSize> TIo;

    // Big sectors (DS213) do not fit to the stack, the sample buffer is used
    // as scratch there, so only loading at start and saving at exit is done
#if defined(DS203)
    #define SETTINGS_AUTOSAVE
#endif

    // ------------------------------------------------------------------
    // the stored values, the same function writes and reads

    static void Analog(TIo& io, CSettings::AnalogChannel& ch)
    {
        io.Enum(ch.Enabled, CSettings::AnalogChannel::_EnabledMax);
        io.Enum(ch.Coupling, CSettings::AnalogChannel::_CouplingMax);
        io.Enum(ch.Resolution, CSettings::AnalogChannel::_ResolutionMax);
        io.Enum(ch.Probe, CSettings::AnalogChannel::_ProbeMax);
        io.Int(ch.u16Color, 0, 0xffff);
        io.Int(ch.u16Position, -20, 300);
    }

    static void Digital(TIo& io, CSettings::DigitalChannel& ch)
    {
        io.Enum(ch.Enabled, CSettings::DigitalChannel::_EnabledMax);
        io.Enum(ch.Polarity, CSettings::DigitalChannel::_PolarityMax);
        io.Int(ch.u16Color, 0, 0xffff);
        io.Int(ch.u16Position, 0, 0xffff);
    }

    static void Marker(TIo& io, CSettings::Marker& m)
    {
        io.Enum(m.Mode, CSettings::Marker::_ModeMaxVoltage);
        io.Enum(m.Source, CSettings::Marker::_SourceMax);
        io.Enum(m.Display, CSettings::Marker::_DisplayMax);
        // in Auto mode the value follows the signal every frame, it is not
        // stored (it would make the autosave write the flash all the time)
        if ( m.Mode == CSettings::Marker::_Auto )
        {
            int nDummy = 0;
            io.Int(nDummy, 0, 0);
        } else
            io.Int(m.nValue, 0, BIOS::ADC::NumSamples);
    }

    static void Decode(TIo& io, Decoder::TConfig& c)
    {
        using namespace Decoder;
        io.Enum(c.protocol, ProtocolMax);
        for ( int p = 0; p < ProtocolCount; p++ )
        {
            for ( int r = 0; r < MaxRoles; r++ )
                io.Enum(c.ch[p][r], ChMax);
            io.Enum(c.invert[p], 1);
        }
        io.Enum(c.uartBaud, RateUartMax);
        io.Enum(c.uartBits, BitsUartMax);
        io.Enum(c.uartParity, 2);
        io.Enum(c.uartStop, 1);
        io.Enum(c.linBaud, RateUartMax);
        io.Enum(c.spiMode, 3);
        io.Enum(c.spiBits, BitsSpiMax);
        io.Enum(c.spiOrder, 1);
        io.Enum(c.canRate, RateCanMax);
        io.Enum(c.wsOrder, 1);
        io.Enum(c.format, FormatMax);
        io.Enum(c.band, BandMax);
    }

    static void Generator(TIo& io, CSettings::Generator& g)
    {
        io.Enum(g.Wave, CSettings::Generator::_WaveMax);
        io.Int(g.nScale, -0x20000, 0x20000);
        io.Int(g.nOffset, -0x20000, 0x20000);
        io.Int(g.nDuty, 0, 100);
        io.Float(g.nFrequency, 0.01f, 20e6f);
        io.Enum(g.nModMode, CCoreGenerator::ModSweep);
        io.Float(g.fModRate, 0.01f, 1e6f);
        io.Float(g.fModDepth, 0, 100);
        io.Float(g.fSweepFrom, 0.01f, 20e6f);
        io.Float(g.fSweepTo, 0.01f, 20e6f);
        io.Float(g.fSweepTime, 0.01f, 1000);
        io.Enum(g.nSweepLog, 1);
        io.Enum(g.nDds, 1);

        // custom (volatile) wave
        int& nLen = CCoreGenerator::GetVolatileLen();
        io.Int(nLen, 0, 256);
        uint8_t* pWave = CCoreGenerator::GetVolatile();
        for ( int i = 0; i < 256; i++ )
            io.Raw(pWave[i]);
    }

    static void Exchange(TIo& io)
    {
        Analog(io, Settings.CH1);
        Analog(io, Settings.CH2);
        Digital(io, Settings.CH3);
        Digital(io, Settings.CH4);

        io.Enum(Settings.Time.Resolution, CSettings::TimeBase::_ResolutionMax);
        io.Enum(Settings.Time.Range, CSettings::TimeBase::_FULL);
        io.Int(Settings.Time.Shift, 0, BIOS::ADC::NumSamples - CWndGraph::MaxX);

        io.Enum(Settings.Trig.Sync, CSettings::Trigger::_SyncMax);
        io.Enum(Settings.Trig.Type, CSettings::Trigger::_TypeMax);
        io.Enum(Settings.Trig.Source, CSettings::Trigger::_SourceMax);
        io.Int(Settings.Trig.nLevel, 0, 255);
        io.Int(Settings.Trig.nTime, 0, 0x7fff);
        io.Int(Settings.Trig.nHoldOff, 0, 0x7fff);

        Marker(io, Settings.MarkT1);
        Marker(io, Settings.MarkT2);
        Marker(io, Settings.MarkY1);
        Marker(io, Settings.MarkY2);

        io.Enum(Settings.Disp.Axes, CSettings::Display::_AxesMax);
        io.Enum(Settings.Disp.Draw, CSettings::Display::_DrawMax);
        io.Enum(Settings.Disp.Average, CSettings::Display::_AverageMax);
        io.Enum(Settings.Disp.Persist, CSettings::Display::_PersistMax);
        io.Enum(Settings.Disp.Grid, CSettings::Display::_GridMax);
        io.Enum(Settings.Disp.Axis, CSettings::Display::_AxisMax);

        Decode(io, Settings.Decode);
        Generator(io, Settings.Gen);

        // PID regulator (the mode is not stored, it always starts off)
        io.Float(Settings.Pid.fTarget, 0, 30.0f);
        io.Float(Settings.Pid.fKp, 0, 100.0f);
        io.Float(Settings.Pid.fKi, 0, 1000.0f);
        io.Float(Settings.Pid.fKd, 0, 10.0f);
        io.Float(Settings.Pid.fPeriod, 10.0f, 5000.0f);

        // last opened tab
        io.Int(Settings.Runtime.m_nMenuItem, -1, 63);
    }

    // ------------------------------------------------------------------

    static uint32_t s_nSavedSum = 0;
    static uint32_t s_nLastSum = 0;
    static uint32_t s_nChanged = 0;
    static uint32_t s_nLastCheck = 0;

    static uint32_t Checksum(int& nLength)
    {
        TIo io;
        io.mode = TIo::Checksum;
        io.nSum = 5381;
        io.nLength = 0;
        Exchange(io);
        nLength = io.nLength;
        return io.nSum;
    }

    static uint8_t* Scratch(uint8_t* pStack)
    {
#if defined(SETTINGS_AUTOSAVE)
        return pStack;
#else
        (void)pStack;
        return Sampler::GetBuffer();
#endif
    }

    static bool _Save(uint8_t* pSector)
    {
        int nLength;
        uint32_t nSum = Checksum(nLength);

        if ( BIOS::FAT::Open( FileName, BIOS::FAT::IoWrite ) != BIOS::FAT::EOk )
            return false;
        TIo io;
        io.mode = TIo::Write;
        io.pSector = pSector;
        io.nPos = 0;
        io.nLength = 0;
        io.nSum = 0;
        io.bOk = true;
        uint32_t nMagic = Magic;
        uint16_t nVersion = Version, nLen = (uint16_t)nLength;
        io.Raw(nMagic);
        io.Raw(nVersion);
        io.Raw(nLen);
        io.Raw(nSum);
        Exchange(io);
        int nTotal = io.nLength;
        if ( io.nPos > 0 )
        {
            memset( pSector + io.nPos, 0, SectorSize - io.nPos );
            if ( BIOS::FAT::Write( pSector ) != BIOS::FAT::EOk )
                io.bOk = false;
        }
        if ( BIOS::FAT::Close( nTotal ) != BIOS::FAT::EOk )
            io.bOk = false;
        if ( io.bOk )
            s_nSavedSum = nSum;
        return io.bOk;
    }

    // reads the file in the given mode, returns false when it is not valid
    static bool _Read(uint8_t* pSector, TIo::EMode mode)
    {
        if ( BIOS::FAT::Open( FileName, BIOS::FAT::IoRead ) != BIOS::FAT::EOk )
            return false;
        TIo io;
        io.mode = TIo::Read;    // header goes to local variables in both modes
        io.pSector = pSector;
        io.nPos = SectorSize;   // forces reading the first sector
        io.nLength = 0;
        io.nSum = 0;
        io.bOk = true;
        io.bValid = true;

        uint32_t nMagic = 0, nSum = 0;
        uint16_t nVersion = 0, nLen = 0;
        io.Raw(nMagic);
        io.Raw(nVersion);
        io.Raw(nLen);
        io.Raw(nSum);
        bool bHeader = io.bOk && nMagic == Magic && nVersion == Version;
        if ( bHeader )
        {
            io.mode = mode;
            io.nSum = 5381;
            io.nLength = 0;
            Exchange(io);
        }
        BIOS::FAT::Close();
        // a value out of range keeps its default, the rest is loaded
        return bHeader && io.bOk && io.nLength == nLen && io.nSum == nSum;
    }

    void Load()
    {
        uint8_t arrSector[SectorSize <= 512 ? SectorSize : 1];
        uint8_t* pSector = Scratch(arrSector);

        // first pass only checks the file, the settings are changed only
        // when the whole file is valid
        if ( _Read( pSector, TIo::Verify ) && _Read( pSector, TIo::Read ) )
        {
            // derived values
            Settings.Time.InvalidFirst = Settings.Trig.Sync == CSettings::Trigger::_None ? 150 : 30;
            if ( Settings.Time.Shift < Settings.Time.InvalidFirst )
                Settings.Time.Shift = Settings.Time.InvalidFirst;
            Settings.Trig.State = Settings.Trig.Sync == CSettings::Trigger::_Single ?
                CSettings::Trigger::_Wait : CSettings::Trigger::_Run;
        }
        int nLength;
        s_nSavedSum = s_nLastSum = Checksum(nLength);
        s_nChanged = s_nLastCheck = BIOS::SYS::GetTick();
    }

    void Save()
    {
        int nLength;
        if ( Checksum(nLength) == s_nSavedSum )
            return;
        uint8_t arrSector[SectorSize <= 512 ? SectorSize : 1];
        _Save( Scratch(arrSector) );
    }

    void Tick()
    {
#if defined(SETTINGS_AUTOSAVE)
        uint32_t nNow = BIOS::SYS::GetTick();
        if ( nNow - s_nLastCheck < 1000 )
            return;
        s_nLastCheck = nNow;

        int nLength;
        uint32_t nSum = Checksum(nLength);
        if ( nSum != s_nLastSum )
        {
            // still being changed, wait until the user stops
            s_nLastSum = nSum;
            s_nChanged = nNow;
            return;
        }
        if ( nSum != s_nSavedSum && nNow - s_nChanged >= 3000 )
        {
            uint8_t arrSector[SectorSize];
            if ( !_Save( arrSector ) )
                s_nSavedSum = nSum; // do not retry every second, next change will
        }
#endif
    }
}

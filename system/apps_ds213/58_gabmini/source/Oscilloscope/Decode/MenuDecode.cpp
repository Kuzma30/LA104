#include "MenuDecode.h"
#include "DecodeBand.h"
#include "HwCounter.h"
#include "FpgaCounter.h"
#include "QuadCounter.h"
#include <MainWnd.h>

using namespace Decoder;

static const char* const g_textFormat[] = {"Hex", "Dec", "ASCII"};
static const char* const g_textParity[] = {"None", "Even", "Odd"};
static const char* const g_textStop[] = {"1", "2"};
static const char* const g_textPolarity[] = {"Normal", "Inverted"};
static const char* const g_textPulse[] = {"High", "Low"};
static const char* const g_textMode[] = {"0", "1", "2", "3"};
static const char* const g_textOrder[] = {"MSB first", "LSB first"};
static const char* const g_textRgb[] = {"GRB", "RGB"};
static const char* const g_textBand[] = {"Top", "Bottom"};
static const char* const g_textEdge[] = {"Rising", "Falling", "Both"};
static const char* const g_textSource[] = {"Input", "Wave Out", "FPGA C/D"};
static const char* const g_textQuadRes[] = {"x4", "x2", "x1"};
static const char* const g_textDirection[] = {"Normal", "Reverse"};
static const char* const g_textActive[] = {"High", "Low"};

// ---------------------------------------------------------------------------

namespace DecodeUi
{
    static bool s_bHold = false;
    static int s_nSelected = -1;

    // software counter: edges of all acquisitions since reset
    static uint64_t s_nSwTotal = 0;
    static uint32_t s_nSwSequence = 0;

    static bool _HwWanted()
    {
        return Settings.Decode.protocol == Counter &&
            Settings.Decode.cntSource == SourceWaveOut && HwCounter::Available();
    }

    static bool _FpgaWanted()
    {
        return Settings.Decode.protocol == Counter &&
            Settings.Decode.cntSource == SourceFpga && FpgaCounter::Available();
    }
    static bool s_bFpgaTried = false;   // do not reload a failed image every tick

    // channel shown first by the FPGA counter: D when Input is CH4
    static int _FpgaChannel()
    {
        return Settings.Decode.ch[Counter][0] == Ch4 ? FpgaCounter::D : FpgaCounter::C;
    }

    // Encoder: the quadrature decoder of the FPGA counts all the time,
    // A, B and Z on any inputs (A, B on CH3 / CH4: by the pins)
    static bool s_bQuadTried = false;

    static bool _QuadChannels()
    {
        const uint8_t* ch = Settings.Decode.ch[Encoder];
        bool bAB = ch[0] <= Ch4 && ch[1] <= Ch4 && ch[0] != ch[1];
        return bAB && (ch[2] == ChOff || (ch[2] <= Ch4 && ch[2] != ch[0] && ch[2] != ch[1]));
    }

    static bool _QuadWanted()
    {
        return Settings.Decode.protocol == Encoder && QuadCounter::Available() && _QuadChannels();
    }

    // the FPGA counts in the orientation of the inputs (forward = A leads B)
    static int _QuadSign()
    {
        return Settings.Decode.quadDir ? -1 : 1;
    }

    // thresholds of the analog inputs: the middle of the levels seen in all
    // acquisitions since the settings of the channel changed (a short index
    // pulse is not in every buffer)
    static int s_arrMin[2] = {255, 255}, s_arrMax[2] = {0, 0};
    static uint32_t s_arrKey[2] = {0xffffffff, 0xffffffff};
    static bool s_bLevelsOk = false;

    static bool _UsedByEncoder(int ch)
    {
        const uint8_t* p = Settings.Decode.ch[Encoder];
        return p[0] == ch || p[1] == ch || p[2] == ch;
    }

    static void _TrackLevels()
    {
        for ( int ch = Ch1; ch <= Ch2; ch++ )
        {
            if ( !_UsedByEncoder(ch) )
                continue;
            CSettings::AnalogChannel& a = ch == Ch1 ? Settings.CH1 : Settings.CH2;
            uint32_t nKey = ((uint32_t)a.Resolution << 4) | ((uint32_t)a.Coupling << 8) |
                ((uint32_t)(a.u16Position & 0xffff) << 12);
            if ( nKey != s_arrKey[ch] )
            {
                s_arrKey[ch] = nKey;
                s_arrMin[ch] = 255;
                s_arrMax[ch] = 0;
            }
            const uint8_t* p = Sampler::GetBuffer() + ch;
            int nMin = s_arrMin[ch], nMax = s_arrMax[ch];
            for ( int i = Settings.Time.InvalidFirst; i < Sampler::GetValid(); i++ )
            {
                int v = p[i * Decoder::BytesPerSample];
                if ( v < nMin ) nMin = v;
                if ( v > nMax ) nMax = v;
            }
            s_arrMin[ch] = nMin;
            s_arrMax[ch] = nMax;
        }
    }

    static void _ForgetLevels()
    {
        for ( int ch = Ch1; ch <= Ch2; ch++ )
        {
            s_arrKey[ch] = 0xffffffff;
            s_arrMin[ch] = 255;
            s_arrMax[ch] = 0;
        }
    }

    // digital inputs and switched off Z need no level
    static bool _LevelKnown(int ch)
    {
        if ( ch != Ch1 && ch != Ch2 )
            return true;
        return s_arrMax[ch] - s_arrMin[ch] >= 12;
    }

    static void _QuadConfigure()
    {
        const uint8_t* ch = Settings.Decode.ch[Encoder];
        int nZ = ch[2] <= Ch4 && _LevelKnown(ch[2]) ? ch[2] : QuadCounter::ChOff;
        QuadCounter::Configure( ch[0], ch[1], nZ, Settings.Decode.invert[Encoder] != 0,
            (s_arrMin[0] + s_arrMax[0]) / 2, (s_arrMax[0] - s_arrMin[0]) / 8,
            (s_arrMin[1] + s_arrMax[1]) / 2, (s_arrMax[1] - s_arrMin[1]) / 8 );
        // A and B on analog inputs: the steps counted before their levels
        // were known are not valid
        bool bOk = _LevelKnown(ch[0]) && _LevelKnown(ch[1]);
        if ( bOk && !s_bLevelsOk )
            QuadCounter::Reset();
        s_bLevelsOk = bOk;
    }

    float SampleRate()
    {
#if defined(DS203) || defined(DS203HD)
        // FPGA sampling clock 72 MHz divided by (PSC+1)*(ARR+1), the same
        // tables as BIOS::ADC::ConfigureTimebase (200ns .. 1s per division)
        static const uint32_t arrDiv[] = {1, 2, 3, 5, 12, 24, 48, 120, 240, 480, 1200, 2400, 4800,
            12000, 24000, 48000, 120000, 240000, 480000, 1200000, 2400000};
        int n = (int)Settings.Time.Resolution;
        if ( n < 0 ) n = 0;
        if ( n >= (int)COUNT(arrDiv) ) n = COUNT(arrDiv)-1;
        return 72e6f / arrDiv[n];
#else
        // approximation: 30 samples per division
        return 30.0f / CSettings::TimeBase::pfValueResolution[ Settings.Time.Resolution ];
#endif
    }

    void Update()
    {
        // annotations only in the samples of this acquisition, the memory
        // after them may hold the persistence history
        Decoder::Run( Settings.Decode, Sampler::GetBuffer(), Sampler::Length(),
            Settings.Time.InvalidFirst, Sampler::GetValid(), SampleRate() );
        if ( Settings.Decode.protocol == Counter && Settings.Decode.cntSource == SourceSamples &&
             Sampler::GetSequence() != s_nSwSequence )
        {
            // every acquisition is added once, decoding it again with other
            // settings does not count it again
            s_nSwSequence = Sampler::GetSequence();
            s_nSwTotal += Decoder::CountedEdges;
        }
        if ( Settings.Decode.protocol == Encoder )
            _TrackLevels();
        if ( s_nSelected >= Decoder::Count )
            s_nSelected = -1;
        if ( MainWnd.m_wndMenuDecode.IsVisible() )
            MainWnd.m_wndMenuDecode.Refresh();
    }

    bool Hold()
    {
        return s_bHold;
    }

    int Selected()
    {
        return s_nSelected;
    }

    void Select(int n)
    {
        s_nSelected = n;
    }

    bool BeginFrame(int nViewBegin, int nViewWidth)
    {
        return DecodeBand::Begin( Settings.Decode, SampleRate(), nViewBegin, nViewWidth, s_nSelected );
    }

    void RenderColumn(ui16* column, int nHeight, int nIndex)
    {
        DecodeBand::Column( column, nHeight, nIndex );
    }

    void CounterReset()
    {
        s_nSwTotal = 0;
        s_nSwSequence = Sampler::GetSequence();   // the shown buffer is not counted
        HwCounter::Reset();
        FpgaCounter::Reset();
        QuadCounter::Reset();
        _ForgetLevels();            // the levels are measured again
        s_bLevelsOk = false;
    }

    int CounterChannel()
    {
        return _FpgaChannel();
    }

    uint64_t CounterTotal()
    {
        if ( HwCounter::Active() )
            return HwCounter::Value();
        if ( FpgaCounter::Active() )
            return FpgaCounter::Value( _FpgaChannel() );
        return s_nSwTotal;
    }

    void Tick()
    {
        bool bWanted = _HwWanted();
        if ( bWanted && (!HwCounter::Active() || HwCounter::Edge() != Settings.Decode.cntEdge) )
            HwCounter::Start( Settings.Decode.cntEdge );
        else if ( !bWanted && HwCounter::Active() )
            HwCounter::Stop();
        HwCounter::Tick();

        bool bFpga = _FpgaWanted();
        if ( !bFpga )
            s_bFpgaTried = false;
        if ( bFpga && !FpgaCounter::Active() && !s_bFpgaTried )
        {
            s_bFpgaTried = true;
            FpgaCounter::Start( Settings.Decode.cntEdge );
            if ( MainWnd.m_wndMenuDecode.IsVisible() )
                MainWnd.m_wndMenuDecode.m_itmInfo.Invalidate();
        }
        else if ( !bFpga && FpgaCounter::Active() )
            FpgaCounter::Stop();
        static int nLastEdge = -1;
        if ( FpgaCounter::Active() && nLastEdge != Settings.Decode.cntEdge )
            FpgaCounter::SetEdge( Settings.Decode.cntEdge );
        nLastEdge = Settings.Decode.cntEdge;
        FpgaCounter::Tick();

        bool bQuad = _QuadWanted();
        if ( !bQuad )
            s_bQuadTried = false;
        if ( bQuad && !QuadCounter::Active() && !s_bQuadTried )
        {
            s_bQuadTried = true;
            QuadCounter::Start();
            if ( MainWnd.m_wndMenuDecode.IsVisible() )
                MainWnd.m_wndMenuDecode.m_itmInfo.Invalidate();
        }
        else if ( !bQuad && QuadCounter::Active() )
            QuadCounter::Stop();
        if ( QuadCounter::Active() )
            _QuadConfigure();
        QuadCounter::Tick();

        if ( (Settings.Decode.protocol != Counter && Settings.Decode.protocol != Encoder) ||
             !MainWnd.m_wndMenuDecode.IsVisible() )
            return;
        static uint32_t nLast = 0;
        uint32_t nNow = BIOS::SYS::GetTick();
        if ( nNow - nLast < 200 )
            return;
        nLast = nNow;
        MainWnd.m_wndMenuDecode.Refresh();
    }

    void Shutdown()
    {
        HwCounter::Stop();
        FpgaCounter::Stop();
        QuadCounter::Stop();
    }

    bool LevelKnown(int nRole)
    {
        int ch = Settings.Decode.ch[Encoder][nRole];
        return ch == ChOff || _LevelKnown(ch);
    }

    int QuadSign()
    {
        return _QuadSign();
    }
}

static void FormatFreq(float f, char* buf);

// count with 8 characters at most: digits, then millions / billions
static void FormatCount(uint64_t n, char* buf)
{
    if ( n < 100000000ull )
    {
        strcpy( buf, CUtils::itoa( (int)n ) );
        return;
    }
    char unit = 'M';
    uint64_t nDiv = 1000000ull;
    if ( n >= 100000000000ull )
    {
        unit = 'G';
        nDiv = 1000000000ull;
    }
    uint32_t nWhole = (uint32_t)(n / nDiv);
    uint32_t nFrac = (uint32_t)((n % nDiv) / (nDiv / 1000));   // 3 decimals
    strcpy( buf, CUtils::itoa( (int)nWhole ) );
    int nDec = 6 - (int)strlen(buf);                            // fits 8 characters
    if ( nDec > 3 ) nDec = 3;
    if ( nDec > 0 )
    {
        char frac[4] = {(char)('0' + nFrac / 100), (char)('0' + nFrac / 10 % 10), (char)('0' + nFrac % 10), 0};
        frac[nDec] = 0;
        strcat( buf, "." );
        strcat( buf, frac );
    }
    int nLen = strlen(buf);
    buf[nLen] = unit;
    buf[nLen+1] = 0;
}

// frequency of the hardware counter, 1 Hz resolution, 8 characters
static void FormatRate(float f, char* buf)
{
    uint32_t n = (uint32_t)(f + 0.5f);
    if ( n < 1000000 )
    {
        strcpy( buf, CUtils::itoa( (int)n ) );
        strcat( buf, "Hz" );
        return;
    }
    // MHz with as many decimals as fit
    strcpy( buf, CUtils::itoa( (int)(n / 1000000) ) );
    int nDec = 6 - (int)strlen(buf);
    uint32_t nFrac = n % 1000000;
    strcat( buf, "." );
    char frac[7];
    for ( int i = 5; i >= 0; i-- )
    {
        frac[i] = (char)('0' + nFrac % 10);
        nFrac /= 10;
    }
    frac[nDec] = 0;
    strcat( buf, frac );
    strcat( buf, "M" );
}

// the three lines of the info panel in counter mode
static void FpgaText(int nLine, char* text)
{
    using namespace FpgaCounter;
    text[0] = 0;
    if ( !Active() )
    {
        // the original FPGA image has no counters, version 2.62 is needed
        if ( nLine == 0 )
            strcpy( text, "FPGA" );
        else if ( nLine == 1 )
        {
            int nVer = FpgaVersion();
            strcpy( text, CUtils::itoa( nVer / 100 ) );
            strcat( text, "." );
            if ( nVer % 100 < 10 )
                strcat( text, "0" );
            strcat( text, CUtils::itoa( nVer % 100 ) );
        }
        else if ( Status() == NoCounters )
            strcpy( text, "no cnt" );
        return;
    }
    if ( nLine < 2 )
    {
        // C and D with the channel letter
        text[0] = nLine == 0 ? 'C' : 'D';
        FormatCount( Value( nLine == 0 ? C : D ), text + 1 );
        if ( strlen(text) > 8 )
            FormatCount( Value( nLine == 0 ? C : D ), text );
        return;
    }
    float fRate;
    if ( Rate( DecodeUi::CounterChannel(), fRate ) )
        FormatRate( fRate, text );
    else
        strcpy( text, "..." );
}

static void CounterText(int nLine, char* text)
{
    if ( Settings.Decode.cntSource == SourceFpga && FpgaCounter::Available() )
    {
        FpgaText( nLine, text );
        return;
    }
    text[0] = 0;
    bool bHw = HwCounter::Active();
    if ( nLine == 0 )
        FormatCount( DecodeUi::CounterTotal(), text );
    else if ( nLine == 1 )
    {
        if ( bHw )
        {
            float fRate;
            if ( HwCounter::Rate( fRate ) )
                FormatRate( fRate, text );
            else
                strcpy( text, "..." );
        } else
        {
            // edges in the shown buffer
            strcpy( text, "+" );
            strcat( text, CUtils::itoa( (int)Decoder::CountedEdges ) );
        }
    }
    else if ( bHw )
        strcpy( text, "Wave Out" );
    else
    {
        strcpy( text, "fs " );
        FormatFreq( DecodeUi::SampleRate(), text + 3 );
    }
}

// ---------------------------------------------------------------------------
// Encoder

// x4 steps in the units of the resolution setting (floor, also negative)
static int64_t QuadScale64(int64_t n)
{
    int nShift = Settings.Decode.quadRes == QuadX1 ? 2 : Settings.Decode.quadRes == QuadX2 ? 1 : 0;
    return n >> nShift;
}

static int QuadDiv()
{
    return Settings.Decode.quadRes == QuadX1 ? 4 : Settings.Decode.quadRes == QuadX2 ? 2 : 1;
}

// signed number in nMax characters at most, M / G when it does not fit
static void FormatSigned(int64_t n, char* buf, int nMax)
{
    char* p = buf;
    uint64_t u = (uint64_t)n;
    if ( n < 0 )
    {
        *p++ = '-';
        u = (uint64_t)(-n);
        nMax--;
    }
    char digits[24];
    int nLen = 0;
    uint64_t v = u;
    do
    {
        digits[nLen++] = (char)('0' + v % 10);
        v /= 10;
    } while ( v );
    if ( nLen <= nMax )
    {
        while ( nLen )
            *p++ = digits[--nLen];
        *p = 0;
        return;
    }
    // with a unit: 1.234M
    char unit = 'M';
    int nUnitDigits = 6;
    if ( nLen - 6 > nMax - 1 )
    {
        unit = 'G';
        nUnitDigits = 9;
    }
    int nWhole = nLen - nUnitDigits;
    for ( int i = 0; i < nWhole; i++ )
        *p++ = digits[nLen - 1 - i];
    int nDec = nMax - 1 - nWhole - 1;           // unit and the point
    if ( nDec > 3 ) nDec = 3;
    if ( nDec > 0 )
    {
        *p++ = '.';
        for ( int i = 0; i < nDec; i++ )
            *p++ = digits[nLen - 1 - nWhole - i];
    }
    *p++ = unit;
    *p = 0;
}

// speed: rpm when the steps of a revolution are known, else steps per second
static void FormatSpeed(float fSteps, bool bPerRev, int32_t nPerRev, char* buf)
{
    float f = fSteps;
    const char* unit = "/s";
    if ( bPerRev && nPerRev != 0 )
    {
        f = fSteps * 60.0f / (float)(nPerRev < 0 ? -nPerRev : nPerRev);
        unit = "rpm";
    } else
        f /= (float)QuadDiv();
    char* p = buf;
    *p++ = f < -0.05f ? '-' : '+';
    if ( f < 0 )
        f = -f;
    char tmp[16];
    if ( f < 99.95f )
    {
        // one decimal
        int n = (int)(f * 10 + 0.5f);
        strcpy( tmp, CUtils::itoa( n / 10 ) );
        strcat( tmp, "." );
        strcat( tmp, CUtils::itoa( n % 10 ) );
    }
    else if ( f < 99999.5f )
        strcpy( tmp, CUtils::itoa( (int)(f + 0.5f) ) );
    else if ( f < 99999.5e3f )
    {
        strcpy( tmp, CUtils::itoa( (int)(f / 1000.0f + 0.5f) ) );
        strcat( tmp, "k" );
    } else
    {
        strcpy( tmp, CUtils::itoa( (int)(f / 1e6f + 0.5f) ) );
        strcat( tmp, "M" );
    }
    // cut the unit when the value is long ("+12345/s" fits, "+12345rpm" -> "+12345rp")
    strcpy( p, tmp );
    strcat( p, unit );
    buf[8] = 0;
}

// the four lines of the info panel: total, in the revolution, steps of a
// revolution, speed
static void EncoderText(int nLine, char* text)
{
    text[0] = 0;
    int nSign = DecodeUi::QuadSign();
    if ( QuadCounter::Active() )
    {
        // FPGA: counted all the time
        switch ( nLine )
        {
        case 0:
            if ( !DecodeUi::LevelKnown(0) || !DecodeUi::LevelKnown(1) )
                strcpy( text, "A/B ?" );        // no level of an analog input seen yet
            else
                FormatSigned( QuadScale64( nSign * QuadCounter::Position() ), text, 8 );
            break;
        case 1:
        {
            int64_t n;
            strcpy( text, "R" );
            if ( Settings.Decode.ch[Encoder][2] == ChOff )
                strcat( text, " no Z" );
            else if ( !DecodeUi::LevelKnown(2) )
                strcat( text, " Z ?" );     // no Z level seen yet
            else if ( QuadCounter::InRev( n ) )
                FormatSigned( QuadScale64( nSign * n ), text + 1, 7 );
            else
                strcat( text, " -" );
            break;
        }
        case 2:
        {
            int32_t n;
            strcpy( text, "P" );
            if ( QuadCounter::PerRev( n ) )
                FormatSigned( QuadScale64( nSign * (int64_t)n ), text + 1, 7 );
            else
                strcat( text, " -" );
            break;
        }
        case 3:
        {
            float f;
            int32_t nPerRev;
            bool bPerRev = QuadCounter::PerRev( nPerRev );
            uint32_t nErrors = QuadCounter::Errors();
            if ( nErrors && BIOS::SYS::GetTick() - QuadCounter::LastErrorTick() < 3000 )
            {
                // steps faster than the sampling: a faster timebase is needed
                strcpy( text, "AB! " );
                FormatSigned( nErrors, text + 4, 4 );
            }
            else if ( QuadCounter::Rate( f ) )
                FormatSpeed( nSign * f, bPerRev, nPerRev, text );
            else
                strcpy( text, "..." );
            break;
        }
        }
        return;
    }
    // samples of the buffer only
    const TQuadStats& q = Decoder::Quad;
    switch ( nLine )
    {
    case 0:
        strcpy( text, "b" );
        FormatSigned( QuadScale64( q.steps ), text + 1, 7 );
        break;
    case 1:
        strcpy( text, "Z " );
        strcat( text, CUtils::itoa( (int)q.zCount ) );
        break;
    case 2:
        strcpy( text, "P" );
        if ( q.havePerRev )
            FormatSigned( QuadScale64( q.perRev ), text + 1, 7 );
        else
            strcat( text, " -" );
        break;
    case 3:
        if ( q.errors )
        {
            strcpy( text, "AB! " );
            strcat( text, CUtils::itoa( (int)q.errors ) );
        }
        else if ( QuadCounter::Status() == QuadCounter::NoDecoder )
        {
            // the FPGA image without the decoder: 2.64 is needed
            int nVer = FpgaCounter::FpgaVersion();
            strcpy( text, "FPGA" );
            strcat( text, CUtils::itoa( nVer / 100 ) );
            strcat( text, "." );
            if ( nVer % 100 < 10 )
                strcat( text, "0" );
            strcat( text, CUtils::itoa( nVer % 100 ) );
        }
        else if ( q.rate != 0 )
            FormatSpeed( q.rate, q.havePerRev, q.perRev, text );
        else
            strcpy( text, "buffer" );
        break;
    }
}

// ---------------------------------------------------------------------------

static void FormatFreq(float f, char* buf)
{
    const char* unit = "Hz";
    if ( f < 1.0f ) { f *= 1e3f; unit = "mHz"; }
    else if ( f >= 1e6f ) { f /= 1e6f; unit = "M"; }
    else if ( f >= 1e3f ) { f /= 1e3f; unit = "k"; }
    Decoder::FormatTime( f, buf );      // 3 significant digits ...
    char* p = buf;
    while ( *p == '.' || (*p >= '0' && *p <= '9') )
        p++;
    strcpy( p, unit );                  // ... with frequency unit
}

/*virtual*/ void CItemDecodeBrowse::OnPaint()
{
    CWndMenuItem::OnPaint();
    char text[16];
    int n = DecodeUi::Selected();
    if ( Decoder::Count == 0 || Settings.Decode.protocol == Off )
        strcpy( text, "-" );
    else
    {
        strcpy( text, n >= 0 ? CUtils::itoa(n+1) : "-" );
        strcat( text, "/" );
        strcat( text, CUtils::itoa(Decoder::Count) );
    }
    int x = m_rcClient.left + 12 + MarginLeft;
    int y = m_rcClient.top + 16;
    if ( HasFocus() )
    {
        x += BIOS::LCD::Draw(x, y, RGB565(000000), RGBTRANS, CShapes::sel_left);
        int w = strlen(text)*8;
        BIOS::LCD::Bar( x, y, x + w, y + 14, RGB565(000000) );
        BIOS::LCD::Print( x, y, RGB565(ffffff), RGBTRANS, text );
        x += w;
        BIOS::LCD::Draw(x, y, RGB565(000000), RGBTRANS, CShapes::sel_right);
    } else
        BIOS::LCD::Print( x, y, RGB565(000000), RGBTRANS, text );
}

/*virtual*/ void CItemDecodeBrowse::OnKey(int nKey)
{
    if ( (nKey == BIOS::KEY::Left || nKey == BIOS::KEY::Right) && Decoder::Count > 0 )
    {
        int nDir = nKey == BIOS::KEY::Left ? -1 : 1;
        int n = DecodeUi::Selected();
        if ( n < 0 )
        {
            // start from the item in the middle of current view
            int nCenter = Settings.Time.Shift + CWndGraph::MaxX/2;
            n = 0;
            while ( n < Decoder::Count-1 && Decoder::Get(n).end < nCenter )
                n++;
        } else
            n += nDir;
        if ( n < 0 )
            n = 0;
        if ( n >= Decoder::Count )
            n = Decoder::Count-1;
        ((CWndMenuDecode*)m_pParent)->ShowItem(n);
        Invalidate();
    }
    CWndMenuItem::OnKey(nKey);
}

void CItemDecodeInfo::Create(CWnd* pParent)
{
    CRect rc = pParent->GetLast()->m_rcClient;
    rc.top = rc.bottom + CWndMenuItem::Space;
    rc.bottom = rc.top + 66;      // 4 lines (Encoder)
    CWnd::Create( "Info", WsVisible | WsNoActivate, rc, pParent );
}

/*virtual*/ void CItemDecodeInfo::OnPaint()
{
    CDesign::MenuItemClearDisabled( m_rcClient );
    int x = m_rcClient.left + 12 + CWndMenuItem::MarginLeft;
    int y = m_rcClient.top;
    char text[16];
    int proto = Settings.Decode.protocol;
    text[0] = 0;
    if ( proto == Encoder )
    {
        static const ui16 arrClr[4] = {RGB565(ffffff), RGB565(ffff00), RGB565(80e0ff), RGB565(b0b0b0)};
        for ( int i = 0; i < 4; i++ )
        {
            EncoderText( i, text );
            BIOS::LCD::Print( x, y + 16*i, arrClr[i], RGBTRANS, text );
        }
        return;
    }
    if ( proto == Counter )
    {
        static const ui16 arrClr[3] = {RGB565(ffffff), RGB565(ffff00), RGB565(b0b0b0)};
        for ( int i = 0; i < 3; i++ )
        {
            CounterText( i, text );
            BIOS::LCD::Print( x, y + 16*i, arrClr[i], RGBTRANS, text );
        }
        return;
    }
    if ( (proto == Uart || proto == Lin) && DetectedRate )
    {
        strcpy( text, CUtils::itoa(DetectedRate) );
        strcat( text, "Bd" );
    } else if ( proto == Can && DetectedRate )
    {
        FormatFreq( (float)DetectedRate, text );
        strcat( text, "b/s" );
    }
    float fFreq, fDuty, fWidth;
    if ( proto == Pulse && Decoder::PulseStats( DecodeUi::SampleRate(), fFreq, fDuty, fWidth ) )
    {
        // PWM: frequency and duty cycle averaged over the whole buffer
        FormatFreq( fFreq, text );
        if ( text[strlen(text)-1] != 'z' )
            strcat( text, "Hz" );
        BIOS::LCD::Print( x, y, RGB565(ffffff), RGBTRANS, text );
        // duty cycle of the high level also when low pulses are measured
        if ( Settings.Decode.invert[Pulse] )
            fDuty = 100.0f - fDuty;
        int nPermille = (int)(fDuty * 10 + 0.5f);
        strcpy( text, "D " );
        strcat( text, CUtils::itoa(nPermille / 10) );
        strcat( text, "." );
        strcat( text, CUtils::itoa(nPermille % 10) );
        strcat( text, "%" );
        BIOS::LCD::Print( x, y + 16, RGB565(ffffff), RGBTRANS, text );
        text[0] = 0;
    }
    if ( text[0] )
        BIOS::LCD::Print( x, y, RGB565(ffffff), RGBTRANS, text );
    // sampling rate, the time resolution of the decoder is 1/fs
    strcpy( text, "fs " );
    FormatFreq( DecodeUi::SampleRate(), text + 3 );
    BIOS::LCD::Print( x, y + 32, RGB565(b0b0b0), RGBTRANS, text );
}

// ---------------------------------------------------------------------------

void CWndListDecode::_Add(const char* pszName, const char* const* ppszText, uint8_t* pValue, int nMax)
{
    _ASSERT( m_nItems < MaxItems );
    m_pro[m_nItems].Create( (const char**)ppszText, (NATIVEENUM*)pValue, (NATIVEENUM)nMax );
    m_itm[m_nItems].Create( pszName, CWnd::WsVisible, &m_pro[m_nItems], this );
    m_nItems++;
}

void CWndListDecode::Create(CWnd* pParent)
{
    TConfig& c = Settings.Decode;
    int p = c.protocol;

    int nItems = 1; // band
    for ( int r = 0; r < MaxRoles; r++ )
        if ( RoleName(p, r) )
            nItems++;
    static const uint8_t arrParams[ProtocolCount] = {0, 5, 2, 0, 3, 0, 2, 1, 1, 0, 1, 2, 3};
    nItems += arrParams[p];

    CListBox::Create( ProtocolText[p], WsVisible | WsModal,
        CRect(96, 26, 319, 26 + 20 + 16*nItems + 8), RGB565(ffffff), pParent );

    m_nItems = 0;
    for ( int r = 0; r < MaxRoles; r++ )
        if ( RoleName(p, r) )
            _Add( RoleName(p, r), ChannelText, &c.ch[p][r], RoleOptional(p, r) ? ChOff : ChMaxRequired );

    switch ( p )
    {
    case Uart:
        _Add( "Baud", RateUartText, &c.uartBaud, RateUartMax );
        _Add( "Data bits", BitsUartText, &c.uartBits, BitsUartMax );
        _Add( "Parity", g_textParity, &c.uartParity, 2 );
        _Add( "Stop bits", g_textStop, &c.uartStop, 1 );
        _Add( "Polarity", g_textPolarity, &c.invert[p], 1 );
        break;
    case Lin:
        _Add( "Baud", RateUartText, &c.linBaud, RateUartMax );
        _Add( "Polarity", g_textPolarity, &c.invert[p], 1 );
        break;
    case Spi:
        _Add( "Mode", g_textMode, &c.spiMode, 3 );
        _Add( "Bits", BitsSpiText, &c.spiBits, BitsSpiMax );
        _Add( "Order", g_textOrder, &c.spiOrder, 1 );
        break;
    case Can:
        _Add( "Bitrate", RateCanText, &c.canRate, RateCanMax );
        _Add( "Polarity", g_textPolarity, &c.invert[p], 1 );
        break;
    case Ws2812:
        _Add( "Order", g_textRgb, &c.wsOrder, 1 );
        break;
    case Nec:
        _Add( "Polarity", g_textPolarity, &c.invert[p], 1 );
        break;
    case Pulse:
        _Add( "Measure", g_textPulse, &c.invert[p], 1 );
        break;
    case Counter:
        _Add( "Edges", g_textEdge, &c.cntEdge, EdgeMax );
        if ( HwCounter::Available() )
            _Add( "Source", g_textSource, &c.cntSource, SourceMax );
        else
            c.cntSource = SourceSamples;
        break;
    case Encoder:
        _Add( "Count", g_textQuadRes, &c.quadRes, QuadResMax );
        _Add( "Direction", g_textDirection, &c.quadDir, 1 );
        _Add( "Z active", g_textActive, &c.invert[p], 1 );
        break;
    }
    _Add( "Band", g_textBand, &c.band, BandMax );
}

// ---------------------------------------------------------------------------

void CWndDecodeList::Create(CWnd* pParent)
{
    CWnd::Create( "Decoded data", WsVisible | WsModal, CRect(4, 18, 316, 236), pParent );
    m_nSel = DecodeUi::Selected();
    if ( m_nSel < 0 )
    {
        m_nSel = 0;
        while ( m_nSel < Decoder::Count-1 && Decoder::Get(m_nSel).end < Settings.Time.Shift )
            m_nSel++;
    }
    m_nFirst = max(0, m_nSel - Lines/2);
}

/*virtual*/ void CWndDecodeList::OnPaint()
{
    CDesign::Window( m_rcClient, RGB565(ffffff) );

    char title[40];
    strcpy( title, ProtocolText[Settings.Decode.protocol] );
    strcat( title, ": " );
    strcat( title, CUtils::itoa(Decoder::Count) );
    strcat( title, Overflow ? "+ items" : " items" );
    BIOS::LCD::Print( m_rcClient.CenterX() - strlen(title)*4, m_rcClient.top+2,
        RGB565(000000), RGBTRANS, title );

    float fRate = DecodeUi::SampleRate();
    int x = m_rcClient.left + 6;
    for ( int i = 0; i < Lines; i++ )
    {
        int k = m_nFirst + i;
        int y = m_rcClient.top + 18 + i*16;
        if ( k >= Decoder::Count )
            break;
        TAnnotation a = Decoder::Get(k);
        bool bSel = k == m_nSel;
        ui16 clr = bSel ? RGB565(ffffff) : (a.flags & FError) ? RGB565(a00000) : RGB565(000000);
        if ( bSel )
            BIOS::LCD::Bar( m_rcClient.left+4, y, m_rcClient.right-4, y+16, RGB565(000000) );

        char line[48];
        int n = Decoder::FormatTime( (a.begin - CSettings::Trigger::Position) / fRate, line );
        while ( n < 9 )
            line[n++] = ' ';
        Decoder::Format( a, line + n, sizeof(line) - n, true, Settings.Decode, fRate );
        line[37] = 0;   // window width
        BIOS::LCD::Print( x, y, clr, RGBTRANS, line );
    }
    if ( Decoder::Count == 0 )
        BIOS::LCD::Print( x, m_rcClient.top + 18, RGB565(000000), RGBTRANS, "No data" );
}

/*virtual*/ void CWndDecodeList::OnKey(int nKey)
{
    int nOld = m_nSel;
    switch ( nKey )
    {
    case BIOS::KEY::Up: m_nSel--; break;
    case BIOS::KEY::Down: m_nSel++; break;
    case BIOS::KEY::Left: m_nSel -= Lines; break;
    case BIOS::KEY::Right: m_nSel += Lines; break;
    case BIOS::KEY::Enter:
    case BIOS::KEY::Escape:
    {
        DecodeUi::s_bHold = false;
        int nSel = m_nSel;
        StopModal();
        if ( nKey == BIOS::KEY::Enter && Decoder::Count > 0 )
            MainWnd.m_wndMenuDecode.ShowItem( nSel );
        return;
    }
    default:
        return;
    }
    if ( m_nSel >= Decoder::Count )
        m_nSel = Decoder::Count - 1;
    if ( m_nSel < 0 )
        m_nSel = 0;
    if ( m_nSel < m_nFirst )
        m_nFirst = m_nSel;
    if ( m_nSel >= m_nFirst + Lines )
        m_nFirst = m_nSel - Lines + 1;
    if ( m_nSel != nOld )
        Invalidate();
}

// ---------------------------------------------------------------------------

/*virtual*/ void CWndMenuDecode::Create(CWnd *pParent, int dwFlags)
{
    CWnd::Create( "CWndMenuDecode", dwFlags, CRect(320-CWndMenuItem::MarginLeft, 20, 400, 240), pParent );

    m_proProtocol.Create( (const char**)ProtocolText, (NATIVEENUM*)&Settings.Decode.protocol, ProtocolMax );
    m_proFormat.Create( (const char**)g_textFormat, (NATIVEENUM*)&Settings.Decode.format, FormatMax );

    m_itmProtocol.Create( "Protocol", RGB565(808080), &m_proProtocol, this );
    m_itmSetup.Create( "Setup", ToWord('d', 's'), this );
    m_itmBrowse.Create( this );
    m_itmList.Create( "List", ToWord('d', 'l'), this );
    m_itmFormat.Create( "Format", RGB565(808080), &m_proFormat, this );
    m_itmInfo.Create( this );
}

void CWndMenuDecode::ShowItem(int n)
{
    if ( n < 0 || n >= Decoder::Count )
        return;
    DecodeUi::Select( n );
    TAnnotation a = Decoder::Get(n);
    int nShift = (a.begin + a.end)/2 - CWndGraph::MaxX/2;
    int nMax = Sampler::Length() - CWndGraph::MaxX;
    if ( nShift > nMax )
        nShift = nMax;
    if ( nShift < Settings.Time.InvalidFirst )
        nShift = Settings.Time.InvalidFirst;
    Settings.Time.Shift = nShift;

    MainWnd.m_wndTReferences.Invalidate();
    MainWnd.m_wndZoomBar.Invalidate();
    MainWnd.m_wndGraph.Invalidate();
    if ( IsVisible() )
        m_itmBrowse.Invalidate();
}

void CWndMenuDecode::Refresh()
{
    static int nLastCount = -1;
    static uint32_t nLastRate = 0;
    static int nLastSel = -2;
    static int nLastPulse = -1;
    if ( nLastCount != Decoder::Count || nLastSel != DecodeUi::Selected() )
        m_itmBrowse.Invalidate();
    if ( nLastRate != Decoder::DetectedRate )
        m_itmInfo.Invalidate();
    if ( Settings.Decode.protocol == Pulse )
    {
        // redraw the PWM values only when they changed (in 0.1% steps)
        float fFreq = 0, fDuty = 0, fWidth;
        Decoder::PulseStats( DecodeUi::SampleRate(), fFreq, fDuty, fWidth );
        int nPulse = (int)(fDuty * 10) ^ ((int)(fFreq * 10) << 10);
        if ( nPulse != nLastPulse )
            m_itmInfo.Invalidate();
        nLastPulse = nPulse;
    }
    if ( Settings.Decode.protocol == Counter || Settings.Decode.protocol == Encoder )
    {
        // redraw only the changed values
        static uint32_t nLastHash = 0;
        uint32_t nHash = 5381;
        char text[16];
        bool bEncoder = Settings.Decode.protocol == Encoder;
        for ( int i = 0; i < (bEncoder ? 4 : 3); i++ )
        {
            if ( bEncoder )
                EncoderText( i, text );
            else
                CounterText( i, text );
            for ( char* p = text; *p; p++ )
                nHash = nHash * 33 + *p;
            nHash = nHash * 33 + '|';
        }
        if ( nHash != nLastHash )
            m_itmInfo.Invalidate();
        nLastHash = nHash;
    }
    // the List button resets the counter
    bool bReset = Settings.Decode.protocol == Counter ||
        (Settings.Decode.protocol == Encoder && QuadCounter::Active());
    const char* pszList = bReset ? "Reset" : "List";
    if ( m_itmList.m_pszId != pszList )
    {
        m_itmList.m_pszId = pszList;
        m_itmList.Invalidate();
    }
    nLastCount = Decoder::Count;
    nLastRate = Decoder::DetectedRate;
    nLastSel = DecodeUi::Selected();
}

/*virtual*/ void CWndMenuDecode::OnMessage(CWnd* pSender, int code, uintptr_t data)
{
    // LAYOUT ENABLE/DISABLE FROM TOP MENU BAR
    if ( code == ToWord('L', 'D') )
    {
        MainWnd.m_wndGraph.ShowWindow(false);
        MainWnd.m_wndZoomBar.ShowWindow(false);
        MainWnd.m_wndLReferences.ShowWindow(false);
        MainWnd.m_wndTReferences.ShowWindow(false);
        return;
    }
    if ( code == ToWord('L', 'E') )
    {
        DecodeUi::Update();
        MainWnd.m_wndGraph.ShowWindow(true);
        MainWnd.m_wndZoomBar.ShowWindow(true);
        MainWnd.m_wndLReferences.ShowWindow(true);
        MainWnd.m_wndTReferences.ShowWindow(true);
        return;
    }

    if ( code == ToWord('u', 'p') && pSender == &m_itmProtocol )
    {
        DecodeUi::Select(-1);
        DecodeUi::Update();
        m_itmInfo.Invalidate();
        m_itmBrowse.Invalidate();
        MainWnd.m_wndGraph.Invalidate();
        return;
    }
    if ( code == ToWord('u', 'p') && pSender == &m_itmFormat )
    {
        MainWnd.m_wndGraph.Invalidate();
        return;
    }

    // setup window
    if ( code == ToWord('d', 's') || (code == ToWord('l', 'e') && data == (uintptr_t)&m_proProtocol) )
    {
        if ( Settings.Decode.protocol == Off )
            return;
        m_wndSetup.Create( this );
        m_wndSetup.StartModal( m_wndSetup.First() );
        return;
    }
    if ( code == ToWord('u', 'p') && pSender && pSender->m_pParent == &m_wndSetup )
    {
        DecodeUi::Select(-1);
        DecodeUi::Update();
        MainWnd.m_wndGraph.Invalidate();
        return;
    }
    if ( (code == ToWord('e', 'x') && pSender == &m_wndSetup) ||
         (code == ToWord('l', 'e') && pSender && pSender->m_pParent == &m_wndSetup) )
    {
        m_wndSetup.StopModal();
        DecodeUi::Update();
        return;
    }

    if ( code == ToWord('d', 'l') && (Settings.Decode.protocol == Counter ||
         (Settings.Decode.protocol == Encoder && QuadCounter::Active())) )
    {
        DecodeUi::CounterReset();
        m_itmInfo.Invalidate();
        return;
    }

    // list window
    if ( code == ToWord('d', 'l') && Settings.Decode.protocol != Off )
    {
        DecodeUi::s_bHold = true;
        m_wndList.Create( this );
        m_wndList.StartModal();
        return;
    }
}

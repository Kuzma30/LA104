#include "MenuDecode.h"
#include "DecodeBand.h"
#include "HwCounter.h"
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
static const char* const g_textSource[] = {"Input", "Wave Out"};

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

    float SampleRate()
    {
#if defined(DS203) || defined(DS203HD)
        // FPGA sampling clock 72 MHz divided by (PSC+1)*(ARR+1), the same
        // tables as BIOS::ADC::ConfigureTimebase (200ns .. 1s per division)
        static const uint32_t arrDiv[] = {1, 2, 3, 5, 12, 24, 48, 120, 240, 480, 1200, 2400, 4800,
            12000, 24000, 48000, 120000, 240000, 480000, 1200000, 1000000};
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
        Decoder::Run( Settings.Decode, Sampler::GetBuffer(), Sampler::Samples,
            Settings.Time.InvalidFirst, Sampler::GetValid(), SampleRate() );
        if ( Settings.Decode.protocol == Counter && Settings.Decode.cntSource == SourceSamples &&
             Sampler::GetSequence() != s_nSwSequence )
        {
            // every acquisition is added once, decoding it again with other
            // settings does not count it again
            s_nSwSequence = Sampler::GetSequence();
            s_nSwTotal += Decoder::CountedEdges;
        }
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
    }

    uint64_t CounterTotal()
    {
        if ( HwCounter::Active() )
            return HwCounter::Value();
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

        if ( Settings.Decode.protocol != Counter || !MainWnd.m_wndMenuDecode.IsVisible() )
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
static void CounterText(int nLine, char* text)
{
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
    rc.bottom = rc.top + 50;
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
    static const uint8_t arrParams[ProtocolCount] = {0, 5, 2, 0, 3, 0, 2, 1, 1, 0, 1, 2};
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
    int nMax = BIOS::ADC::NumSamples - CWndGraph::MaxX;
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
    if ( Settings.Decode.protocol == Counter )
    {
        // redraw only the changed values
        static uint32_t nLastHash = 0;
        uint32_t nHash = 5381;
        char text[16];
        for ( int i = 0; i < 3; i++ )
        {
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
    const char* pszList = Settings.Decode.protocol == Counter ? "Reset" : "List";
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

    if ( code == ToWord('d', 'l') && Settings.Decode.protocol == Counter )
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

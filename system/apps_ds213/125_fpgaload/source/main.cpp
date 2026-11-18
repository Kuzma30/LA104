// FPGA loader for the DS203: loads an FPGA image (.BIN) from the disk into
// the FPGA for a test (until power off), writes it as the boot image into
// the internal flash (0x0802C000), reloads the FPGA from the flash and
// saves the boot image into a file.
#include <library.h>
#include "fpga.h"

uint8_t gFatSharedBuffer[BIOS::FAT::SharedBufferSize];
static uint8_t s_page[Fpga::PageSize];

namespace Ui
{
    enum { Width = 400, Height = 240, LineH = 16 };
    const uint16_t clrBack = RGB565(101830), clrText = RGB565(ffffff),
        clrDim = RGB565(a0a0a0), clrOk = RGB565(40ff40), clrErr = RGB565(ff5050),
        clrWarn = RGB565(ffd040), clrSel = RGB565(3060c0), clrTitle = RGB565(2040a0);

    static int s_y = 0;

    void Title(const char* str)
    {
        BIOS::LCD::Bar(0, 0, Width, 18, clrTitle);
        BIOS::LCD::Print(8, 1, clrText, RGBTRANS, str);
    }

    void Clear()
    {
        BIOS::LCD::Bar(0, 18, Width, Height, clrBack);
        s_y = 24;
    }

    void Line(uint16_t clr, const char* fmt, ...)
    {
        char buf[64];
        va_list args;
        va_start(args, fmt);
        vsprintf(buf, fmt, args);
        va_end(args);
        buf[49] = 0;
        if ( s_y > Height - LineH )
        {
            Clear();
        }
        BIOS::LCD::Bar(0, s_y, Width, s_y + LineH, clrBack);
        BIOS::LCD::Print(8, s_y, clr, RGBTRANS, buf);
        s_y += LineH;
    }

    void Progress(int nDone, int nTotal)
    {
        // the last page of WriteFlash passes more than the image size; a bar
        // with right <= left trips the assertion of LCD::Bar in the OS,
        // which halts the device (it stopped at 100 % after a flash write)
        if ( nDone > nTotal )
            nDone = nTotal;
        if ( nDone < 0 )
            nDone = 0;
        int x0 = 8, x1 = Width - 8;
        int x = x0 + (int)((int64_t)(x1 - x0) * nDone / (nTotal ? nTotal : 1));
        if ( x > x0 )
            BIOS::LCD::Bar(x0, s_y + 2, x, s_y + 10, clrOk);
        if ( x < x1 )
            BIOS::LCD::Bar(x, s_y + 2, x1, s_y + 10, RGB565(404040));
    }

    void ProgressEnd()
    {
        s_y += LineH;
    }

    BIOS::KEY::EKey WaitKey()
    {
        BIOS::KEY::EKey key;
        while ( (key = BIOS::KEY::GetKey()) == BIOS::KEY::None )
            BIOS::SYS::DelayMs(10);
        return key;
    }
}

// ---------------------------------------------------------------------------
// file browser: directories and *.BIN

class CBrowser
{
    char mPath[64];
    int mIndex{0}, mScroll{0}, mCount{0};
    enum { Lines = 11, MaxName = 13 };

    static bool _IsBin(const char* name)
    {
        int n = strlen(name);
        return n > 4 && strcmp(name + n - 4, ".BIN") == 0;
    }

    static bool _Show(const BIOS::FAT::TFindFile& f)
    {
        if ( f.strName[0] == '.' && f.strName[1] == 0 )
            return false;
        if ( f.nAtrib & BIOS::FAT::EDirectory )
            return true;
        return _IsBin(f.strName);
    }

    // entry nIndex (0 = ".." outside of the root)
    bool _Get(int nIndex, BIOS::FAT::TFindFile& out)
    {
        bool bRoot = mPath[0] == 0;
        if ( !bRoot )
        {
            if ( nIndex == 0 )
            {
                memset(&out, 0, sizeof(out));
                strcpy(out.strName, "..");
                out.nAtrib = BIOS::FAT::EDirectory;
                return true;
            }
            nIndex--;
        }
        if ( BIOS::FAT::OpenDir(mPath) != BIOS::FAT::EOk )
            return false;
        BIOS::FAT::TFindFile f;
        while ( BIOS::FAT::FindNext(&f) == BIOS::FAT::EOk )
        {
            if ( strcmp(f.strName, "..") == 0 || !_Show(f) )
                continue;
            if ( nIndex-- == 0 )
            {
                out = f;
                return true;
            }
        }
        return false;
    }

    int _Count()
    {
        int n = mPath[0] ? 1 : 0;
        if ( BIOS::FAT::OpenDir(mPath) != BIOS::FAT::EOk )
            return n;
        BIOS::FAT::TFindFile f;
        while ( BIOS::FAT::FindNext(&f) == BIOS::FAT::EOk )
            if ( strcmp(f.strName, "..") != 0 && _Show(f) )
                n++;
        return n;
    }

    void _Paint()
    {
        Ui::Clear();
        BIOS::LCD::Printf(8, 22, Ui::clrDim, RGBTRANS, "/%s", mPath);
        int y = 44;
        for ( int i = mScroll; i < mScroll + Lines && i < mCount; i++, y += Ui::LineH )
        {
            BIOS::FAT::TFindFile f;
            if ( !_Get(i, f) )
                break;
            bool bSel = i == mIndex;
            uint16_t clrB = bSel ? Ui::clrSel : Ui::clrBack;
            BIOS::LCD::Bar(4, y, Ui::Width - 4, y + Ui::LineH, clrB);
            if ( f.nAtrib & BIOS::FAT::EDirectory )
                BIOS::LCD::Printf(8, y, Ui::clrWarn, RGBTRANS, "<%s>", f.strName);
            else
                BIOS::LCD::Printf(8, y, Ui::clrText, RGBTRANS, "%-12s %6d B", f.strName, (int)f.nFileLength);
        }
        if ( mCount == 0 )
            BIOS::LCD::Print(8, y, Ui::clrDim, RGBTRANS, "no .BIN files");
        BIOS::LCD::Bar(0, Ui::Height - 16, Ui::Width, Ui::Height, RGB565(303030));
        BIOS::LCD::Print(8, Ui::Height - 16, Ui::clrDim, RGBTRANS, "Up/Down, F1: open/select, F2: back");
    }

public:
    CBrowser()
    {
        // start in the folder of the application
        strcpy(mPath, BIOS::OS::GetArgument());
        char* last = strrchr(mPath, '/');
        if ( last )
            *last = 0;
        else
            mPath[0] = 0;
    }

    // full path of the selected file, false when cancelled
    bool Select(char* result)
    {
        mCount = _Count();
        if ( mIndex >= mCount )
            mIndex = mScroll = 0;
        _Paint();
        while ( true )
        {
            BIOS::KEY::EKey key = Ui::WaitKey();
            if ( key == BIOS::KEY::Escape )
                return false;
            if ( key == BIOS::KEY::Up && mIndex > 0 )
            {
                mIndex--;
                if ( mIndex < mScroll )
                    mScroll = mIndex;
                _Paint();
            }
            if ( key == BIOS::KEY::Down && mIndex < mCount - 1 )
            {
                mIndex++;
                if ( mIndex >= mScroll + Lines )
                    mScroll = mIndex - Lines + 1;
                _Paint();
            }
            if ( key == BIOS::KEY::Enter && mCount > 0 )
            {
                BIOS::FAT::TFindFile f;
                if ( !_Get(mIndex, f) )
                    continue;
                if ( f.nAtrib & BIOS::FAT::EDirectory )
                {
                    if ( strcmp(f.strName, "..") == 0 )
                    {
                        char* last = strrchr(mPath, '/');
                        if ( last )
                            *last = 0;
                        else
                            mPath[0] = 0;
                    } else if ( strlen(mPath) + strlen(f.strName) + 2 < sizeof(mPath) )
                    {
                        if ( mPath[0] )
                            strcat(mPath, "/");
                        strcat(mPath, f.strName);
                    }
                    mIndex = mScroll = 0;
                    mCount = _Count();
                    _Paint();
                    continue;
                }
                strcpy(result, mPath);
                if ( result[0] )
                    strcat(result, "/");
                strcat(result, f.strName);
                return true;
            }
        }
    }
};

CBrowser gBrowser;

// ---------------------------------------------------------------------------

static void ShowFpga()
{
    bool bDone = Fpga::Done();
    uint16_t nStatus = Fpga::Status();
    int nVer = nStatus >> 6;
    Ui::Line(bDone ? Ui::clrOk : Ui::clrErr, "FPGA: CDONE=%d, version %d.%02d (status %04x)",
        bDone ? 1 : 0, nVer / 100, nVer % 100, nStatus);
    if ( bDone && nVer >= 262 && nVer < 1000 )
    {
        uint16_t nCnt = Fpga::ReadSelected(0x18), nQuad = Fpga::ReadSelected(0x19);
        Ui::Line(Ui::clrDim, "      counters %04x, quadrature %04x", nCnt, nQuad);
    }
}

static void ShowFlashImage()
{
    char comment[48];
    bool bOk = Fpga::ParseHeader((const uint8_t*)Fpga::FlashBase, 512, comment, sizeof(comment));
    Ui::Line(bOk ? Ui::clrText : Ui::clrErr, "Flash image: %s", bOk ? "header ok" : "no FPGA header!");
    if ( comment[0] )
        Ui::Line(Ui::clrDim, "  %s", comment);
}

// opens the file and checks it, the first sector stays in the shared buffer
static bool OpenImage(const char* name, uint32_t& nSize)
{
    if ( BIOS::FAT::Open(name, BIOS::FAT::IoRead) != BIOS::FAT::EOk )
    {
        Ui::Line(Ui::clrErr, "Cannot open %s", name);
        return false;
    }
    nSize = BIOS::FAT::GetFileSize();
    if ( nSize < 1024 || nSize > Fpga::ImageSize )
    {
        Ui::Line(Ui::clrErr, "Size %d B, expected up to %d B", (int)nSize, Fpga::ImageSize);
        BIOS::FAT::Close();
        return false;
    }
    if ( BIOS::FAT::Read(gFatSharedBuffer) != BIOS::FAT::EOk )
    {
        Ui::Line(Ui::clrErr, "Read error");
        BIOS::FAT::Close();
        return false;
    }
    char comment[48];
    if ( !Fpga::ParseHeader(gFatSharedBuffer, BIOS::FAT::SectorSize, comment, sizeof(comment)) )
    {
        Ui::Line(Ui::clrErr, "Not an iCE bitstream (no 7E AA 99 7E)");
        BIOS::FAT::Close();
        return false;
    }
    Ui::Line(Ui::clrText, "%s, %d B", name, (int)nSize);
    if ( comment[0] )
        Ui::Line(Ui::clrDim, "  %s", comment);
    return true;
}

// the FPGA from a file, until power off
static void LoadFile()
{
    char name[80];
    if ( !gBrowser.Select(name) )
        return;
    Ui::Clear();
    uint32_t nSize;
    if ( !OpenImage(name, nSize) )
        return;
    Ui::Line(Ui::clrText, "Loading into the FPGA (test, until power off)");
    Fpga::ConfigBegin();
    uint32_t nSent = 0;
    bool bOk = true;
    while ( nSent < nSize )
    {
        int n = nSize - nSent < (uint32_t)BIOS::FAT::SectorSize ? nSize - nSent : BIOS::FAT::SectorSize;
        Fpga::ConfigData(gFatSharedBuffer, n);
        nSent += n;
        if ( (nSent & 0x1fff) == 0 || nSent == nSize )
            Ui::Progress(nSent, nSize);
        if ( nSent < nSize && BIOS::FAT::Read(gFatSharedBuffer) != BIOS::FAT::EOk )
        {
            bOk = false;
            break;
        }
    }
    bool bDone = Fpga::ConfigEnd();
    BIOS::FAT::Close();
    Ui::ProgressEnd();
    if ( !bOk )
        Ui::Line(Ui::clrErr, "Read error, the FPGA is not configured");
    Ui::Line(bDone ? Ui::clrOk : Ui::clrErr, bDone ? "Configured (CDONE = 1)" : "Configuration failed (CDONE = 0)");
    ShowFpga();
    Ui::Line(Ui::clrDim, "Power off or 'Reload from flash' restores the boot image");
}

// the FPGA from the boot image in the flash
static void ReloadFlash()
{
    Ui::Clear();
    ShowFlashImage();
    Ui::Line(Ui::clrText, "Loading the flash image into the FPGA");
    Fpga::ConfigBegin();
    const uint8_t* p = (const uint8_t*)Fpga::FlashBase;
    for ( int i = 0; i < Fpga::ImageSize; i += 4096 )
    {
        int n = Fpga::ImageSize - i < 4096 ? Fpga::ImageSize - i : 4096;
        Fpga::ConfigData(p + i, n);
        Ui::Progress(i + n, Fpga::ImageSize);
    }
    bool bDone = Fpga::ConfigEnd();
    Ui::ProgressEnd();
    Ui::Line(bDone ? Ui::clrOk : Ui::clrErr, bDone ? "Configured (CDONE = 1)" : "Configuration failed (CDONE = 0)");
    ShowFpga();
}

// the boot image into a file
static void Backup()
{
    Ui::Clear();
    const char* name = "FPGABAK.BIN";
    Ui::Line(Ui::clrText, "Saving the flash image to %s", name);
    if ( BIOS::FAT::Open(name, BIOS::FAT::IoWrite) != BIOS::FAT::EOk )
    {
        Ui::Line(Ui::clrErr, "Cannot create the file");
        return;
    }
    const uint8_t* p = (const uint8_t*)Fpga::FlashBase;
    bool bOk = true;
    for ( int i = 0; bOk && i < Fpga::ImageSize; i += BIOS::FAT::SectorSize )
    {
        int n = Fpga::ImageSize - i < BIOS::FAT::SectorSize ? Fpga::ImageSize - i : BIOS::FAT::SectorSize;
        memset(gFatSharedBuffer, 0, BIOS::FAT::SectorSize);
        memcpy(gFatSharedBuffer, p + i, n);
        bOk = BIOS::FAT::Write(gFatSharedBuffer) == BIOS::FAT::EOk;
        Ui::Progress(i + n, Fpga::ImageSize);
    }
    BIOS::FAT::Close(Fpga::ImageSize);
    Ui::ProgressEnd();
    Ui::Line(bOk ? Ui::clrOk : Ui::clrErr, bOk ? "Saved %d B" : "Write error", Fpga::ImageSize);
}

// the file as the boot image: 68088 bytes (the file, then zeros), page by
// page, the rest of the last page (calibration) is kept
static void WriteFlash()
{
    char name[80];
    if ( !gBrowser.Select(name) )
        return;
    Ui::Clear();
    uint32_t nSize;
    if ( !OpenImage(name, nSize) )
        return;
    Ui::Line(Ui::clrWarn, "Write it as the boot image of the FPGA?");
    Ui::Line(Ui::clrDim, "(a bad image: restore with DFU, CFG_FPGA.ADR + .BIN)");
    Ui::Line(Ui::clrWarn, "F1 = write, other key = cancel");
    if ( Ui::WaitKey() != BIOS::KEY::Enter )
    {
        BIOS::FAT::Close();
        Ui::Line(Ui::clrDim, "Cancelled");
        return;
    }

    // the first sector is already in the shared buffer
    int nInBuf = BIOS::FAT::SectorSize, nBufPos = 0;
    bool bOk = true;
    for ( uint32_t nAddr = Fpga::FlashBase; bOk && nAddr < Fpga::FlashBase + Fpga::ImageSize; nAddr += Fpga::PageSize )
    {
        memcpy(s_page, (const void*)nAddr, Fpga::PageSize);
        for ( int i = 0; i < Fpga::PageSize; i++ )
        {
            uint32_t nOffset = nAddr - Fpga::FlashBase + i;
            if ( nOffset >= (uint32_t)Fpga::ImageSize )
                break;                  // calibration / device info stays
            if ( nOffset >= nSize )
            {
                s_page[i] = 0;          // padding after a shorter image
                continue;
            }
            if ( nBufPos >= nInBuf )
            {
                if ( BIOS::FAT::Read(gFatSharedBuffer) != BIOS::FAT::EOk )
                {
                    bOk = false;
                    break;
                }
                nBufPos = 0;
            }
            s_page[i] = gFatSharedBuffer[nBufPos++];
        }
        if ( !bOk )
        {
            Ui::ProgressEnd();
            Ui::Line(Ui::clrErr, "Read error at %d, flash not changed further", (int)(nAddr - Fpga::FlashBase));
            break;
        }
        if ( !Fpga::FlashPage(nAddr, s_page) )
        {
            bOk = false;
            Ui::ProgressEnd();
            Ui::Line(Ui::clrErr, "Flash error at %08x", (int)nAddr);
            break;
        }
        Ui::Progress(nAddr - Fpga::FlashBase + Fpga::PageSize, Fpga::ImageSize);
    }
    BIOS::FAT::Close();
    if ( bOk )
    {
        Ui::ProgressEnd();
        Ui::Line(Ui::clrOk, "Written and verified");
        // and run it
        Fpga::ConfigBegin();
        Fpga::ConfigData((const uint8_t*)Fpga::FlashBase, Fpga::ImageSize);
        bool bDone = Fpga::ConfigEnd();
        Ui::Line(bDone ? Ui::clrOk : Ui::clrErr, bDone ? "FPGA configured from the flash" : "FPGA configuration failed!");
        ShowFpga();
    } else
        Ui::Line(Ui::clrErr, "The boot image may be broken: write again or use DFU");
}

// ---------------------------------------------------------------------------

static const char* const g_menu[] = {
    "Load file into FPGA (test)",
    "Write file to flash (boot image)",
    "Reload FPGA from flash",
    "Save flash image to FPGABAK.BIN",
    "Exit"
};
enum { MenuCount = 5 };

static void PaintMenu(int nSel)
{
    Ui::Clear();
    ShowFpga();
    ShowFlashImage();
    int y = 24 + 5 * Ui::LineH;
    for ( int i = 0; i < MenuCount; i++, y += 20 )
    {
        uint16_t clrB = i == nSel ? Ui::clrSel : Ui::clrBack;
        BIOS::LCD::Bar(4, y - 2, Ui::Width - 4, y + Ui::LineH + 2, clrB);
        BIOS::LCD::Print(16, y, Ui::clrText, RGBTRANS, g_menu[i]);
    }
    BIOS::LCD::Bar(0, Ui::Height - 16, Ui::Width, Ui::Height, RGB565(303030));
    BIOS::LCD::Print(8, Ui::Height - 16, Ui::clrDim, RGBTRANS, "Up/Down, F1: select, F2: exit");
}

__attribute__((__section__(".entry")))
int _main(void)
{
    BIOS::FAT::SetSharedBuffer(gFatSharedBuffer);
    Ui::Title("FPGA loader (DS203)");

    int nSel = 0;
    PaintMenu(nSel);
    while ( true )
    {
        BIOS::KEY::EKey key = Ui::WaitKey();
        if ( key == BIOS::KEY::Escape )
            break;
        if ( key == BIOS::KEY::Up && nSel > 0 )
            PaintMenu(--nSel);
        if ( key == BIOS::KEY::Down && nSel < MenuCount - 1 )
            PaintMenu(++nSel);
        if ( key != BIOS::KEY::Enter )
            continue;
        if ( nSel == MenuCount - 1 )
            break;
        switch ( nSel )
        {
        case 0: LoadFile(); break;
        case 1: WriteFlash(); break;
        case 2: ReloadFlash(); break;
        case 3: Backup(); break;
        }
        Ui::Line(Ui::clrDim, "Press any key");
        Ui::WaitKey();
        PaintMenu(nSel);
    }

    BIOS::FAT::SetSharedBuffer(nullptr);
    return 0;
}

void _HandleAssertion(const char* file, int line, const char* cond)
{
    BIOS::DBG::Print("Assertion failed in %s [%d]: %s\n", file, line, cond);
    while (1);
}

// PC test of the settings stream (SettingsIo.h) with an emulated file.
//   g++ -Wall -Isource tests/settings_io_test.cpp && ./a.out
#include <stdio.h>
#include <string.h>
#include <vector>
#include <stdint.h>

namespace BIOS { namespace FAT {
    enum EResult { EOk, EDiskError };
    static std::vector<uint8_t> file;
    static size_t readPos = 0;
    const int Sector = 16;   // small sectors: values cross sector boundaries
    EResult Write(uint8_t* p) { file.insert(file.end(), p, p + Sector); return EOk; }
    EResult Read(uint8_t* p)
    {
        for (int i = 0; i < Sector; i++)
            p[i] = readPos < file.size() ? file[readPos++] : 0;
        return EOk;
    }
}}

#include "SettingsIo.h"

typedef TSettingsIo<BIOS::FAT::Sector> TIo;

enum EColor { Red, Green, Blue, ColorMax = Blue };

struct TCfg
{
    EColor color;
    uint8_t mode;
    short pos;
    int value;
    float freq;
    uint8_t wave[37];
};

static void Exchange(TIo& io, TCfg& c)
{
    io.Enum(c.color, ColorMax);
    io.Enum(c.mode, 3);
    io.Int(c.pos, -100, 100);
    io.Int(c.value, 0, 1000000);
    io.Float(c.freq, 0.1f, 1e6f);
    for (auto& b : c.wave)
        io.Raw(b);
}

static uint32_t Save(TCfg& c)
{
    TIo io{};
    io.mode = TIo::Checksum;
    io.nSum = 5381;
    Exchange(io, c);
    uint32_t sum = io.nSum;
    uint16_t len = (uint16_t)io.nLength;

    uint8_t sector[BIOS::FAT::Sector];
    BIOS::FAT::file.clear();
    TIo w{};
    w.mode = TIo::Write;
    w.pSector = sector;
    w.bOk = true;
    uint32_t magic = 0x46434d47;
    w.Raw(magic);
    w.Raw(len);
    w.Raw(sum);
    Exchange(w, c);
    if (w.nPos)
    {
        memset(sector + w.nPos, 0, sizeof(sector) - w.nPos);
        BIOS::FAT::Write(sector);
    }
    BIOS::FAT::file.resize(w.nLength);
    return sum;
}

static bool Load(TCfg& c, TIo::EMode mode)
{
    uint8_t sector[BIOS::FAT::Sector];
    BIOS::FAT::readPos = 0;
    TIo io{};
    io.mode = TIo::Read;
    io.pSector = sector;
    io.nPos = BIOS::FAT::Sector;
    io.bOk = io.bValid = true;
    uint32_t magic = 0, sum = 0;
    uint16_t len = 0;
    io.Raw(magic);
    io.Raw(len);
    io.Raw(sum);
    if (magic != 0x46434d47)
        return false;
    io.mode = mode;
    io.nSum = 5381;
    io.nLength = 0;
    Exchange(io, c);
    return io.bOk && io.nLength == len && io.nSum == sum;
}

static int failures = 0;
static void Check(const char* name, bool ok)
{
    printf("%-28s %s\n", name, ok ? "OK" : "FAIL");
    failures += !ok;
}

int main()
{
    TCfg a{Blue, 2, -42, 123456, 1234.5f, {}};
    for (int i = 0; i < 37; i++) a.wave[i] = (uint8_t)(i * 7);
    Save(a);
    // header 4+2+4, enums 1+1, ints stored as int32 4+4, float 4, wave 37
    Check("file length", BIOS::FAT::file.size() == 10 + 2 + 8 + 4 + 37);

    TCfg b{Red, 0, 0, 0, 1.0f, {}};
    TCfg before = b;
    Check("verify pass", Load(b, TIo::Verify));
    Check("verify keeps values", memcmp(&b, &before, sizeof(b)) == 0);
    Check("read pass", Load(b, TIo::Read));
    Check("round trip", b.color == a.color && b.mode == a.mode && b.pos == a.pos && b.value == a.value &&
        b.freq == a.freq && memcmp(b.wave, a.wave, sizeof(a.wave)) == 0);

    // corrupted byte -> checksum fails
    BIOS::FAT::file[20] ^= 0x55;
    Check("corruption detected", !Load(b, TIo::Verify));
    BIOS::FAT::file[20] ^= 0x55;

    // value out of range in a file with a valid checksum
    TCfg c = a;
    c.mode = 9;           // not allowed (max 3)
    Save(c);
    TCfg d = before;
    Check("bad enum: file loads", Load(d, TIo::Read));
    Check("bad enum keeps default", d.mode == before.mode && d.value == a.value && d.color == a.color);

    // integer out of range
    c = a;
    c.pos = 500;
    Save(c);
    d = before;
    Check("bad int keeps default", Load(d, TIo::Read) && d.pos == before.pos && d.value == a.value);

    // NaN float
    c = a;
    c.freq = 0.0f / 0.0f;
    Save(c);
    d = before;
    Check("NaN keeps default", Load(d, TIo::Read) && d.freq == before.freq && d.value == a.value);

    // truncated file
    Save(a);
    BIOS::FAT::file.resize(BIOS::FAT::file.size() - 5);
    Check("truncated file rejected", !Load(d, TIo::Verify));

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures != 0;
}

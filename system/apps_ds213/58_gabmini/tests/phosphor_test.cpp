// PC test of the persistence history (Oscilloscope/Controls/Phosphor) and of
// the roll window of the sampler, renders preview images (PPM).
//   g++ -O1 -Wall -Isource -Itests/stub tests/phosphor_test.cpp
//       source/Oscilloscope/Controls/Phosphor.cpp && ./a.out [out dir]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <string>
#include "Oscilloscope/Controls/Phosphor.h"

// ---- minimal BIOS for Sampler.cpp
namespace BIOS { namespace ADC {
    enum { NumSamples = 4096 };
    struct TSample { typedef uint32_t SampleType; };
    const uint32_t NoData = 0xffffffff;
    static uint32_t next = 0;
    uint32_t Get() { return next++; }
}}
#define _LIBRARY_STUB
#include "../source/Framework/Sampler.h"
#include "../source/Framework/Sampler.cpp"

static int failures = 0;
static void Check(const char* name, bool ok, const char* detail = "")
{
    printf("%-34s %s %s\n", name, ok ? "OK  " : "FAIL", detail);
    failures += !ok;
}

static uint8_t area[10848];

// ---- image output
struct Image
{
    int w, h;
    uint8_t* p;
    Image(int w_, int h_) : w(w_), h(h_), p(new uint8_t[w_*h_*3]) { memset(p, 0, w*h*3); }
    void Set(int x, int y, uint16_t c)
    {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* q = p + (y*w + x)*3;
        q[0] = ((c >> 11) & 0x1f) << 3; q[1] = ((c >> 5) & 0x3f) << 2; q[2] = (c & 0x1f) << 3;
    }
    void Save(const std::string& name)
    {
        FILE* f = fopen(name.c_str(), "wb");
        if (!f) return;
        fprintf(f, "P6 %d %d 255\n", w, h);
        fwrite(p, 1, w*h*3, f);
        fclose(f);
    }
};

// renders the T-Y column like CWndOscGraph::_PhosphorColumn
static void RenderTY(Image& img, bool graded, uint16_t clr1, uint16_t clr2, bool two)
{
    uint16_t lut1[Phosphor::LutSize], lut2[Phosphor::LutSize];
    Phosphor::MakeLut(clr1, lut1);
    Phosphor::MakeLut(clr2, lut2);
    uint8_t s1[Phosphor::Height], s2[Phosphor::Height];
    for (int x = 0; x < Phosphor::Width; x++)
    {
        for (int y = 0; y < Phosphor::Height; y++)
            img.Set(x, y, ((x % 30) == 0 || (y % 25) == 0) && ((x + y) % 5 == 0) ? 0x8410 : 0x0101);
        Phosphor::SumColumn(x, 0, true, graded, s1);
        if (two)
            Phosphor::SumColumn(x, 1, true, graded, s2);
        else
            memset(s2, 0, sizeof(s2));
        for (int y = 0; y < Phosphor::Height; y++)
        {
            int v1 = s1[y], v2 = s2[y];
            if (!v1 && !v2) continue;
            uint16_t c = graded ? (v1 >= v2 ? lut1[Phosphor::LutIndex(v1)] : lut2[Phosphor::LutIndex(v2)])
                                : (v1 ? clr1 : clr2);
            img.Set(x, Phosphor::Height - 1 - y, c);
        }
    }
}

static double Luma(uint16_t c)
{
    return 0.3 * (((c >> 11) & 0x1f) << 3) + 0.59 * (((c >> 5) & 0x3f) << 2) + 0.11 * ((c & 0x1f) << 3);
}

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : ".";
    char buf[128];

    // capacity: 10848 bytes after one screen -> 16 frames, 2 channels
    bool bNew = true;
    Check("T-Y history fits", Phosphor::BeginTY(area, sizeof(area), 1, bNew) && Phosphor::Frames() == 1);
    Check("too small area refused", !Phosphor::BeginTY(area, 1000, 1, bNew));

    // stable flat trace at y=100 for 20 frames, an excursion at y=150 only in
    // the newest frame and at y=50 in the oldest kept frame
    for (int f = 0; f < 20; f++)
    {
        bNew = true;
        Phosphor::BeginTY(area, sizeof(area), 2, bNew);
        for (int x = 0; x < Phosphor::Width; x++)
        {
            int y = 100;
            if (x == 10 && f == 19) y = 150;
            if (x == 20 && f == 4) y = 50;
            Phosphor::StoreTY(x, 0, y);
            Phosphor::StoreTY(x, 1, Phosphor::None);
        }
    }
    uint8_t sum[Phosphor::Height];
    Phosphor::SumColumn(100, 0, false, true, sum);
    snprintf(buf, sizeof(buf), "sum %d, frames %d", sum[100], Phosphor::Frames());
    Check("stable trace sums all 16 frames", sum[100] == Phosphor::MaxSum && Phosphor::Frames() == 16, buf);
    Phosphor::SumColumn(10, 0, false, true, sum);
    Check("newest excursion weight 16", sum[150] == 16 && sum[100] == Phosphor::MaxSum - 16);
    Phosphor::SumColumn(20, 0, false, true, sum);
    Check("oldest kept frame weight 1", sum[50] == 1);
    Phosphor::SumColumn(10, 0, true, true, sum);
    Check("lines join the columns", sum[125] == 16 && sum[149] == 16);
    Phosphor::SumColumn(10, 0, false, false, sum);
    Check("envelope counts frames", sum[150] == 1 && sum[100] == 15);

    // repaint without a new frame does not change the history
    bNew = false;
    Phosphor::BeginTY(area, sizeof(area), 2, bNew);
    Phosphor::SumColumn(100, 0, false, true, sum);
    Check("repaint keeps history", !bNew && sum[100] == Phosphor::MaxSum);
    // new settings clear it and ask for the frame
    bNew = false;
    Phosphor::BeginTY(area, sizeof(area), 3, bNew);
    Check("key change clears", bNew && Phosphor::Frames() == 1);
    // Clear() by the user
    Phosphor::Clear();
    bNew = false;
    Phosphor::BeginTY(area, sizeof(area), 3, bNew);
    Check("clear asks for a frame", bNew && Phosphor::Frames() == 1);
    // scratch after the frames, inside the area
    uint8_t* sc = Phosphor::Scratch();
    Check("scratch inside area", sc >= area + 16 * 600 && sc + Phosphor::ScratchBytes <= area + sizeof(area));

    // colors: brightness grows with the newest hit, density goes to white
    uint16_t lut[Phosphor::LutSize];
    Phosphor::MakeLut(0xffe0, lut);   // yellow
    bool mono = true;
    for (int i = 2; i < Phosphor::LutSize; i++)
        mono &= Luma(lut[i]) >= Luma(lut[i-1]);
    snprintf(buf, sizeof(buf), "1:%.0f 20:%.0f 31:%.0f", Luma(lut[1]), Luma(lut[20]), Luma(lut[31]));
    Check("LUT monotonic", mono && lut[20] == 0xffe0 && (lut[31] & 0x1f) > 0x10, buf);
    snprintf(buf, sizeof(buf), "1:%d 16:%d 136:%d", Phosphor::LutIndex(1), Phosphor::LutIndex(16), Phosphor::LutIndex(136));
    Check("LUT index", Phosphor::LutIndex(0) == 0 && Phosphor::LutIndex(1) == 2 && Phosphor::LutIndex(16) == 10 &&
        Phosphor::LutIndex(136) == 31 && Phosphor::LutIndex(255) == 31, buf);
    bool idxMono = true;
    for (int i = 1; i <= 255; i++) idxMono &= Phosphor::LutIndex(i) >= Phosphor::LutIndex(i - 1);
    Check("LUT index monotonic", idxMono);
    Check("age index", Phosphor::AgeIndex(0) == 20 && Phosphor::AgeIndex(15) == 1);

    // X-Y
    bNew = true;
    Check("X-Y history fits", Phosphor::BeginXY(area, sizeof(area), 9, bNew));
    for (int f = 0; f < 18; f++)
    {
        uint8_t x[3] = {(uint8_t)f, 1, 2}, y[3] = {3, 4, 5};
        bNew = true;
        Phosphor::BeginXY(area, sizeof(area), 9, bNew);
        Phosphor::StoreXY(3, x, y);
    }
    int n;
    const uint8_t* p0 = Phosphor::FrameXY(0, n);
    const uint8_t* p15 = Phosphor::FrameXY(15, n);
    Check("X-Y frames by age", Phosphor::Frames() == 16 && p0[0] == 17 && p15[0] == 2 && n == 3);

    // ---- roll window of the sampler
    {
        uint32_t s[700];
        for (int i = 0; i < 700; i++) s[i] = (i & 0xff) | ((i * 3 & 0xff) << 8);
        Sampler::SetLength(4096);
        Sampler::RollStart(30);
        Sampler::RollAppend(s, 100, 30, 300);
        bool ok = Sampler::GetValid() == 130 && (Sampler::GetAt(30) & 0xff) == 0 && (Sampler::GetAt(129) & 0xff) == 99
            && Sampler::GetAt(130) == BIOS::ADC::NoData && (Sampler::GetAt(10) & 0xff) == 0x80;
        Sampler::RollAppend(s + 100, 250, 30, 300);   // 350 total, window shows 50..349
        ok &= Sampler::GetValid() == 330 && (Sampler::GetAt(30) & 0xff) == 50 && (Sampler::GetAt(329) & 0xff) == (349 & 0xff);
        Sampler::RollAppend(s + 350, 350, 30, 300);   // more than the window at once
        ok &= Sampler::GetValid() == 330 && (Sampler::GetAt(30) & 0xff) == (400 & 0xff) && (Sampler::GetAt(329) & 0xff) == (699 & 0xff)
            && ((Sampler::GetAt(329) >> 8) & 0xff) == (699 * 3 & 0xff);
        Check("roll window", ok);
        // screen length leaves the history area
        Sampler::SetLength(Sampler::ScreenSamples);
        int nBytes;
        uint8_t* pArea = Sampler::HistoryArea(nBytes);
        Check("history area after one screen", nBytes == (4096 - 480) * 3 && pArea == Sampler::GetBuffer() + 480 * 3);
        BIOS::ADC::next = 0;
        Sampler::Copy();
        Check("copy reads one screen", BIOS::ADC::next == 480 && Sampler::GetValid() == 480);
    }

    // ---- preview images: noisy sine with jitter and a rare glitch
    srand(3);
    const int kFrames = 16;
    for (int mode = 0; mode < 2; mode++)
    {
        Phosphor::Clear();
        for (int f = 0; f < 40; f++)
        {
            bNew = true;
            Phosphor::BeginTY(area, sizeof(area), 77, bNew);
            double jitter = (rand() % 100 - 50) * 0.04;
            for (int x = 0; x < Phosphor::Width; x++)
            {
                double t = (x + jitter) / 300.0 * 2 * M_PI * 2;
                int y1 = 140 + (int)(35 * sin(t)) + (rand() % 5 - 2);
                if (f == 37 && x > 200 && x < 215) y1 = 185;            // rare glitch
                int y2 = 50 + (int)(25 * ((fmod((x + jitter * 3) / 75.0, 1.0) < 0.5) ? 1 : -1)) + (rand() % 3 - 1);
                Phosphor::StoreTY(x, 0, y1);
                Phosphor::StoreTY(x, 1, y2);
            }
        }
        Image img(300, 200);
        RenderTY(img, mode == 1, 0xffe0, 0x07ff, true);
        img.Save(dir + (mode ? "/phosphor_ty.ppm" : "/persist_ty.ppm"));
    }
    // X-Y: Lissajous 3:2 with slowly drifting phase
    {
        Phosphor::Clear();
        Image img(300, 200);
        for (int y = 0; y < 200; y++) for (int x = 0; x < 300; x++)
            img.Set(x, y, (x >= 50 && x < 250 && (((x - 50) % 25 == 0) || (y % 25 == 0)) && (x + y) % 5 == 0) ? 0x4208 : 0x0101);
        for (int f = 0; f < 30; f++)
        {
            bNew = true;
            Phosphor::BeginXY(area, sizeof(area), 5, bNew);
            uint8_t xs[256], ys[256];
            for (int i = 0; i < 256; i++)
            {
                double t = i / 256.0 * 2 * M_PI * 2;
                xs[i] = (uint8_t)(100 + 85 * sin(3 * t + f * 0.02));
                ys[i] = (uint8_t)(100 + 85 * sin(2 * t));
            }
            Phosphor::StoreXY(256, xs, ys);
        }
        uint16_t lutx[Phosphor::LutSize];
        Phosphor::MakeLut(0xfbe0, lutx);
        for (int age = Phosphor::Frames() - 1; age >= 0; age--)
        {
            int np;
            const uint8_t* p = Phosphor::FrameXY(age, np);
            uint16_t c = lutx[Phosphor::AgeIndex(age)];
            for (int i = 1; i < np; i++)
            {
                // line as in CWndOscGraph::_XyLine
                int x1 = p[2*i-2], y1 = p[2*i-1], x2 = p[2*i], y2 = p[2*i+1];
                int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1, dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1, err = dx + dy;
                while (true)
                {
                    img.Set(50 + x1, 199 - y1, c);
                    if (x1 == x2 && y1 == y2) break;
                    int e2 = 2 * err;
                    if (e2 >= dy) { err += dy; x1 += sx; }
                    if (e2 <= dx) { err += dx; y1 += sy; }
                }
            }
        }
        img.Save(dir + "/phosphor_xy.ppm");
    }
    (void)kFrames;

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures != 0;
}

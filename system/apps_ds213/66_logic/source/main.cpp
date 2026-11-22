// Logic analyzer for the DS203 (LA104 OS): 4 channels (A, B against a
// threshold, C, D digital), run length capture in the FPGA (image
// LAFPGA.BIN, loaded for the time the application runs), up to 72 MS/s,
// 4095 changes, trigger with pretrigger, zoom / scroll, cursors, VCD and BMP.
//
// Keys:
//   Up / Down     menu item
//   Left / Right  change the item (View: scroll, Zoom: zoom, X1 / X2: move)
//   F1            run / stop
//   F2            exit (the oscilloscope FPGA image is loaded back)
//   F3            X1 / X2: next edge; otherwise: whole capture
//   F4            save VCD (item BMP: screenshot)

#include <library.h>
#include "fpga.h"
#include "la.h"
#include "../../65_minical/source/calib.h"

using namespace BIOS;

namespace
{
  // ------------------------------------------------------------ colors
  const uint16_t clrBack = RGB565(000000);
  const uint16_t clrBar = RGB565(202060);
  const uint16_t clrText = RGB565(ffffff);
  const uint16_t clrDim = RGB565(808080);
  const uint16_t clrGrid = RGB565(303030);
  const uint16_t clrSel = RGB565(4060ff);
  const uint16_t clrTrig = RGB565(ff4040);
  const uint16_t clrX1 = RGB565(ff8080);
  const uint16_t clrX2 = RGB565(ffa020);
  const uint16_t clrOk = RGB565(40ff40);
  const uint16_t clrWarn = RGB565(ffc040);
  // lanes A, B, C, D (colors of the oscilloscope)
  const uint16_t clrLane[4] = { RGB565(ffff00), RGB565(00ffff), RGB565(ff60ff), RGB565(60ff60) };
  const char* const nameLane[4] = { "A", "B", "C", "D" };
  // state bit of a lane (state: bit 0 C, 1 D, 2 A, 3 B)
  const int bitLane[4] = { 4, 8, 1, 2 };

  // ------------------------------------------------------------ layout
  const int W = 400, H = 240;
  const int TitleH = 16, RulerY = 17, RulerH = 14;
  const int LaneTop = RulerY + RulerH, LaneH = 38, Lanes = 4;
  const int PlotX = 20, PlotW = W - PlotX - 2;        // 378 columns
  const int InfoY = LaneTop + Lanes * LaneH + 1;      // 184
  const int MenuY = InfoY + 15;                       // 199, two rows

  // ------------------------------------------------------------ capture data
  // words, then events in place: 16 KB in the RAM above the OS (DS203 VE)
  __attribute__((section(".bss_hi"))) uint32_t gEv[La::MaxEvents];
  int gEvents = 0;
  uint32_t gEnd = 0, gTrig = 0;         // samples
  float gRate = 1;                      // samples per second of the capture
  bool gHave = false;

  // ------------------------------------------------------------ settings
  // sample rates: time bases of BIOS::ADC::ConfigureTimebase (30 samples / div)
  const float arrTime[] = { 200e-9f, 500e-9f, 1e-6f, 2e-6f, 5e-6f, 10e-6f, 20e-6f, 50e-6f,
    100e-6f, 200e-6f, 500e-6f, 1e-3f, 2e-3f, 5e-3f, 10e-3f, 20e-3f, 50e-3f, 100e-3f,
    200e-3f, 500e-3f, 1.0f };
  const uint32_t arrDiv[] = { 1, 2, 3, 5, 12, 24, 48, 120, 240, 480, 1200, 2400, 4800,
    12000, 24000, 48000, 120000, 240000, 480000, 1200000, 2400000 };
  const int Rates = sizeof(arrDiv) / sizeof(arrDiv[0]);
  int nRate = 2;                        // 24 MS/s

  // trigger: none, then rising / falling of A, B, C, D
  const char* const nameTrig[] = { "none", "A rise", "A fall", "B rise", "B fall",
    "C rise", "C fall", "D rise", "D fall" };
  const int Trigs = 9;
  int nTrig = 5;
  const int arrPre[] = { 10, 25, 50, 75, 90 };
  int nPre = 1;
  int32_t thrMv[2] = { 1400, 1400 };    // A, B
  bool bRepeat = false;

  // view: left edge and zoom in 1/8 samples
  uint32_t viewLeft = 0;                // 1/8 sample
  int zoom = 0;                         // 1/8 samples per column = 2^zoom
  uint32_t cur[2] = { 0, 0 };           // cursors, samples

  enum { ItRate, ItTrig, ItPre, ItThrA, ItThrB, ItMode, ItView, ItZoom, ItX1, ItX2, ItBmp, Items };
  int nItem = ItView;

  enum { Idle, Waiting, Filling } eRun = Idle;
  char status[52] = "";
  uint16_t statusClr = clrText;

  Cal::TData gCal;
  uint8_t gSector[512];

  // ------------------------------------------------------------ text helpers
  char* PutU(char* p, uint32_t n)
  {
    char t[12]; int i = 0;
    do { t[i++] = '0' + n % 10; n /= 10; } while (n);
    while (i) *p++ = t[--i];
    *p = 0;
    return p;
  }

  // 3 significant digits with SI prefix, sign
  char* PutSI(char* p, float v, const char* unit)
  {
    static const char* const pre[] = { "p", "n", "u", "m", "", "k", "M", "G" };
    if (v < 0) { *p++ = '-'; v = -v; }
    int e = 4;
    if (v != 0)
    {
      while (v >= 999.5f && e < 7) { v /= 1000; e++; }
      while (v < 0.9995f && e > 0) { v *= 1000; e--; }
    }
    int dec = v >= 99.95f ? 0 : v >= 9.995f ? 1 : 2;
    int32_t n = (int32_t)(v * (dec == 0 ? 1 : dec == 1 ? 10 : 100) + 0.5f);
    if (dec == 0) p = PutU(p, n);
    else if (dec == 1) { p = PutU(p, n / 10); *p++ = '.'; p = PutU(p, n % 10); }
    else { p = PutU(p, n / 100); *p++ = '.'; *p++ = '0' + (n / 10) % 10; *p++ = '0' + n % 10; *p = 0; }
    strcpy(p, pre[e]); p += strlen(p);
    strcpy(p, unit); p += strlen(p);
    return p;
  }

  char* PutMv(char* p, int32_t mv)
  {
    if (mv < 0) { *p++ = '-'; mv = -mv; }
    p = PutU(p, mv / 1000); *p++ = '.';
    *p++ = '0' + (mv / 100) % 10; *p++ = '0' + (mv / 10) % 10;
    *p++ = 'V'; *p = 0;
    return p;
  }

  void Bar(int x1, int y1, int x2, int y2, uint16_t c)
  {
    // LCD::Bar asserts (halts) on an empty rectangle
    if (x1 < 0) x1 = 0;
    if (x2 > W) x2 = W;
    if (x2 > x1 && y2 > y1)
      LCD::Bar(x1, y1, x2, y2, c);
  }

  int Text(int x, int y, uint16_t c, uint16_t b, const char* s) { return LCD::Print(x, y, c, b, s); }

  // ------------------------------------------------------------ time / view
  float SamplesToSec(int64_t n) { return (float)n / gRate; }
  uint32_t Total8() { return (gEnd ? gEnd : 1) << 3; }
  uint32_t Span8() { return (uint32_t)PlotW << zoom; }

  void ClampView()
  {
    uint32_t total = Total8(), span = Span8();
    if (span >= total) viewLeft = 0;
    else if (viewLeft > total - span) viewLeft = total - span;
  }

  // zoom: 1/8 sample per column (0) .. 2^21 / 8 samples per column (the
  // longest capture, 4095 * 16384 samples, fits in 378 columns at 21)
  enum { ZoomMax = 21 };

  void Fit()
  {
    zoom = 0;
    while (Span8() < Total8() && zoom < ZoomMax) zoom++;
    viewLeft = 0;
  }

  int SampleToX(uint32_t s)
  {
    int64_t d = ((int64_t)s << 3) - viewLeft;
    return (int)(d >> zoom);
  }

  uint32_t XToSample(int x)
  {
    return (uint32_t)(((uint64_t)viewLeft + ((uint64_t)x << zoom)) >> 3);
  }

  // ------------------------------------------------------------ drawing
  void DrawTitle()
  {
    Bar(0, 0, W, TitleH, clrBar);
    char s[60], *p = s;
    strcpy(p, "Logic "); p += 6;
    p = PutSI(p, 72e6f / arrDiv[nRate], "S/s");
    strcpy(p, "  "); p += 2;
    strcpy(p, nameTrig[nTrig]); p += strlen(p);
    int x = 4 + Text(4, 0, clrText, clrBar, s);
    const char* st = eRun == Waiting ? "WAIT" : eRun == Filling ? "TRIG" : gHave ? "STOP" : "";
    uint16_t c = eRun == Waiting ? clrWarn : eRun == Filling ? clrOk : clrTrig;
    if (st[0])
    {
      int w = strlen(st) * 8;
      Bar(W - w - 8, 1, W - 2, TitleH - 1, c);
      Text(W - w - 5, 0, clrBack, RGBTRANS, st);
    }
    if (gHave)
    {
      p = s; p = PutU(p, gEvents); strcpy(p, " ch "); p += 4;
      p = PutSI(p, SamplesToSec(gEnd), "s");
      Text(x + 8, 0, clrDim, RGBTRANS, s);
    }
  }

  // nice step 1-2-5 for about 70 columns
  float RulerStep()
  {
    float colSec = SamplesToSec(1) * (float)(1u << zoom) / 8.0f;
    float want = colSec * 70;
    float d = 1e-9f;
    while (d * 10 <= want) d *= 10;
    if (d * 5 <= want) return d * 5;
    if (d * 2 <= want) return d * 2;
    return d;
  }

  void DrawRuler()
  {
    Bar(0, RulerY, W, RulerY + RulerH, clrBack);
    if (!gHave)
      return;
    float step = RulerStep();
    // times relative to the trigger
    float tLeft = SamplesToSec((int64_t)XToSample(0) - gTrig);
    float tRight = SamplesToSec((int64_t)XToSample(PlotW - 1) - gTrig);
    int k0 = (int)(tLeft / step) - 1, k1 = (int)(tRight / step) + 1;
    for (int k = k0; k <= k1; k++)
    {
      float t = k * step;
      int64_t s = (int64_t)(t * gRate + 0.5f) + gTrig;
      if (s < 0) continue;
      int x = SampleToX((uint32_t)s);
      if (x < 0 || x >= PlotW) continue;
      char str[16];
      PutSI(str, t, "s");
      Bar(PlotX + x, RulerY + RulerH - 3, PlotX + x + 1, RulerY + RulerH, clrDim);
      int w = strlen(str) * 8;
      int tx = PlotX + x - w / 2;
      if (tx < 0) tx = 0;
      if (tx + w > W) tx = W - w;
      Text(tx, RulerY - 2, clrDim, RGBTRANS, str);
    }
  }

  // column x of the lane: state and whether it changes inside the column
  void DrawLane(int lane)
  {
    int y0 = LaneTop + lane * LaneH;
    int yHi = y0 + 6, yLo = y0 + LaneH - 8;
    Bar(0, y0, W, y0 + LaneH, clrBack);
    Bar(0, y0 + LaneH - 1, W, y0 + LaneH, clrGrid);
    Text(4, y0 + 11, clrLane[lane], RGBTRANS, nameLane[lane]);
    if (!gHave || gEvents == 0)
      return;
    int bit = bitLane[lane];
    uint16_t c = clrLane[lane];
    int i = La::Find(gEv, gEvents, XToSample(0));
    int segStart = 0, segLevel = -1;
    for (int x = 0; x <= PlotW; x++)
    {
      int level = -1;
      bool edge = false;
      if (x < PlotW)
      {
        uint32_t s0 = XToSample(x), s1 = XToSample(x + 1);
        if (s1 > s0) s1--;
        if (s0 >= gEnd) { level = -2; }
        else
        {
          while (i + 1 < gEvents && La::Time(gEv[i + 1]) <= s0) i++;
          level = (La::State(gEv[i]) & bit) ? 1 : 0;
          // changes of this bit within the column
          for (int j = i + 1; j < gEvents && La::Time(gEv[j]) <= s1; j++)
            if (((La::State(gEv[j]) & bit) ? 1 : 0) != level) { edge = true; break; }
        }
      }
      if (level != segLevel || edge || x == PlotW)
      {
        // finish the horizontal segment
        if (segLevel >= 0 && x > segStart)
        {
          int y = segLevel ? yHi : yLo;
          Bar(PlotX + segStart, y, PlotX + x, y + 2, c);
        }
        // vertical line at a level change between columns or inside a column
        if (x < PlotW && level >= 0 && (edge || (segLevel >= 0 && level != segLevel)))
          Bar(PlotX + x, yHi, PlotX + x + 1, yLo + 2, c);
        segStart = x;
        segLevel = level;
        if (edge)
        {
          // the level at the end of the column
          uint32_t s1 = XToSample(x + 1);
          int j = La::Find(gEv, gEvents, s1 > 0 ? s1 - 1 : 0);
          segLevel = (La::State(gEv[j]) & bit) ? 1 : 0;
          segStart = x + 1;
        }
      }
    }
  }

  void DrawMarkers()
  {
    if (!gHave)
      return;
    int xs[3] = { SampleToX(gTrig), SampleToX(cur[0]), SampleToX(cur[1]) };
    uint16_t cs[3] = { clrTrig, clrX1, clrX2 };
    for (int m = 0; m < 3; m++)
    {
      int x = xs[m];
      if (x < 0 || x >= PlotW) continue;
      for (int y = LaneTop; y < InfoY - 1; y += 4)
        Bar(PlotX + x, y, PlotX + x + 1, y + 2, cs[m]);
    }
  }

  void DrawInfo()
  {
    Bar(0, InfoY, W, InfoY + 14, clrBar);
    if (!gHave)
    {
      Text(4, InfoY - 1, clrDim, RGBTRANS, "F1 run   inputs: A, B (threshold), C, D");
      return;
    }
    char s[64], *p = s;
    float t1 = SamplesToSec((int64_t)cur[0] - gTrig), t2 = SamplesToSec((int64_t)cur[1] - gTrig);
    int x = 4;
    p = s; strcpy(p, "X1 "); p = PutSI(p + 3, t1, "s");
    x += Text(x, InfoY - 1, clrX1, RGBTRANS, s) + 8;
    p = s; strcpy(p, "X2 "); p = PutSI(p + 3, t2, "s");
    x += Text(x, InfoY - 1, clrX2, RGBTRANS, s) + 8;
    float dt = t2 - t1;
    p = s; strcpy(p, "dX "); p = PutSI(p + 3, dt, "s");
    if (dt != 0) { strcpy(p, " "); p = PutSI(p + 1, 1.0f / (dt < 0 ? -dt : dt), "Hz"); }
    Text(x, InfoY - 1, clrText, RGBTRANS, s);
  }

  void ItemText(int it, char* s)
  {
    char* p = s;
    switch (it)
    {
      case ItRate: PutSI(p, 72e6f / arrDiv[nRate], "S/s"); break;
      case ItTrig: strcpy(p, nameTrig[nTrig]); break;
      case ItPre: strcpy(p, "Pre "); p = PutU(p + 4, arrPre[nPre]); strcpy(p, "%"); break;
      case ItThrA: strcpy(p, "A "); PutMv(p + 2, thrMv[0]); break;
      case ItThrB: strcpy(p, "B "); PutMv(p + 2, thrMv[1]); break;
      case ItMode: strcpy(p, bRepeat ? "Repeat" : "Single"); break;
      case ItView: strcpy(p, "View"); break;
      case ItZoom: strcpy(p, "Zoom"); break;
      case ItX1: strcpy(p, "X1"); break;
      case ItX2: strcpy(p, "X2"); break;
      case ItBmp: strcpy(p, "BMP"); break;
    }
  }

  void DrawMenu()
  {
    Bar(0, MenuY, W, H, clrBack);
    // row 1: settings, row 2: view items
    int x = 3, y = MenuY;
    for (int it = 0; it < Items; it++)
    {
      if (it == ItView) { x = 3; y = MenuY + 14; }
      char s[24];
      ItemText(it, s);
      int w = strlen(s) * 8;
      bool sel = it == nItem;
      if (sel) Bar(x - 2, y + 1, x + w + 2, y + 14, clrSel);
      Text(x, y, sel ? clrText : clrDim, RGBTRANS, s);
      x += w + 6;
    }
    // status at the right of row 2
    int w = strlen(status) * 8;
    if (w)
      Text(W - w - 2, MenuY + 14, statusClr, RGBTRANS, status);
  }

  void Status(uint16_t c, const char* s)
  {
    statusClr = c;
    strncpy(status, s, sizeof(status) - 1);
    status[sizeof(status) - 1] = 0;
    DrawMenu();
  }

  void DrawPlot()
  {
    DrawRuler();
    for (int l = 0; l < Lanes; l++)
      DrawLane(l);
    DrawMarkers();
    DrawInfo();
  }

  void DrawAll()
  {
    DrawTitle();
    DrawPlot();
    DrawMenu();
  }

  // ------------------------------------------------------------ analog inputs
  // 1 V/div up to 6 V, 2 V/div up to 13 V, else 5 V/div; 0 V at ADC 24
  int RangeFor(int32_t mv)
  {
    int32_t a = mv < 0 ? -mv : mv;
    return a <= 6000 ? 4 : a <= 13000 ? 5 : 6;
  }

  struct TAnalog { int range, pos, thr, hys; };

  TAnalog Analog(int ch)
  {
    TAnalog a;
    a.range = RangeFor(thrMv[ch]);
    const Cal::TCurve& c = gCal.c[ch][a.range];
    // position with the zero nearest to ADC 24
    a.pos = 0;
    float best = 1e9f;
    for (int p = 0; p < 256; p++)
    {
      float d = Cal::Zero(c, p) - 24.0f;
      if (d < 0) d = -d;
      if (d < best) { best = d; a.pos = p; }
    }
    // raw = zero + V * 65536 / (K * V/div)
    float perVolt = 65536.0f / (c.k * (Cal::RangeMv[a.range] / 1000.0f));
    float raw = Cal::Zero(c, a.pos) + thrMv[ch] / 1000.0f * perVolt;
    a.thr = (int)(raw + 0.5f);
    if (a.thr < 9) a.thr = 9;
    if (a.thr > 246) a.thr = 246;
    // hysteresis about 0.1 V, at least 2 steps
    a.hys = (int)(0.1f * perVolt + 0.5f);
    if (a.hys < 2) a.hys = 2;
    if (a.hys > 8) a.hys = 8;
    return a;
  }

  void LoadCalibration()
  {
    Cal::Default(gCal);
    if (FAT::Open(Cal::FileName, FAT::IoRead) == FAT::EOk)
    {
      if (FAT::Read(gSector) == FAT::EOk)
        Cal::Read(gCal, gSector);
      FAT::Close();
    }
  }

  // ------------------------------------------------------------ FPGA image
  const char* const ImageFile = "LAFPGA.BIN";
  enum { LaVersion = 901 };

  bool LoadImage()
  {
    if (FAT::Open(ImageFile, FAT::IoRead) != FAT::EOk)
      return false;
    uint32_t size = FAT::GetFileSize();
    if (size < 30000 || size > 140000)
    {
      FAT::Close();
      return false;
    }
    Fpga::ConfigBegin();
    uint32_t sent = 0;
    bool ok = true;
    while (sent < size && ok)
    {
      if (FAT::Read(gSector) != FAT::EOk) { ok = false; break; }
      int n = size - sent < 512 ? (int)(size - sent) : 512;
      Fpga::ConfigData(gSector, n);
      sent += n;
    }
    bool done = Fpga::ConfigEnd();
    FAT::Close();
    return ok && done && (Fpga::Status() >> 6) == LaVersion;
  }

  // the oscilloscope image back from the flash
  void RestoreImage()
  {
    Fpga::ConfigBegin();
    const uint8_t* p = (const uint8_t*)Fpga::FlashBase;
    for (int i = 0; i < Fpga::ImageSize; i += 4096)
      Fpga::ConfigData(p + i, Fpga::ImageSize - i < 4096 ? Fpga::ImageSize - i : 4096);
    Fpga::ConfigEnd();
  }

  // ------------------------------------------------------------ capture
  int gPreWords = 0;

  void StartCapture()
  {
    TAnalog a = Analog(0), b = Analog(1);
    ADC::Enable(true);
    ADC::ConfigureInput(ADC::EInput::CH1, ADC::ECouple::DC, (ADC::EResolution)a.range, a.pos * 4);
    ADC::ConfigureInput(ADC::EInput::CH2, ADC::ECouple::DC, (ADC::EResolution)b.range, b.pos * 4);
    ADC::ConfigureTimebase(arrTime[nRate]);

    // trigger mode: (source << 3) | 1 rising / 0 falling; 0x20 none
    int mode = 0x20, vthr = 128;
    if (nTrig > 0)
    {
      int src = (nTrig - 1) / 2;        // 0 A, 1 B, 2 C, 3 D
      bool rise = ((nTrig - 1) & 1) == 0;
      mode = (src << 3) | (rise ? 1 : 0);
      if (src == 0) vthr = a.thr;
      if (src == 1) vthr = b.thr;
    }
    gPreWords = nTrig > 0 ? 4094 * arrPre[nPre] / 100 : 0;
    // accept the trigger after the pretrigger words or 0.2 s at the latest
    float rate = 72e6f / arrDiv[nRate];
    uint32_t arm = (uint32_t)(rate * 0.2f / 1024.0f) + 1;
    if (arm > 0x7fff) arm = 0x7fff;

    Fpga::SetParam(0x00, mode);         // resets Depth / PerCnt / Delay
    Fpga::SetParam(0x01, vthr);
    Fpga::SetParam(0x06, 4094 & 255);   // Depth 4094: 4095 words are read
    Fpga::SetParam(0x07, 4094 >> 8);
    Fpga::SetParam(0x08, gPreWords & 255);
    Fpga::SetParam(0x09, gPreWords >> 8);
    Fpga::SetParam(0x10, a.thr);
    Fpga::SetParam(0x11, a.hys);
    Fpga::SetParam(0x12, b.thr);
    Fpga::SetParam(0x13, b.hys);
    Fpga::SetParam(0x14, arm & 255);
    Fpga::SetParam(0x15, arm >> 8);
    ADC::Restart(0);
    eRun = Waiting;
    DrawTitle();
  }

  // pointers while the capture may still run: two equal reads
  uint16_t ReadStable(uint8_t sel)
  {
    uint16_t v = Fpga::ReadSelected(sel), w;
    for (int i = 0; i < 4; i++)
    {
      w = Fpga::ReadSelected(sel);
      if (w == v) break;
      v = w;
    }
    return v;
  }

  // reads the words (full, or up to the write pointer when stopped)
  bool ReadCapture()
  {
    uint16_t r50 = ReadStable(0x50), r51 = ReadStable(0x51);
    int bptr = r50 & 0xfff, wptr = r51 & 0xfff;
    bool wrapped = r50 & 0x8000, full = r51 & 0x8000, ready = r51 & 0x4000;
    if (!ready)
      return false;
    int n = full ? La::MaxWords : ((wptr - (bptr - gPreWords)) & 0xfff);
    int skip = (!wrapped && bptr < gPreWords) ? gPreWords - bptr : 0;
    if (n <= skip + 1)
      return false;
    ADC::Get();                         // the output register: previous word
    int m = 0;
    for (int i = 0; i < n; i++)
    {
      uint32_t v = ADC::Get();
      if (i >= skip)
        gEv[m++] = (((v >> 16) & 3) << 16) | (v & 0xffff);
    }
    uint32_t trig;
    gEvents = La::Decode(gEv, m, gPreWords + 1 - skip, gEv, gEnd, trig);
    // the triggering edge is one sample before the trigger word
    gTrig = nTrig > 0 && trig > 0 ? trig - 1 : 0;
    gRate = 72e6f / arrDiv[nRate];
    gHave = true;
    return true;
  }

  void FinishCapture(bool stopped)
  {
    bool ok = ReadCapture();
    eRun = Idle;
    if (ok)
    {
      Fit();
      // cursors around the trigger
      cur[0] = gTrig;
      cur[1] = gTrig + (gEnd - gTrig) / 4;
      Status(clrOk, stopped ? "stopped" : "");
    } else
      Status(clrWarn, "no trigger");
    DrawTitle();
    DrawPlot();
  }

  void PollCapture()
  {
    // status word: bit 5 Start, 4 Empty, 3 Full, 2 Ready, 1..0 data
    uint16_t st = Fpga::Status();
    bool full = st & 0x0008, ready = st & 0x0004;
    if (eRun == Waiting && ready)
    {
      eRun = Filling;
      DrawTitle();
    }
    if (full)
    {
      FinishCapture(false);
      if (bRepeat)
        StartCapture();
    }
  }

  // ------------------------------------------------------------ files
  struct TWriter
  {
    int n = 0;
    uint32_t total = 0;
    bool ok = true;
    void Put(char c)
    {
      gSector[n++] = (uint8_t)c;
      total++;
      if (n == 512) Flush();
    }
    void Str(const char* s) { while (*s) Put(*s++); }
    void Flush()
    {
      if (n && FAT::Write(gSector) != FAT::EOk) ok = false;
      n = 0;
    }
    void U64(uint64_t v)
    {
      char t[21]; int i = 0;
      do { t[i++] = '0' + (int)(v % 10); v /= 10; } while (v);
      while (i) Put(t[--i]);
    }
    void Le16(uint16_t v) { Put(v & 0xff); Put(v >> 8); }
    void Le32(uint32_t v) { Le16(v & 0xffff); Le16(v >> 16); }
  };

  // first free name PREFIXnnn.EXT
  bool OpenNew(char* name, const char* prefix, const char* ext)
  {
    for (int i = 0; i < 1000; i++)
    {
      char* p = name;
      strcpy(p, prefix); p += strlen(p);
      *p++ = '0' + i / 100; *p++ = '0' + (i / 10) % 10; *p++ = '0' + i % 10;
      *p++ = '.'; strcpy(p, ext);
      if (FAT::Open(name, FAT::IoRead) == FAT::EOk)
      {
        FAT::Close();
        continue;
      }
      return FAT::Open(name, FAT::IoWrite) == FAT::EOk;
    }
    return false;
  }

  bool Finish(TWriter& w)
  {
    uint32_t len = w.total;
    if (w.n)
    {
      memset(gSector + w.n, 0, 512 - w.n);
      w.n = 512;
      w.Flush();
    }
    return FAT::Close(len) == FAT::EOk && w.ok;
  }

  // Value Change Dump: time in ps, the trigger at 0 ns is marked by $comment
  void SaveVcd()
  {
    if (!gHave) { Status(clrWarn, "no capture"); return; }
    char name[16];
    if (!OpenNew(name, "LA", "VCD")) { Status(clrTrig, "file error"); return; }
    TWriter w;
    uint64_t ps = (uint64_t)(1e12f / gRate + 0.5f);
    w.Str("$comment LA104 logic analyzer, DS203 $end\n$comment trigger at #");
    w.U64(ps * gTrig);
    w.Str(" $end\n$timescale 1ps $end\n$scope module la $end\n");
    const char ids[4] = { '!', '"', '#', '$' };
    for (int l = 0; l < 4; l++)
    {
      w.Str("$var wire 1 "); w.Put(ids[l]); w.Put(' '); w.Str(nameLane[l]); w.Str(" $end\n");
    }
    w.Str("$upscope $end\n$enddefinitions $end\n");
    int last = -1;
    for (int i = 0; i < gEvents && w.ok; i++)
    {
      int s = La::State(gEv[i]);
      w.Put('#'); w.U64(ps * La::Time(gEv[i])); w.Put('\n');
      for (int l = 0; l < 4; l++)
      {
        int b = bitLane[l];
        if (last < 0 || ((s ^ last) & b))
        {
          w.Put((s & b) ? '1' : '0'); w.Put(ids[l]); w.Put('\n');
        }
      }
      last = s;
    }
    w.Put('#'); w.U64(ps * gEnd); w.Put('\n');
    bool ok = Finish(w);
    char s[40];
    strcpy(s, ok ? name : "save error");
    Status(ok ? clrOk : clrTrig, s);
  }

  void SaveBmp()
  {
    char name[16];
    if (!OpenNew(name, "LA", "BMP")) { Status(clrTrig, "file error"); return; }
    TWriter w;
    const uint32_t nHeader = 14 + 40 + 12, nData = W * H * 2;
    w.Put('B'); w.Put('M'); w.Le32(nHeader + nData); w.Le32(0); w.Le32(nHeader);
    w.Le32(40); w.Le32(W); w.Le32(H); w.Le16(1); w.Le16(16); w.Le32(3); w.Le32(nData);
    w.Le32(2835); w.Le32(2835); w.Le32(0); w.Le32(0);
    w.Le32(0xf800); w.Le32(0x07e0); w.Le32(0x001f);
    for (int y = H - 1; y >= 0 && w.ok; y--)
      for (int x = 0; x < W; x++)
        w.Le16(LCD::GetPixel(x, y));
    bool ok = Finish(w);
    Status(ok ? clrOk : clrTrig, ok ? name : "save error");
  }

  // ------------------------------------------------------------ keys
  KEY::EKey lastKey = KEY::None;
  uint32_t lastTick = 0;
  int repeat = 0;

  int Accel() { return repeat < 4 ? 1 : repeat < 12 ? 4 : repeat < 30 ? 16 : 64; }

  void MoveCursor(int c, int d)
  {
    int64_t s = (int64_t)cur[c] + (int64_t)d * Accel() * (((int64_t)1 << zoom) > 8 ? ((int64_t)1 << zoom) / 8 : 1);
    if (s < 0) s = 0;
    if (s > (int64_t)gEnd) s = gEnd;
    cur[c] = (uint32_t)s;
    // keep it visible
    int x = SampleToX(cur[c]);
    if (x < 0 || x >= PlotW)
    {
      int64_t left = ((int64_t)cur[c] << 3) - ((int64_t)PlotW << zoom) / 2;
      viewLeft = left < 0 ? 0 : (uint32_t)left;
      ClampView();
    }
  }

  void Adjust(int d)
  {
    switch (nItem)
    {
      case ItRate: nRate = max(0, min(Rates - 1, nRate - d)); break;   // right: faster
      case ItTrig: nTrig = (nTrig + d + Trigs) % Trigs; break;
      case ItPre: nPre = max(0, min(4, nPre + d)); break;
      case ItThrA: case ItThrB:
      {
        int32_t& t = thrMv[nItem - ItThrA];
        t = max((int32_t)-500, min((int32_t)30000, t + d * 50 * (repeat < 8 ? 1 : 10)));
        break;
      }
      case ItMode: bRepeat = !bRepeat; break;
      case ItView:
      {
        int64_t step = (int64_t)(PlotW / 8) << zoom;
        int64_t v = (int64_t)viewLeft + d * step * (repeat < 6 ? 1 : 4);
        viewLeft = v < 0 ? 0 : (uint32_t)v;
        ClampView();
        DrawPlot();
        return;
      }
      case ItZoom:
      {
        // around the middle of the view
        uint32_t mid = viewLeft + (Span8() / 2);
        zoom = max(0, min((int)ZoomMax, zoom - d));
        int64_t left = (int64_t)mid - Span8() / 2;
        viewLeft = left < 0 ? 0 : (uint32_t)left;
        ClampView();
        DrawPlot();
        return;
      }
      case ItX1: case ItX2:
        if (gHave) { MoveCursor(nItem - ItX1, d); DrawPlot(); }
        return;
      default:
        return;
    }
    DrawTitle();
    DrawMenu();
  }

  void NextEdge()
  {
    int c = nItem - ItX1;
    int i = La::NextEdge(gEv, gEvents, cur[c], 15);
    if (i < 0) { Status(clrDim, "no more edges"); return; }
    cur[c] = La::Time(gEv[i]);
    MoveCursor(c, 0);
    DrawPlot();
  }

  // false: exit
  bool OnKey(KEY::EKey key)
  {
    uint32_t now = SYS::GetTick();
    repeat = key == lastKey && now - lastTick < 300 ? repeat + 1 : 0;
    lastKey = key;
    lastTick = now;
    switch (key)
    {
      case KEY::Up: nItem = (nItem + Items - 1) % Items; DrawMenu(); break;
      case KEY::Down: nItem = (nItem + 1) % Items; DrawMenu(); break;
      case KEY::Left: Adjust(-1); break;
      case KEY::Right: Adjust(1); break;
      case KEY::F1:
        if (eRun == Idle)
        {
          Status(clrText, "");
          StartCapture();
        } else
        {
          bool rep = bRepeat;
          bRepeat = false;
          FinishCapture(true);
          bRepeat = rep;
        }
        break;
      case KEY::F3:
        if (!gHave) break;
        if (nItem == ItX1 || nItem == ItX2) NextEdge();
        else { Fit(); DrawPlot(); }
        break;
      case KEY::F4:
        if (nItem == ItBmp) SaveBmp(); else SaveVcd();
        break;
      case KEY::Escape:
        return false;
      default:
        break;
    }
    return true;
  }
}

#ifdef _ARM
__attribute__((__section__(".entry")))
#endif
int _main(void)
{
  LCD::Clear(clrBack);
  Text(8, 100, clrText, RGBTRANS, "Loading LAFPGA.BIN into the FPGA...");
  if (!LoadImage())
  {
    // whatever happened, the oscilloscope image back
    RestoreImage();
    LCD::Clear(clrBack);
    Text(8, 90, clrTrig, RGBTRANS, "LAFPGA.BIN missing or not the logic");
    Text(8, 106, clrTrig, RGBTRANS, "analyzer image (version 9.01).");
    Text(8, 130, clrDim, RGBTRANS, "Copy it to the disk. Any key: exit");
    while (KEY::GetKey() == KEY::None);
    return 0;
  }
  LoadCalibration();
  LCD::Clear(clrBack);
  DrawAll();

  while (true)
  {
    KEY::EKey key = KEY::GetKey();
    if (key != KEY::None && !OnKey(key))
      break;
    if (eRun != Idle)
      PollCapture();
  }
  RestoreImage();
  return 0;
}

void _HandleAssertion(const char* file, int line, const char* cond)
{
  BIOS::DBG::Print("Assertion failed in ");
  BIOS::DBG::Print(file);
  BIOS::DBG::Print(" [%d]: %s\n", line, cond);
  while (1);
}

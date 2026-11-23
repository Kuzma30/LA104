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
//   F3            X1 / X2: next edge; Zoom: X1..X2 on the screen / whole
//                 capture; Dec: decoder setup; List: decoded frames;
//                 otherwise: whole capture
//   F4            save VCD (BMP: screenshot, List / Dec: decoded frames,
//                 X1 / X2: X1..X2 on the screen / whole capture)
//
// Protocol decoders (decode.h), those of the oscilloscope: UART, LIN, I2C,
// SPI, 1-Wire, CAN, WS2812, NEC IR, PS/2, pulses, counter, encoder on any
// lanes; the frames are drawn under the waveform of their lane and listed
// (List). Protocol trigger (Trig: Decoder): a condition on the frames,
// searched after every capture; without a match the analyzer captures again.

#include <library.h>
#include "fpga.h"
#include "la.h"
#include "decode.h"
#include "gen.h"
#include "../../65_minical/source/calib.h"

using namespace BIOS;

#ifdef _ARM
extern "C" char _addressRamEnd;         // end of .bss (app_ds203.lds); the stack ends above .got
#endif

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

  // ------------------------------------------------------------ decoders
  enum { MaxFrames = 1024 };
  Dec::TFrame gFrames[MaxFrames];
  int gFrameN = 0;
  Dec::TStats gStats;
  bool gFrameOvf = false;
  char gDecNote[40] = "";

  Dec::TCfg dc;
  const char* const DecFile = "LOGICDEC.CFG";
  void DecodeAndShow();
  // generator (Wave Out): defined with the generator setup
  extern Gen::TCfg gc;
  bool gSqOn = false;                   // square wave running
  uint32_t gFireAt = 0;                 // tick of the automatic shot (0: none)

  // protocol trigger: the capture matched (gTrig is the frame), retries
  bool gDecMatch = false;
  uint32_t gRetries = 0;

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
  // the last: the protocol trigger of the decoder (setup page)
  const char* const nameTrig[] = { "none", "A rise", "A fall", "B rise", "B fall",
    "C rise", "C fall", "D rise", "D fall", "Decoder" };
  const int Trigs = 10;
  enum { TrigDec = 9 };
  int nTrig = 5;
  bool DecTrigOn()
  {
    return nTrig == TrigDec && dc.proto > Dec::POff && Dec::Cond(dc).id != 0;
  }
  const int arrPre[] = { 10, 25, 50, 75, 90 };
  int nPre = 1;
  int32_t thrMv[2] = { 1400, 1400 };    // A, B
  bool bRepeat = false;

  // channels on (FPGA 9.02 register 0x16; state bits: 1 C, 2 D, 4 A, 8 B):
  // the changes of a channel that is off take no words, the 4095 words go
  // to the channels that are on
  const uint8_t arrLanes[] = { 15, 7, 11, 13, 14, 3, 12, 5, 6, 9, 10, 1, 2, 4, 8 };
  const int LaneSets = sizeof(arrLanes);
  int nLanes = 0;                       // index of arrLanes
  int gLaVersion = 0;                   // of the loaded image
  uint8_t gCapMask = 15;                // channels on in the capture shown

  // view: left edge and zoom in 1/8 samples
  uint32_t viewLeft = 0;                // 1/8 sample
  uint32_t scale = 1;                   // 1/8 samples per column (any, not only 2^n)
  bool gZoomX = false;                  // the view is X1 .. X2 (Zoom F3 again: all)
  uint32_t cur[2] = { 0, 0 };           // cursors, samples

  enum { ItRate, ItTrig, ItPre, ItThrA, ItThrB, ItMode, ItLanes, ItView, ItZoom, ItX1, ItX2, ItDec, ItList, ItGen, ItBmp, Items };
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
  uint32_t Span8() { return (uint32_t)PlotW * scale; }

  void ClampView()
  {
    uint32_t total = Total8(), span = Span8();
    if (span >= total) viewLeft = 0;
    else if (viewLeft > total - span) viewLeft = total - span;
  }

  // zoom: 1/8 sample per column (8 columns per sample) .. 2^21 / 8 samples
  // per column (the longest capture, 4095 * 16384 samples, fits in 378
  // columns)
  const uint32_t ScaleMax = 1u << 21;

  // n / d rounded up
  uint32_t CeilDiv(uint64_t n, uint32_t d) { return (uint32_t)((n + d - 1) / d); }

  // the whole capture exactly on the screen
  void Fit()
  {
    scale = CeilDiv(Total8(), PlotW);
    if (scale < 1) scale = 1;
    if (scale > ScaleMax) scale = ScaleMax;
    viewLeft = 0;
    gZoomX = false;
  }

  int SampleToX(uint32_t s)
  {
    int64_t d = ((int64_t)s << 3) - viewLeft;
    // rounded down also for negative d
    return (int)(d >= 0 ? d / scale : -((-d + scale - 1) / scale));
  }

  uint32_t XToSample(int x)
  {
    return (uint32_t)(((uint64_t)viewLeft + (uint64_t)x * scale) >> 3);
  }

  // the samples [a, b) on the whole screen
  void ShowRange(uint32_t a, uint32_t b)
  {
    if (b <= a) b = a + 1;
    scale = CeilDiv((uint64_t)(b - a) << 3, PlotW);
    if (scale < 1) scale = 1;
    if (scale > ScaleMax) scale = ScaleMax;
    // centered when the range is shorter than the screen (8 columns per sample)
    int64_t left = ((int64_t)a << 3) - ((int64_t)PlotW * scale - ((int64_t)(b - a) << 3)) / 2;
    viewLeft = left < 0 ? 0 : (uint32_t)left;
    ClampView();
  }

  // the view centered on sample t (the zoom stays)
  void CenterOn(uint32_t t)
  {
    int64_t left = ((int64_t)t << 3) - (int64_t)Span8() / 2;
    viewLeft = left < 0 ? 0 : (uint32_t)left;
    ClampView();
  }

  // ------------------------------------------------------------ drawing
  void DrawTitle()
  {
    Bar(0, 0, W, TitleH, clrBar);
    char s[60], *p = s;
    strcpy(p, "Logic "); p += 6;
    p = PutSI(p, 72e6f / arrDiv[nRate], "S/s");
    strcpy(p, "  "); p += 2;
    if (nTrig == TrigDec && dc.proto > Dec::POff)
    {
      strcpy(p, Dec::ProtoName[(int)dc.proto]); p += strlen(p); *p++ = ' ';
      strcpy(p, Dec::Cond(dc).name);
    }
    else
      strcpy(p, nameTrig[nTrig]);
    p += strlen(p);
    // the generator is working or fires with every capture
    if (gSqOn || (gc.autoFire && gc.mode != Gen::GOff && gc.mode != Gen::GSquare))
    {
      strcpy(p, " +Gen");
      p += 5;
    }
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

  // ruler label: 3 digits without trailing zeros (20us, 1.5ms, 0)
  void RulerLabel(char* str, float t)
  {
    if (t > -1e-12f && t < 1e-12f) { strcpy(str, "0"); return; }
    PutSI(str, t, "s");
    char* dot = strchr(str, '.');
    if (!dot) return;
    char* e = dot + 1;
    while (*e >= '0' && *e <= '9') e++;           // the unit follows
    char* z = e;
    while (z > dot + 1 && z[-1] == '0') z--;
    if (z == dot + 1) z = dot;
    memmove(z, e, strlen(e) + 1);
  }

  // width in pixels of the ruler label of t
  int LabelW(float t)
  {
    char str[16];
    RulerLabel(str, t);
    return strlen(str) * 8;
  }

  // step 1-2-5: the widest label of the view plus a gap between labels
  float RulerStep()
  {
    float colSec = SamplesToSec(1) * (float)scale / 8.0f;
    float tLeft = SamplesToSec((int64_t)XToSample(0) - gTrig);
    float tRight = SamplesToSec((int64_t)XToSample(PlotW - 1) - gTrig);
    float d = 1e-9f;
    for (int i = 0; i < 40; i++)
    {
      for (int m = 0; m < 3; m++)
      {
        float step = d * (m == 0 ? 1 : m == 1 ? 2 : 5);
        // labels at both ends of the view and next to them (the longest)
        float fl = tLeft / step, fr = tRight / step;
        if (fl < -1e8f || fl > 1e8f || fr < -1e8f || fr > 1e8f) continue;   // step far too small
        int k0 = (int)fl - 1, k1 = (int)fr + 1;
        int w = 0;
        for (int k = k0; k <= k0 + 2; k++) w = max(w, LabelW(k * step));
        for (int k = k1 - 2; k <= k1; k++) w = max(w, LabelW(k * step));
        if (step / colSec >= w + 10)
          return step;
      }
      d *= 10;
    }
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
    int lastEnd = -100;
    for (int k = k0; k <= k1; k++)
    {
      float t = k * step;
      int64_t s = (int64_t)(t * gRate + (t < 0 ? -0.5f : 0.5f)) + gTrig;
      if (s < 0) continue;
      int x = SampleToX((uint32_t)s);
      if (x < 0 || x >= PlotW) continue;
      char str[16];
      RulerLabel(str, k == 0 ? 0.0f : t);
      Bar(PlotX + x, RulerY + RulerH - 3, PlotX + x + 1, RulerY + RulerH, clrDim);
      int w = strlen(str) * 8;
      int tx = PlotX + x - w / 2;
      if (tx < 0) tx = 0;
      if (tx + w > W) tx = W - w;
      // never over the previous label (at the edges of the screen)
      if (tx < lastEnd + 4)
        continue;
      Text(tx, RulerY - 2, clrDim, RGBTRANS, str);
      lastEnd = tx + w;
    }
  }

  // ------------------------------------------------------------ decoded frames
  // role of a lane in the decoder ("" none); decoded: frames on it
  const char* Role(int lane, bool* decoded = nullptr)
  {
    if (decoded) *decoded = false;
    if (dc.proto <= Dec::POff || dc.proto >= Dec::Protos)
      return "";
    for (int r = 0; r < Dec::MaxRoles; r++)
      if (Dec::RoleName[(int)dc.proto][r] && dc.lane[(int)dc.proto][r] == lane)
      {
        if (decoded)
        {
          // the roles with frames
          static const uint8_t frameRoles[Dec::Protos] = { 0, 3, 1, 2, 6, 1, 1, 1, 1, 2, 1, 1, 7 };
          *decoded = (frameRoles[(int)dc.proto] >> r) & 1;
        }
        return Dec::RoleShort[(int)dc.proto][r];
      }
    return "";
  }

  void FrameText(int i, char* s, int size, bool full)
  {
    Dec::Format(gFrames, i, gFrameN, dc, gRate, s, size, full);
  }

  uint16_t FrameColor(const Dec::TFrame& f)
  {
    if (f.flags & (Dec::FlErr | Dec::FlPar)) return clrTrig;
    if (f.flags & Dec::FlNack) return clrX1;
    switch (f.kind)
    {
      case Dec::AddrW: case Dec::AddrR: case Dec::Pid: case Dec::CanId: case Dec::NecAddr: return clrX2;
      case Dec::Start: case Dec::Restart: case Dec::Reset: case Dec::Break: case Dec::Leader:
      case Dec::Sync: case Dec::QuadZ: return clrOk;
      case Dec::Stop: case Dec::Presence: case Dec::Repeat: case Dec::CanDlc: case Dec::CanCrc:
      case Dec::CanAck: case Dec::Checksum: return clrWarn;
      case Dec::Color:
      {
        // the LED color itself, at least a bit bright
        uint32_t v = f.v;
        uint32_t r = (v >> 16) & 0xff, g = (v >> 8) & 0xff, b = v & 0xff;
        uint32_t m = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (m < 0x40) return clrText;
        // lighter, readable on black: halfway to white
        return RGB565RGB((r + 255) / 2, (g + 255) / 2, (b + 255) / 2);
      }
      default: return clrText;
    }
  }

  // frame row of a decoded lane: y0 + 18 .. y0 + 35
  void DrawFrames(int lane, int y0)
  {
    const int yT = y0 + 18, yB = y0 + 35;
    int textEnd = -100;                 // no overlapping labels
    for (int i = 0; i < gFrameN; i++)
    {
      const Dec::TFrame& f = gFrames[i];
      if (f.lane != lane)
        continue;
      int x0 = SampleToX(f.t0), x1 = SampleToX(f.t1 + 1);
      if (x1 < 0) continue;
      if (x0 >= PlotW) break;
      uint16_t c = FrameColor(f);
      char s[24];
      FrameText(i, s, sizeof(s), false);
      int tw = strlen(s) * 8;
      if (f.flags & Dec::FlPoint)
      {
        // a point: line and the text at its right when the next frame of
        // the lane leaves room
        if (x0 < 0) continue;
        Bar(PlotX + x0, yT, PlotX + x0 + 2, yB, c);
        int next = PlotW;
        for (int j = i + 1; j < gFrameN; j++)
          if (gFrames[j].lane == lane) { next = SampleToX(gFrames[j].t0); break; }
        if (x0 + 3 > textEnd && x0 + 3 + tw <= next - 1 && x0 + 3 + tw <= PlotW)
        {
          Text(PlotX + x0 + 3, yT + 1, c, RGBTRANS, s);
          textEnd = x0 + 3 + tw;
        }
        continue;
      }
      int cx0 = x0 < 0 ? 0 : x0, cx1 = x1 > PlotW ? PlotW : x1;
      if (cx1 - cx0 < 3)
      {
        Bar(PlotX + cx0, yT + 4, PlotX + cx0 + (cx1 > cx0 ? cx1 - cx0 : 1), yB - 4, c);
        continue;
      }
      Bar(PlotX + cx0, yT, PlotX + cx1, yT + 1, c);
      Bar(PlotX + cx0, yB - 1, PlotX + cx1, yB, c);
      if (x0 >= 0) Bar(PlotX + x0, yT, PlotX + x0 + 1, yB, c);
      if (x1 <= PlotW) Bar(PlotX + x1 - 1, yT, PlotX + x1, yB, c);
      // the text; too long: its first word (pulse width without the duty
      // cycle), never a cut value
      int room = (cx1 - cx0 - 2) / 8;
      if (room > 0 && (int)strlen(s) > room)
      {
        char* sp = f.kind == Dec::Pulse ? strchr(s, ' ') : nullptr;
        if (!sp || sp - s > room) continue;
        *sp = 0;
        tw = strlen(s) * 8;
      }
      if (room > 0)
      {
        int tx = (cx0 + cx1 - tw) / 2;
        if (tx > textEnd)
        {
          Text(PlotX + tx, yT + 1, c, RGBTRANS, s);
          textEnd = tx + tw;
        }
      }
    }
  }

  // column x of the lane: state and whether it changes inside the column
  void DrawLane(int lane)
  {
    int y0 = LaneTop + lane * LaneH;
    bool decoded;
    const char* role = Role(lane, &decoded);
    // decoded lane: the waveform in the upper half, the frames below
    int yHi = y0 + (decoded ? 3 : 6), yLo = y0 + (decoded ? 13 : LaneH - 8);
    Bar(0, y0, W, y0 + LaneH, clrBack);
    Bar(0, y0 + LaneH - 1, W, y0 + LaneH, clrGrid);
    Text(4, y0 + (role[0] ? 2 : 11), clrLane[lane], RGBTRANS, nameLane[lane]);
    if (role[0])
      Text(2, y0 + 19, clrDim, RGBTRANS, role);
    if (gHave && !(gCapMask & bitLane[lane]))
    {
      Text(PlotX + 4, y0 + 11, clrDim, RGBTRANS, "off");
      return;
    }
    if (!gHave || gEvents == 0)
      return;
    if (decoded)
      DrawFrames(lane, y0);
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
      case ItMode: strcpy(p, bRepeat ? "Rep" : "Sgl"); break;
      case ItLanes:
      {
        // the letters of the channels on: A B C D
        static const uint8_t bit[4] = { 4, 8, 1, 2 };
        for (int l = 0; l < 4; l++)
          if (arrLanes[nLanes] & bit[l]) *p++ = "ABCD"[l];
        *p = 0;
        break;
      }
      case ItView: strcpy(p, "View"); break;
      case ItZoom: strcpy(p, "Zoom"); break;
      case ItX1: strcpy(p, "X1"); break;
      case ItX2: strcpy(p, "X2"); break;
      case ItDec: strcpy(p, Dec::ProtoName[(int)dc.proto]); break;
      case ItList: strcpy(p, "List"); break;
      case ItGen: strcpy(p, gc.mode == Gen::GSquare && gSqOn ? "Sq ON" : Gen::ModeName[(int)gc.mode]); break;
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
    // status at the right of row 2, its end cut when it would cover the items
    char st[sizeof(status)];
    strcpy(st, status);
    int room = (W - 2 - (x + 2)) / 8;
    if (room < 0) room = 0;
    if ((int)strlen(st) > room) st[room] = 0;
    int w = strlen(st) * 8;
    if (w)
      Text(W - w - 2, MenuY + 14, statusClr, RGBTRANS, st);
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
  enum { LaVersion = 901, LaVersionMask = 902 };

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
    gLaVersion = Fpga::Status() >> 6;
    return ok && done && (gLaVersion == LaVersion || gLaVersion == LaVersionMask);
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
    int src = -1;
    bool rise = true;
    if (nTrig == TrigDec)
    {
      // protocol trigger: the capture starts at the activity of the main
      // lane, 10 % before it; the frames are searched after the capture
      int lane;
      if (DecTrigOn() && Dec::StartEdge(dc, lane, rise))
        src = lane;
    }
    else if (nTrig > 0)
    {
      src = (nTrig - 1) / 2;            // 0 A, 1 B, 2 C, 3 D
      rise = ((nTrig - 1) & 1) == 0;
    }
    if (src >= 0)
    {
      mode = (src << 3) | (rise ? 1 : 0);
      if (src == 0) vthr = a.thr;
      if (src == 1) vthr = b.thr;
    }
    gPreWords = src >= 0 ? 4094 * (nTrig == TrigDec ? 10 : arrPre[nPre]) / 100 : 0;
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
    gCapMask = 15;
    if (gLaVersion >= LaVersionMask)
    {
      Fpga::SetParam(0x16, arrLanes[nLanes]);
      gCapMask = arrLanes[nLanes];
    }
    ADC::Restart(0);
    // the shot of the generator when the trigger is armed: after the
    // pretrigger words or the arm time, whichever comes first (a quiet line)
    gFireAt = 0;
    if (gc.autoFire && gc.mode != Gen::GOff && gc.mode != Gen::GSquare)
    {
      uint32_t ms = (uint32_t)((float)arm * 1024.0f * 1000.0f / rate) + 3;
      gFireAt = SYS::GetTick() + ms;
      if (!gFireAt) gFireAt = 1;
    }
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
    gTrig = gPreWords > 0 && trig > 0 ? trig - 1 : 0;
    gRate = 72e6f / arrDiv[nRate];
    gHave = true;
    return true;
  }

  // view: the frame k at a third of the screen, about 60 columns wide
  void ShowFrame(int k)
  {
    uint64_t w8 = ((uint64_t)(gFrames[k].t1 - gFrames[k].t0 + 1)) << 3;
    scale = CeilDiv(w8, 60);
    if (scale < 1) scale = 1;
    gZoomX = false;
    int64_t left = ((int64_t)gFrames[k].t0 << 3) - (int64_t)Span8() / 3;
    viewLeft = left < 0 ? 0 : (uint32_t)left;
    ClampView();
  }

  // the data between X1 and X2 on the whole screen
  void ZoomX1X2()
  {
    if (!gHave) return;
    uint32_t a = cur[0] < cur[1] ? cur[0] : cur[1], b = cur[0] < cur[1] ? cur[1] : cur[0];
    if (a == b)
    {
      DrawAll();
      Status(clrWarn, "X1 = X2");
      return;
    }
    ShowRange(a, b);
    gZoomX = true;
    DrawAll();
    Status(clrOk, "X1-X2  F3: all");
  }

  // false: the protocol trigger did not match, capture again
  bool FinishCapture(bool stopped)
  {
    bool ok = ReadCapture();
    eRun = Idle;
    gDecMatch = false;
    if (ok)
    {
      Fit();
      // cursors: X1 on the first edge of the capture, X2 on the last one
      // (no edge: both on the trigger)
      if (gEvents >= 2)
      {
        cur[0] = La::Time(gEv[1]);
        cur[1] = La::Time(gEv[gEvents - 1]);
      }
      else
        cur[0] = cur[1] = gTrig;
      Status(clrOk, stopped ? "stopped" : "");
      DecodeAndShow();
      if (DecTrigOn())
      {
        int k = Dec::FindTrigger(gFrames, gFrameN, dc, gRate);
        if (k >= 0)
        {
          gDecMatch = true;
          gTrig = gFrames[k].t0;
          cur[0] = gFrames[k].t0;
          cur[1] = gFrames[k].t1 + 1;
          ShowFrame(k);
          char s[40], *p = s;
          strcpy(p, "match"); p += 5;
          if (gRetries) { strcpy(p, " #"); p = PutU(p + 2, gRetries + 1); }
          Status(clrOk, s);
        }
        else if (!stopped)
        {
          gRetries++;
          char s[40], *p = s;
          strcpy(p, "no match #"); p = PutU(p + 10, gRetries + 1);
          Status(clrWarn, s);
          DrawTitle();
          DrawPlot();
          return false;
        }
        else
          Status(clrWarn, "no match");
      }
    } else
      Status(clrWarn, "no trigger");
    DrawTitle();
    DrawPlot();
    return true;
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
      if (!FinishCapture(false) || bRepeat)
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

  // decoded frames: time from the trigger in ns, lane, role, text
  bool SaveCsv()
  {
    char name[16];
    if (!OpenNew(name, "LA", "CSV")) { strcpy(status, "file error"); return false; }
    TWriter w;
    w.Str("time_ns,lane,role,frame\n");
    uint64_t ps = (uint64_t)(1e12f / gRate + 0.5f);
    for (int i = 0; i < gFrameN && w.ok; i++)
    {
      const Dec::TFrame& f = gFrames[i];
      int64_t d = (int64_t)f.t0 - gTrig;
      if (d < 0) { w.Put('-'); d = -d; }
      w.U64((uint64_t)d * ps / 1000);
      w.Put(','); w.Str(nameLane[f.lane]); w.Put(','); w.Str(Role(f.lane)); w.Put(',');
      char s[64];
      FrameText(i, s, sizeof(s), true);
      // a comma or quote in ASCII: quoted
      bool q = strchr(s, ',') || strchr(s, '"');
      if (q) w.Put('"');
      for (char* c = s; *c; c++) { if (*c == '"') w.Put('"'); w.Put(*c); }
      if (q) w.Put('"');
      w.Put('\n');
    }
    bool ok = Finish(w);
    strcpy(status, ok ? name : "save error");
    statusClr = ok ? clrOk : clrTrig;
    return ok;
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

  // ------------------------------------------------------------ decoding
  void RunDecoder()
  {
    gFrameN = 0;
    gDecNote[0] = 0;
    gFrameOvf = false;
    memset(&gStats, 0, sizeof(gStats));
    if (!gHave || dc.proto == Dec::POff || gEvents < 2)
      return;
    gFrameN = Dec::Decode(gEv, gEvents, gEnd, gRate, dc, gFrames, MaxFrames, gStats, gFrameOvf);
    char* p = gDecNote;
    *p = 0;
    if (gStats.noLane) { strcpy(p, "lanes: F3 on Dec"); return; }
    if (gStats.slow) { strcpy(p, "rate too low"); return; }
    if (gFrameOvf) { strcpy(p, "1024 frames max"); return; }
    switch (dc.proto)
    {
      case Dec::PUart: case Dec::PLin: case Dec::PCan:
        if (gStats.baud) { p = PutU(p, gStats.baud); strcpy(p, dc.proto == Dec::PCan ? " bit/s" : " Bd"); }
        break;
      case Dec::PCounter:
        p = PutU(p, gStats.edges); strcpy(p, " edges");
        break;
      case Dec::PPulse:
      {
        // mean frequency and duty cycle of the complete periods
        uint64_t sumP = 0, sumW = 0;
        uint32_t nP = 0;
        int last = -1;
        for (int i = 0; i < gFrameN; i++)
        {
          if (gFrames[i].kind != Dec::Pulse) continue;
          if (last >= 0) { sumP += gFrames[i].t0 - gFrames[last].t0; sumW += gFrames[last].v; nP++; }
          last = i;
        }
        if (nP)
        {
          p = PutSI(p, gRate * nP / (float)sumP, "Hz");
          strcpy(p, " duty "); p += 6;
          uint32_t pm = (uint32_t)(sumW * 1000 / sumP);
          p = PutU(p, pm / 10); *p++ = '.'; p = PutU(p, pm % 10); strcpy(p, "%");
        }
        break;
      }
      case Dec::PEncoder:
      {
        int32_t steps = Dec::QuadScale(gStats.steps < 0 ? -gStats.steps : gStats.steps, dc);
        *p++ = gStats.steps < 0 ? '-' : '+';
        p = PutU(p, steps);
        if (gStats.havePerRev)
        {
          strcpy(p, " rev "); p += 5;
          int32_t r = Dec::QuadScale(gStats.perRev < 0 ? -gStats.perRev : gStats.perRev, dc);
          p = PutU(p, r);
        }
        if (gStats.qRate != 0)
        {
          *p++ = ' ';
          float r = gStats.qRate / (dc.quadRes == 2 ? 4 : dc.quadRes == 1 ? 2 : 1);
          p = PutSI(p, r, "/s");
        }
        if (gStats.qErrors) { strcpy(p, " err "); p = PutU(p + 5, gStats.qErrors); }
        break;
      }
    }
  }

  void DecodeAndShow()
  {
    RunDecoder();
    // a warning starts with a letter
    bool warn = gDecNote[0] >= 'a' && gDecNote[0] <= 'z';
    if (gDecNote[0])
      Status(warn ? clrWarn : clrOk, gDecNote);
  }

  void LoadDecoder()
  {
    if (FAT::Open(DecFile, FAT::IoRead) != FAT::EOk)
      return;
    if (FAT::Read(gSector) == FAT::EOk)
    {
      Dec::TCfg c;
      memcpy(&c, gSector, sizeof(c));
      if (c.magic == Dec::CfgMagic && c.proto >= 0 && c.proto < Dec::Protos)
        dc = c;
    }
    FAT::Close();
  }

  void SaveDecoder()
  {
    memset(gSector, 0, sizeof(gSector));
    memcpy(gSector, &dc, sizeof(dc));
    if (FAT::Open(DecFile, FAT::IoWrite) == FAT::EOk)
    {
      FAT::Write(gSector);
      FAT::Close(sizeof(dc));
    }
  }

  // ------------------------------------------------------------ decoder setup page
  enum { PkProto, PkLane, PkLaneOpt, PkNum, PkList, PkCond, PkHex, PkDec, PkHead };
  struct TParam
  {
    const char* name;
    int kind;
    int8_t* v;
    uint32_t* v32;
    int lo, hi;
    const char* const* list;           // PkList: names
  };
  int gDigit = 0;                       // PkHex / PkDec: digit edited by Left / Right

  const char* const lstParity[] = { "none", "even", "odd" };
  const char* const lstLevel[] = { "normal", "inverted" };
  const char* const lstPulse[] = { "high pulses", "low pulses" };
  const char* const lstNec[] = { "idle low", "idle high (receiver)" };
  const char* const lstMode[] = { "0 CPOL 0 CPHA 0", "1 CPOL 0 CPHA 1", "2 CPOL 1 CPHA 0", "3 CPOL 1 CPHA 1" };
  const char* const lstOrder[] = { "MSB first", "LSB first" };
  const char* const lstWs[] = { "GRB (WS2812)", "RGB" };
  const char* const lstEdge[] = { "rising", "falling", "both" };
  const char* const lstRes[] = { "x4 (every edge)", "x2", "x1 (A periods)" };
  const char* const lstDir[] = { "A leads B: +", "A leads B: -" };
  const char* const lstZ[] = { "Z active high", "Z active low" };
  const char* const lstFmt[] = { "hex", "decimal", "ASCII" };
  const char* const lstOnOff[] = { "off", "on" };
  const char* const lstBaudU[] = { "auto", "300", "600", "1200", "2400", "4800", "9600", "14400",
    "19200", "28800", "31250", "38400", "57600", "76800", "115200", "230400", "250000", "460800",
    "500000", "921600", "1M", "1.5M", "2M", "3M", "4M" };
  const char* const lstBaudL[] = { "auto", "2400", "9600", "10417", "19200" };
  const char* const lstBaudC[] = { "auto", "10k", "20k", "33.3k", "50k", "83.3k", "100k", "125k",
    "250k", "500k", "800k", "1M" };

  int Params(TParam* p)
  {
    int n = 0;
    auto add = [&](const char* name, int kind, int8_t* v, int lo, int hi, const char* const* list = nullptr) {
      p[n++] = { name, kind, v, nullptr, lo, hi, list };
    };
    auto add32 = [&](const char* name, int kind, uint32_t* v) { p[n++] = { name, kind, nullptr, v, 0, 0, nullptr }; };
    const int pr = dc.proto;
    add("Protocol", PkProto, &dc.proto, 0, Dec::Protos - 1);
    if (pr == Dec::POff)
      return n;
    static char roleNames[Dec::MaxRoles][16];
    for (int r = 0; r < Dec::MaxRoles; r++)
      if (Dec::RoleName[pr][r])
      {
        strcpy(roleNames[r], Dec::RoleName[pr][r]);
        strcat(roleNames[r], " lane");
        add(roleNames[r], Dec::RoleOptional(pr, r) ? PkLaneOpt : PkLane, &dc.lane[pr][r], -1, 3);
      }
    switch (pr)
    {
      case Dec::PUart:
        add("Baud rate", PkList, &dc.uartBaud, 0, Dec::RateUartN - 1, lstBaudU);
        add("Data bits", PkNum, &dc.uartBits, 5, 9);
        add("Parity", PkList, &dc.uartParity, 0, 2, lstParity);
        add("Stop bits", PkNum, &dc.uartStop, 1, 2);
        add("Polarity", PkList, &dc.inv[pr], 0, 1, lstLevel);
        break;
      case Dec::PLin:
        add("Baud rate", PkList, &dc.linBaud, 0, Dec::RateLinN - 1, lstBaudL);
        add("Polarity", PkList, &dc.inv[pr], 0, 1, lstLevel);
        break;
      case Dec::PSpi:
        add("Mode", PkList, &dc.spiMode, 0, 3, lstMode);
        add("Word bits", PkNum, &dc.spiBits, 4, 32);
        add("Bit order", PkList, &dc.spiLsb, 0, 1, lstOrder);
        break;
      case Dec::PCan:
        add("Bit rate", PkList, &dc.canRate, 0, Dec::RateCanN - 1, lstBaudC);
        add("Polarity", PkList, &dc.inv[pr], 0, 1, lstLevel);
        break;
      case Dec::PWs2812: add("Color order", PkList, &dc.wsOrder, 0, 1, lstWs); break;
      case Dec::PNec: add("Polarity", PkList, &dc.inv[pr], 0, 1, lstNec); break;
      case Dec::PPulse: add("Pulses", PkList, &dc.inv[pr], 0, 1, lstPulse); break;
      case Dec::PCounter: add("Edges", PkList, &dc.cntEdge, 0, 2, lstEdge); break;
      case Dec::PEncoder:
        add("Resolution", PkList, &dc.quadRes, 0, 2, lstRes);
        add("Direction", PkList, &dc.quadDir, 0, 1, lstDir);
        add("Index Z", PkList, &dc.inv[pr], 0, 1, lstZ);
        break;
    }
    if (pr != Dec::PPulse && pr != Dec::PCounter && pr != Dec::PEncoder && pr != Dec::PWs2812)
      add("Format", PkList, &dc.fmt, 0, 2, lstFmt);
    // the protocol trigger
    add("-- Trigger (Trig: Decoder)", PkHead, nullptr, 0, 0);
    int nc;
    Dec::Conds(pr, nc);
    add("Condition", PkCond, &dc.tCond[pr], 0, nc - 1);
    const Dec::TCond& cd = Dec::Cond(dc);
    if (cd.id)
    {
      if (cd.val == Dec::VHex)
      {
        add32("Value", PkHex, &dc.tValue);
        add32("Mask", PkHex, &dc.tMask);
      }
      if (cd.val == Dec::VUs) add32("Time us", PkDec, &dc.tValue);
      if (cd.val == Dec::VNum) add32("Number", PkDec, &dc.tValue);
      if (cd.val == Dec::VHex)
      {
        add("Then next =", PkList, &dc.tThen, 0, 1, lstOnOff);
        if (dc.tThen) add32("Next value", PkHex, &dc.tThenValue);
      }
      add("Occurrence", PkNum, (int8_t*)&dc.tNth, 1, 99);
    }
    return n;
  }

  // digits of a value row
  int Digits(const TParam& q) { return 8; }

  void ParamText(const TParam& q, char* s)
  {
    if (q.kind == PkHead) { s[0] = 0; return; }
    if (q.kind == PkHex) { strcpy(s, "0x"); char* p = s + 2; for (int i = 7; i >= 0; i--) *p++ = "0123456789ABCDEF"[(*q.v32 >> (i * 4)) & 15]; *p = 0; return; }
    if (q.kind == PkDec)
    {
      uint32_t v = *q.v32, d = 10000000;
      char* p = s;
      for (int i = 0; i < 8; i++, d /= 10) *p++ = '0' + (v / d) % 10;
      *p = 0;
      return;
    }
    int v = q.kind == PkNum && q.lo == 1 && q.hi == 99 ? *(uint8_t*)q.v : *q.v;
    switch (q.kind)
    {
      case PkProto: strcpy(s, Dec::ProtoName[v]); break;
      case PkLane: case PkLaneOpt:
        strcpy(s, v < 0 ? "-" : v == 0 ? "A (CH1)" : v == 1 ? "B (CH2)" : v == 2 ? "C" : "D");
        break;
      case PkNum: PutU(s, v); break;
      case PkList: strcpy(s, v >= q.lo && v <= q.hi ? q.list[v] : "?"); break;
      case PkCond: { int nc; strcpy(s, Dec::Conds(dc.proto, nc)[v < nc ? v : 0].name); break; }
    }
  }

  // ------------------------------------------------------------ generator setup
  Gen::TCfg gc;
  const char* const GenFile = "LOGICGEN.CFG";
  const char* const lstGenMode[] = { "off", "single pulse", "square wave", "burst", "test of the decoder" };
  const char* const lstPol[] = { "active high", "active low" };

  int GenParams(TParam* p)
  {
    int n = 0;
    auto add = [&](const char* name, int kind, int8_t* v, int lo, int hi, const char* const* list = nullptr) {
      p[n++] = { name, kind, v, nullptr, lo, hi, list };
    };
    auto add32 = [&](const char* name, int kind, uint32_t* v) { p[n++] = { name, kind, nullptr, v, 0, 0, nullptr }; };
    add("Mode", PkList, &gc.mode, 0, Gen::GModes - 1, lstGenMode);
    switch (gc.mode)
    {
      case Gen::GPulse:
        add32("Width us", PkDec, &gc.pulseUs);
        add("Polarity", PkList, &gc.pol, 0, 1, lstPol);
        break;
      case Gen::GSquare:
        add32("Frequency Hz", PkDec, &gc.freqHz);
        add("Duty %", PkNum, &gc.duty, 1, 99);
        break;
      case Gen::GBurst:
        add32("Pulses", PkDec, &gc.count);
        add32("Frequency Hz", PkDec, &gc.freqHz);
        add("Duty %", PkNum, &gc.duty, 1, 99);
        add("Polarity", PkList, &gc.pol, 0, 1, lstPol);
        break;
      case Gen::GTest:
        // the interface: the settings of the decoder (the same values as on
        // its setup page; auto baud / bit rate: 115200 / 19200 / 125 k)
        if (dc.proto == Dec::PUart)
        {
          static const char* const lstPay[] = { "text Hello DS203!", "4 bytes of Value" };
          add("Data", PkList, &gc.payload, 0, 1, lstPay);
          if (gc.payload) add32("Value", PkHex, &gc.value);
          add("Baud rate", PkList, &dc.uartBaud, 0, Dec::RateUartN - 1, lstBaudU);
          add("Data bits", PkNum, &dc.uartBits, 5, 9);
          add("Parity", PkList, &dc.uartParity, 0, 2, lstParity);
          add("Stop bits", PkNum, &dc.uartStop, 1, 2);
          add("Polarity", PkList, &dc.inv[Dec::PUart], 0, 1, lstLevel);
        }
        if (dc.proto == Dec::PLin || dc.proto == Dec::PCan || dc.proto == Dec::POneWire || dc.proto == Dec::PNec)
          add32("Value", PkHex, &gc.value);
        if (dc.proto == Dec::PLin)
        {
          add("Baud rate", PkList, &dc.linBaud, 0, Dec::RateLinN - 1, lstBaudL);
          add("Polarity", PkList, &dc.inv[Dec::PLin], 0, 1, lstLevel);
        }
        if (dc.proto == Dec::PCan)
        {
          add("Bit rate", PkList, &dc.canRate, 0, Dec::RateCanN - 1, lstBaudC);
          add("Polarity", PkList, &dc.inv[Dec::PCan], 0, 1, lstLevel);
        }
        if (dc.proto == Dec::PNec)
          add("Polarity", PkList, &dc.inv[Dec::PNec], 0, 1, lstNec);
        if (dc.proto == Dec::PPulse || dc.proto == Dec::PCounter)
        {
          add32("Pulses", PkDec, &gc.count);
          add32("Frequency Hz", PkDec, &gc.freqHz);
          add("Duty %", PkNum, &gc.duty, 1, 99);
        }
        break;
    }
    if (gc.mode != Gen::GOff && gc.mode != Gen::GSquare)
      add("With capture", PkList, &gc.autoFire, 0, 1, lstOnOff);
    return n;
  }

  uint32_t gGenTab[Gen::MaxTab];        // levels, then GPIOB_BSRR words

  bool GenBusy();

  void SeqInfo(const Gen::TSeq& q, char* s)
  {
    char* p = s;
    if (gc.mode == Gen::GPulse) { p = PutSI(p, q.div / 72e6f, "s"); strcpy(p, gc.pol ? " low" : " high"); return; }
    if (gc.mode == Gen::GTest) { strcpy(p, Dec::ProtoName[(int)dc.proto]); p += strlen(p); *p++ = ' '; }
    if (q.real > 0)
    {
      bool hz = gc.mode == Gen::GBurst || dc.proto == Dec::PPulse || dc.proto == Dec::PCounter;
      p = PutSI(p, q.real, hz ? "Hz" : "bit/s");
      *p++ = ' ';
    }
    p = PutSI(p, q.n * (float)q.div / 72e6f, "s");
    *p = 0;
    // the sample rate of the analyzer needed to see it: 8 samples per bit
    // (per period of a burst, per width of a pulse)
    float need = 0;
    if (gc.mode == Gen::GPulse) need = 8 * 72e6f / q.div;
    else if (q.real > 0) need = 8 * q.real;
    else if (gc.mode == Gen::GTest && (dc.proto == Dec::POneWire)) need = 1e6f;
    else if (gc.mode == Gen::GTest && (dc.proto == Dec::PNec)) need = 30e3f;
    if (need > 0 && 72e6f / arrDiv[nRate] < need)
    {
      strcpy(p, " rate>="); p += 7;
      PutSI(p, need, "S/s");
    }
  }

  // what the generator will do (setup page, status)
  void GenInfo(char* s)
  {
    *s = 0;
    char* p = s;
    switch (gc.mode)
    {
      case Gen::GOff: strcpy(s, "Wave Out not used"); return;
      case Gen::GSquare:
      {
        uint32_t psc, arr;
        float real;
        if (!Gen::SquarePlan(gc.freqHz, psc, arr, real)) { strcpy(s, "frequency: 1 Hz .. 18 MHz"); return; }
        p = PutSI(p, real, "Hz");
        strcpy(p, " (TIM4)");
        return;
      }
      default:
      {
        // the table is in use while a shot runs
        if (GenBusy()) { strcpy(s, "shot running"); return; }
        Gen::TSeq q;
        if (!Gen::Build(gc, dc, gGenTab, q)) { strcpy(s, q.err); return; }
        SeqInfo(q, s);
        return;
      }
    }
  }

  void LoadGen()
  {
    Gen::Defaults(gc);
    if (FAT::Open(GenFile, FAT::IoRead) != FAT::EOk)
      return;
    if (FAT::Read(gSector) == FAT::EOk)
    {
      Gen::TCfg c;
      memcpy(&c, gSector, sizeof(c));
      if (c.magic == Gen::CfgMagic && c.mode >= 0 && c.mode < Gen::GModes)
        gc = c;
    }
    FAT::Close();
  }

  void SaveGen()
  {
    memset(gSector, 0, sizeof(gSector));
    memcpy(gSector, &gc, sizeof(gc));
    if (FAT::Open(GenFile, FAT::IoWrite) == FAT::EOk)
    {
      FAT::Write(gSector);
      FAT::Close(sizeof(gc));
    }
  }

  // ------------------------------------------------------------ generator hardware
#ifdef _ARM
  inline volatile uint32_t& Reg(uint32_t a) { return *(volatile uint32_t*)a; }
#else
  // host builds (renderer, tests): no registers
  inline volatile uint32_t& Reg(uint32_t) { static volatile uint32_t dummy; return dummy; }
#endif
  enum : uint32_t {
    RCC_AHBENR = 0x40021014, RCC_APB1ENR = 0x4002101C,
    DMA2_CCR4 = 0x40020444, DMA2_CNDTR4 = 0x40020448, DMA2_CPAR4 = 0x4002044C, DMA2_CMAR4 = 0x40020450,
    TIM7_CR1 = 0x40001400, TIM7_DIER = 0x4000140C, TIM7_SR = 0x40001410, TIM7_EGR = 0x40001414,
    TIM7_CNT = 0x40001424, TIM7_PSC = 0x40001428, TIM7_ARR = 0x4000142C,
    TIM4_CR1 = 0x40000800, TIM4_EGR = 0x40000814, TIM4_PSC = 0x40000828, TIM4_ARR = 0x4000082C,
    TIM4_CCR1 = 0x40000834, GPIOA_CRL = 0x40010800, DAC_CR = 0x40007400,
    GPIOB_BSRR = 0x40010C10, Pb6Set = 1u << 6, Pb6Reset = 1u << 22 };

  bool GenBusy()
  {
#ifdef _ARM
    return (Reg(DMA2_CCR4) & 1) && Reg(DMA2_CNDTR4) != 0;
#else
    return false;
#endif
  }

  void GenStopDma()
  {
    Reg(DMA2_CCR4) = 0;
    Reg(TIM7_CR1) = 0x84;
  }

  // PB6 a GPIO output at a level (BIOS: DAC off, TIM4 and TIM7 stopped)
  void GenLevel(int level)
  {
    GenStopDma();
    DAC::SetMode(level ? DAC::EMode::LogicHigh : DAC::EMode::LogicLow, nullptr, 0);
  }

  void GenOff()
  {
    gSqOn = false;
    gFireAt = 0;
    GenLevel(0);
  }

  // square wave from TIM4 channel 1 on PB6 (as the generator of the
  // oscilloscope); false: frequency out of range
  bool GenSquare()
  {
    uint32_t psc, arr;
    float real;
    if (!Gen::SquarePlan(gc.freqHz, psc, arr, real))
      return false;
    GenStopDma();
    // PA4 (DAC) released: it is tied to PB6 on the board
    Reg(GPIOA_CRL) = (Reg(GPIOA_CRL) & ~(0xfu << 16)) | (0x4u << 16);
    Reg(DAC_CR) &= ~0x1fffu;
    DAC::SetMode(DAC::EMode::Square, nullptr, 0);
    DAC::SetFrequency((int)(real + 0.5f) > 0 ? (int)(real + 0.5f) : 1);
    DAC::SetDuty(gc.duty);
    Reg(TIM4_ARR) = arr - 1;
    Reg(TIM4_PSC) = psc - 1;
    Reg(TIM4_CCR1) = arr * (100 - gc.duty) / 100;     // same polarity as BIOS
    Reg(TIM4_EGR) = 1;
    return true;
  }

  // one shot: the table to GPIOB_BSRR by DMA2 channel 4, paced by TIM7;
  // s: what was generated or why not
  bool GenFire(char* s)
  {
    Gen::TSeq q;
    if (gc.mode == Gen::GOff || gc.mode == Gen::GSquare)
    {
      strcpy(s, "no shot in this mode");
      return false;
    }
    if (!Gen::Build(gc, dc, gGenTab, q))
    {
      strcpy(s, q.err);
      return false;
    }
    for (int i = 0; i < q.n; i++)
      gGenTab[i] = gGenTab[i] ? Pb6Set : Pb6Reset;
    gSqOn = false;
    // PB6 an output at the idle level (the end of the table); the first level
    // (a pulse: the active one) is set right before the timer starts
    GenLevel(gGenTab[q.n - 1] == Pb6Set ? 1 : 0);
    Reg(RCC_AHBENR) |= 2;               // DMA2
    Reg(RCC_APB1ENR) |= 0x20;           // TIM7
    Reg(DMA2_CPAR4) = GPIOB_BSRR;
    Reg(DMA2_CMAR4) = (uint32_t)(uintptr_t)gGenTab;
    Reg(DMA2_CNDTR4) = q.n;
    // priority very high, 32 bit to 32 bit, memory increment, to the peripheral
    Reg(DMA2_CCR4) = 0x3A90;
    uint32_t psc = (q.div - 1) / 65536 + 1, arr = (q.div + psc / 2) / psc;
    Reg(TIM7_CR1) = 0x84;               // stopped, update request only by overflow
    Reg(TIM7_PSC) = psc - 1;
    Reg(TIM7_ARR) = arr - 1;
    Reg(TIM7_CNT) = 0;
    Reg(TIM7_EGR) = 1;                  // load the prescaler (no DMA request: URS)
    Reg(TIM7_SR) = 0;
    Reg(TIM7_DIER) |= 0x100;            // DMA request on update
    Reg(DMA2_CCR4) |= 1;
    Reg(GPIOB_BSRR) = q.first ? Pb6Set : Pb6Reset;
    Reg(TIM7_CR1) = 0x85;
    SeqInfo(q, s);
    return true;
  }

  // every setting in its range (a damaged LOGICDEC.CFG / LOGICGEN.CFG or
  // RAM must not lead to a table index out of range); true: repaired
  bool Sanitize()
  {
    bool fixed = false;
    Dec::TCfg d;
    Dec::Defaults(d);
    auto cl = [&](int8_t& v, int lo, int hi, int def) { if (v < lo || v > hi) { v = (int8_t)def; fixed = true; } };
    cl(dc.proto, 0, Dec::Protos - 1, 0);
    for (int pr = 0; pr < Dec::Protos; pr++)
    {
      for (int r = 0; r < Dec::MaxRoles; r++)
        if (Dec::RoleName[pr][r]) cl(dc.lane[pr][r], -1, 3, d.lane[pr][r]);
        else dc.lane[pr][r] = -1;
      cl(dc.inv[pr], 0, 1, d.inv[pr]);
      int nc;
      Dec::Conds(pr, nc);
      cl(dc.tCond[pr], 0, nc - 1, 0);
    }
    cl(dc.uartBaud, 0, Dec::RateUartN - 1, 0); cl(dc.uartBits, 5, 9, 8); cl(dc.uartParity, 0, 2, 0);
    cl(dc.uartStop, 1, 2, 1); cl(dc.linBaud, 0, Dec::RateLinN - 1, 0); cl(dc.spiMode, 0, 3, 0);
    cl(dc.spiBits, 4, 32, 8); cl(dc.spiLsb, 0, 1, 0); cl(dc.canRate, 0, Dec::RateCanN - 1, 0);
    cl(dc.wsOrder, 0, 1, 0); cl(dc.cntEdge, 0, 2, 0); cl(dc.quadRes, 0, 2, 0); cl(dc.quadDir, 0, 1, 0);
    cl(dc.fmt, 0, 2, 0); cl(dc.tThen, 0, 1, 0);
    if (dc.tNth < 1 || dc.tNth > 99) { dc.tNth = 1; fixed = true; }
    cl(gc.mode, 0, Gen::GModes - 1, 0); cl(gc.pol, 0, 1, 0); cl(gc.autoFire, 0, 1, 0);
    cl(gc.payload, 0, 1, 0); cl(gc.duty, 1, 99, 50);
    return fixed;
  }

  // the rows of the setup page (one array for SetupPage and DrawSetup: 670
  // bytes less stack each)
  TParam gPar[24];

  // the setup page shows the decoder (0) or the generator (1)
  int gPage = 0;
  int PageParams(TParam* p) { return gPage ? GenParams(p) : Params(p); }

  const int SetupY = 20, SetupH = 16;
  int SetupRows() { return gPage ? 10 : 11; }

  void DrawSetup(int sel, int top)
  {
    Sanitize();
    Bar(0, 0, W, TitleH, clrBar);
    Text(4, 0, clrText, clrBar, gPage ? "Generator (Wave Out)" : "Decoder setup");
    Bar(0, TitleH, W, H - 34, clrBack);
    TParam* p = gPar;
    int n = PageParams(p);
    const int rows = SetupRows();
    for (int r = 0; r < rows; r++)
    {
      int i = top + r;
      if (i >= n) break;
      int y = SetupY + r * SetupH;
      bool hl = i == sel;
      if (p[i].kind == PkHead)
      {
        Text(4, y, clrWarn, RGBTRANS, p[i].name);
        continue;
      }
      if (hl) Bar(0, y, W, y + SetupH, clrSel);
      Text(12, y, hl ? clrText : clrDim, RGBTRANS, p[i].name);
      char s[32];
      ParamText(p[i], s);
      int x = 148;
      Text(x, y, clrText, RGBTRANS, s);
      if (hl && (p[i].kind == PkHex || p[i].kind == PkDec))
      {
        // the edited digit
        int off = p[i].kind == PkHex ? 2 : 0, nd = Digits(p[i]);
        int k = off + nd - 1 - gDigit;
        char d[2] = { s[k], 0 };
        Text(x + k * 8, y, clrBack, clrWarn, d);
      }
    }
    // scroll marks
    if (top > 0) Text(W - 12, SetupY, clrDim, RGBTRANS, "^");
    if (top + rows < n) Text(W - 12, SetupY + (rows - 1) * SetupH, clrDim, RGBTRANS, "v");
    if (gPage)
    {
      // what will be generated
      char s[48];
      GenInfo(s);
      s[48 - 1] = 0;
      Text(4, SetupY + rows * SetupH, clrOk, RGBTRANS, s);
    }
    Bar(0, H - 34, W, H, clrBar);
    Text(4, H - 33, clrDim, RGBTRANS, "Up/Down item  Left/Right change");
    Text(4, H - 17, clrDim, RGBTRANS, "F1 digit  F2/F3/F4 back (saved)");
  }

  void SetupPage(int page)
  {
    gPage = page;
    Dec::TCfg old = dc;
    Gen::TCfg oldGen = gc;
    int sel = 0, top = 0;
    gDigit = 0;
    LCD::Clear(clrBack);
    DrawSetup(sel, top);
    while (true)
    {
      KEY::EKey key = KEY::GetKey();
      if (key == KEY::None)
        continue;
      if (key == KEY::Escape || key == KEY::F3 || key == KEY::F4)
        break;
      TParam* p = gPar;
      int n = PageParams(p);
      if (key == KEY::Up || key == KEY::Down)
      {
        int d = key == KEY::Up ? -1 : 1;
        do sel = (sel + d + n) % n; while (p[sel].kind == PkHead);
        gDigit = 0;
      }
      if (key == KEY::F1 && (p[sel].kind == PkHex || p[sel].kind == PkDec))
        gDigit = (gDigit + 1) % Digits(p[sel]);
      if (key == KEY::Left || key == KEY::Right)
      {
        int d = key == KEY::Right ? 1 : -1;
        TParam& q = p[sel];
        if (q.kind == PkHex || q.kind == PkDec)
        {
          uint32_t base = q.kind == PkHex ? 16 : 10, step = 1;
          for (int i = 0; i < gDigit; i++) step *= base;
          uint32_t digit = (*q.v32 / step) % base;
          digit = (digit + base + d) % base;
          *q.v32 = *q.v32 - ((*q.v32 / step) % base) * step + digit * step;
          if (q.kind == PkDec && *q.v32 > 99999999) *q.v32 = 99999999;
        }
        else if (q.kind == PkNum && q.lo == 1 && q.hi == 99)
        {
          int v = *(uint8_t*)q.v + d;
          *(uint8_t*)q.v = (uint8_t)(v < 1 ? 99 : v > 99 ? 1 : v);
        }
        else
        {
          int v = *q.v + d, lo = q.kind == PkLane ? 0 : q.lo;
          if (v < lo) v = q.hi;
          if (v > q.hi) v = lo;
          *q.v = (int8_t)v;
        }
        n = PageParams(p);
        if (sel >= n) sel = n - 1;
      }
      if (sel < top) top = sel;
      if (sel >= top + SetupRows()) top = sel - SetupRows() + 1;
      if (top > 0 && p[top - 1].kind == PkHead && sel == top) top--;   // keep the heading
      DrawSetup(sel, top);
    }
    if (memcmp(&old, &dc, sizeof(dc)) != 0)
      SaveDecoder();
    if (memcmp(&oldGen, &gc, sizeof(gc)) != 0)
      SaveGen();
    LCD::Clear(clrBack);
    status[0] = 0;
    DrawAll();
    DecodeAndShow();
    DrawPlot();
  }

  // ------------------------------------------------------------ list of frames
  const int ListRows = 13, ListY = 18, ListH = 15;

  // time of the frame relative to the trigger, the lane, the text
  void ListLine(int i, char* s, int size)
  {
    const Dec::TFrame& f = gFrames[i];
    char* p = PutSI(s, SamplesToSec((int64_t)f.t0 - gTrig), "s");
    while (p - s < 9) *p++ = ' ';
    *p++ = ' ';
    *p++ = nameLane[f.lane][0];
    *p++ = ' ';
    strcpy(p, Role(f.lane));
    p += strlen(p);
    *p++ = ' ';
    FrameText(i, p, size - (int)(p - s), true);
  }

  void DrawList(int top, int sel)
  {
    Bar(0, 0, W, TitleH, clrBar);
    char s[72], *p;
    strcpy(s, Dec::ProtoName[(int)dc.proto]);
    p = s + strlen(s);
    strcpy(p, "  "); p = PutU(p + 2, gFrameN); strcpy(p, " frames");
    Text(4, 0, clrText, clrBar, s);
    Bar(0, TitleH, W, H - 16, clrBack);
    for (int r = 0; r < ListRows; r++)
    {
      int i = top + r;
      if (i >= gFrameN) break;
      int y = ListY + r * ListH;
      if (i == sel) Bar(0, y, W, y + ListH, clrSel);
      ListLine(i, s, sizeof(s));
      s[49] = 0;                        // LCD::Print does not clip
      Text(4, y, i == sel ? clrText : FrameColor(gFrames[i]), RGBTRANS, s);
    }
    Bar(0, H - 16, W, H, clrBar);
    Text(4, H - 16, clrDim, RGBTRANS, "F1 X1  F3 X2  F4 CSV  F2 back");
    // marks of the frames under X1 / X2
    for (int r = 0; r < ListRows; r++)
    {
      int i = top + r;
      if (i >= gFrameN) break;
      int y = ListY + r * ListH;
      if (gFrames[i].t0 == cur[0]) Text(W - 36, y, clrBack, clrX1, "X1");
      if (gFrames[i].t1 + 1 == cur[1] || (gFrames[i].t0 == cur[1] && (gFrames[i].flags & Dec::FlPoint)))
        Text(W - 18, y, clrBack, clrX2, "X2");
    }
  }

  // F1: X1 to the start of the selected frame, F3: X2 to its end; back with
  // both set: X1 .. X2 on the whole screen, one: the view at it
  void ListPage()
  {
    if (!gFrameN)
    {
      Status(clrWarn, dc.proto == Dec::POff ? "decoder off" : "no frames");
      return;
    }
    // the first frame at X1
    int sel = 0;
    while (sel + 1 < gFrameN && gFrames[sel].t0 < cur[0]) sel++;
    int top = sel > 2 ? sel - 2 : 0;
    bool set1 = false, set2 = false;
    LCD::Clear(clrBack);
    DrawList(top, sel);
    while (true)
    {
      KEY::EKey key = KEY::GetKey();
      if (key == KEY::None)
        continue;
      if (key == KEY::Escape) break;
      if (key == KEY::F1 || key == KEY::F3)
      {
        const Dec::TFrame& f = gFrames[sel];
        if (key == KEY::F1) { cur[0] = f.t0; set1 = true; }
        else { cur[1] = (f.flags & Dec::FlPoint) ? f.t0 : f.t1 + 1; set2 = true; }
        DrawList(top, sel);
        continue;
      }
      if (key == KEY::F4)
      {
        bool ok = SaveCsv();
        Bar(W - 120, H - 16, W, H, clrBar);
        Text(W - 116, H - 16, ok ? clrOk : clrTrig, RGBTRANS, ok ? status : "save error");
        continue;
      }
      int d = key == KEY::Up ? -1 : key == KEY::Down ? 1 : key == KEY::Left ? -ListRows : key == KEY::Right ? ListRows : 0;
      if (!d) continue;
      sel += d;
      if (sel < 0) sel = 0;
      if (sel >= gFrameN) sel = gFrameN - 1;
      if (sel < top) top = sel;
      if (sel >= top + ListRows) top = sel - ListRows + 1;
      DrawList(top, sel);
    }
    LCD::Clear(clrBack);
    status[0] = 0;
    if (set1 && set2)
    {
      ZoomX1X2();                       // draws
      return;
    }
    if (set1 || set2)
    {
      CenterOn(cur[set1 ? 0 : 1]);
      nItem = set1 ? ItX1 : ItX2;
    }
    DrawAll();
  }

  // ------------------------------------------------------------ keys
  KEY::EKey lastKey = KEY::None;
  uint32_t lastTick = 0;
  int repeat = 0;

  int Accel() { return repeat < 4 ? 1 : repeat < 12 ? 4 : repeat < 30 ? 16 : 64; }

  void MoveCursor(int c, int d)
  {
    int64_t s = (int64_t)cur[c] + (int64_t)d * Accel() * (scale > 8 ? scale / 8 : 1);
    if (s < 0) s = 0;
    if (s > (int64_t)gEnd) s = gEnd;
    cur[c] = (uint32_t)s;
    // keep it visible
    int x = SampleToX(cur[c]);
    if (x < 0 || x >= PlotW)
    {
      CenterOn(cur[c]);
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
      case ItLanes:
        if (gLaVersion < LaVersionMask) { Status(clrWarn, "needs LAFPGA.BIN 9.02"); return; }
        nLanes = (nLanes + d + LaneSets) % LaneSets;
        break;
      case ItView:
      {
        int64_t step = (int64_t)(PlotW / 8) * scale;
        int64_t v = (int64_t)viewLeft + d * step * (repeat < 6 ? 1 : 4);
        viewLeft = v < 0 ? 0 : (uint32_t)v;
        ClampView();
        DrawPlot();
        return;
      }
      case ItZoom:
      {
        // around the middle of the view
        // right: in (half the samples per column), left: out
        uint32_t mid = viewLeft + (Span8() / 2);
        if (d > 0) scale = scale > 1 ? scale / 2 : 1;
        else scale = scale < ScaleMax ? scale * 2 : ScaleMax;
        gZoomX = false;
        int64_t left = (int64_t)mid - Span8() / 2;
        viewLeft = left < 0 ? 0 : (uint32_t)left;
        ClampView();
        DrawPlot();
        return;
      }
      case ItX1: case ItX2:
        if (gHave) { MoveCursor(nItem - ItX1, d); DrawPlot(); }
        return;
      case ItGen:
        gc.mode = (int8_t)((gc.mode + d + Gen::GModes) % Gen::GModes);
        if (gSqOn) GenOff();
        {
          char s[48];
          GenInfo(s);
          s[40] = 0;
          Status(clrText, s);
        }
        DrawTitle();
        return;
      case ItDec:
        dc.proto = (int8_t)((dc.proto + d + Dec::Protos) % Dec::Protos);
        Status(clrText, "");
        DecodeAndShow();
        DrawPlot();
        DrawMenu();
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
          gRetries = 0;
          if (nTrig == TrigDec && !DecTrigOn())
            Status(clrWarn, "trigger: F3 on Dec");
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
        if (nItem == ItDec) { SetupPage(0); break; }
        if (nItem == ItGen)
        {
          SetupPage(1);
          if (gSqOn && (gc.mode != Gen::GSquare || !GenSquare())) GenOff();
          DrawMenu();
          break;
        }
        if (!gHave) break;
        if (nItem == ItList) ListPage();
        else if (nItem == ItX1 || nItem == ItX2) NextEdge();
        else if (nItem == ItZoom)
        {
          // X1 .. X2 on the screen, again: the whole capture
          if (gZoomX) { Fit(); DrawPlot(); Status(clrText, ""); }
          else ZoomX1X2();
        }
        else { Fit(); DrawPlot(); }
        break;
      case KEY::F4:
        if (nItem == ItX1 || nItem == ItX2)
        {
          // X1 .. X2 on the screen, again: the whole capture
          if (gZoomX) { Fit(); DrawPlot(); Status(clrText, ""); }
          else ZoomX1X2();
        }
        else if (nItem == ItBmp) SaveBmp();
        else if (nItem == ItList || nItem == ItDec) { if (gHave) ListPage(); }
        else if (nItem == ItGen)
        {
          char s[48];
          if (gc.mode == Gen::GSquare)
          {
            if (gSqOn) { GenOff(); strcpy(s, "square off"); }
            else if (GenSquare()) { gSqOn = true; GenInfo(s); }
            else strcpy(s, "frequency out of range");
          }
          else if (gc.mode == Gen::GOff) strcpy(s, "generator off: F3");
          else GenFire(s);
          s[40] = 0;
          Status(clrOk, s);
          DrawTitle();
        }
        else SaveVcd();
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
    Text(8, 106, clrTrig, RGBTRANS, "analyzer image (version 9.01 / 9.02).");
    Text(8, 130, clrDim, RGBTRANS, "Copy it to the disk. Any key: exit");
    while (KEY::GetKey() == KEY::None);
    return 0;
  }
  LoadCalibration();
  Dec::Defaults(dc);
  LoadDecoder();
  LoadGen();
  bool repaired = Sanitize();
  LCD::Clear(clrBack);
  DrawAll();
  if (repaired)
    Status(clrWarn, "settings repaired");

  // stack guard: 128 bytes above .got; changed bytes mean the stack came
  // down to the variables of the application
  uint8_t* guard = nullptr;
  bool guardHit = false;
#ifdef _ARM
  {
    uint8_t here;
    uint8_t* g = (uint8_t*)((((uintptr_t)&_addressRamEnd) + 0x80 + 3) & ~(uintptr_t)3);
    if (&here - g > 1024)
    {
      memset(g, 0xA5, 128);
      guard = g;
    }
  }
#endif

  while (true)
  {
    KEY::EKey key = KEY::GetKey();
    if (key != KEY::None && !OnKey(key))
      break;
    if (eRun != Idle)
      PollCapture();
    if (guard && !guardHit)
      for (int i = 0; i < 128; i++)
        if (guard[i] != 0xA5)
        {
          guardHit = true;
          Status(clrTrig, "stack overflow!");
          break;
        }
    if (gFireAt && (int32_t)(SYS::GetTick() - gFireAt) >= 0)
    {
      gFireAt = 0;
      char s[48];
      GenFire(s);
    }
  }
  GenOff();
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

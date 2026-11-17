// FPGA 2.66 tester for the DS203: edge counters of CH_A..CH_D and the
// quadrature decoders, with the signal of Wave Out.
//
// Pages (Left / Right):
//   Tests  automatic tests, groups with their own wiring (A-C, A-D, A-A,
//          A-B: edge counters of one channel, B: quadrature; Up / Down
//          chooses the group, F1 runs it)
//   Edges  the eight edge counters live, rates, generator (Up / Down),
//          F3 clears the counters, F4 measures the thresholds of CH_A/CH_B
//   Quad   both quadrature decoders live, control presets (Up / Down),
//          F3 sets the zero of the display, F4 measures the thresholds
// F2 leaves the application.

#include <library.h>
#include "fpga.h"

using namespace BIOS;

namespace
{
  const uint16_t clrBack = RGB565(101010);
  const uint16_t clrTitle = RGB565(4040b0);
  const uint16_t clrText = RGB565(ffffff);
  const uint16_t clrDim = RGB565(a0a0a0);
  const uint16_t clrOk = RGB565(40ff40);
  const uint16_t clrFail = RGB565(ff4040);
  const uint16_t clrWarn = RGB565(ffff40);
  const int RowH = 14;

  enum class EPage { Tests, Edges, Quad, Total };
  EPage page = EPage::Tests;

  // ------------------------------------------------------------ drawing
  void Line(int row, uint16_t clr, const char* format, ...)
  {
    char buf[64];
    va_list args;
    va_start(args, format);
    vsprintf(buf, format, args);
    va_end(args);
    int y = 18 + row * RowH;
    int x = 4 + LCD::Print(4, y, clr, clrBack, buf);   // Print returns the width
    if (x < LCD::Width)
      LCD::Bar(x, y, LCD::Width, y + RowH, clrBack);
  }

  void ClearBody()
  {
    LCD::Bar(0, 16, LCD::Width, LCD::Height, clrBack);
  }

  void Title()
  {
    const char* names[] = {"Tests", "Edges", "Quad"};
    LCD::Bar(0, 0, LCD::Width, 16, clrTitle);
    int x = 4 + LCD::Print(4, 1, clrText, clrTitle, "FPGA 2.66 test");   // Print returns the width
    x += 16;
    for (int i = 0; i < (int)EPage::Total; i++)
    {
      bool sel = i == (int)page;
      x += LCD::Print(x, 1, sel ? RGB565(000000) : clrDim, sel ? clrText : clrTitle, names[i]);
      x += 8;
    }
    LCD::Print(LCD::Width - 8 * 9, 1, clrDim, clrTitle, "F2: exit");
  }

  // ------------------------------------------------------------ ADC / MCI
  // sampling clock MCI = 72 MHz / timer of the time base: 500 ns/div 36 MHz,
  // 5 us/div 6 MHz, 10 us/div 3 MHz
  void Sampling(float timePerDiv)
  {
    ADC::Enable(true);
    ADC::ConfigureTrigger(0, 0, ADC::ETriggerType::None, ADC::EInput::CH1);
    ADC::ConfigureTimebase(timePerDiv);
    ADC::Restart(0);
  }

  void AdcFast()
  {
    // sampling clock MCI 3 MHz for the decoder in MCI
    ADC::Enable(true);
    ADC::ConfigureTrigger(0, 0, ADC::ETriggerType::None, ADC::EInput::CH1);
    ADC::ConfigureTimebase(10e-6f);
    ADC::Restart(0);
  }

  struct TThresholds
  {
    bool valid{false}, validA{false}, validB{false};
    int minA, maxA, minB, maxB;
    uint8_t thrA{128}, hysA{8}, thrB{128}, hysB{8};
  } thresholds;

  // inputs at 1 V/div, the square of Wave Out running: levels of CH_A and CH_B
  bool MeasureThresholds()
  {
    ADC::Enable(true);
    ADC::ConfigureInput(ADC::EInput::CH1, ADC::ECouple::DC, ADC::EResolution::_1V, 40 * 4);
    ADC::ConfigureInput(ADC::EInput::CH2, ADC::ECouple::DC, ADC::EResolution::_1V, 40 * 4);
    ADC::ConfigureTrigger(0, 0, ADC::ETriggerType::None, ADC::EInput::CH1);
    ADC::ConfigureTimebase(1e-3f);       // 30 kS/s: 4096 samples hold 136 periods of 1 kHz
    SYS::DelayMs(50);
    ADC::Restart(0);
    uint32_t t0 = SYS::GetTick();
    while (ADC::GetState() != ADC::EState::Full && SYS::GetTick() - t0 < 1000)
      SYS::DelayMs(10);

    int minA = 255, maxA = 0, minB = 255, maxB = 0;
    for (int i = 0; i < ADC::NumSamples; i++)
    {
      ADC::TSample s;
      s.value = ADC::Get();
      if (i < 16)
        continue;   // first samples of the FIFO
      minA = min(minA, (int)s.CH1); maxA = max(maxA, (int)s.CH1);
      minB = min(minB, (int)s.CH2); maxB = max(maxB, (int)s.CH2);
    }
    thresholds.minA = minA; thresholds.maxA = maxA;
    thresholds.minB = minB; thresholds.maxB = maxB;
    thresholds.validA = maxA - minA >= 20;
    thresholds.validB = maxB - minB >= 20;
    thresholds.valid = thresholds.validA && thresholds.validB;
    // a channel without a signal gets a threshold above its range: no edges
    thresholds.thrA = thresholds.validA ? (minA + maxA) / 2 : 250;
    thresholds.hysA = thresholds.validA ? max(2, (maxA - minA) / 8) : 4;
    thresholds.thrB = thresholds.validB ? (minB + maxB) / 2 : 250;
    thresholds.hysB = thresholds.validB ? max(2, (maxB - minB) / 8) : 4;
    Fpga::SetThresholds(thresholds.thrA, thresholds.hysA, thresholds.thrB, thresholds.hysB);
    AdcFast();
    return thresholds.valid;
  }

  // ------------------------------------------------------------ tests
  struct TResult
  {
    char text[52];
    int state;      // 0 info, 1 ok, 2 fail
  };
  TResult results[11];   // rows 3..13 of the page
  int resultCount = 0;
  int passed = 0, failed = 0;

  void Result(int state, const char* format, ...)
  {
    if (resultCount >= (int)COUNT(results))
      return;
    TResult& r = results[resultCount++];
    va_list args;
    va_start(args, format);
    vsprintf(r.text, format, args);
    va_end(args);
    r.state = state;
    if (state == 1) passed++;
    if (state == 2) failed++;
  }

  void Check(bool ok, const char* format, ...)
  {
    if (resultCount >= (int)COUNT(results))
      return;
    TResult& r = results[resultCount++];
    char buf[48];
    va_list args;
    va_start(args, format);
    vsprintf(buf, format, args);
    va_end(args);
    strcpy(r.text, ok ? "OK   " : "FAIL ");
    strcat(r.text, buf);
    r.state = ok ? 1 : 2;
    if (ok) passed++; else failed++;
  }

  uint32_t Diff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

  void TestIdentity()
  {
    uint16_t status = Fpga::Status();
    Check((status >> 6) == 266, "version %d (status %04x)", status >> 6, status);
    uint16_t id1 = Fpga::Read(0x18), id2 = Fpga::Read(0x19), id3 = Fpga::Read(0x1A);
    Check(id1 == 0xC0D2 && id2 == 0xC0D4 && id3 == 0xC0D6, "ID %04x %04x %04x", id1, id2, id3);
  }

  // groups A-C / A-D: Wave Out -> CH_C or CH_D (one connection)
  void TestEdges(int ch)
  {
    const char name = ch == 0 ? 'C' : 'D';
    const int r = 2 * ch, f = 2 * ch + 1;   // rising / falling counter of the channel
    Wave::Low();
    SYS::DelayMs(5);

    Fpga::ClearEdges();
    uint32_t e[4];
    for (int i = 0; i < 4; i++) e[i] = Fpga::Edges(i);
    Check((e[0] | e[1] | e[2] | e[3]) == 0, "clear: %u %u %u %u", e[0], e[1], e[2], e[3]);

    // exact number of pulses
    const uint32_t n = 100000;
    Wave::Pulses(n);
    SYS::DelayMs(2);
    for (int i = 0; i < 4; i++) e[i] = Fpga::Edges(i);
    if (e[r] == 0 && e[f] == 0)
    {
      Result(2, "FAIL no edges: Wave Out -> CH_%c?", name);
      return;
    }
    Check(e[r] == n && e[f] == n, "%c %u pulses: %u/%u", name, n, e[r], e[f]);

    // square bursts: rising = falling, count = f * t. 12 MHz only informs: the
    // output stage of Wave Out does not pass it (DS203 HW 2.7: 0 edges at
    // 12 MHz, exact at 6 MHz), a failure there is not one of the FPGA.
    static const uint32_t freqs[] = {1000, 100000, 1000000, 6000000, 9000000, 12000000};
    for (uint32_t freq : freqs)
    {
      Fpga::ClearEdges();
      uint32_t real = Wave::Square(freq);
      uint32_t t0 = SYS::GetTick();
      SYS::DelayMs(500);
      uint32_t t1 = SYS::GetTick();
      Wave::Low();
      SYS::DelayMs(2);
      uint32_t up = Fpga::Edges(r), down = Fpga::Edges(f);
      uint32_t expect = (uint32_t)((uint64_t)real * (t1 - t0) / 1000);
      uint32_t tol = expect / 200 + 3;      // 0.5 %: start / stop by the ms tick
      bool ok = up == down && Diff(up, expect) <= tol;
      uint32_t fk = real >= 1000000 ? real / 1000000 : real / 1000;
      if (freq == 12000000)
        Result(0, "info %c %u%s: %u/%u (%u)", name, fk, real >= 1000000 ? "M" : "k", up, down, expect);
      else
        Check(ok, "%c %u%s: %u/%u (%u)", name, fk, real >= 1000000 ? "M" : "k", up, down, expect);
    }

    // reading while counting: the Gray code latch gives no jumps
    Fpga::ClearEdges();
    Wave::Square(6000000);
    uint32_t first = Fpga::Edges(r);
    uint32_t prev = first, bad = 0, maxStep = 0;
    for (int i = 0; i < 2000; i++)
    {
      uint32_t v = Fpga::Edges(i & 1 ? f : r);
      if ((i & 1) == 0)
      {
        uint32_t step = v - prev;
        if (v < prev || step > 50000) bad++;
        maxStep = max(maxStep, step);
        prev = v;
      }
    }
    Wave::Low();
    // the counter has to run (~6 counts per us), never go back, never jump
    Check(bad == 0 && prev - first > 1000, "%c read at 6 MHz: %u bad, step %u", name, bad, maxStep);
  }

  // square bursts of 0.5 s, rising = falling = f * t (0.5 % + 3: the ms tick)
  void Burst(char name, int r, int f, uint32_t freq, bool info, const char* note = "")
  {
    Fpga::ClearEdges();
    uint32_t real = Wave::Square(freq);
    uint32_t t0 = SYS::GetTick();
    SYS::DelayMs(500);
    uint32_t t1 = SYS::GetTick();
    Wave::Low();
    SYS::DelayMs(2);
    uint32_t up = Fpga::Edges(r), down = Fpga::Edges(f);
    uint32_t expect = (uint32_t)((uint64_t)real * (t1 - t0) / 1000);
    uint32_t tol = expect / 200 + 3;
    bool ok = up == down && Diff(up, expect) <= tol;
    uint32_t fk = real >= 1000000 ? real / 1000000 : real / 1000;
    const char* unit = real >= 1000000 ? "M" : "k";
    if (info)
      Result(0, "info %c %u%s%s: %u/%u (%u)", name, fk, unit, note, up, down, expect);
    else
      Check(ok, "%c %u%s%s: %u/%u (%u)", name, fk, unit, note, up, down, expect);
  }

  // groups A-A / A-B: Wave Out -> CH_A or CH_B (one connection). The FPGA
  // counts the edges of the comparator in the sampling clock: exact up to
  // half the sampling rate, the thresholds come from the ADC levels.
  void TestEdgesAnalog(int ch)
  {
    const char name = ch == 0 ? 'A' : 'B', other = ch == 0 ? 'B' : 'A';
    const int r = 4 + 2 * ch, f = r + 1, ro = 4 + 2 * (1 - ch);
    Wave::Low();
    Wave::Square(1000);
    MeasureThresholds();           // the other channel gets a threshold above its range
    Wave::Low();
    bool levels = ch == 0 ? thresholds.validA : thresholds.validB;
    if (ch == 0)
      Check(levels, "levels A %d..%d, thr %d/%d", thresholds.minA, thresholds.maxA, thresholds.thrA, thresholds.hysA);
    else
      Check(levels, "levels B %d..%d, thr %d/%d", thresholds.minB, thresholds.maxB, thresholds.thrB, thresholds.hysB);
    if (!levels)
    {
      Result(0, "     Wave Out -> CH_%c?", name);
      return;
    }

    Sampling(500e-9f);             // MCI 36 MHz
    SYS::DelayMs(5);
    Fpga::ClearEdges();
    SYS::DelayMs(1);
    uint32_t c0 = Fpga::Edges(r) + Fpga::Edges(f);

    // exact number of pulses (about 1 MHz, short high level), from a clear
    const uint32_t n = 100000;
    Wave::Pulses(n);
    SYS::DelayMs(2);
    uint32_t e0 = Fpga::Edges(r), e1 = Fpga::Edges(f);
    if (e0 == 0 && e1 == 0)
    {
      Result(2, "FAIL no edges: Wave Out -> CH_%c?", name);
      return;
    }
    Check(c0 == 0 && e0 == n && e1 == n, "%c clear %u, %u pulses: %u/%u", name, c0, n, e0, e1);

    // sampled at 36 MHz; 6 MHz informs only (analog bandwidth of the input)
    static const uint32_t freqs[] = {1000000, 3000000};
    for (uint32_t freq : freqs)
      Burst(name, r, f, freq, false);
    Burst(name, r, f, 6000000, true);
    uint32_t q = Fpga::Edges(ro) + Fpga::Edges(ro + 1);
    Check(q == 0, "CH_%c without signal: %u edges", other, q);

    // half the sampling rate: MCI 6 MHz, 2 MHz is exact, 4 MHz is above fs/2
    Sampling(5e-6f);
    SYS::DelayMs(5);
    Burst(name, r, f, 2000000, false, " fs6M");
    Burst(name, r, f, 4000000, true, " fs6M");

    // reading while counting: no jumps
    Sampling(500e-9f);
    SYS::DelayMs(5);
    Fpga::ClearEdges();
    Wave::Square(1000000);
    uint32_t first = Fpga::Edges(r);
    uint32_t prev = first, bad = 0, maxStep = 0;
    for (int i = 0; i < 2000; i++)
    {
      uint32_t v = Fpga::Edges(i & 1 ? f : r);
      if ((i & 1) == 0)
      {
        uint32_t step = v - prev;
        if (v < prev || step > 10000) bad++;
        maxStep = max(maxStep, step);
        prev = v;
      }
    }
    Wave::Low();
    Check(bad == 0 && prev - first > 100, "%c read at 1 MHz: %u bad, step %u", name, bad, maxStep);
  }

  // group B: Wave Out -> CH_A, CH_C; Wave Out -> 10k -> CH_B, CH_D, 33n to GND
  void TestQuadrature()
  {
    Wave::Low();
    Fpga::SetParam(0x0F, Fpga::QuadCtrlDefault);
    Wave::Square(1000);
    bool levels = MeasureThresholds();
    Wave::Low();
    Check(levels, "levels A %d..%d B %d..%d", thresholds.minA, thresholds.maxA, thresholds.minB, thresholds.maxB);
    if (!levels)
    {
      Result(0, "     CH_A, CH_B: wiring of group B?");
      return;
    }

    for (int dir = 0; dir < 2; dir++)
    {
      // A = CH_A, B = CH_B (delayed: forward), then swapped; Z = CH_C
      uint8_t ctrl = dir == 0 ? Fpga::QuadCtrl(0, 1, 2, true, false) : Fpga::QuadCtrl(1, 0, 2, true, false);
      Fpga::SetParam(0x0F, ctrl);
      SYS::DelayMs(20);
      Fpga::ClearEdges();
      Fpga::Sync s0 = Fpga::ReadSync();
      Fpga::Quad q0 = Fpga::ReadQuad(0x20);

      Wave::Square(1000);
      SYS::DelayMs(1000);
      Wave::Low();
      SYS::DelayMs(20);        // B (RC) settles low: both decoders end in phase 0

      uint32_t n = Fpga::Edges(0);
      Fpga::Sync s1 = Fpga::ReadSync();
      Fpga::Quad q1 = Fpga::ReadQuad(0x20);
      int32_t dSync = Fpga::Sign24(s1.pos - s0.pos);
      int32_t dPins = Fpga::Sign24(q1.pos - q0.pos);
      uint32_t dErr = (s1.err - s0.err) & 0xff;
      uint32_t dZ = (s1.z - s0.z) & 0xffff;
      int32_t expect = (dir == 0 ? 4 : -4) * (int32_t)n;

      if (n < 900)
      {
        Result(2, "FAIL CH_C: %u edges (Wave Out -> CH_C?)", n);
        continue;
      }
      Check(dSync == expect && dErr == 0, "MCI %s: %d (4x%u), err %u", dir ? "B,A" : "A,B", dSync, n, dErr);
      Check(dZ == n, "MCI Z on CH_C: %u", dZ);
      if (dir == 0)
        Check(dPins == 4 * (int32_t)n, "pins C,D: %d (4x%u)", dPins, n);
    }
    Fpga::SetParam(0x0F, Fpga::QuadCtrlDefault);
  }

  int testGroup = 0;
  bool testsRun = false;

  void DrawTests()
  {
    const char* groups[] = {"A-C", "A-D", "A-A", "A-B", "B"};
    const char chs[] = {'C', 'D', 'A', 'B'};
    Line(0, clrText, "Group %s (Up/Down), F1 runs it", groups[testGroup]);
    if (testGroup < 4)
    {
      Line(1, clrDim, "A: edge counters of CH_%c. Wiring:", chs[testGroup]);
      Line(2, clrDim, "   Wave Out -> CH_%c", chs[testGroup]);
    }
    else
    {
      Line(1, clrDim, "B: quadrature. Wave Out -> CH_A, CH_C;");
      Line(2, clrDim, "   Wave Out -10k- CH_B, CH_D, 33n to GND");
    }
    if (!testsRun)
    {
      for (int i = 3; i < 15; i++)
        Line(i, clrBack, "");
      return;
    }
    for (int i = 0; i < 11; i++)
    {
      if (i < resultCount)
        Line(3 + i, results[i].state == 1 ? clrOk : results[i].state == 2 ? clrFail : clrText, "%s", results[i].text);
      else
        Line(3 + i, clrBack, "");
    }
    Line(14, failed ? clrFail : clrOk, "passed %d, failed %d", passed, failed);
  }

  void RunTests()
  {
    resultCount = 0;
    passed = failed = 0;
    testsRun = true;
    for (int i = 3; i < 15; i++)
      Line(i, clrBack, "");
    Line(3, clrWarn, "running...");
    TestIdentity();
    if (testGroup < 2)
      TestEdges(testGroup);
    else if (testGroup < 4)
      TestEdgesAnalog(testGroup - 2);
    else
      TestQuadrature();
    AdcFast();
    Fpga::SetThresholds(thresholds.thrA, thresholds.hysA, thresholds.thrB, thresholds.hysB);
  }

  // ------------------------------------------------------------ edges page
  struct TGenPreset { uint32_t freq; const char* name; };
  const TGenPreset genPresets[] = {
    {0, "off"}, {1, "1 Hz"}, {10, "10 Hz"}, {100, "100 Hz"}, {1000, "1 kHz"}, {10000, "10 kHz"},
    {100000, "100 kHz"}, {1000000, "1 MHz"}, {6000000, "6 MHz"}, {12000000, "12 MHz"}};
  int genPreset = 0;
  uint32_t genReal = 0;

  void SetGenerator()
  {
    if (genPresets[genPreset].freq == 0)
    {
      Wave::Low();
      genReal = 0;
    } else
      genReal = Wave::Square(genPresets[genPreset].freq);
  }

  uint32_t lastEdges[8];
  uint32_t lastTick = 0;
  uint32_t rates[8];

  void DrawEdges(bool force)
  {
    uint32_t now = SYS::GetTick();
    uint32_t e[8];
    for (int i = 0; i < 8; i++) e[i] = Fpga::Edges(i);
    if (now - lastTick >= 1000 || force)
    {
      uint32_t dt = now - lastTick;
      for (int i = 0; i < 8; i++)
      {
        rates[i] = (force || dt == 0) ? 0 : (uint32_t)((uint64_t)(e[i] - lastEdges[i]) * 1000 / dt);
        lastEdges[i] = e[i];
      }
      lastTick = now;
    }
    const char* names[] = {"C rising ", "C falling", "D rising ", "D falling",
                           "A rising ", "A falling", "B rising ", "B falling"};
    Line(0, clrText, "Generator (Up/Down): %s  %u Hz", genPresets[genPreset].name, genReal);
    Line(1, clrDim, "Wave Out -> any CH, F3 clears, F4 thr A/B");
    Line(2, clrDim, "A/B: 3 MS/s, thr A %d/%d B %d/%d", thresholds.thrA, thresholds.hysA, thresholds.thrB, thresholds.hysB);
    Line(3, clrDim, "counter        count        per second");
    for (int i = 0; i < 8; i++)
      Line(4 + i, clrText, "%s  %10u  %10u", names[i], e[i], rates[i]);
    Line(12, clrDim, "rising - falling: C %d  D %d", (int)(e[0] - e[1]), (int)(e[2] - e[3]));
    Line(13, clrDim, "rising - falling: A %d  B %d", (int)(e[4] - e[5]), (int)(e[6] - e[7]));
  }

  // ------------------------------------------------------------ quad page
  struct TCtrlPreset { uint8_t ctrl; const char* name; };
  const TCtrlPreset ctrlPresets[] = {
    {0x0E, "A=C B=D, Z off"},
    {0x4E, "A=C B=D, Z=CH_A"},
    {0xCE, "A=C B=D, Z=CH_A low"},
    {0x5E, "A=C B=D, Z=CH_B"},
    {0x64, "A=CH_A B=CH_B, Z=CH_C"},
    {0x61, "A=CH_B B=CH_A, Z=CH_C"},
    {0x04, "A=CH_A B=CH_B, Z off"},
  };
  int ctrlPreset = 0;
  int32_t zeroPins = 0, zeroSync = 0;

  void DrawQuad()
  {
    Fpga::Quad q = Fpga::ReadQuad(0x20);
    Fpga::Quad qz = Fpga::ReadQuad(0x28);
    uint32_t zc = Fpga::ZCount();
    Fpga::Sync s = Fpga::ReadSync();

    Line(0, clrText, "Control (Up/Down): %s", ctrlPresets[ctrlPreset].name);
    Line(1, clrDim, "F3 zero, F4 thresholds (needs 1 kHz on A/B)");
    Line(2, clrDim, "thr A %d/%d B %d/%d", thresholds.thrA, thresholds.hysA, thresholds.thrB, thresholds.hysB);
    Line(4, clrWarn, "pins (A=CH_C, B=CH_D)");
    Line(5, clrText, " position   %9d", Fpga::Sign24(q.pos - zeroPins));
    Line(6, clrText, " at last Z  %9d   Z count %u", Fpga::Sign24(qz.pos - zeroPins), zc);
    Line(7, clrText, " in turn    %9d", Fpga::Sign24(q.pos - qz.pos));
    Line(8, clrDim, " AR %u AF %u BR %u BF %u", q.part[0], q.part[1], q.part[2], q.part[3]);
    Line(10, clrWarn, "decoder in MCI (control above)");
    Line(11, clrText, " position   %9d   errors %u", Fpga::Sign24(s.pos - zeroSync), s.err);
    Line(12, clrText, " at last Z  %9d   Z count %u", Fpga::Sign24(s.atZ - zeroSync), s.z);
    Line(13, clrText, " in turn    %9d", Fpga::Sign24(s.pos - s.atZ));
  }

  // ------------------------------------------------------------ app
  void Redraw()
  {
    Title();
    ClearBody();
    switch (page)
    {
      case EPage::Tests: DrawTests(); break;
      case EPage::Edges: DrawEdges(true); break;
      case EPage::Quad: DrawQuad(); break;
      default: break;
    }
  }

  void OnKey(KEY::EKey key)
  {
    switch (key)
    {
      case KEY::Left:
        page = (EPage)(((int)page + (int)EPage::Total - 1) % (int)EPage::Total);
        Redraw();
        return;
      case KEY::Right:
        page = (EPage)(((int)page + 1) % (int)EPage::Total);
        Redraw();
        return;
      default:
        break;
    }

    switch (page)
    {
      case EPage::Tests:
        if (key == KEY::Up || key == KEY::Down)
        {
          testGroup = (testGroup + (key == KEY::Up ? 1 : 4)) % 5;   // A-C, A-D, A-A, A-B, B
          testsRun = false;
        }
        if (key == KEY::F1)
          RunTests();
        DrawTests();
        break;
      case EPage::Edges:
        if (key == KEY::Up && genPreset < (int)COUNT(genPresets) - 1) genPreset++;
        if (key == KEY::Down && genPreset > 0) genPreset--;
        if (key == KEY::Up || key == KEY::Down) SetGenerator();
        if (key == KEY::F3) Fpga::ClearEdges();
        if (key == KEY::F4)
        {
          // levels of the square of Wave Out at 1 kHz, then the generator as it was
          Wave::Square(1000);
          MeasureThresholds();
          SetGenerator();
        }
        DrawEdges(true);
        break;
      case EPage::Quad:
        if (key == KEY::Up && ctrlPreset < (int)COUNT(ctrlPresets) - 1) ctrlPreset++;
        if (key == KEY::Down && ctrlPreset > 0) ctrlPreset--;
        if (key == KEY::Up || key == KEY::Down)
          Fpga::SetParam(0x0F, ctrlPresets[ctrlPreset].ctrl);
        if (key == KEY::F3)
        {
          zeroPins = Fpga::ReadQuad(0x20).pos;
          zeroSync = Fpga::ReadSync().pos;
        }
        if (key == KEY::F4)
          MeasureThresholds();
        DrawQuad();
        break;
      default:
        break;
    }
  }
}

#ifdef _ARM
__attribute__((__section__(".entry")))
#endif
int _main(void)
{
  LCD::Clear(clrBack);
  DAC::SetMode(DAC::EMode::LogicLow, nullptr, 0);   // releases PA4 (DAC), Wave Out low
  Fpga::Init();
  Wave::Low();
  // inputs as used by the threshold measurement (F4, group B)
  ADC::Enable(true);
  ADC::ConfigureInput(ADC::EInput::CH1, ADC::ECouple::DC, ADC::EResolution::_1V, 40 * 4);
  ADC::ConfigureInput(ADC::EInput::CH2, ADC::ECouple::DC, ADC::EResolution::_1V, 40 * 4);
  AdcFast();
  Fpga::SetParam(0x0F, Fpga::QuadCtrlDefault);
  Fpga::SetThresholds(thresholds.thrA, thresholds.hysA, thresholds.thrB, thresholds.hysB);

  Redraw();
  uint32_t lastDraw = 0;
  while (true)
  {
    KEY::EKey key = KEY::GetKey();
    if (key == KEY::Escape)
      break;
    if (key != KEY::None)
      OnKey(key);

    uint32_t now = SYS::GetTick();
    if (now - lastDraw >= 200)
    {
      lastDraw = now;
      if (page == EPage::Edges) DrawEdges(false);
      if (page == EPage::Quad) DrawQuad();
    }
  }

  Wave::Low();
  Fpga::SetParam(0x0F, Fpga::QuadCtrlDefault);
  return 0;
}

void _HandleAssertion(const char* file, int line, const char* cond)
{
  BIOS::DBG::Print("Assertion failed in ");
  BIOS::DBG::Print(file);
  BIOS::DBG::Print(" [%d]: %s\n", line, cond);
  while (1);
}

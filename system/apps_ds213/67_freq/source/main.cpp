// Frequency response analyzer for the DS203 (LA104 OS), after the one of
// Petteri Aimonen (essentialscrap.com/dsoquad/freq.html).
//
// The generator (Wave Out, DAC) gives a sine from a table with a whole
// number of periods, so its frequency is exact; CH A (and CH B) are sampled
// over whole periods and the component at that frequency is taken with a
// single bin DFT (Hann window). V/div and position are chosen for every
// point. Mode A/B: B on the input of the circuit (the generator), A on its
// output -> gain and phase of A/B. Mode A: |A| in dBV (no phase reference).
// Cal: a sweep with the inputs on the generator directly, later sweeps are
// divided by it (removes the differences of the channels / the generator).
//
// Keys: Up / Down item, Left / Right change (Cursor: move over the points),
// F1 sweep / stop, F2 exit, F3 calibration sweep, F4 save CSV.

#include <library.h>
#include "fra.h"
#include "../../65_minical/source/calib.h"

using namespace BIOS;

namespace
{
  const uint16_t clrBack = RGB565(000000);
  const uint16_t clrBar = RGB565(202060);
  const uint16_t clrText = RGB565(ffffff);
  const uint16_t clrDim = RGB565(808080);
  const uint16_t clrGrid = RGB565(303030);
  const uint16_t clrSel = RGB565(4060ff);
  const uint16_t clrGain = RGB565(ffff00);
  const uint16_t clrPhase = RGB565(00ffff);
  const uint16_t clrCur = RGB565(ff6060);
  const uint16_t clrOk = RGB565(40ff40);
  const uint16_t clrWarn = RGB565(ffc040);
  const uint16_t clrErr = RGB565(ff4040);

  const int W = 400, H = 240;
  const int TitleH = 16;
  const int PlotX = 34, PlotW = 330, PlotY = 20, PlotH = 160;   // 8 divisions of 20
  const int InfoY = PlotY + PlotH + 14;                          // below the frequency labels
  const int MenuY = InfoY + 15;

  // ------------------------------------------------------------ settings
  const float arrFreq[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1e3f, 2e3f, 5e3f,
    10e3f, 20e3f, 50e3f, 100e3f, 200e3f };
  const int Freqs = sizeof(arrFreq) / sizeof(arrFreq[0]);
  int nStart = 3, nStop = 16;                 // 10 Hz .. 200 kHz
  const int arrPoints[] = { 10, 20, 50, 100 };
  int nPoints = 2;
  bool bModeAB = true;
  bool bAc = true;
  const int arrAmp[] = { 25, 50, 75, 100 };
  int nAmp = 3;

  enum { ItStart, ItStop, ItPoints, ItMode, ItCouple, ItAmp, ItCursor, Items };
  int nItem = ItCursor;

  // ------------------------------------------------------------ results
  enum { MaxPoints = 100 };
  struct TPoint
  {
    float f;
    Fra::TCplx a, b;                          // volts (peak)
    bool ok;
  };
  TPoint gPt[MaxPoints];
  int gCount = 0, gTotal = 0;                 // measured / planned points
  Fra::TCplx gCal[MaxPoints];                 // reference H (calibration sweep)
  bool gHaveCal = false, gCalAB = true;
  int gCalPoints = 0, gCalStart = -1, gCalStop = -1;
  int nCursor = 0;

  enum { Idle, Sweep, CalSweep } eRun = Idle;
  char status[48] = "";
  uint16_t statusClr = clrText;

  Cal::TData gCalData;
  uint8_t gSector[512];

  // generator table, samples (in the RAM above the OS)
  uint16_t gDac[Fra::DacMax];
  __attribute__((section(".bss_hi"))) uint8_t gA[4096];
  __attribute__((section(".bss_hi"))) uint8_t gB[4096];

  int rangeA = 4, rangeB = 4, posA = 128, posB = 128;   // current input setup

  // ------------------------------------------------------------ text
  char* PutU(char* p, uint32_t n)
  {
    char t[12]; int i = 0;
    do { t[i++] = '0' + n % 10; n /= 10; } while (n);
    while (i) *p++ = t[--i];
    *p = 0;
    return p;
  }

  char* PutSI(char* p, float v, const char* unit)
  {
    static const char* const pre[] = { "n", "u", "m", "", "k", "M" };
    if (v < 0) { *p++ = '-'; v = -v; }
    int e = 3;
    if (v != 0)
    {
      while (v >= 999.5f && e < 5) { v /= 1000; e++; }
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

  // fixed point with 1 or 2 decimals
  char* PutFix(char* p, float v, int dec)
  {
    if (v < 0) { *p++ = '-'; v = -v; }
    int32_t scale = dec == 2 ? 100 : 10;
    int32_t n = (int32_t)(v * scale + 0.5f);
    p = PutU(p, n / scale);
    *p++ = '.';
    if (dec == 2) *p++ = '0' + (n / 10) % 10;
    *p++ = '0' + n % 10;
    *p = 0;
    return p;
  }

  void Bar(int x1, int y1, int x2, int y2, uint16_t c)
  {
    if (x1 < 0) x1 = 0;
    if (x2 > W) x2 = W;
    if (y1 < 0) y1 = 0;
    if (y2 > H) y2 = H;
    if (x2 > x1 && y2 > y1)
      LCD::Bar(x1, y1, x2, y2, c);
  }

  int Text(int x, int y, uint16_t c, const char* s) { return LCD::Print(x, y, c, RGBTRANS, s); }

  // line inside the plot
  void Line(int x0, int y0, int x1, int y1, uint16_t c)
  {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int n = 0; n < 2000; n++)
    {
      if (x0 >= PlotX && x0 < PlotX + PlotW && y0 >= PlotY && y0 < PlotY + PlotH)
        LCD::PutPixel(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  // ------------------------------------------------------------ results
  float FreqAt(int i, int n)
  {
    float f0 = arrFreq[nStart], f1 = arrFreq[nStop];
    if (n <= 1) return f0;
    float l0 = Fra::Log10(f0), l1 = Fra::Log10(f1);
    return Fra::Pow10(l0 + (l1 - l0) * i / (n - 1));
  }

  bool CalValid()
  {
    return gHaveCal && gCalAB == bModeAB && gCalPoints == arrPoints[nPoints] &&
           gCalStart == nStart && gCalStop == nStop;
  }

  // transfer function of a point: A/B (or A in volts), divided by the
  // calibration
  Fra::TCplx Response(int i)
  {
    const TPoint& p = gPt[i];
    Fra::TCplx h = bModeAB ? Fra::Div(p.a, p.b) : p.a;
    if (!bModeAB)
    {
      // no phase reference: magnitude only
      h.re = Fra::Mag(h);
      h.im = 0;
    }
    if (CalValid())
    {
      Fra::TCplx c = gCal[i];
      if (!bModeAB) { c.re = Fra::Mag(c); c.im = 0; }
      h = Fra::Div(h, c);
    }
    return h;
  }

  float GainDb(int i)
  {
    float m = Fra::Mag(Response(i));
    return m > 1e-9f ? 20 * Fra::Log10(m) : -180;
  }

  bool HasPhase() { return bModeAB; }

  // ------------------------------------------------------------ drawing
  // gain axis: top in dB (multiple of 10), 80 dB over the plot
  int gTopDb = 10;

  void UpdateScale()
  {
    float mx = -1000;
    for (int i = 0; i < gCount; i++)
      if (gPt[i].ok) { float g = GainDb(i); if (g > mx) mx = g; }
    if (mx < -900) { gTopDb = bModeAB || CalValid() ? 10 : 10; return; }
    int top = (int)((mx + 2) / 10);
    if (mx + 2 > 0) top++;
    gTopDb = top * 10;
    if (gTopDb > 60) gTopDb = 60;
    if (gTopDb < -40) gTopDb = -40;
  }

  int YGain(float db)
  {
    float y = PlotY + (gTopDb - db) * PlotH / 80.0f;
    return (int)y;
  }
  int YPhase(float deg) { return (int)(PlotY + (180 - deg) * PlotH / 360.0f); }
  int XFreq(float f)
  {
    float l0 = Fra::Log10(arrFreq[nStart]), l1 = Fra::Log10(arrFreq[nStop]);
    return PlotX + (int)((Fra::Log10(f) - l0) / (l1 - l0) * (PlotW - 1) + 0.5f);
  }

  void DrawTitle()
  {
    Bar(0, 0, W, TitleH, clrBar);
    char s[60], *p = s;
    strcpy(p, "Freq response "); p += strlen(p);
    strcpy(p, bModeAB ? "A/B" : "A (dBV)"); p += strlen(p);
    if (CalValid()) { strcpy(p, " cal"); p += 4; }
    Text(4, 0, clrText, s);
    const char* st = eRun == Sweep ? "SWEEP" : eRun == CalSweep ? "CAL" : "";
    if (st[0])
    {
      int w = strlen(st) * 8;
      Bar(W - w - 8, 1, W - 2, TitleH - 1, eRun == CalSweep ? clrWarn : clrOk);
      Text(W - w - 5, 0, clrBack, st);
    }
  }

  void DrawGrid()
  {
    Bar(0, TitleH, W, InfoY, clrBack);
    // horizontal: 8 divisions; left gain, right phase
    for (int d = 0; d <= 8; d++)
    {
      int y = PlotY + d * PlotH / 8;
      for (int x = PlotX; x < PlotX + PlotW; x += 3)
        LCD::PutPixel(x, y, clrGrid);
      char s[12];
      int db = gTopDb - d * 10;
      char* p = s;
      if (db < 0) { *p++ = '-'; PutU(p, -db); } else PutU(p, db);
      Text(PlotX - 4 - strlen(s) * 8, y - 7, clrGain, s);
      if (HasPhase() && (d % 2 == 0))
      {
        int deg = 180 - d * 45;
        p = s;
        if (deg < 0) { *p++ = '-'; PutU(p, -deg); } else PutU(p, deg);
        Text(PlotX + PlotW + 3, y - 7, clrPhase, s);
      }
    }
    // vertical: 1, 2, 5 of every decade
    float f0 = arrFreq[nStart], f1 = arrFreq[nStop];
    for (float dec = 1; dec <= 1e6f; dec *= 10)
    {
      const float mult[3] = { 1, 2, 5 };
      for (int m = 0; m < 3; m++)
      {
        float f = dec * mult[m];
        if (f < f0 * 0.999f || f > f1 * 1.001f) continue;
        int x = XFreq(f);
        for (int y = PlotY; y < PlotY + PlotH; y += (m == 0 ? 2 : 4))
          LCD::PutPixel(x, y, m == 0 ? RGB565(505050) : clrGrid);
        if (m == 0 || (f1 / f0 <= 1000))
        {
          char s[12];
          PutSI(s, f, "");
          int w = strlen(s) * 8;
          int tx = x - w / 2;
          if (tx + w > W) tx = W - w;
          Text(tx, PlotY + PlotH - 1, clrDim, s);
        }
      }
    }
  }

  void DrawCurves()
  {
    int px = -1, pyG = 0, pyP = 0;
    for (int i = 0; i < gCount; i++)
    {
      if (!gPt[i].ok) { px = -1; continue; }
      int x = XFreq(gPt[i].f);
      int yG = YGain(GainDb(i));
      int yP = HasPhase() ? YPhase(Fra::PhaseDeg(Response(i))) : 0;
      if (px >= 0)
      {
        Line(px, pyG, x, yG, clrGain);
        // no line over the +-180 wrap
        if (HasPhase() && (pyP - yP < PlotH / 2 && yP - pyP < PlotH / 2))
          Line(px, pyP, x, yP, clrPhase);
      }
      Bar(x - 1, yG - 1, x + 2, yG + 2, clrGain);
      if (HasPhase()) Bar(x - 1, yP - 1, x + 2, yP + 2, clrPhase);
      px = x; pyG = yG; pyP = yP;
    }
    // cursor
    if (nCursor < gCount && gPt[nCursor].ok)
    {
      int x = XFreq(gPt[nCursor].f);
      for (int y = PlotY; y < PlotY + PlotH; y += 3)
        LCD::PutPixel(x, y, clrCur);
    }
  }

  void DrawInfo()
  {
    Bar(0, InfoY, W, InfoY + 14, clrBar);
    if (nCursor >= gCount || !gPt[nCursor].ok)
    {
      Text(4, InfoY - 1, clrDim, bModeAB ? "B: Wave Out / circuit input, A: output" :
                                            "A: circuit output (Wave Out on its input)");
      return;
    }
    const TPoint& p = gPt[nCursor];
    char s[80], *q = s;
    q = PutSI(q, p.f, "Hz"); strcpy(q, " "); q++;
    q = PutFix(q, GainDb(nCursor), 2);
    strcpy(q, bModeAB || CalValid() ? "dB " : "dBV "); q += strlen(q);
    if (HasPhase()) { q = PutFix(q, Fra::PhaseDeg(Response(nCursor)), 1); strcpy(q, "deg "); q += 4; }
    strcpy(q, "A "); q = PutSI(q + 2, Fra::Mag(p.a), "V");
    if (bModeAB) { strcpy(q, " B "); q = PutSI(q + 3, Fra::Mag(p.b), "V"); }
    Text(4, InfoY - 1, clrText, s);
  }

  void ItemText(int it, char* s)
  {
    switch (it)
    {
      case ItStart: PutSI(s, arrFreq[nStart], "Hz"); break;
      case ItStop: PutSI(s, arrFreq[nStop], "Hz"); break;
      case ItPoints: { char* p = PutU(s, arrPoints[nPoints]); strcpy(p, " pts"); break; }
      case ItMode: strcpy(s, bModeAB ? "A/B" : "A"); break;
      case ItCouple: strcpy(s, bAc ? "AC" : "DC"); break;
      case ItAmp: { strcpy(s, "Out "); char* p = PutU(s + 4, arrAmp[nAmp]); strcpy(p, "%"); break; }
      case ItCursor: strcpy(s, "Cursor"); break;
    }
  }

  void DrawMenu()
  {
    Bar(0, MenuY, W, H, clrBack);
    int x = 3, y = MenuY;
    for (int it = 0; it < Items; it++)
    {
      char s[20];
      ItemText(it, s);
      int w = strlen(s) * 8;
      bool sel = it == nItem;
      if (sel) Bar(x - 2, y + 1, x + w + 2, y + 14, clrSel);
      Text(x, y, sel ? clrText : clrDim, s);
      x += w + 8;
    }
    Text(3, MenuY + 14, clrDim, "F1 sweep  F3 cal  F4 CSV  F2 exit");
    int w = strlen(status) * 8;
    if (w) Text(W - w - 2, MenuY + 14, statusClr, status);
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
    UpdateScale();
    DrawGrid();
    DrawCurves();
    DrawInfo();
  }

  void DrawAll()
  {
    DrawTitle();
    DrawPlot();
    DrawMenu();
  }

  // ------------------------------------------------------------ generator
  inline volatile uint32_t& Reg(uint32_t a) { return *(volatile uint32_t*)a; }
  enum : uint32_t { GPIOA_CRL = 0x40010800, TIM7_CR1 = 0x40001400, DAC_CR = 0x40007400,
    DMA2_CCR4 = 0x40020444, DacCh1 = 0x1fff, EN1 = 1, BOFF1 = 2 };

  // as the generator of the oscilloscope (Wildcat): PA4 analog, DAC channel 1
  // without the output buffer, the samples by DMA paced by TIM7
  void DacOn()
  {
    Reg(GPIOA_CRL) &= ~(0xfu << 16);
    if ((Reg(DAC_CR) & DacCh1) != (EN1 | BOFF1))
    {
      Reg(DAC_CR) &= ~DacCh1;
      Reg(DAC_CR) |= BOFF1;
      Reg(DAC_CR) |= EN1;
    }
    Reg(TIM7_CR1) = 0x85;
  }

  // the table lives in this application: stop the DMA before leaving, the
  // DAC off, PA4 released; BIOS restarts the DMA with the next buffer
  void DacOff()
  {
    Reg(DMA2_CCR4) &= ~1u;
    Reg(TIM7_CR1) = 0x84;
    Reg(DAC_CR) &= ~DacCh1;
    Reg(GPIOA_CRL) = (Reg(GPIOA_CRL) & ~(0xfu << 16)) | (0x4u << 16);
    DAC::SetMode(DAC::EMode::Square, nullptr, 0);
  }

  void SetGenerator(const Fra::TGen& g)
  {
    int amp = 2000 * arrAmp[nAmp] / 100;
    for (int k = 0; k < g.n; k++)
    {
      float v = 2048 + amp * Fra::Cos(2 * Fra::Pi * (float)((int64_t)g.m * k % g.n) / g.n);
      int iv = (int)(v + 0.5f);
      gDac[k] = (uint16_t)(iv < 0 ? 0 : iv > 4095 ? 4095 : iv);
    }
    DAC::SetMode(DAC::EMode::Buffer, gDac, g.n);
    DAC::SetFrequency(72000000 / g.div > 0 ? 72000000 / g.div : 1);
    DacOn();
    // exact pace of the DMA: TIM7 (as the generator of the oscilloscope)
    int psc, arr;
    Fra::SplitDiv(g.div, psc, arr);
    *(volatile uint32_t*)0x4000142C = arr;      // TIM7_ARR
    *(volatile uint32_t*)0x40001428 = psc;      // TIM7_PSC
    *(volatile uint32_t*)0x40001414 = 1;        // TIM7_EGR = UG
  }

  // ------------------------------------------------------------ inputs
  // volts per ADC step of a range (calibrated gain)
  float VoltsPerStep(int ch, int range)
  {
    const Cal::TCurve& c = gCalData.c[ch][range];
    return c.k * (Cal::RangeMv[range] / 1000.0f) / 65536.0f;
  }

  // position where 0 V is at ADC value z
  int PosFor(int ch, int range, float z)
  {
    const Cal::TCurve& c = gCalData.c[ch][range];
    int best = 128;
    float bd = 1e9f;
    for (int p = 0; p < 256; p++)
    {
      float d = Fra::Abs(Cal::Zero(c, p) - z);
      if (d < bd) { bd = d; best = p; }
    }
    return best;
  }

  void LoadCalibration()
  {
    Cal::Default(gCalData);
    if (FAT::Open(Cal::FileName, FAT::IoRead) == FAT::EOk)
    {
      if (FAT::Read(gSector) == FAT::EOk)
        Cal::Read(gCalData, gSector);
      FAT::Close();
    }
  }

  bool Capture(const Fra::TSample& s, int settleMs)
  {
    ADC::ConfigureInput(ADC::EInput::CH1, bAc ? ADC::ECouple::AC : ADC::ECouple::DC,
                        (ADC::EResolution)rangeA, posA * 4);
    ADC::ConfigureInput(ADC::EInput::CH2, bAc ? ADC::ECouple::AC : ADC::ECouple::DC,
                        (ADC::EResolution)rangeB, posB * 4);
    ADC::ConfigureTimebase(Fra::RateTime[s.rate]);
    ADC::ConfigureTrigger(0, 0, ADC::ETriggerType::None, ADC::EInput::CH1);
    SYS::DelayMs(settleMs);
    ADC::Restart(0);
    uint32_t timeout = (uint32_t)(4096.0f / s.fs * 1500.0f) + 300;
    uint32_t t0 = SYS::GetTick();
    while (ADC::GetState() != ADC::EState::Full)
      if (SYS::GetTick() - t0 > timeout)
        return false;
    for (int i = 0; i < 4096; i++)
    {
      uint32_t v = ADC::Get();
      gA[i] = v & 0xff;
      gB[i] = (v >> 8) & 0xff;
    }
    return true;
  }

  struct TStat { int mn, mx; float mean; };
  TStat Stat(const uint8_t* x, int n)
  {
    TStat st = { 255, 0, 0 };
    int32_t sum = 0;
    for (int i = 0; i < n; i++)
    {
      int v = x[Fra::SkipSamples + i];
      sum += v;
      if (v < st.mn) st.mn = v;
      if (v > st.mx) st.mx = v;
    }
    st.mean = (float)sum / n;
    return st;
  }

  // range and position of one channel after a capture; true: changed
  bool Adjust(int ch, const TStat& st, int& range, int& pos)
  {
    float vps = VoltsPerStep(ch, range);
    float zero = Cal::Zero(gCalData.c[ch][range], pos);
    float meanV = (st.mean - zero) * vps;
    int pp = st.mx - st.mn;
    int newRange = range;
    if (st.mn <= 2 || st.mx >= 253)
    {
      if (range < 7) newRange = range + 1;
    }
    else if (range > 0)
    {
      // the next lower range must hold the signal with margin
      float ratio = (float)Cal::RangeMv[range] / Cal::RangeMv[range - 1];
      if (pp * ratio < 170 && Fra::Abs(meanV) / VoltsPerStep(ch, range - 1) < 90)
        newRange = range - 1;
    }
    // the mean in the middle (DC); with AC it stays at 128
    float zTarget = 128 - (bAc ? 0 : meanV / VoltsPerStep(ch, newRange));
    int newPos = PosFor(ch, newRange, zTarget);
    bool changed = newRange != range || (newPos - pos > 6 || pos - newPos > 6);
    range = newRange;
    pos = newPos;
    return changed;
  }

  bool MeasurePoint(int i)
  {
    TPoint& pt = gPt[i];
    Fra::TGen g = Fra::PlanGen(FreqAt(i, gTotal));
    SetGenerator(g);
    pt.f = g.f;
    Fra::TSample s = Fra::PlanSample(g.f);
    // settling: 20 periods, at least 30 ms
    int settle = (int)(20000.0f / g.f);
    if (settle < 30) settle = 30;
    pt.ok = false;
    for (int iter = 0; iter < 6; iter++)
    {
      if (!Capture(s, iter == 0 ? settle : 40))
        return false;
      TStat sa = Stat(gA, s.ns), sb = Stat(gB, s.ns);
      bool ch = Adjust(0, sa, rangeA, posA);
      if (bModeAB)
        ch = Adjust(1, sb, rangeB, posB) || ch;
      if (!ch || iter == 5)
      {
        float va = VoltsPerStep(0, rangeA), vb = VoltsPerStep(1, rangeB);
        Fra::TCplx a = Fra::Dft([](int k) { return (float)gA[Fra::SkipSamples + k]; }, s.ns, g.f, s.fs);
        Fra::TCplx b = Fra::Dft([](int k) { return (float)gB[Fra::SkipSamples + k]; }, s.ns, g.f, s.fs);
        pt.a.re = a.re * va; pt.a.im = a.im * va;
        pt.b.re = b.re * vb; pt.b.im = b.im * vb;
        // clipped even at 10 V/div or nothing measured
        pt.ok = !(sa.mn <= 2 || sa.mx >= 253) && (!bModeAB || (!(sb.mn <= 2 || sb.mx >= 253) && Fra::Mag(pt.b) > 1e-4f));
        return true;
      }
    }
    return true;
  }

  void StartSweep(bool cal)
  {
    gTotal = arrPoints[nPoints];
    gCount = 0;
    nCursor = 0;
    eRun = cal ? CalSweep : Sweep;
    ADC::Enable(true);
    Status(clrText, cal ? "inputs on Wave Out" : "");
    DrawAll();
  }

  void FinishSweep(bool complete)
  {
    if (eRun == CalSweep && complete)
    {
      bool ok = true;
      for (int i = 0; i < gCount; i++)
      {
        if (!gPt[i].ok) ok = false;
        gCal[i] = bModeAB ? Fra::Div(gPt[i].a, gPt[i].b) : gPt[i].a;
      }
      gHaveCal = ok;
      gCalAB = bModeAB;
      gCalPoints = gTotal;
      gCalStart = nStart;
      gCalStop = nStop;
      Status(ok ? clrOk : clrErr, ok ? "calibrated" : "cal failed: clipped");
    } else
      Status(complete ? clrOk : clrWarn, complete ? "done" : "stopped");
    eRun = Idle;
    DacOff();
    DrawAll();
  }

  void SweepStep()
  {
    if (gCount >= gTotal) { FinishSweep(true); return; }
    if (!MeasurePoint(gCount))
    {
      Status(clrErr, "ADC timeout");
      FinishSweep(false);
      return;
    }
    nCursor = gCount;
    gCount++;
    DrawPlot();
  }

  // ------------------------------------------------------------ CSV
  bool SaveCsv()
  {
    if (!gCount) { Status(clrWarn, "no data"); return false; }
    char name[16];
    int i;
    for (i = 0; i < 1000; i++)
    {
      char* p = name;
      strcpy(p, "FR"); p += 2;
      *p++ = '0' + i / 100; *p++ = '0' + (i / 10) % 10; *p++ = '0' + i % 10;
      strcpy(p, ".CSV");
      if (FAT::Open(name, FAT::IoRead) == FAT::EOk) { FAT::Close(); continue; }
      break;
    }
    if (i == 1000 || FAT::Open(name, FAT::IoWrite) != FAT::EOk) { Status(clrErr, "file error"); return false; }
    int n = 0;
    uint32_t total = 0;
    bool ok = true;
    auto put = [&](const char* s) {
      while (*s)
      {
        gSector[n++] = *s++;
        total++;
        if (n == 512) { if (FAT::Write(gSector) != FAT::EOk) ok = false; n = 0; }
      }
    };
    put(bModeAB ? "f_Hz;gain_dB;phase_deg;A_V;B_V\n" : "f_Hz;A_dBV;A_V\n");
    for (int k = 0; k < gCount && ok; k++)
    {
      char s[80], *q = s;
      q = PutFix(q, gPt[k].f, 2); *q++ = ';';
      q = PutFix(q, GainDb(k), 2); *q++ = ';';
      if (bModeAB) { q = PutFix(q, Fra::PhaseDeg(Response(k)), 2); *q++ = ';'; }
      // volts with 4 decimals as mV with 1
      q = PutFix(q, Fra::Mag(gPt[k].a) * 1000, 1); strcpy(q, "e-3"); q += 3;
      if (bModeAB) { *q++ = ';'; q = PutFix(q, Fra::Mag(gPt[k].b) * 1000, 1); strcpy(q, "e-3"); q += 3; }
      *q++ = '\n'; *q = 0;
      put(s);
    }
    if (n)
    {
      memset(gSector + n, 0, 512 - n);
      if (FAT::Write(gSector) != FAT::EOk) ok = false;
    }
    ok = FAT::Close(total) == FAT::EOk && ok;
    Status(ok ? clrOk : clrErr, ok ? name : "save error");
    return ok;
  }

  // ------------------------------------------------------------ keys
  void Change(int d)
  {
    switch (nItem)
    {
      case ItStart: nStart = max(0, min(nStop - 1, nStart + d)); break;
      case ItStop: nStop = max(nStart + 1, min(Freqs - 1, nStop + d)); break;
      case ItPoints: nPoints = max(0, min(3, nPoints + d)); break;
      case ItMode: bModeAB = !bModeAB; break;
      case ItCouple: bAc = !bAc; break;
      case ItAmp: nAmp = max(0, min(3, nAmp + d)); break;
      case ItCursor:
        if (gCount) nCursor = max(0, min(gCount - 1, nCursor + d));
        DrawPlot();
        return;
    }
    if (nItem == ItStart || nItem == ItStop || nItem == ItPoints)
      gCount = 0;                       // the old points do not fit the axis
    DrawAll();
  }

  bool OnKey(KEY::EKey key)
  {
    if (eRun != Idle)
    {
      // a sweep runs: F1 / F2 stop it
      if (key == KEY::F1 || key == KEY::Escape)
        FinishSweep(false);
      return true;
    }
    switch (key)
    {
      case KEY::Up: nItem = (nItem + Items - 1) % Items; DrawMenu(); break;
      case KEY::Down: nItem = (nItem + 1) % Items; DrawMenu(); break;
      case KEY::Left: Change(-1); break;
      case KEY::Right: Change(1); break;
      case KEY::F1: StartSweep(false); break;
      case KEY::F3: StartSweep(true); break;
      case KEY::F4: SaveCsv(); break;
      case KEY::Escape: return false;
      default: break;
    }
    return true;
  }
}

#ifdef _ARM
__attribute__((__section__(".entry")))
#endif
int _main(void)
{
  LoadCalibration();
  LCD::Clear(clrBack);
  DrawAll();
  while (true)
  {
    KEY::EKey key = KEY::GetKey();
    if (key != KEY::None && !OnKey(key))
      break;
    if (eRun != Idle)
      SweepStep();
  }
  DacOff();
  return 0;
}

void _HandleAssertion(const char* file, int line, const char* cond)
{
  BIOS::DBG::Print("Assertion failed in ");
  BIOS::DBG::Print(file);
  BIOS::DBG::Print(" [%d]: %s\n", line, cond);
  while (1);
}

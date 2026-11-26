// MiniCal: manual calibration of CH1 / CH2 of the DS203 for the oscilloscope
// 58_gabmini. Edits MINICAL.CFG (zero and gain of every V/div range).
//
// Screen: a table of the ranges (zero and gain of both channels at the
// position below), the position, the reference voltage, the file actions and
// the live measurement of the selected channel and range.
//
// Keys:
//   Up / Down     row
//   F3            next column (CH1 zero, CH1 gain, CH2 zero, CH2 gain),
//                 on the File row the next action
//   Left / Right  change the selected value (held: faster)
//   F1            zero column: automatic zero (input to ground),
//                 gain column: automatic gain (Ref voltage on the input),
//                 File row: the action
//   F4            save
//   F2            exit (twice when not saved)
//
// DAC page (File row, action "DAC"): calibration of the Wave Out voltage with
// an external voltmeter, stored in the same MINICAL.CFG.
//   Up / Down     row: Code, Vmeas, Point 1, Point 2, Target
//   Left / Right  change the value (held: faster)
//   F3            preset DAC code (0, 256, 1024, 2048, 3072, 3840, 4095)
//   F1            Point 1 / Point 2 row: the point = Code and Vmeas (two
//                 arbitrary points on the linear part of the output)
//                 Code / Vmeas / Target row: digit editing
//   F4            save
//   F2            back to the ADC page

#include <library.h>
#include "calib.h"

using namespace BIOS;

namespace
{
  const uint16_t clrBack = RGB565(101010);
  const uint16_t clrTitle = RGB565(4040b0);
  const uint16_t clrText = RGB565(ffffff);
  const uint16_t clrDim = RGB565(a0a0a0);
  const uint16_t clrCh1 = RGB565(ffff00);
  const uint16_t clrCh2 = RGB565(00ffff);
  const uint16_t clrSel = RGB565(4060ff);
  const uint16_t clrOk = RGB565(40ff40);
  const uint16_t clrFail = RGB565(ff4040);
  const int RowH = 14, Top = 16;

  // rows
  enum { RowRange0 = 0, RowPos = Cal::Ranges, RowRef, RowFile, RowCount };
  const int colX[4] = {64, 136, 224, 296};
  const char* const fileActions[4] = {"Save", "Reload", "Defaults", "DAC"};

  Cal::TData data;
  uint8_t sector[512];
  bool modified = false;

  int row = 0, col = 0, range = 4;      // range: of the live measurement
  int position = 128;                   // vertical position (0..255)
  int32_t refMv = 1000;                 // reference voltage for the gain
  bool refFromDac = false;              // Ref comes from the Wave Out (DAC), else an external source
  int fileAction = 0;
  bool armedExit = false, armedDefaults = false;

  // DAC page
  enum { DRowCode = 0, DRowMeas, DRowPt1, DRowPt2, DRowTarget, DRowCount };
  const int dacPresets[7] = {0, 256, 1024, 2048, 3072, 3840, Cal::DacMax};
  int page = 0;                         // 0: ADC, 1: DAC
  int dacRow = 0;
  int dacCode = 0;                      // code on the Wave Out
  int32_t dacMeasUv = 0;                // voltage read on the voltmeter
  int32_t dacTargetUv = 1000000;        // wanted voltage (check)
  bool dacOn = false;

  char status[52] = "";
  uint16_t statusClr = clrText;

  // live measurement
  struct TLive
  {
    bool valid{false};
    int32_t mean2048{0};
    int min{0}, max{0};
  } live;
  uint32_t configKey = 0xffffffff;

  int Channel() { return col / 2; }
  Cal::TCurve& Curve(int ch, int r) { return data.c[ch][r]; }

  // ------------------------------------------------------------ text
  char* PutInt(char* p, uint32_t n)
  {
    char tmp[11];
    int i = 0;
    do { tmp[i++] = '0' + n % 10; n /= 10; } while (n);
    while (i)
      *p++ = tmp[--i];
    *p = 0;
    return p;
  }

  // v / 10^dec with dec decimals
  char* PutFix(char* p, int32_t v, int dec)
  {
    if (v < 0)
    {
      *p++ = '-';
      v = -v;
    }
    int32_t scale = 1;
    for (int i = 0; i < dec; i++)
      scale *= 10;
    p = PutInt(p, v / scale);
    if (dec)
    {
      *p++ = '.';
      int32_t f = v % scale;
      for (int32_t s = scale / 10; s > 1 && f < s; s /= 10)
        *p++ = '0';
      p = PutInt(p, f);
    }
    *p = 0;
    return p;
  }

  int32_t Round(float f) { return (int32_t)(f < 0 ? f - 0.5f : f + 0.5f); }

  // volts with 3 decimals, mV below 1 V
  char* PutVolts(char* p, float v)
  {
    float a = v < 0 ? -v : v;
    if (a < 1.0f)
    {
      p = PutFix(p, Round(v * 10000.0f), 1);
      strcpy(p, " mV");
    } else
    {
      p = PutFix(p, Round(v * 1000.0f), 3);
      strcpy(p, " V");
    }
    return p + strlen(p);
  }

  void Text(int x, int y, uint16_t clr, uint16_t back, const char* str)
  {
    LCD::Print(x, y, clr, back, str);
  }

  void ClearTo(int x, int y, int x2)
  {
    if (x < x2)
      LCD::Bar(x, y, x2, y + RowH, clrBack);
  }

  int RowY(int r) { return Top + RowH + r * RowH; }   // below the header

  // ------------------------------------------------------------ digit editing
  // as in the generator: F1 starts, Left / Right select the digit, Up / Down
  // change it (on the DS203 the '>' side of the rocker is Down: increases),
  // F1 / F2 finish. The number is an integer in units of its last digit.
  enum { EdNone = 0, EdPos, EdRef, EdCode, EdMeas, EdTarget };

  struct TEdit
  {
    int target{EdNone};
    int pos{0};                 // selected digit: 0 = last digit, digits = sign
    int digits{1}, dec{0};
    int32_t vmin{0}, vmax{0};
    bool neg{false};
    int32_t mag{0};
    const char* unit{""};
  } ed;

  bool Editing(int target) { return ed.target == target; }
  void DacOutput(int code);
  void DacOff();
  void DacRefApply();
  void BeginEdit(int target);

  // microvolts -> units of 0.1 mV
  int32_t Units100(int32_t uv) { return (uv >= 0 ? uv + 50 : uv - 50) / 100; }

  int32_t Pow10(int n)
  {
    int32_t f = 1;
    while (n-- > 0)
      f *= 10;
    return f;
  }

  // the number with the selected digit highlighted, returns the width
  int DrawEdit(int x, int y)
  {
    bool sign = ed.vmin < 0;
    int chars = ed.digits + (sign ? 1 : 0) + (ed.dec ? 1 : 0) + (int)strlen(ed.unit);
    LCD::Bar(x - 2, y, x + chars * 8 + 2, y + RowH, clrSel);
    int x0 = x;
    char c[2] = {0, 0};
    if (sign)
    {
      bool sel = ed.pos == ed.digits;
      c[0] = ed.neg ? '-' : '+';
      Text(x, y, sel ? clrBack : clrText, sel ? clrText : clrSel, c);
      x += 8;
    }
    bool leading = true;
    for (int p = ed.digits - 1; p >= 0; p--)
    {
      int digit = (ed.mag / Pow10(p)) % 10;
      if (digit != 0 || p <= ed.dec)
        leading = false;
      bool sel = ed.pos == p;
      c[0] = '0' + digit;
      Text(x, y, sel ? clrBack : leading ? clrDim : clrText, sel ? clrText : clrSel, c);
      x += 8;
      if (p == ed.dec && ed.dec)
      {
        c[0] = '.';
        Text(x, y, clrText, clrSel, c);
        x += 8;
      }
    }
    Text(x, y, clrText, clrSel, ed.unit);
    return chars * 8;
  }

  // ------------------------------------------------------------ drawing
  void DrawTitle()
  {
    LCD::Bar(0, 0, LCD::Width, 16, clrTitle);
    int x = 4 + LCD::Print(4, 1, clrText, clrTitle, page ? "MiniCal - DAC (Wave Out)" : "MiniCal - CH1/CH2 calibration");
    if (modified)
      LCD::Print(x + 16, 1, RGB565(ffc040), clrTitle, "not saved");
  }

  void DrawHeader()
  {
    int y = Top;
    LCD::Bar(0, y, LCD::Width, y + RowH, clrBack);
    Text(4, y, clrDim, clrBack, "Range");
    Text(colX[0], y, clrCh1, clrBack, "CH1 zero");
    Text(colX[1], y, clrCh1, clrBack, "gain");
    Text(colX[2], y, clrCh2, clrBack, "CH2 zero");
    Text(colX[3], y, clrCh2, clrBack, "gain");
  }

  void DrawRange(int r)
  {
    int y = RowY(RowRange0 + r);
    LCD::Bar(0, y, LCD::Width, y + RowH, clrBack);
    Text(4, y, r == range ? clrText : clrDim, clrBack, Cal::RangeName[r]);
    for (int c = 0; c < 4; c++)
    {
      const Cal::TCurve& cv = Curve(c / 2, r);
      char str[16];
      if (c & 1)
      {
        // gain in % with 2 decimals
        char* p = PutFix(str, Round(cv.k * 10000.0f / Cal::KOne), 2);
        strcpy(p, "%");
      } else
        PutFix(str, Round(Cal::Zero(cv, position) * 100.0f), 2);
      bool sel = row == RowRange0 + r && col == c;
      uint16_t back = sel ? clrSel : clrBack;
      if (sel)
        LCD::Bar(colX[c] - 2, y, colX[c] + 8 * 8, y + RowH, back);
      Text(colX[c], y, clrText, back, str);
    }
  }

  void DrawRefSource(int y)
  {
    LCD::Bar(colX[2], y, LCD::Width, y + RowH, clrBack);
    Text(colX[2], y, refFromDac ? clrOk : clrDim, clrBack, refFromDac ? "DAC (F3:ext)" : "ext (F3:DAC)");
  }

  void DrawSetting(int r)
  {
    int y = RowY(r);
    LCD::Bar(0, y, LCD::Width, y + RowH, clrBack);
    char str[24];
    const char* label = "";
    switch (r)
    {
      case RowPos:
        label = "Position";
        PutInt(str, position);
        break;
      case RowRef:
        label = "Ref";
        PutVolts(str, refMv / 1000.0f);
        break;
      default:
        label = "File";
        str[0] = 0;
        break;
    }
    Text(4, y, clrDim, clrBack, label);
    if ((r == RowPos && Editing(EdPos)) || (r == RowRef && Editing(EdRef)))
    {
      DrawEdit(colX[0], y);
      if (r == RowRef)
        DrawRefSource(y);
      return;
    }
    if (r == RowFile)
    {
      int x = colX[0];
      for (int i = 0; i < 4; i++)
      {
        bool sel = row == RowFile && fileAction == i;
        const char* name = i == 2 && armedDefaults ? "sure? F1" : fileActions[i];
        int w = strlen(name) * 8;
        uint16_t back = sel ? clrSel : clrBack;
        if (sel)
          LCD::Bar(x - 2, y, x + w + 2, y + RowH, back);
        Text(x, y, sel ? clrText : clrDim, back, name);
        x += w + 24;
      }
      return;
    }
    bool sel = row == r;
    int w = strlen(str) * 8;
    if (sel)
      LCD::Bar(colX[0] - 2, y, colX[0] + w + 2, y + RowH, clrSel);
    Text(colX[0], y, clrText, sel ? clrSel : clrBack, str);
    if (r == RowRef)
      DrawRefSource(y);
  }

  void DrawLive()
  {
    int y = RowY(RowCount);
    int ch = Channel();
    const Cal::TCurve& cv = Curve(ch, range);
    char str[64], *p = str;
    strcpy(p, ch ? "CH2 " : "CH1 ");
    p += 4;
    strcpy(p, Cal::RangeName[range]);
    p += strlen(p);
    strcpy(p, " pos ");
    p = PutInt(p + 5, position);
    if (live.valid)
    {
      strcpy(p, "  ADC ");
      p = PutFix(p + 6, Round(live.mean2048 * 100.0f / 2048), 2);
      strcpy(p, " (");
      p = PutInt(p + 2, live.min);
      *p++ = '-';
      p = PutInt(p, live.max);
      strcpy(p, ")");
    }
    int x = 4 + LCD::Print(4, y, ch ? clrCh2 : clrCh1, clrBack, str);
    ClearTo(x, y, LCD::Width);

    y += RowH;
    p = str;
    if (live.valid)
    {
      float raw = live.mean2048 / 2048.0f;
      strcpy(p, "Vavg ");
      p = PutVolts(p + 5, Cal::Volts(cv, position, range, raw));
      strcpy(p, "   zero ");
      p = PutFix(p + 8, Round(Cal::Zero(cv, position) * 100.0f), 2);
      if (live.min <= 1 || live.max >= 254)
        strcpy(p, "  CLIPPED");
    } else
      strcpy(p, "no data");
    x = 4 + LCD::Print(4, y, clrText, clrBack, str);
    ClearTo(x, y, LCD::Width);
  }

  void DrawStatus()
  {
    int y = LCD::Height - 2 * RowH;
    int x = 4 + LCD::Print(4, y, statusClr, clrBack, status);
    ClearTo(x, y, LCD::Width);
    y += RowH;
    x = 4 + LCD::Print(4, y, clrDim, clrBack, page ? "F3 preset  F1 edit/set  F4 save  F2 back" :
      "F3 column  F1 auto/do  F4 save  F2 exit");
    ClearTo(x, y, LCD::Width);
  }

  void Status(uint16_t clr, const char* str)
  {
    statusClr = clr;
    strncpy(status, str, sizeof(status) - 1);
    status[sizeof(status) - 1] = 0;
    DrawStatus();
  }

  void DrawAll()
  {
    LCD::Clear(clrBack);
    DrawTitle();
    DrawHeader();
    for (int r = 0; r < Cal::Ranges; r++)
      DrawRange(r);
    for (int r = RowPos; r < RowCount; r++)
      DrawSetting(r);
    DrawLive();
    DrawStatus();
  }

  void SetModified()
  {
    if (!modified)
    {
      modified = true;
      DrawTitle();
    }
  }

  // ------------------------------------------------------------ ADC
  // both channels at the range and position, DC; 500 us/div = 60 kS/s:
  // 3600 samples are 60 ms, whole periods of 50 Hz and 60 Hz hum
  enum { SkipSamples = 160, MeanSamples = 3600 };

  struct TMeas
  {
    int32_t mean2048[2];
    int min[2], max[2];
  };

  bool Capture(int r, int pos, int settleMs, TMeas& m)
  {
    ADC::Enable(true);
    ADC::ConfigureInput(ADC::EInput::CH1, ADC::ECouple::DC, (ADC::EResolution)r, pos * 4);
    ADC::ConfigureInput(ADC::EInput::CH2, ADC::ECouple::DC, (ADC::EResolution)r, pos * 4);
    ADC::ConfigureTimebase(500e-6f);
    ADC::ConfigureTrigger(0, 0, ADC::ETriggerType::None, ADC::EInput::CH1);
    SYS::DelayMs(settleMs);
    ADC::Restart(0);
    uint32_t t0 = SYS::GetTick();
    while (ADC::GetState() != ADC::EState::Full)
      if (SYS::GetTick() - t0 > 500)
        return false;
    int32_t sum[2] = {0, 0};
    for (int c = 0; c < 2; c++)
    {
      m.min[c] = 255;
      m.max[c] = 0;
    }
    for (int i = 0; i < SkipSamples + MeanSamples; i++)
    {
      ADC::TSample s;
      s.value = ADC::Get();
      if (i < SkipSamples)
        continue;
      int v[2] = {(int)s.CH1, (int)s.CH2};
      for (int c = 0; c < 2; c++)
      {
        sum[c] += v[c];
        if (v[c] < m.min[c]) m.min[c] = v[c];
        if (v[c] > m.max[c]) m.max[c] = v[c];
      }
    }
    for (int c = 0; c < 2; c++)
      m.mean2048[c] = (int32_t)(((int64_t)sum[c] * 2048 + MeanSamples / 2) / MeanSamples);
    return true;
  }

  void UpdateLive()
  {
    uint32_t key = (range << 9) | position;
    int settle = key != configKey ? 200 : 20;
    configKey = key;
    TMeas m;
    int ch = Channel();
    live.valid = Capture(range, position, settle, m);
    if (live.valid)
    {
      live.mean2048 = m.mean2048[ch];
      live.min = m.min[ch];
      live.max = m.max[ch];
    }
    DrawLive();
  }

  // ------------------------------------------------------------ actions
  // zero of the selected channel and range, the input on ground: three
  // positions, a clipped one is moved towards the middle
  void AutoZero(int ch, int r)
  {
    static const int16_t start[Cal::Points] = {40, 128, 216};
    int16_t pos[Cal::Points];
    int32_t mean[Cal::Points];
    int n = 0;
    // From the DAC: the code of 0 V is not the code 0. The voltage that the
    // chosen code really has (the quantization of the DAC, the offset when
    // 0 V is out of its range) is subtracted from the measured values.
    int32_t corr2048 = 0;
    if (refFromDac)
    {
      Status(clrText, "zero: measuring, DAC at 0 V on the input...");
      int code = Cal::DacCode(data.dac, 0);
      float volts = Cal::DacUv(data.dac, code) / 1000000.0f;
      corr2048 = Round(volts * 65536.0f * 2048.0f / ((Cal::RangeMv[r] / 1000.0f) * Curve(ch, r).k));
      DacOutput(code);
      SYS::DelayMs(300);
    } else
      Status(clrText, "zero: measuring, input on ground...");
    for (int k = 0; k < Cal::Points; k++)
    {
      int p = start[k];
      for (int t = 0; t < 5; t++)
      {
        TMeas m;
        if (!Capture(r, p, t == 0 && k == 0 ? 250 : 120, m))
          break;
        if (m.min[ch] <= 1 && p < 255) { p = min(255, p + 24); continue; }
        if (m.max[ch] >= 254 && p > 0) { p = max(0, p - 24); continue; }
        // ascending, not too close to the previous point
        if (n == 0 || p - pos[n - 1] >= 16)
        {
          pos[n] = p;
          mean[n] = m.mean2048[ch] - corr2048;
          n++;
        }
        break;
      }
    }
    configKey = 0xffffffff;
    if (n == 0)
    {
      Status(clrFail, "zero: clipped at every position, input?");
      DacRefApply();
      return;
    }
    Cal::SetZero(Curve(ch, r), pos, mean, n);
    DacRefApply();                      // the DAC goes back to the Ref
    SetModified();
    DrawRange(r);
    char str[52], *p = str;
    strcpy(p, n == Cal::Points ? "zero: done, " : "zero: partly, ");
    p += strlen(p);
    p = PutInt(p, n);
    strcpy(p, " points");
    Status(n == Cal::Points ? clrOk : RGB565(ffc040), str);
  }

  // gain of the selected channel and range from the Ref voltage on the input
  void AutoGain(int ch, int r)
  {
    Status(clrText, "gain: measuring the Ref voltage...");
    float refVolts = refMv / 1000.0f;
    int settle = 200;
    if (refFromDac)
    {
      // the voltage of the DAC code (calibrated), not the wanted Ref
      int code = Cal::DacCode(data.dac, refMv * 1000);
      int32_t uv = Cal::DacUv(data.dac, code);
      int32_t err = uv - refMv * 1000;
      if (err < 0)
        err = -err;
      if (err > 2000)
      {
        Status(clrFail, "gain: Ref is outside the DAC range");
        return;
      }
      DacOutput(code);
      refVolts = uv / 1000000.0f;
      settle = 400;
    }
    TMeas m;
    if (!Capture(r, position, settle, m))
    {
      Status(clrFail, "gain: no data");
      return;
    }
    configKey = 0xffffffff;
    if (m.min[ch] <= 1 || m.max[ch] >= 254)
    {
      Status(clrFail, "gain: clipped, change the position");
      return;
    }
    Cal::TCurve& cv = Curve(ch, r);
    float raw = m.mean2048[ch] / 2048.0f;
    float steps = raw - Cal::Zero(cv, position);
    // at least 1 division (32 steps) from the zero for 0.1 % resolution
    if (steps < 32.0f && steps > -32.0f)
    {
      Status(clrFail, "gain: Ref must be >= 1 div from zero");
      return;
    }
    // volts = steps * K * mult / 65536  ->  K = Ref * 65536 / (steps * mult)
    float k = refVolts * 65536.0f / (steps * (Cal::RangeMv[r] / 1000.0f));
    int32_t nk = Round(k);
    if (nk < Cal::KMin || nk > Cal::KMax)
    {
      Status(clrFail, "gain: out of 50..200 %, Ref or input?");
      return;
    }
    Cal::SetGain(cv, nk);
    SetModified();
    DrawRange(r);
    char str[52];
    char* p = str;
    strcpy(p, "gain: ");
    p = PutFix(p + 6, Round(nk * 10000.0f / Cal::KOne), 2);
    strcpy(p, " %");
    Status(clrOk, str);
  }

  bool Save()
  {
    int len = Cal::Write(data, sector);
    bool ok = FAT::Open(Cal::FileName, FAT::IoWrite) == FAT::EOk &&
      FAT::Write(sector) == FAT::EOk;
    if (FAT::Close(len) != FAT::EOk)
      ok = false;
    if (ok)
    {
      modified = false;
      DrawTitle();
      Status(clrOk, "saved to MINICAL.CFG, start the oscilloscope");
    } else
      Status(clrFail, "save failed");
    return ok;
  }

  void Load(bool quiet)
  {
    bool ok = false;
    if (FAT::Open(Cal::FileName, FAT::IoRead) == FAT::EOk)
    {
      ok = FAT::Read(sector) == FAT::EOk;
      FAT::Close();
      ok = ok && Cal::Read(data, sector);
    }
    if (!ok)
      Cal::Default(data);
    modified = false;
    if (!quiet)
      DrawAll();
    Status(ok ? clrOk : RGB565(ffc040), ok ? "MINICAL.CFG loaded" : "no valid MINICAL.CFG, defaults");
  }

  // ------------------------------------------------------------ key repeat
  KEY::EKey lastKey = KEY::None;
  uint32_t lastKeyTick = 0;
  int repeat = 0;

  int Step()
  {
    return repeat < 6 ? 1 : repeat < 16 ? 4 : repeat < 40 ? 16 : 64;
  }

  // ------------------------------------------------------------ DAC
  char* PutHex(char* p, uint32_t n, int digits)
  {
    for (int i = digits - 1; i >= 0; i--)
    {
      int d = (n >> (i * 4)) & 15;
      *p++ = d < 10 ? '0' + d : 'A' + d - 10;
    }
    *p = 0;
    return p;
  }

  // constant voltage on the Wave Out: one sample repeated by the DMA, set up
  // as the generator of the oscilloscope does it for the DC wave.
  // The DS203 BIOS has no DAC Off mode (assertion), so the registers that are
  // touched are saved at the first output and restored by DacOff.
#ifdef DS203
  struct TSavedRegs
  {
    bool valid{false};
    uint32_t gpioaCrl{0}, gpiobCrl{0}, dacCr{0}, tim7Cr1{0};
  } savedRegs;
  // plain macros: no dynamic initialization in this application
  #define RegGpioaCrl (*(volatile uint32_t*)0x40010800)
  #define RegGpiobCrl (*(volatile uint32_t*)0x40010C00)
  #define RegDacCr (*(volatile uint32_t*)0x40007400)
  #define RegDacDhr12r1 (*(volatile uint32_t*)0x40007408)
  #define RegTim7Cr1 (*(volatile uint32_t*)0x40001400)
  #define RegDma2Ccr4 (*(volatile uint32_t*)0x40020444)
#endif

  void DacOutput(int code)
  {
    static uint16_t sample;
#ifdef DS203
    if (!savedRegs.valid)
    {
      savedRegs.valid = true;
      savedRegs.gpioaCrl = RegGpioaCrl;
      savedRegs.gpiobCrl = RegGpiobCrl;
      savedRegs.dacCr = RegDacCr;
      savedRegs.tim7Cr1 = RegTim7Cr1;
    }
#endif
    sample = (uint16_t)code;
    BIOS::DAC::SetMode(BIOS::DAC::EMode::Buffer, &sample, 1);
    BIOS::DAC::SetFrequency(1000);
#ifdef DS203
    // PA4 analog, DAC channel 1 without the output buffer, TIM7 running;
    // PB6 (tied to the Wave Out, logic / square output) is released
    const uint32_t mask = 0x1fff, en1 = 1 << 0, boff1 = 1 << 1;
    RegGpioaCrl &= ~(0xfu << 16);
    RegGpiobCrl = (RegGpiobCrl & ~(0xfu << 24)) | (0x4u << 24);
    if ((RegDacCr & mask) != (en1 | boff1))
    {
      RegDacCr &= ~mask;
      RegDacCr |= boff1;
      RegDacCr |= en1;
    }
    RegTim7Cr1 = 0x85;
#endif
    dacOn = true;
  }

  void DacOff()
  {
    if (!dacOn)
      return;
    dacOn = false;
#ifdef DS203
    RegDma2Ccr4 &= ~1u;                 // stop the sample stream
    RegDacDhr12r1 = 0;
    if (savedRegs.valid)
    {
      RegDacCr = savedRegs.dacCr;
      RegTim7Cr1 = savedRegs.tim7Cr1;
      RegGpioaCrl = savedRegs.gpioaCrl;
      RegGpiobCrl = savedRegs.gpiobCrl;
      savedRegs.valid = false;
    }
#endif
    // forget the buffer in the BIOS (it is in this application)
    BIOS::DAC::SetMode(BIOS::DAC::EMode::Square, nullptr, 0);
  }

  // the Ref on the output when it comes from the DAC, else the output is free
  void DacRefApply()
  {
    if (refFromDac)
      DacOutput(Cal::DacCode(data.dac, refMv * 1000));
    else
      DacOff();
  }

  void ToggleRefSource()
  {
    refFromDac = !refFromDac;
    DacRefApply();
    DrawSetting(RowRef);
    if (!refFromDac)
    {
      Status(clrText, "Ref: an external source");
      return;
    }
    int code = Cal::DacCode(data.dac, refMv * 1000);
    char str[52], *p = str;
    strcpy(p, "Ref from the DAC: code ");
    p = PutInt(p + 23, code);
    strcpy(p, " = ");
    PutVolts(p + 3, Cal::DacUv(data.dac, code) / 1000000.0f);
    Status(clrOk, str);
  }

  void DrawDacRow(int r)
  {
    int y = Top + r * RowH;
    LCD::Bar(0, y, LCD::Width, y + RowH, clrBack);
    char str[40], hint[40];
    const char* label = "";
    hint[0] = 0;
    char* p = str;
    switch (r)
    {
      case DRowCode:
        label = "Code";
        p = PutInt(str, dacCode);
        strcpy(p, "  0x");
        PutHex(p + 4, dacCode, 3);
        strcpy(hint, "F1 edit");
        break;
      case DRowMeas:
        label = "Vmeas";
        p = PutFix(str, Units100(dacMeasUv), 1);
        strcpy(p, " mV");
        strcpy(hint, "F1 edit");
        break;
      case DRowPt1:
      case DRowPt2:
        label = r == DRowPt1 ? "Point 1" : "Point 2";
        p = PutInt(str, data.dac.code[r == DRowPt1 ? 0 : 1]);
        strcpy(p, " = ");
        PutVolts(p + 3, data.dac.uv[r == DRowPt1 ? 0 : 1] / 1000000.0f);
        strcpy(hint, "F1: set");
        break;
      default:
        label = "Target";
        p = PutFix(str, Units100(dacTargetUv), 1);
        strcpy(p, " mV");
        strcpy(hint, "F1 edit");
        break;
    }
    Text(4, y, clrDim, clrBack, label);
    if ((r == DRowCode && Editing(EdCode)) || (r == DRowMeas && Editing(EdMeas)) ||
      (r == DRowTarget && Editing(EdTarget)))
    {
      DrawEdit(colX[0], y);
      return;
    }
    bool sel = dacRow == r;
    int w = strlen(str) * 8;
    if (sel)
      LCD::Bar(colX[0] - 2, y, colX[0] + w + 2, y + RowH, clrSel);
    Text(colX[0], y, clrText, sel ? clrSel : clrBack, str);
    if (sel)
      Text(colX[2] + 16, y, clrDim, clrBack, hint);
  }

  void DrawDacInfo()
  {
    const Cal::TDac& d = data.dac;
    char str[52], *p;
    int y = Top + (DRowCount + 1) * RowH;
    for (int i = 0; i < 3; i++)
    {
      p = str;
      if (i == 0)
      {
        // the line through the points: the voltage at code 0 and the slope
        strcpy(p, "Offset (code 0): ");
        p = PutVolts(p + 17, Cal::DacUv(d, 0) / 1000000.0f);
      } else if (i == 1)
      {
        strcpy(p, "Gain: ");
        p = PutFix(p + 6, Round(Cal::DacSlope(d) * 10.0f), 4);
        strcpy(p, " mV/code");
      } else
      {
        strcpy(p, "Output now: code ");
        p = PutInt(p + 17, dacCode);
        strcpy(p, " = ");
        p = PutVolts(p + 3, Cal::DacUv(d, dacCode) / 1000000.0f);
      }
      LCD::Bar(0, y, LCD::Width, y + RowH, clrBack);
      Text(4, y, i < 2 ? clrDim : clrOk, clrBack, str);
      y += RowH;
    }
  }

  void DrawDacAll()
  {
    LCD::Clear(clrBack);
    DrawTitle();
    for (int r = 0; r < DRowCount; r++)
      DrawDacRow(r);
    DrawDacInfo();
    DrawStatus();
  }

  // the output follows the calibration: Vmeas is prepared with the voltage
  // the calibration predicts, adjust it to the voltmeter
  void DacSetCode(int code)
  {
    dacCode = max(0, min(Cal::DacMax, code));
    DacOutput(dacCode);
    dacMeasUv = Cal::DacUv(data.dac, dacCode);
    DrawDacRow(DRowCode);
    DrawDacRow(DRowMeas);
    DrawDacInfo();
  }

  void DacApplyTarget()
  {
    DacSetCode(Cal::DacCode(data.dac, dacTargetUv));
  }

  void EnterDac()
  {
    page = 1;
    dacRow = DRowCode;
    DacSetCode(dacCode);
    DrawDacAll();
    Status(clrText, "read the output with a voltmeter");
  }

  void ExitDac()
  {
    DacOff();
    page = 0;
    DacRefApply();
    DrawAll();
    Status(clrText, "");
  }

  void DacSetPoint(int idx)
  {
    if (!Cal::SetDacPoint(data.dac, idx, dacCode, dacMeasUv))
    {
      Status(clrFail, "rejected: points >= 256 codes apart, gain?");
      return;
    }
    SetModified();
    // the output at once follows the new calibration: the code of the Target
    DacApplyTarget();
    for (int r = 0; r < DRowCount; r++)
      DrawDacRow(r);
    DrawDacInfo();
    Status(clrOk, idx ? "point 2 set, output = Target" : "point 1 set, output = Target");
  }

  int StepV()
  {
    return repeat < 6 ? 1 : repeat < 16 ? 10 : repeat < 40 ? 100 : 1000;
  }

  void DacMoveRow(int d)
  {
    int old = dacRow;
    dacRow = (dacRow + d + DRowCount) % DRowCount;
    DrawDacRow(old);
    DrawDacRow(dacRow);
    if (dacRow == DRowTarget)
      DacApplyTarget();
  }

  void DacAdjust(int d)
  {
    switch (dacRow)
    {
      case DRowCode:
        DacSetCode(dacCode + Step() * d);
        break;
      case DRowMeas:
        // 0.1 mV steps
        dacMeasUv = max((int32_t)-1000000, min((int32_t)4000000, dacMeasUv + (int32_t)StepV() * d * 100));
        DrawDacRow(DRowMeas);
        break;
      case DRowTarget:
        dacTargetUv = max((int32_t)0, min((int32_t)3300000, dacTargetUv + (int32_t)StepV() * d * 100));
        DacApplyTarget();
        DrawDacRow(DRowTarget);
        break;
      default:
        break;
    }
  }

  bool OnDacKey(KEY::EKey key)
  {
    switch (key)
    {
      case KEY::Up: DacMoveRow(-1); break;
      case KEY::Down: DacMoveRow(1); break;
      case KEY::Left: DacAdjust(-1); break;
      case KEY::Right: DacAdjust(1); break;
      case KEY::F3:
      {
        int i = 0;
        while (i < 7 && dacPresets[i] <= dacCode)
          i++;
        DacSetCode(dacPresets[i % 7]);
        break;
      }
      case KEY::F1:
        if (dacRow == DRowPt1)
          DacSetPoint(0);
        else if (dacRow == DRowPt2)
          DacSetPoint(1);
        else
          BeginEdit(dacRow == DRowCode ? EdCode : dacRow == DRowMeas ? EdMeas : EdTarget);
        break;
      case KEY::F4:
        Save();
        break;
      case KEY::Escape:
        ExitDac();
        break;
      default:
        break;
    }
    return true;
  }

  // ------------------------------------------------------------ digit editing, keys
  int32_t EditGet(int t)
  {
    switch (t)
    {
      case EdPos: return position;
      case EdRef: return refMv;
      case EdCode: return dacCode;
      case EdMeas: return Units100(dacMeasUv);
      default: return Units100(dacTargetUv);
    }
  }

  void EditRedraw(int t)
  {
    switch (t)
    {
      case EdPos: DrawSetting(RowPos); break;
      case EdRef: DrawSetting(RowRef); break;
      case EdCode: DrawDacRow(DRowCode); break;
      case EdMeas: DrawDacRow(DRowMeas); break;
      case EdTarget: DrawDacRow(DRowTarget); break;
    }
  }

  // applies the edited value
  void EditSet(int t, int32_t v)
  {
    switch (t)
    {
      case EdPos:
        position = v;
        for (int r = 0; r < Cal::Ranges; r++)
          DrawRange(r);
        break;
      case EdRef:
        refMv = v;
        if (refFromDac)
          DacRefApply();
        break;
      case EdCode:
        DacSetCode(v);
        break;
      case EdMeas:
        dacMeasUv = v * 100;
        break;
      default:
        dacTargetUv = v * 100;
        DacApplyTarget();
        break;
    }
  }

  void BeginEdit(int t)
  {
    ed.target = t;
    switch (t)
    {
      case EdPos: ed.digits = 3; ed.dec = 0; ed.vmin = 0; ed.vmax = 255; ed.unit = ""; break;
      case EdRef: ed.digits = 5; ed.dec = 3; ed.vmin = -80000; ed.vmax = 80000; ed.unit = " V"; break;
      case EdCode: ed.digits = 4; ed.dec = 0; ed.vmin = 0; ed.vmax = Cal::DacMax; ed.unit = ""; break;
      case EdMeas: ed.digits = 5; ed.dec = 1; ed.vmin = -10000; ed.vmax = 40000; ed.unit = " mV"; break;
      default: ed.digits = 5; ed.dec = 1; ed.vmin = 0; ed.vmax = 33000; ed.unit = " mV"; break;
    }
    int32_t v = EditGet(t);
    ed.neg = v < 0;
    ed.mag = v < 0 ? -v : v;
    ed.pos = ed.dec;                    // the units digit
    EditRedraw(t);
    Status(clrText, "Left/Right digit  Up/Down value  F1 done");
  }

  void EndEdit()
  {
    int t = ed.target;
    ed.target = EdNone;
    EditRedraw(t);
    Status(clrText, "");
  }

  // consumes every key while editing
  void OnEditKey(KEY::EKey key)
  {
    bool sign = ed.vmin < 0;
    switch (key)
    {
      case KEY::Left:
        if (ed.pos < (sign ? ed.digits : ed.digits - 1))
          ed.pos++;
        EditRedraw(ed.target);
        break;
      case KEY::Right:
        if (ed.pos > 0)
          ed.pos--;
        EditRedraw(ed.target);
        break;
      case KEY::Up:
      case KEY::Down:
      {
#ifdef DS203
        bool inc = key == KEY::Down;
#else
        bool inc = key == KEY::Up;
#endif
        if (ed.pos == ed.digits)
          ed.neg = !ed.neg;
        else
          ed.mag += inc ? Pow10(ed.pos) : -Pow10(ed.pos);
        int32_t maxMag = ed.neg ? -ed.vmin : ed.vmax;
        if (ed.mag > maxMag) ed.mag = maxMag;
        if (ed.mag < 0) ed.mag = 0;
        EditSet(ed.target, ed.neg ? -ed.mag : ed.mag);
        EditRedraw(ed.target);
        break;
      }
      case KEY::F1:
      case KEY::Escape:
        EndEdit();
        break;
      default:
        break;
    }
  }

  // ------------------------------------------------------------ keys
  void Redraw(int r)
  {
    if (r < Cal::Ranges)
      DrawRange(r);
    else
      DrawSetting(r);
  }

  void MoveRow(int d)
  {
    int old = row;
    row = (row + d + RowCount) % RowCount;
    armedDefaults = false;
    if (row < Cal::Ranges && row != range)
    {
      int oldRange = range;
      range = row;
      DrawRange(oldRange);
    }
    Redraw(old);
    Redraw(row);
    if (old == RowFile || row == RowFile)
      DrawSetting(RowFile);
  }

  void Adjust(int d)
  {
    int step = Step() * d;
    if (row < Cal::Ranges)
    {
      Cal::TCurve& cv = Curve(col / 2, row);
      if (col & 1)
        Cal::SetGain(cv, cv.k + step);            // 1/2048 = 0.05 %
      else
        Cal::MoveZero(cv, step * 16);             // 1/16 of an ADC step
      SetModified();
      DrawRange(row);
      return;
    }
    switch (row)
    {
      case RowPos:
        position = max(0, min(255, position + step));
        for (int r = 0; r < Cal::Ranges; r++)
          DrawRange(r);
        break;
      case RowRef:
        refMv = max((int32_t)-80000, min((int32_t)80000, refMv + step));
        if (refFromDac)
          DacRefApply();
        break;
      case RowFile:
        fileAction = (fileAction + (d > 0 ? 1 : 3)) % 4;
        armedDefaults = false;
        break;
    }
    DrawSetting(row);
  }

  void DoFile()
  {
    switch (fileAction)
    {
      case 0:
        Save();
        break;
      case 1:
        Load(false);
        break;
      case 3:
        EnterDac();
        break;
      default:
        if (!armedDefaults)
        {
          armedDefaults = true;
          DrawSetting(RowFile);
          return;
        }
        armedDefaults = false;
        Cal::Default(data);
        modified = true;
        DrawAll();
        Status(clrText, "defaults (not saved)");
        break;
    }
  }

  // false: exit
  bool OnKey(KEY::EKey key)
  {
    uint32_t now = SYS::GetTick();
    repeat = key == lastKey && now - lastKeyTick < 300 ? repeat + 1 : 0;
    lastKey = key;
    lastKeyTick = now;

    if (ed.target != EdNone)
    {
      OnEditKey(key);
      return true;
    }
    if (page)
      return OnDacKey(key);
    if (key != KEY::Escape && armedExit)
    {
      armedExit = false;
      Status(clrText, "");
    }
    switch (key)
    {
      case KEY::Up: MoveRow(-1); break;
      case KEY::Down: MoveRow(1); break;
      case KEY::Left: Adjust(-1); break;
      case KEY::Right: Adjust(1); break;
      case KEY::F3:
        if (row == RowFile)
          Adjust(1);
        else
        {
          col = (col + 1) % 4;
          for (int r = 0; r < Cal::Ranges; r++)
            DrawRange(r);
          DrawLive();
          configKey = 0xffffffff;
        }
        break;
      case KEY::F1:
        if (row < Cal::Ranges)
        {
          if (col & 1)
            AutoGain(col / 2, row);
          else
            AutoZero(col / 2, row);
        } else if (row == RowPos)
          BeginEdit(EdPos);
        else if (row == RowRef)
          BeginEdit(EdRef);
        else if (row == RowFile)
          DoFile();
        break;
      case KEY::F4:
        Save();
        break;
      case KEY::Escape:
        if (!modified || armedExit)
          return false;
        armedExit = true;
        Status(RGB565(ffc040), "not saved: F2 again exits, F4 saves");
        break;
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
  Load(true);
  char str[52];
  strcpy(str, status);
  DrawAll();
  Status(statusClr, str);
  ADC::Enable(true);

  uint32_t lastLive = 0;
  while (true)
  {
    KEY::EKey key = KEY::GetKey();
    if (key != KEY::None && !OnKey(key))
      break;
    uint32_t now = SYS::GetTick();
    if (page == 0 && now - lastLive >= 300 && key == KEY::None)
    {
      UpdateLive();
      lastLive = SYS::GetTick();
    }
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

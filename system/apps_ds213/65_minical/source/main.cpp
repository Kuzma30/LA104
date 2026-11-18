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
  const char* const fileActions[3] = {"Save", "Reload", "Defaults"};

  Cal::TData data;
  uint8_t sector[512];
  bool modified = false;

  int row = 0, col = 0, range = 4;      // range: of the live measurement
  int position = 128;                   // vertical position (0..255)
  int32_t refMv = 1000;                 // reference voltage for the gain
  int fileAction = 0;
  bool armedExit = false, armedDefaults = false;

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

  // ------------------------------------------------------------ drawing
  void DrawTitle()
  {
    LCD::Bar(0, 0, LCD::Width, 16, clrTitle);
    int x = 4 + LCD::Print(4, 1, clrText, clrTitle, "MiniCal - CH1/CH2 calibration");
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
    if (r == RowFile)
    {
      int x = colX[0];
      for (int i = 0; i < 3; i++)
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
      Text(colX[2], y, clrDim, clrBack, "for the gain (F1)");
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
    x = 4 + LCD::Print(4, y, clrDim, clrBack, "F3 column  F1 auto/do  F4 save  F2 exit");
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
    Status(clrText, "zero: measuring, input on ground...");
    static const int16_t start[Cal::Points] = {40, 128, 216};
    int16_t pos[Cal::Points];
    int32_t mean[Cal::Points];
    int n = 0;
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
          mean[n] = m.mean2048[ch];
          n++;
        }
        break;
      }
    }
    configKey = 0xffffffff;
    if (n == 0)
    {
      Status(clrFail, "zero: clipped at every position, input?");
      return;
    }
    Cal::SetZero(Curve(ch, r), pos, mean, n);
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
    TMeas m;
    if (!Capture(r, position, 200, m))
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
    float k = (refMv / 1000.0f) * 65536.0f / (steps * (Cal::RangeMv[r] / 1000.0f));
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

  // ------------------------------------------------------------ keys
  KEY::EKey lastKey = KEY::None;
  uint32_t lastKeyTick = 0;
  int repeat = 0;

  int Step()
  {
    return repeat < 6 ? 1 : repeat < 16 ? 4 : repeat < 40 ? 16 : 64;
  }

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
        break;
      case RowFile:
        fileAction = (fileAction + (d > 0 ? 1 : 2)) % 3;
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
        } else if (row == RowFile)
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
    if (now - lastLive >= 300 && key == KEY::None)
    {
      UpdateLive();
      lastLive = SYS::GetTick();
    }
  }
  return 0;
}

void _HandleAssertion(const char* file, int line, const char* cond)
{
  BIOS::DBG::Print("Assertion failed in ");
  BIOS::DBG::Print(file);
  BIOS::DBG::Print(" [%d]: %s\n", line, cond);
  while (1);
}

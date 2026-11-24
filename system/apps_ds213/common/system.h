#pragma once
// System settings of the DS203 applications (menu System of the
// oscilloscope, used by the logic analyzer too):
//   language    LANG.CFG (lang.h): EN or a directory of LANG/
//   brightness  of the backlight, 10..100 % (0: as the SYS set it)
//   volume      of the beeper, 0..100 %
//   sleep       of the display after minutes without keys and signal (sleep.h)
// SYSTEM.CFG keeps brightness, volume and sleep as text lines
//   sleep=5
//   bright=60
//   volume=50
// (the older SLEEP.CFG with the minutes only is read when SYSTEM.CFG is
// missing).
#include <stdint.h>
#include <string.h>
#include "lang.h"
#include "sleep.h"

namespace System
{
  enum { DefaultVolume = 100 };
  inline int& Volume() { static int n = DefaultVolume; return n; }

  // the backlight through the SYS (sleep.h)
  inline void ApplyBrightness()
  {
    if (Sleep::Brightness() > 0 && !Sleep::Asleep())
      Sleep::_Backlight(Sleep::Brightness());
  }

  // a beep at the volume: the OS beeps at full volume (duty of TIM8 CH2,
  // ds203/imports.c xBeep), the duty is set to the volume right after
  inline void Beep(int ms)
  {
    if (Volume() <= 0)
      return;
    BIOS::SYS::Beep(ms);
#if defined(_ARM) && defined(DS203)
    *(volatile uint32_t*)(0x40013400 + 0x38) = 100 - Volume() / 2;
#endif
  }

  inline int _Number(const uint8_t* p, int& i, int n)
  {
    int v = 0;
    while (i < n && p[i] >= '0' && p[i] <= '9')
      v = v * 10 + (p[i++] - '0');
    return v;
  }

  inline bool _Key(const uint8_t* p, int i, int n, const char* key)
  {
    int k = strlen(key);
    return i + k <= n && memcmp(p + i, key, k) == 0;
  }

  inline void _SetSleepMinutes(int v)
  {
    int best = 0;
    for (int c = 1; c < Sleep::Choices; c++)
    {
      int d1 = v - Sleep::arrMinutes[c], d0 = v - Sleep::arrMinutes[best];
      if ((d1 < 0 ? -d1 : d1) < (d0 < 0 ? -d0 : d0)) best = c;
    }
    Sleep::State().nChoice = best;
  }

  // reads SYSTEM.CFG (or SLEEP.CFG) and applies the brightness
  inline void Load()
  {
    uint8_t sector[512];
    if (BIOS::FAT::Open("SYSTEM.CFG", BIOS::FAT::IoRead) == BIOS::FAT::EOk)
    {
      int n = BIOS::FAT::GetFileSize();
      if (n > 512) n = 512;
      if (BIOS::FAT::Read(sector) == BIOS::FAT::EOk)
      {
        for (int i = 0; i < n; )
        {
          if (_Key(sector, i, n, "sleep=")) { i += 6; _SetSleepMinutes(_Number(sector, i, n)); }
          else if (_Key(sector, i, n, "bright=")) { i += 7; int v = _Number(sector, i, n); Sleep::Brightness() = v > 100 ? 100 : v; }
          else if (_Key(sector, i, n, "volume=")) { i += 7; int v = _Number(sector, i, n); Volume() = v > 100 ? 100 : v; }
          while (i < n && sector[i] != '\n') i++;
          i++;
        }
      }
      BIOS::FAT::Close();
    }
    else if (BIOS::FAT::Open("SLEEP.CFG", BIOS::FAT::IoRead) == BIOS::FAT::EOk)
    {
      if (BIOS::FAT::Read(sector) == BIOS::FAT::EOk)
      {
        int i = 0;
        if (sector[0] >= '0' && sector[0] <= '9')
          _SetSleepMinutes(_Number(sector, i, 8));
      }
      BIOS::FAT::Close();
    }
    ApplyBrightness();
  }

  inline char* _Put(char* p, const char* key, int v)
  {
    strcpy(p, key);
    p += strlen(p);
    if (v >= 100) *p++ = '0' + v / 100;
    if (v >= 10) *p++ = '0' + v / 10 % 10;
    *p++ = '0' + v % 10;
    *p++ = '\r';
    *p++ = '\n';
    return p;
  }

  inline bool Save()
  {
    uint8_t sector[512];
    memset(sector, 0, sizeof(sector));
    char* p = (char*)sector;
    p = _Put(p, "sleep=", Sleep::Minutes());
    p = _Put(p, "bright=", Sleep::Brightness());
    p = _Put(p, "volume=", Volume());
    int n = p - (char*)sector;
    if (BIOS::FAT::Open("SYSTEM.CFG", BIOS::FAT::IoWrite) != BIOS::FAT::EOk)
      return false;
    bool ok = BIOS::FAT::Write(sector) == BIOS::FAT::EOk;
    BIOS::FAT::Close(n);
    return ok;
  }

  // ---------------------------------------------------------- languages
  // EN and the directories of LANG/ (codes of up to 3 letters)
  enum { MaxLanguages = 8 };
  struct TLanguages { int n; char code[MaxLanguages][4]; };

  inline void FindLanguages(TLanguages& l)
  {
    l.n = 1;
    strcpy(l.code[0], "EN");
    char path[8];
    strcpy(path, "LANG");
    if (BIOS::FAT::OpenDir(path) != BIOS::FAT::EOk)
      return;
    BIOS::FAT::TFindFile f;
    while (l.n < MaxLanguages && BIOS::FAT::FindNext(&f) == BIOS::FAT::EOk)
    {
      if (!(f.nAtrib & 0x10) || f.strName[0] == '.')   // directories only
        continue;
      int k = strlen(f.strName);
      if (k < 2 || k > 3)
        continue;
      char c[4];
      for (int i = 0; i <= k; i++)
        c[i] = f.strName[i] >= 'a' && f.strName[i] <= 'z' ? f.strName[i] - 32 : f.strName[i];
      if (strcmp(c, "EN") == 0)
        continue;
      strcpy(l.code[l.n++], c);
    }
  }

  // the code of LANG.CFG, "EN" when none
  inline void CurrentLanguage(char* code)
  {
    uint8_t sector[512];
    Lang::ReadCode(code, sector);
    if (!code[0])
      strcpy(code, "EN");
  }

  inline bool SetLanguage(const char* code)
  {
    uint8_t sector[512];
    memset(sector, 0, sizeof(sector));
    strcpy((char*)sector, code);
    int n = strlen(code);
    sector[n++] = '\r';
    sector[n++] = '\n';
    if (BIOS::FAT::Open("LANG.CFG", BIOS::FAT::IoWrite) != BIOS::FAT::EOk)
      return false;
    bool ok = BIOS::FAT::Write(sector) == BIOS::FAT::EOk;
    BIOS::FAT::Close(n);
    return ok;
  }
}

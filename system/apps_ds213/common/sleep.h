#pragma once
// Sleep of the display (DS203 applications): when no key was pressed and no
// signal was seen for the time of SYSTEM.CFG (system.h; minutes, 0: never), the
// backlight is switched off; any key switches it on again (the key is
// used only for that).
//
// The backlight is set by the factory SYS (__Set(BACKLIGHT, level), the
// entry of the SYS table the OS uses, os_host/source/bios/ds203/BIOS.s).
// The level before the sleep is not known to the OS: the compare registers
// of the timers are read before and after switching off, the one that
// changed (the PWM of the backlight) gets its old value back at the wake,
// after __Set(BACKLIGHT, WakeLevel). When none changed, the level is
// WakeLevel.
#include <stdint.h>
#include <string.h>

namespace Sleep
{
  enum { WakeLevel = 100 };
  // the brightness set in the System menu (system.h), 0: not set (SYS)
  inline int& Brightness() { static int n = 0; return n; }
  static const uint8_t arrMinutes[] = { 0, 1, 2, 5, 10, 30 };
  enum { Choices = sizeof(arrMinutes) };

  struct TState
  {
    int nChoice;                        // index of arrMinutes
    uint32_t lLast;                     // tick of the last key / signal
    bool bAsleep;
    int nSaved;                         // compare registers changed by the switch off
    uint32_t arrAddr[4], arrValue[4];
  };
  inline TState& State() { static TState s = { 3, 0, false, 0, {0}, {0} }; return s; }

#if defined(_ARM) && defined(DS203)
  typedef void (*TSysSet)(uint8_t, uint32_t);
  inline void _Backlight(uint32_t nLevel) { ((TSysSet)0x0800424D)(2, nLevel); }   // __Set(BACKLIGHT, n)
  // CCR1..CCR4 of TIM1, TIM2, TIM3, TIM4, TIM5, TIM8
  static const uint32_t arrTimers[] = { 0x40012C00, 0x40000000, 0x40000400, 0x40000800, 0x40000C00, 0x40013400 };
  enum { Regs = 6 * 4 };
  inline volatile uint32_t& _Ccr(int i) { return *(volatile uint32_t*)(arrTimers[i / 4] + 0x34 + 4 * (i % 4)); }
#else
  inline void _Backlight(uint32_t) {}
  enum { Regs = 0 };
  inline volatile uint32_t& _Ccr(int) { static uint32_t r; return r; }
#endif

  inline int Minutes() { return arrMinutes[State().nChoice]; }
  inline bool Asleep() { return State().bAsleep; }

  // key or signal: the time starts again
  inline void Activity()
  {
    State().lLast = BIOS::SYS::GetTick();
  }

  inline void Off()
  {
    TState& s = State();
    if (s.bAsleep)
      return;
    uint32_t arrBefore[Regs > 0 ? Regs : 1];
    for (int i = 0; i < Regs; i++)
      arrBefore[i] = _Ccr(i);
    _Backlight(0);
    s.nSaved = 0;
    for (int i = 0; i < Regs && s.nSaved < 4; i++)
      if (_Ccr(i) != arrBefore[i])
      {
        s.arrAddr[s.nSaved] = (uint32_t)(uintptr_t)&_Ccr(i);
        s.arrValue[s.nSaved] = arrBefore[i];
        s.nSaved++;
      }
    s.bAsleep = true;
  }

  // switches the backlight on, true when it was off
  inline bool Wake()
  {
    TState& s = State();
    Activity();
    if (!s.bAsleep)
      return false;
    s.bAsleep = false;
    // the SYS switches the backlight on (as it does it), the level of
    // before comes from the compare registers (or the brightness set)
    _Backlight(Brightness() > 0 ? Brightness() : WakeLevel);
    if (Brightness() > 0)
      return true;
#if defined(_ARM) && defined(DS203)
    for (int i = 0; i < s.nSaved; i++)
      *(volatile uint32_t*)s.arrAddr[i] = s.arrValue[i];
#endif
    return true;
  }

  // call often: switches off after the time
  inline void Tick()
  {
    TState& s = State();
    if (s.lLast == 0)
      s.lLast = BIOS::SYS::GetTick();
    int m = Minutes();
    if (!s.bAsleep && m > 0 && (int32_t)(BIOS::SYS::GetTick() - s.lLast) > m * 60000)
      Off();
  }

  // the choice of the menus: 0..Choices-1
  inline void SetChoice(int c)
  {
    if (c < 0) c = 0;
    if (c >= Choices) c = Choices - 1;
    State().nChoice = c;
    Activity();
  }
  inline int Choice() { return State().nChoice; }
}

#pragma once
// Run length words of the logic analyzer FPGA image (system/fpga_ds203/la)
// and the event list of a capture. No hardware access (host tests).
//
// Word (18 bits): bit 17 B, 16 A, 15 D, 14 C, bits 13..0 length - 1 in
// samples. Event: (state << 28) | first sample; state bit 0 C, 1 D, 2 A, 3 B.
// Consecutive words of the same state (a run longer than 16384 samples, or
// split at the trigger) become one event.

#include <stdint.h>

namespace La
{
  enum { MaxWords = 4095, MaxEvents = 4096, TimeMask = 0x0fffffff };

  inline int State(uint32_t e) { return (int)(e >> 28); }
  inline uint32_t Time(uint32_t e) { return e & TimeMask; }
  inline uint32_t Event(int state, uint32_t t) { return ((uint32_t)state << 28) | (t & TimeMask); }

  // words[0..n) -> events (in place allowed: ev == words); returns the number
  // of events; end = the sample after the last one; trig = first sample of
  // word nTrigWord (or 0 when it is outside)
  inline int Decode(const uint32_t* words, int n, int nTrigWord, uint32_t* ev,
                    uint32_t& end, uint32_t& trig)
  {
    uint32_t t = 0;
    int ne = 0, last = -1;
    trig = 0;
    for (int i = 0; i < n; i++)
    {
      uint32_t w = words[i];
      int s = (int)((w >> 14) & 15);
      uint32_t len = (w & 0x3fff) + 1;
      if (i == nTrigWord)
        trig = t;
      if (s != last)
      {
        ev[ne++] = Event(s, t);
        last = s;
      }
      t += len;
    }
    end = t;
    return ne;
  }

  // index of the event that holds sample t (0 before the first)
  inline int Find(const uint32_t* ev, int n, uint32_t t)
  {
    int lo = 0, hi = n - 1;
    while (lo < hi)
    {
      int mid = (lo + hi + 1) / 2;
      if (Time(ev[mid]) <= t)
        lo = mid;
      else
        hi = mid - 1;
    }
    return lo;
  }

  // first event after sample t where the channels of mask change (-1: none)
  inline int NextEdge(const uint32_t* ev, int n, uint32_t t, int mask)
  {
    int i = Find(ev, n, t);
    int s = State(ev[i]);
    for (i = i + 1; i < n; i++)
    {
      if ((State(ev[i]) ^ s) & mask)
        return i;
      s = State(ev[i]);
    }
    return -1;
  }
}

#pragma once
// Protocol decoders of the logic analyzer: UART, SPI, I2C, 1-Wire.
// Input: the event list of la.h (state << 28 | first sample). Output: a
// list of frames (first / end sample, value, lane, kind, flags). No hardware
// access (host tests: tools in test/).

#include <stdint.h>
#include "la.h"

namespace Dec
{
  enum EProto { POff, PUart, PSpi, PI2c, POneWire, Protos };
  enum EKind { Data, AddrW, AddrR, Start, Restart, Stop, Reset, Presence };
  enum { FlErr = 1, FlNack = 2, FlPar = 4 };

  struct TFrame
  {
    uint32_t t0, t1;          // samples: first, end (t0 == t1: a point)
    uint16_t v;
    uint8_t lane;             // 0 A, 1 B, 2 C, 3 D
    uint8_t kind : 4, flags : 4;
  };

  struct TOut
  {
    TFrame* f;
    int cap;
    int n = 0;
    bool overflow = false;
    TOut(TFrame* frames, int nMax) : f(frames), cap(nMax) {}
    void Add(uint32_t t0, uint32_t t1, uint32_t v, int lane, int kind, int flags)
    {
      if (n >= cap) { overflow = true; return; }
      TFrame& r = f[n++];
      r.t0 = t0; r.t1 = t1; r.v = (uint16_t)v; r.lane = (uint8_t)lane;
      r.kind = (uint8_t)kind; r.flags = (uint8_t)flags;
    }
  };

  inline int Level(const uint32_t* ev, int n, uint32_t t, int mask)
  {
    return (La::State(ev[La::Find(ev, n, t)]) & mask) ? 1 : 0;
  }

  // ------------------------------------------------------------ UART
  // shortest run of the lane (at least 2 samples) as one bit, rounded to
  // a standard rate within 8 %; 0: no two changes
  inline uint32_t AutoBaud(const uint32_t* ev, int n, int mask, float rate)
  {
    static const uint32_t std[] = { 300, 600, 1200, 2400, 4800, 9600, 14400, 19200,
      28800, 31250, 38400, 57600, 76800, 115200, 230400, 250000, 460800, 500000,
      921600, 1000000, 1500000, 2000000, 3000000, 4000000 };
    uint32_t best = 0xffffffff, last = 0;
    bool have = false;
    int s = La::State(ev[0]) & mask;
    for (int i = 1; i < n; i++)
    {
      int s2 = La::State(ev[i]) & mask;
      if (s2 == s) continue;
      uint32_t t = La::Time(ev[i]);
      if (have && t - last >= 2 && t - last < best)
        best = t - last;
      last = t; have = true; s = s2;
    }
    if (best == 0xffffffff)
      return 0;
    // refined: runs as whole numbers of bits (the shortest run alone is off
    // by up to a sample): up to 3 bits, then with that bit time up to 10
    float bit = (float)best;
    for (int pass = 0; pass < 2; pass++)
    {
      uint64_t sumT = 0, sumB = 0;
      uint32_t maxB = pass ? 10 : 3;
      s = La::State(ev[0]) & mask; have = false;
      for (int i = 1; i < n; i++)
      {
        int s2 = La::State(ev[i]) & mask;
        if (s2 == s) continue;
        uint32_t t = La::Time(ev[i]);
        if (have)
        {
          uint32_t d = t - last, nb = (uint32_t)(d / bit + 0.5f);
          if (nb >= 1 && nb <= maxB) { sumT += d; sumB += nb; }
        }
        last = t; have = true; s = s2;
      }
      if (sumB)
        bit = (float)sumT / sumB;
    }
    float baud = rate / bit;
    for (unsigned i = 0; i < sizeof(std) / sizeof(std[0]); i++)
    {
      float d = baud / std[i];
      if (d > 0.92f && d < 1.08f)
        return std[i];
    }
    return (uint32_t)(baud + 0.5f);
  }

  // 1 start bit, bits data bits (LSB first), parity 0 none / 1 even / 2 odd,
  // stop bits; inv: idle low. false: less than 3 samples per bit
  inline bool Uart(const uint32_t* ev, int n, uint32_t end, int mask, int lane,
                   float rate, uint32_t baud, int bits, int parity, int stop, bool inv, TOut& o)
  {
    if (baud == 0)
      return false;
    uint64_t bt = (uint64_t)(rate * 256.0f / baud + 0.5f);   // samples << 8 per bit
    if (bt < 3 * 256)
      return false;
    const int idle = inv ? 0 : 1, x = inv ? 1 : 0;
    const int total = 1 + bits + (parity ? 1 : 0) + stop;
    uint32_t tmin = 0;
    int k = 1;
    while (true)
    {
      // start edge (idle -> active) at or after tmin
      k = La::Find(ev, n, tmin);
      if (k < 1) k = 1;
      for (; k < n; k++)
      {
        if (La::Time(ev[k]) < tmin) continue;
        int a = (La::State(ev[k]) & mask) ? 1 : 0, b = (La::State(ev[k - 1]) & mask) ? 1 : 0;
        if (a != idle && b == idle) break;
      }
      if (k >= n)
        break;
      uint32_t ts = La::Time(ev[k]);
      uint64_t base = (uint64_t)ts << 8;
      // middle of bit b
      auto mid = [&](int b) { return (uint32_t)((base + (uint64_t)(2 * b + 1) * bt / 2) >> 8); };
      if (mid(total - 1) >= end)
        break;
      if (Level(ev, n, mid(0), mask) == idle)
      {
        tmin = ts + 1;                  // a glitch, not a start bit
        continue;
      }
      uint32_t v = 0;
      int ones = 0;
      for (int b = 0; b < bits; b++)
      {
        int bit = Level(ev, n, mid(1 + b), mask) ^ x;
        v |= (uint32_t)bit << b;
        ones += bit;
      }
      int flags = 0, idx = 1 + bits;
      if (parity)
      {
        int pb = Level(ev, n, mid(idx), mask) ^ x;
        if (((ones + pb) & 1) != (parity == 2 ? 1 : 0))
          flags |= FlPar;
        idx++;
      }
      for (int s = 0; s < stop; s++)
        if (Level(ev, n, mid(idx + s), mask) != idle)
          flags |= FlErr;
      o.Add(ts, (uint32_t)((base + (uint64_t)total * bt) >> 8), v, lane, Data, flags);
      tmin = mid(idx);                  // the middle of the stop bit
    }
    return true;
  }

  // ------------------------------------------------------------ SPI
  // masks: clk, mosi, miso, cs (0: not used; cs active low). Mode: data
  // sampled on the rising clock edge when cpol == cpha. A word cut by CS
  // is marked FlErr.
  inline void Spi(const uint32_t* ev, int n, int clk, int mosi, int miso, int cs,
                  int laneMosi, int laneMiso, bool cpol, bool cpha, int bits, bool msb, TOut& o)
  {
    const bool sampleRise = cpol == cpha;
    int cnt = 0;
    uint32_t vo = 0, vi = 0, t0 = 0, lastClk = 0;
    bool started = false, haveClk = false;
    bool active = !cs || !(La::State(ev[0]) & cs);
    auto emit = [&](uint32_t t1, int flags) {
      if (mosi) o.Add(t0, t1, vo, laneMosi, Data, flags);
      if (miso) o.Add(t0, t1, vi, laneMiso, Data, flags);
    };
    for (int i = 1; i < n; i++)
    {
      int p = La::State(ev[i - 1]), s = La::State(ev[i]);
      uint32_t t = La::Time(ev[i]);
      if (cs)
      {
        bool a = !(s & cs);
        if (a != active)
        {
          if (!a && cnt > 0)
            emit(t, FlErr);
          cnt = 0; vo = vi = 0; started = false; haveClk = false;
          active = a;
        }
      }
      if (!active || !((p ^ s) & clk))
        continue;
      bool rise = (s & clk) != 0;
      if (!started) { started = true; t0 = t; }
      if (rise == sampleRise)
      {
        int bo = (s & mosi) ? 1 : 0, bi = (s & miso) ? 1 : 0;
        if (msb) { vo = (vo << 1) | bo; vi = (vi << 1) | bi; }
        else { vo |= (uint32_t)bo << cnt; vi |= (uint32_t)bi << cnt; }
        if (++cnt == bits)
        {
          emit(t + (haveClk && t > lastClk ? t - lastClk : 1), 0);
          cnt = 0; vo = vi = 0; started = false;
        }
      }
      lastClk = t; haveClk = true;
    }
  }

  // ------------------------------------------------------------ I2C
  // frames on the SDA lane: Start / Restart / Stop (points), the address
  // byte (AddrW / AddrR, 7 bit address), data bytes; FlNack: no acknowledge
  inline void I2c(const uint32_t* ev, int n, int scl, int sda, int lane, TOut& o)
  {
    bool in = false;
    int cnt = 0, bytes = 0;
    uint32_t v = 0, t0 = 0, lastFall = 0;
    for (int i = 1; i < n; i++)
    {
      int p = La::State(ev[i - 1]), s = La::State(ev[i]);
      uint32_t t = La::Time(ev[i]);
      bool sclP = p & scl, sclS = s & scl;
      if (((p ^ s) & sda) && sclP && sclS)
      {
        if (!(s & sda)) { o.Add(t, t, 0, lane, in ? Restart : Start, 0); in = true; }
        else { o.Add(t, t, 0, lane, Stop, 0); in = false; }
        cnt = 0; bytes = 0; v = 0;
        continue;
      }
      if (in && !sclP && sclS)
      {
        int b = (s & sda) ? 1 : 0;
        if (cnt == 0)
          t0 = lastFall ? lastFall : t;
        if (cnt < 8) { v = (v << 1) | b; cnt++; }
        else
        {
          uint32_t t1 = t + (t > lastFall ? t - lastFall : 1);
          if (bytes == 0)
            o.Add(t0, t1, v >> 1, lane, (v & 1) ? AddrR : AddrW, b ? FlNack : 0);
          else
            o.Add(t0, t1, v, lane, Data, b ? FlNack : 0);
          bytes++; cnt = 0; v = 0;
        }
      }
      if (sclP && !sclS)
        lastFall = t;
    }
  }

  // ------------------------------------------------------------ 1-Wire
  // standard speed: low >= 380 us reset, then presence; a slot low < 16 us
  // is 1, up to 120 us 0; bytes LSB first
  inline void OneWire(const uint32_t* ev, int n, int mask, int lane, float rate, TOut& o)
  {
    const float spu = rate * 1e-6f;     // samples per us
    int cnt = 0;
    uint32_t v = 0, t0 = 0, lastRise = 0;
    bool afterReset = false;
    for (int i = 1; i < n; i++)
    {
      if (!((La::State(ev[i - 1]) & mask) && !(La::State(ev[i]) & mask)))
        continue;
      uint32_t tf = La::Time(ev[i]);
      int k = i + 1;
      while (k < n && !(La::State(ev[k]) & mask)) k++;
      if (k >= n)
        break;
      uint32_t tr = La::Time(ev[k]);
      float d = (tr - tf) / spu;
      if (afterReset && (tf - lastRise) / spu < 80 && d >= 40 && d <= 300)
        o.Add(tf, tr, 0, lane, Presence, 0);
      else if (d >= 380)
      {
        o.Add(tf, tr, 0, lane, Reset, 0);
        cnt = 0; v = 0;
        afterReset = true;
        lastRise = tr; i = k;
        continue;
      }
      else if (d > 120)
      {
        cnt = 0; v = 0;                 // not a slot
      }
      else
      {
        if (cnt == 0) t0 = tf;
        v |= (uint32_t)(d < 16 ? 1 : 0) << cnt;
        if (++cnt == 8)
        {
          uint32_t slot = (uint32_t)(60 * spu + 0.5f);
          o.Add(t0, tf + (tr - tf > slot ? tr - tf : slot), v, lane, Data, 0);
          cnt = 0; v = 0;
        }
      }
      afterReset = false;
      lastRise = tr; i = k;
    }
  }

  // frames by first sample (several lanes are decoded one after another)
  inline void Sort(TFrame* f, int n)
  {
    for (int gap = n / 2; gap > 0; gap /= 2)
      for (int i = gap; i < n; i++)
      {
        TFrame x = f[i];
        int j = i;
        for (; j >= gap && f[j - gap].t0 > x.t0; j -= gap)
          f[j] = f[j - gap];
        f[j] = x;
      }
  }
}

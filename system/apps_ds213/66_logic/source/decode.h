#pragma once
// Protocol decoders of the logic analyzer, the protocols of the oscilloscope
// (58_gabmini Decode): UART, LIN, I2C, SPI, 1-Wire, CAN, WS2812, NEC IR,
// PS/2, pulse width, edge counter, quadrature encoder, USB (the core is
// shared with the oscilloscope: ../../common/usbdecode.h). Input: the event
// list of la.h (state << 28 | first sample), so a capture of up to 67 M
// samples is decoded without a sample buffer. Output: frames (first / end
// sample, value, lane, kind, flags). Also the text of a frame and the
// protocol trigger (a condition on the frames of a capture).
// No hardware access (host tests: test/test_decode.cpp).

#include <stdint.h>
#include <string.h>
#include "la.h"
#include "../../common/usbdecode.h"

namespace Dec
{
  enum EProto { POff, PUart, PLin, PI2c, PSpi, POneWire, PCan, PWs2812, PNec, PPs2,
    PPulse, PCounter, PEncoder, PUsb, Protos };
  enum { MaxRoles = 4 };

  enum EKind { Data, AddrW, AddrR, Start, Restart, Stop, Break, Sync, Pid, Checksum,
    Reset, Presence, CanId, CanDlc, CanCrc, CanAck, Color, Leader, Repeat, NecAddr,
    NecCmd, Pulse, Count, QuadRun, QuadZ, QuadErr, StuffErr,
    UsbItem };                // USB: aux = Usb::EKind, v its value (bytes are Data)
  // flags; Data aux: number of bits | the bits below
  enum { FlErr = 1, FlNack = 2, FlPoint = 4, FlPar = 8, FlFirst = 16 };
  enum { AuxRom = 0x100, AuxFunc = 0x200, AuxBadSlot = 0x400 };
  enum ECountEdge { EdgeRise, EdgeFall, EdgeBoth };

  struct TFrame
  {
    uint32_t t0, t1;          // samples: first, last (t0 == t1: a point)
    uint32_t v;
    uint16_t aux;
    uint8_t kind;
    uint8_t lane : 2, flags : 6;
  };

  // ------------------------------------------------------------ settings
  // lanes: 0 A, 1 B, 2 C, 3 D, -1 none
  struct TCfg
  {
    uint32_t magic;
    int8_t proto;
    int8_t lane[Protos][MaxRoles];
    int8_t inv[Protos];
    int8_t uartBaud, uartBits, uartParity, uartStop;   // uartBits 5..9, uartStop 1..2
    int8_t linBaud;
    int8_t spiMode, spiBits, spiLsb;                    // spiBits 4..32
    int8_t canRate;
    int8_t wsOrder;                                     // 0 GRB, 1 RGB
    int8_t cntEdge;
    int8_t quadRes, quadDir;                            // 0 x4, 1 x2, 2 x1
    int8_t fmt;                                         // 0 hex, 1 decimal, 2 ASCII
    // protocol trigger: condition (per protocol, 0 off), value, mask, then
    // (0 off: the next frame of the lane equals thenValue), occurrence
    int8_t tCond[Protos];
    int8_t tThen;
    uint8_t tNth;
    uint32_t tValue, tMask, tThenValue;
    int8_t usbSpeed;                                    // Usb::ESpeed: auto, full, low
  };
  const uint32_t CfgMagic = 0x4c444333;   // "LDC3" (LDC2: without USB)

  static const uint32_t RateUart[] = { 0, 300, 600, 1200, 2400, 4800, 9600, 14400, 19200,
    28800, 31250, 38400, 57600, 76800, 115200, 230400, 250000, 460800, 500000, 921600,
    1000000, 1500000, 2000000, 3000000, 4000000 };
  enum { RateUartN = sizeof(RateUart) / sizeof(RateUart[0]) };
  static const uint32_t RateLin[] = { 0, 2400, 9600, 10417, 19200 };
  enum { RateLinN = sizeof(RateLin) / sizeof(RateLin[0]) };
  static const uint32_t RateCan[] = { 0, 10000, 20000, 33333, 50000, 83333, 100000, 125000,
    250000, 500000, 800000, 1000000 };
  enum { RateCanN = sizeof(RateCan) / sizeof(RateCan[0]) };

  static const char* const ProtoName[Protos] = { "Dec off", "UART", "LIN", "I2C", "SPI",
    "1-Wire", "CAN", "WS2812", "NEC IR", "PS/2", "Pulse", "Counter", "Encoder", "USB" };
  // role names (nullptr: not used), short names (lane labels)
  static const char* const RoleName[Protos][MaxRoles] = {
    { nullptr, nullptr, nullptr, nullptr }, { "RX", "TX", nullptr, nullptr },
    { "Bus", nullptr, nullptr, nullptr }, { "SCL", "SDA", nullptr, nullptr },
    { "CLK", "MOSI", "MISO", "CS" }, { "DQ", nullptr, nullptr, nullptr },
    { "RX", nullptr, nullptr, nullptr }, { "DIN", nullptr, nullptr, nullptr },
    { "IR", nullptr, nullptr, nullptr }, { "CLK", "DATA", nullptr, nullptr },
    { "In", nullptr, nullptr, nullptr }, { "In", nullptr, nullptr, nullptr },
    { "A", "B", "Z", nullptr }, { "D+", "D-", nullptr, nullptr } };
  static const char* const RoleShort[Protos][MaxRoles] = {
    { "", "", "", "" }, { "RX", "TX", "", "" }, { "LN", "", "", "" }, { "CL", "DA", "", "" },
    { "CK", "MO", "MI", "CS" }, { "1W", "", "", "" }, { "CN", "", "", "" }, { "WS", "", "", "" },
    { "IR", "", "", "" }, { "CK", "DA", "", "" }, { "PW", "", "", "" }, { "CT", "", "", "" },
    { "QA", "QB", "QZ", "" }, { "D+", "D-", "", "" } };

  inline bool RoleOptional(int p, int r)
  {
    return (p == PUart) || (p == PSpi && r >= 1) || (p == PEncoder && r == 2);
  }
  inline bool HasInvert(int p)
  {
    return p == PUart || p == PLin || p == PCan || p == PNec || p == PPulse || p == PEncoder;
  }

  inline void Defaults(TCfg& c)
  {
    memset(&c, 0, sizeof(c));
    c.magic = CfgMagic;
    for (int p = 0; p < Protos; p++)
      for (int r = 0; r < MaxRoles; r++)
        c.lane[p][r] = !RoleName[p][r] ? -1 : r == 0 ? 2 : r == 1 ? 3 : -1;
    c.lane[PSpi][2] = 0;                  // MISO A, CS B
    c.lane[PSpi][3] = 1;
    c.lane[PEncoder][2] = -1;
    c.inv[PNec] = 1;                      // IR receivers: active low
    c.uartBits = 8; c.uartStop = 1;
    c.spiBits = 8;
    c.tNth = 1;
    c.tMask = 0xffffffff;
  }

  // ------------------------------------------------------------ output
  struct TOut
  {
    TFrame* f;
    int cap;
    int n = 0;
    bool overflow = false;
    TOut(TFrame* frames, int nMax) : f(frames), cap(nMax) {}
    bool Add(uint32_t t0, uint32_t t1, int kind, uint32_t v, int lane, int flags = 0, int aux = 0)
    {
      if (n >= cap) { overflow = true; return false; }
      if (t1 < t0) t1 = t0;
      TFrame& r = f[n++];
      r.t0 = t0; r.t1 = t1; r.v = v; r.aux = (uint16_t)aux; r.kind = (uint8_t)kind;
      r.lane = (uint8_t)lane; r.flags = (uint8_t)flags;
      return true;
    }
  };

  // statistics of the last decode
  struct TStats
  {
    uint32_t baud;            // detected or set rate (UART, LIN, CAN), 0 n/a
    uint32_t edges;           // Counter: all counted edges
    bool slow;                // sample rate too low
    bool noLane;              // a required lane is missing
    // Encoder (x4 steps)
    int32_t steps, perRev, inRev;
    uint32_t qEdges, zCount, qErrors;
    bool havePerRev, haveZ;
    float qRate;              // steps / s
  };

  // ------------------------------------------------------------ lines
  struct TLine
  {
    const uint32_t* ev = nullptr;
    int n = 0;
    uint32_t end = 0;
    int mask = 0, inv = 0;
    bool valid = false;
    int lane = -1;

    int Lv(int i) const { return ((La::State(ev[i]) & mask) ? 1 : 0) ^ inv; }
    int L(uint32_t t) const { return Lv(La::Find(ev, n, t)); }
    uint32_t T(int i) const { uint32_t t = La::Time(ev[i]); return t < end ? t : end; }
    // first sample >= from with the level (end: none)
    uint32_t Find(uint32_t from, int level) const
    {
      if (from >= end) return end;
      int i = La::Find(ev, n, from);
      if (Lv(i) == level) return from;
      for (i++; i < n; i++)
        if (Lv(i) == level) return T(i);
      return end;
    }
    // first sample > from with another level (end: none)
    uint32_t NextChange(uint32_t from) const
    {
      if (from >= end) return end;
      int i = La::Find(ev, n, from);
      int l = Lv(i);
      for (i++; i < n; i++)
        if (Lv(i) != l) return T(i);
      return end;
    }
  };

  inline int Popcount(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

  class CDecoder
  {
  public:
    const uint32_t* ev;
    int n;
    uint32_t end;
    float fs;
    const TCfg& c;
    TOut& o;
    TStats& st;

    CDecoder(const uint32_t* e, int ne, uint32_t nEnd, float rate, const TCfg& cfg, TOut& out, TStats& s)
      : ev(e), n(ne), end(nEnd), fs(rate), c(cfg), o(out), st(s) {}

    TLine Line(int role, bool invert = false) const
    {
      TLine l;
      int lane = c.lane[(int)c.proto][role];
      l.ev = ev; l.n = n; l.end = end;
      l.valid = lane >= 0 && lane < 4;
      static const int bit[4] = { 4, 8, 1, 2 };   // lane -> state bit
      l.mask = l.valid ? bit[lane] : 0;
      l.inv = invert ? 1 : 0;
      l.lane = lane;
      return l;
    }

    // ---------------------------------------------------------- bit rate
    // the shortest runs (> 1 sample), then all runs up to 10 bits as whole
    // numbers of bits; 0: no signal
    float EstimateBit(const TLine& ln)
    {
      uint32_t nLimit = 1;
      for (int iter = 0; iter < 8; iter++)
      {
        uint32_t nMin = 0xffffffff;
        uint32_t p = ln.NextChange(0);
        while (true)
        {
          uint32_t q = ln.NextChange(p);
          if (q >= end) break;
          if (q - p > nLimit && q - p < nMin) nMin = q - p;
          p = q;
        }
        if (nMin == 0xffffffff)
          return 0;
        uint32_t nHigh = nMin + nMin * 3 / 10 + 1;
        uint32_t nRuns = 0;
        uint64_t nSum = 0;
        p = ln.NextChange(0);
        while (true)
        {
          uint32_t q = ln.NextChange(p);
          if (q >= end) break;
          if (q - p >= nMin && q - p <= nHigh) { nRuns++; nSum += q - p; }
          p = q;
        }
        if (nRuns >= 2)
        {
          float fBit = (float)nSum / nRuns;
          for (int pass = 0; pass < 2; pass++)
          {
            uint64_t sumL = 0, sumN = 0;
            p = ln.NextChange(0);
            while (true)
            {
              uint32_t q = ln.NextChange(p);
              if (q >= end) break;
              uint32_t k = (uint32_t)((q - p) / fBit + 0.5f);
              if (k >= 1 && k <= (pass ? 10u : 3u)) { sumL += q - p; sumN += k; }
              p = q;
            }
            if (sumN) fBit = (float)sumL / sumN;
          }
          return fBit;
        }
        nLimit = nMin;
      }
      return 0;
    }

    // samples per bit: the rate, or detected and rounded to the table (5 %)
    float BitLength(const TLine& ln, uint32_t rate, const uint32_t* table, int nTable)
    {
      if (rate) { st.baud = rate; return fs / rate; }
      float fBit = EstimateBit(ln);
      if (fBit <= 0) return 0;
      float fRate = fs / fBit;
      for (int i = 1; i < nTable; i++)
        if (fRate > table[i] * 0.95f && fRate < table[i] * 1.05f)
        {
          st.baud = table[i];
          return fs / table[i];
        }
      st.baud = (uint32_t)(fRate + 0.5f);
      return fBit;
    }

    // ---------------------------------------------------------- UART
    void UartLine(const TLine& ln, int nBits, int nParity, int nStop, float fBit)
    {
      const uint64_t bt = (uint64_t)(fBit * 256.0f + 0.5f);   // samples << 8
      const int nFrame = 1 + nBits + (nParity ? 1 : 0) + nStop;
      uint32_t i = 0;
      while (!o.overflow)
      {
        i = ln.Find(i, 1);
        i = ln.Find(i, 0);
        if (i >= end) break;
        uint32_t s0 = i;
        uint64_t base = (uint64_t)s0 << 8;
        auto at = [&](int k) { return (uint32_t)((base + (uint64_t)(2 * k + 1) * bt / 2) >> 8); };
        if (at(nFrame - 1) >= end) break;
        if (ln.L(at(0))) { i = s0 + 1; continue; }         // a glitch
        uint32_t v = 0;
        for (int k = 0; k < nBits; k++)
          if (ln.L(at(1 + k))) v |= 1u << k;
        int k = 1 + nBits, pb = 0;
        bool bPar = false;
        if (nParity)
        {
          pb = ln.L(at(k++));
          int ones = Popcount(v) + pb;
          bPar = nParity == 1 ? (ones & 1) : !(ones & 1);
        }
        bool bStop = true;
        for (int j = 0; j < nStop; j++)
          if (!ln.L(at(k + j))) bStop = false;
        if (v == 0 && !bStop && !pb)
        {
          uint32_t e = ln.Find(at(k), 1);                 // break: low for the frame
          o.Add(s0, e - 1, Break, 0, ln.lane);
          i = e;
          continue;
        }
        o.Add(s0, (uint32_t)((base + (uint64_t)nFrame * bt) >> 8) - 1, Data, v, ln.lane,
              (bStop ? 0 : FlErr) | (bPar ? FlPar : 0), nBits);
        i = at(k + nStop - 1);                            // middle of the last stop bit
      }
    }

    bool Uart(bool lin)
    {
      TLine rx = Line(0, c.inv[(int)c.proto]);
      TLine tx = lin ? TLine() : Line(1, c.inv[(int)c.proto]);
      if (!rx.valid && !tx.valid) { st.noLane = true; return false; }
      uint32_t rate = lin ? RateLin[(int)c.linBaud] : RateUart[(int)c.uartBaud];
      float fBit = rx.valid ? (lin ? BitLength(rx, rate, RateLin, RateLinN) : BitLength(rx, rate, RateUart, RateUartN)) : 0;
      if (fBit == 0 && tx.valid)
        fBit = BitLength(tx, rate, RateUart, RateUartN);
      if (fBit == 0) return false;
      if (fBit < 2.5f) { st.slow = true; return false; }
      if (lin) { UartLine(rx, 8, 0, 1, fBit); return true; }
      if (rx.valid) UartLine(rx, c.uartBits, c.uartParity, c.uartStop, fBit);
      if (tx.valid) UartLine(tx, c.uartBits, c.uartParity, c.uartStop, fBit);
      return true;
    }

    // ---------------------------------------------------------- LIN
    uint8_t LinChecksum(int from, int to, int seed)
    {
      int sum = seed;
      for (int j = from; j < to; j++)
      {
        if (o.f[j].kind != Data) continue;
        sum += o.f[j].v & 0xff;
        if (sum > 255) sum -= 255;
      }
      return (uint8_t)~sum;
    }

    void LinClose(int nFirst, int nLast, int nPid, bool closed)
    {
      if (nFirst < 0 || nLast < nFirst) return;
      TFrame& a = o.f[nLast];
      uint8_t cs = a.v & 0xff;
      int aux = LinChecksum(nFirst, nLast, 0) == cs ? 1 : (nPid >= 0 && LinChecksum(nFirst, nLast, nPid) == cs) ? 2 : 0;
      if (aux || closed)
      {
        a.kind = Checksum;
        a.aux = (uint16_t)aux;
        if (!aux) a.flags |= FlErr;
      }
    }

    void PostLin(int from)
    {
      int state = 0, nPid = -1, nFirst = -1, nLast = -1;
      for (int j = from; j < o.n; j++)
      {
        TFrame& a = o.f[j];
        if (a.kind == Break)
        {
          if (state == 3) LinClose(nFirst, nLast, nPid, true);
          state = 1; nPid = -1; nFirst = -1; nLast = -1;
          continue;
        }
        if (a.kind != Data) continue;
        switch (state)
        {
          case 1:
            a.kind = Sync;
            if ((a.v & 0xff) != 0x55) a.flags |= FlErr;
            state = 2;
            break;
          case 2:
          {
            a.kind = Pid;
            int id = a.v & 0x3f;
            int p0 = (id ^ (id >> 1) ^ (id >> 2) ^ (id >> 4)) & 1;
            int p1 = !(((id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id >> 5)) & 1);
            if ((int)(a.v & 0xff) != (id | (p0 << 6) | (p1 << 7))) a.flags |= FlErr;
            nPid = a.v & 0xff;
            state = 3;
            break;
          }
          case 3:
            if (nFirst < 0) nFirst = j;
            nLast = j;
            break;
        }
      }
      if (state == 3) LinClose(nFirst, nLast, nPid, false);
    }

    // ---------------------------------------------------------- I2C
    void I2c()
    {
      TLine scl = Line(0), sda = Line(1);
      if (!scl.valid || !sda.valid || scl.lane == sda.lane) { st.noLane = true; return; }
      bool in = false, addr = false;
      int cnt = 0;
      uint32_t v = 0, t0 = 0, lastFall = 0;
      bool haveFall = false;
      for (int i = 1; i < n && !o.overflow; i++)
      {
        int p = La::State(ev[i - 1]), s = La::State(ev[i]);
        uint32_t t = La::Time(ev[i]);
        bool cp = p & scl.mask, cs = s & scl.mask;
        if (((p ^ s) & sda.mask) && cp && cs)
        {
          if (!(s & sda.mask)) { o.Add(t, t, in ? Restart : Start, 0, sda.lane, FlPoint); in = true; addr = true; }
          else { o.Add(t, t, Stop, 0, sda.lane, FlPoint); in = false; }
          cnt = 0; v = 0;
          continue;
        }
        if (in && !cp && cs)
        {
          int b = (s & sda.mask) ? 1 : 0;
          if (cnt == 0) { t0 = haveFall ? lastFall : t; v = 0; }
          if (cnt < 8) { v = (v << 1) | b; cnt++; }
          else
          {
            uint32_t half = haveFall && t > lastFall ? t - lastFall : 1;
            if (addr)
              o.Add(t0, t + half, (v & 1) ? AddrR : AddrW, v >> 1, sda.lane, b ? FlNack : 0, 8);
            else
              o.Add(t0, t + half, Data, v, sda.lane, b ? FlNack : 0, 8);
            addr = false;
            cnt = 0;
          }
        }
        if (cp && !cs) { lastFall = t; haveFall = true; }
      }
    }

    // ---------------------------------------------------------- SPI
    void Spi()
    {
      TLine clk = Line(0), mosi = Line(1), miso = Line(2), cs = Line(3);
      if (!clk.valid || (!mosi.valid && !miso.valid)) { st.noLane = true; return; }
      const int bits = c.spiBits;
      const bool cpol = c.spiMode & 2, cpha = c.spiMode & 1, sampleRise = cpol == cpha;
      int cnt = 0;
      uint32_t vo = 0, vi = 0, t0 = 0, lastClk = 0, lastSample = 0, period = 0;
      bool started = false, haveClk = false, first = true;
      bool active = !cs.valid || !(La::State(ev[0]) & cs.mask);
      auto emit = [&](uint32_t t1, int flags) {
        if (mosi.valid) o.Add(t0, t1, Data, vo, mosi.lane, flags, cnt);
        if (miso.valid) o.Add(t0, t1, Data, vi, miso.lane, flags, cnt);
      };
      for (int i = 1; i < n && !o.overflow; i++)
      {
        int p = La::State(ev[i - 1]), s = La::State(ev[i]);
        uint32_t t = La::Time(ev[i]);
        if (cs.valid)
        {
          bool a = !(s & cs.mask);
          if (a != active)
          {
            if (!a && cnt > 0) emit(t, FlErr | (first ? FlFirst : 0));
            cnt = 0; vo = vi = 0; started = false; haveClk = false; first = true; period = 0;
            active = a;
          }
        }
        if (!active || !((p ^ s) & clk.mask))
          continue;
        bool rise = (s & clk.mask) != 0;
        if (rise == sampleRise)
        {
          // without CS: a pause of the clock (> 4 periods) starts a new word
          if (!cs.valid && period && t - lastSample > 4 * period + 2)
          {
            cnt = 0; vo = vi = 0; first = true;
            started = true; t0 = cpha && haveClk ? lastClk : t;
          }
          if (cnt > 0) period = t - lastSample;
          if (!started) { started = true; t0 = t; }
          int bo = (s & mosi.mask) ? 1 : 0, bi = (s & miso.mask) ? 1 : 0;
          if (!c.spiLsb) { vo = (vo << 1) | bo; vi = (vi << 1) | bi; }
          else { vo |= (uint32_t)bo << cnt; vi |= (uint32_t)bi << cnt; }
          lastSample = t;
          if (++cnt == bits)
          {
            emit(t + (haveClk && t > lastClk ? t - lastClk : 1), first ? FlFirst : 0);
            cnt = 0; vo = vi = 0; started = false; first = false;
          }
        }
        else if (!started)
        {
          started = true; t0 = t;          // CPHA 1: the word starts at the shift edge
        }
        lastClk = t; haveClk = true;
      }
    }

    // ---------------------------------------------------------- 1-Wire
    void OneWire()
    {
      TLine dq = Line(0);
      if (!dq.valid) { st.noLane = true; return; }
      if (fs < 250e3f) { st.slow = true; return; }
      const float spu = fs / 1e6f;
      int nBits = -1, nByte = 0, nFunc = -1;
      uint32_t v = 0, t0 = 0;
      uint32_t i = dq.Find(0, 1);
      while (!o.overflow)
      {
        uint32_t f = dq.Find(i, 0), r = dq.Find(f, 1);
        if (r >= end) break;
        float w = (r - f) / spu;
        if (w >= 380)
        {
          uint32_t f2 = dq.Find(r, 0), r2 = dq.Find(f2, 1);
          float d = (f2 - r) / spu, w2 = (r2 - f2) / spu;
          bool pres = r2 < end && d >= 10 && d <= 80 && w2 >= 50 && w2 <= 300;
          o.Add(f, r - 1, Reset, 0, dq.lane, pres ? 0 : FlErr);
          if (pres) { o.Add(f2, r2 - 1, Presence, 0, dq.lane); r = r2; }
          nBits = 0; nByte = 0; nFunc = -1; v = 0;
        }
        else if (w < 120 && nBits >= 0)
        {
          if (nBits == 0) { t0 = f; v = 0; }
          v |= (uint32_t)(w < 15 ? 1 : 0) << nBits;
          if (++nBits == 8)
          {
            uint32_t e = f + (uint32_t)(60 * spu);
            if (e < r) e = r;
            int aux = 8;
            if (nByte == 0) { aux |= AuxRom; nFunc = v == 0xcc ? 1 : v == 0x55 ? 9 : -1; }
            else if (nByte == nFunc) aux |= AuxFunc;
            o.Add(t0, e, Data, v, dq.lane, 0, aux);
            nByte++;
            nBits = 0;
          }
        }
        else if (w >= 120)
        {
          o.Add(f, r - 1, Data, 0, dq.lane, FlErr, AuxBadSlot);
          nBits = -1;
        }
        i = r;
      }
    }

    // ---------------------------------------------------------- CAN
    uint32_t CanFrame(const TLine& ln, uint32_t sof, float fBit)
    {
      enum { MaxBits = 128 };
      uint8_t bits[MaxBits];
      uint32_t pos[MaxBits];
      const uint64_t bt = (uint64_t)(fBit * 256.0f + 0.5f);
      uint64_t bitStart = (uint64_t)sof << 8;
      // one raw bit; a recessive to dominant edge resynchronizes
      auto readRaw = [&](uint32_t& begin) -> int {
        begin = (uint32_t)(bitStart >> 8);
        uint32_t sp = (uint32_t)((bitStart + bt * 7 / 10) >> 8);
        if (sp >= end) return -1;
        int b = ln.L(sp);
        uint64_t next = bitStart + bt;
        if (b)
        {
          uint32_t to = (uint32_t)((next + bt / 2) >> 8);
          uint32_t k = ln.Find(sp + 1, 0);
          if (k < to) next = (uint64_t)k << 8;
        }
        bitStart = next;
        return b;
      };
      int nb = 0, same = 0, last = -1, total = MaxBits, ide = 0, dlcPos = 0, rtrPos = 0, bytes = 0;
      while (nb < total)
      {
        uint32_t p;
        int b = readRaw(p);
        if (b < 0) return end;
        if (same == 5)
        {
          if (b == last)
          {
            o.Add(p, p + (uint32_t)fBit, StuffErr, 0, ln.lane, FlErr);
            return p + 1;
          }
          same = 1; last = b;
          continue;
        }
        if (b == last) same++;
        else { same = 1; last = b; }
        bits[nb] = (uint8_t)b;
        pos[nb] = p;
        nb++;
        if (nb == 14) { ide = bits[13]; rtrPos = ide ? 32 : 12; dlcPos = ide ? 35 : 15; }
        if (nb >= 14 && nb == dlcPos + 4)
        {
          int dlc = (bits[dlcPos] << 3) | (bits[dlcPos + 1] << 2) | (bits[dlcPos + 2] << 1) | bits[dlcPos + 3];
          bytes = bits[rtrPos] ? 0 : (dlc > 8 ? 8 : dlc);
          total = dlcPos + 4 + bytes * 8 + 15;
        }
      }
      auto get = [&](int from, int cnt) { uint32_t v = 0; for (int k = 0; k < cnt; k++) v = (v << 1) | bits[from + k]; return v; };
      uint32_t bl = (uint32_t)fBit;
      auto endOf = [&](int k) { return pos[k] + bl - 1; };
      uint32_t id = get(1, 11);
      if (ide) id = (id << 18) | get(14, 18);
      int rtr = bits[rtrPos];
      o.Add(pos[1], endOf(rtrPos), CanId, id, ln.lane, 0, rtr | (ide << 1));
      o.Add(pos[dlcPos], endOf(dlcPos + 3), CanDlc, get(dlcPos, 4), ln.lane);
      int nData = dlcPos + 4;
      for (int k = 0; k < bytes; k++)
        o.Add(pos[nData + k * 8], endOf(nData + k * 8 + 7), Data, get(nData + k * 8, 8), ln.lane, 0, 8);
      int crcPos = nData + bytes * 8;
      uint16_t crc = 0;
      for (int k = 0; k < crcPos; k++)
      {
        int nx = bits[k] ^ ((crc >> 14) & 1);
        crc = (crc << 1) & 0x7fff;
        if (nx) crc ^= 0x4599;
      }
      uint32_t rxCrc = get(crcPos, 15);
      uint32_t pDel, pAck, pAckDel;
      int bDel = readRaw(pDel), bAck = readRaw(pAck), bAckDel = readRaw(pAckDel);
      bool crcOk = crc == rxCrc && bDel == 1;
      o.Add(pos[crcPos], bDel >= 0 ? pDel + bl - 1 : endOf(crcPos + 14), CanCrc, rxCrc, ln.lane, crcOk ? 0 : FlErr);
      if (bAck < 0) return end;
      o.Add(pAck, pAck + bl - 1, CanAck, bAck == 0, ln.lane, (bAck == 0 && bAckDel == 1) ? 0 : FlErr);
      return bAckDel >= 0 ? pAckDel + bl : end;
    }

    void Can()
    {
      TLine ln = Line(0, c.inv[PCan]);
      if (!ln.valid) { st.noLane = true; return; }
      float fBit = BitLength(ln, RateCan[(int)c.canRate], RateCan, RateCanN);
      if (fBit == 0) return;
      if (fBit < 3.0f) { st.slow = true; return; }
      uint32_t idle = (uint32_t)(7 * fBit), i = 0;
      while (!o.overflow && i < end)
      {
        // at least 7 recessive bits, then the start of frame
        uint32_t t = ln.Find(i, 1), nx = ln.NextChange(t);
        if (nx >= end) break;
        if (nx - t < idle) { i = nx; continue; }
        i = CanFrame(ln, nx, fBit);
      }
    }

    // ---------------------------------------------------------- WS2812
    void Ws2812()
    {
      TLine ln = Line(0);
      if (!ln.valid) { st.noLane = true; return; }
      if (fs < 3e6f) { st.slow = true; return; }
      const float spu = fs / 1e6f;
      const float thr = 0.6f * spu;                 // T0H 0.4 us, T1H 0.8 us
      const uint32_t gap = (uint32_t)(9 * spu);     // reset / end of the frame
      int led = 0, nBits = 0;
      uint32_t v = 0, t0 = 0, i = ln.Find(0, 0);
      while (!o.overflow)
      {
        uint32_t r = ln.Find(i, 1), f = ln.Find(r, 0);
        if (f >= end) break;
        uint32_t r2 = ln.Find(f, 1);
        if (nBits == 0) t0 = r;
        v = (v << 1) | ((f - r) > thr ? 1 : 0);
        nBits++;
        bool bGap = r2 - f > gap;
        if (nBits == 24)
        {
          uint32_t a = (v >> 16) & 0xff, b = (v >> 8) & 0xff, cc = v & 0xff;
          uint32_t rgb = c.wsOrder ? (a << 16) | (b << 8) | cc : (b << 16) | (a << 8) | cc;
          o.Add(t0, bGap ? f + (uint32_t)(0.45f * spu) : r2 - 1, Color, rgb, ln.lane, 0, led++);
          nBits = 0; v = 0;
        }
        if (bGap)
        {
          if (nBits) o.Add(t0, f, Color, 0, ln.lane, FlErr, led);
          nBits = 0; v = 0; led = 0;
        }
        i = r2;
      }
    }

    // ---------------------------------------------------------- USB
    struct TUsbIn
    {
      TLine dp, dm;
      uint32_t Begin() { return 0; }
      uint32_t End() { return dp.end; }
      int State(uint32_t t) { return dp.L(t) | (dm.L(t) << 1); }
      uint32_t Next(uint32_t t)
      {
        int i = La::Find(dp.ev, dp.n, t);
        int s = dp.Lv(i) | (dm.Lv(i) << 1);
        for (i++; i < dp.n; i++)
          if ((dp.Lv(i) | (dm.Lv(i) << 1)) != s) return dp.T(i);
        return dp.end;
      }
    };
    struct TUsbOut
    {
      TOut& o;
      int lane;
      bool Add(const Usb::TItem& it)
      {
        if (it.kind == Usb::KData) return o.Add(it.t0, it.t1, Data, it.v, lane, 0, 8);
        return o.Add(it.t0, it.t1, UsbItem, it.v, lane, it.err ? FlErr : 0, it.kind);
      }
    };
    void UsbDecode()
    {
      TUsbIn in{ Line(0), Line(1) };
      if (!in.dp.valid || !in.dm.valid || in.dp.lane == in.dm.lane) { st.noLane = true; return; }
      TUsbOut out{ o, in.dp.lane };
      Usb::CDecoder<TUsbIn, TUsbOut> d(in, out, fs);
      Usb::TResult r = d.Run(c.usbSpeed);
      if (r.slow) st.slow = true;
      else if (o.n) st.baud = r.rate;
    }

    // ---------------------------------------------------------- NEC IR
    void Nec()
    {
      TLine ln = Line(0, c.inv[PNec]);
      if (!ln.valid) { st.noLane = true; return; }
      if (fs < 5e3f) { st.slow = true; return; }
      const float usPer = 1e6f / fs;
      auto in = [&](uint32_t nn, float lo, float hi) { float us = nn * usPer; return us >= lo && us <= hi; };
      int nBits = -1;
      uint32_t v = 0, seg = 0, i = ln.Find(0, 0);
      while (!o.overflow)
      {
        uint32_t r = ln.Find(i, 1), f = ln.Find(r, 0);
        if (f >= end) break;
        uint32_t r2 = ln.Find(f, 1);
        uint32_t mark = f - r, space = r2 - f;
        if (in(mark, 7000, 11000))
        {
          nBits = -1;
          if (in(space, 3500, 5500)) { o.Add(r, r2 - 1, Leader, 0, ln.lane); nBits = 0; v = 0; }
          else if (in(space, 1700, 2900)) o.Add(r, f + (uint32_t)(562 / usPer), Repeat, 0, ln.lane);
        }
        else if (nBits >= 0 && nBits < 32 && in(mark, 300, 900))
        {
          int bit = in(space, 300, 900) ? 0 : in(space, 1300, 2100) ? 1 : -1;
          if (bit < 0) nBits = -1;
          else
          {
            if ((nBits & 15) == 0) seg = r;
            v |= (uint32_t)bit << nBits;
            nBits++;
            if (nBits == 16)
            {
              uint32_t a = v & 0xff, na = (v >> 8) & 0xff;
              if ((a ^ na) == 0xff) o.Add(seg, r2 - 1, NecAddr, a, ln.lane, 0, 8);
              else o.Add(seg, r2 - 1, NecAddr, v & 0xffff, ln.lane, 0, 16);
            }
            if (nBits == 32)
            {
              uint32_t cmd = (v >> 16) & 0xff, nc = (v >> 24) & 0xff;
              o.Add(seg, r2 - 1, NecCmd, cmd, ln.lane, (cmd ^ nc) == 0xff ? 0 : FlErr, 8);
              nBits = -1;
            }
          }
        }
        else
          nBits = -1;
        i = r2;
      }
    }

    // ---------------------------------------------------------- PS/2
    void Ps2()
    {
      TLine clk = Line(0), dat = Line(1);
      if (!clk.valid || !dat.valid || clk.lane == dat.lane) { st.noLane = true; return; }
      if (fs < 100e3f) { st.slow = true; return; }
      const uint32_t gap = (uint32_t)(fs * 150e-6f);
      int nBits = 0, par = 0;
      uint32_t v = 0, t0 = 0, lastFall = 0, half = 1;
      bool haveFall = false;
      for (int i = 1; i < n && !o.overflow; i++)
      {
        int p = La::State(ev[i - 1]), s = La::State(ev[i]);
        if (!((p & clk.mask) && !(s & clk.mask)))
          continue;
        uint32_t t = La::Time(ev[i]);
        if (nBits > 0 && haveFall && t - lastFall > gap) nBits = 0;
        else if (nBits > 0) half = (t - lastFall) / 2;
        int d = (s & dat.mask) ? 1 : 0;
        if (nBits == 0) { if (d == 0) { t0 = t; v = 0; nBits = 1; } }
        else if (nBits <= 8) { v |= (uint32_t)d << (nBits - 1); nBits++; }
        else if (nBits == 9) { par = d; nBits++; }
        else
        {
          bool ok = d == 1 && ((Popcount(v) + par) & 1) == 1;
          o.Add(t0 > half ? t0 - half : 0, t + half, Data, v, dat.lane, ok ? 0 : FlErr, 8);
          nBits = 0;
        }
        lastFall = t; haveFall = true;
      }
    }

    // ---------------------------------------------------------- pulses
    void PulseW()
    {
      TLine ln = Line(0, c.inv[PPulse]);
      if (!ln.valid) { st.noLane = true; return; }
      uint32_t i = ln.Find(0, 0);
      while (!o.overflow)
      {
        uint32_t r = ln.Find(i, 1), f = ln.Find(r, 0);
        if (f >= end) break;
        o.Add(r, f - 1, Pulse, f - r, ln.lane);
        i = f;
      }
    }

    void Counter()
    {
      TLine ln = Line(0, c.cntEdge == EdgeFall);
      if (!ln.valid) { st.noLane = true; return; }
      bool both = c.cntEdge == EdgeBoth;
      uint32_t cnt = 0, i = ln.NextChange(0);
      while (i < end)
      {
        uint32_t next = ln.NextChange(i);
        if (both || ln.L(i))
        {
          cnt++;
          if (!o.overflow)
          {
            if (both || next >= end) o.Add(i, i, Count, cnt, ln.lane, FlPoint);
            else o.Add(i, next - 1, Count, cnt, ln.lane);
          }
        }
        i = next;
      }
      st.edges = cnt;
      o.overflow = false;           // every edge is counted anyway
    }

    // ---------------------------------------------------------- encoder
    void Encoder()
    {
      TLine la = Line(0), lb = Line(1), lz = Line(2, c.inv[PEncoder]);
      if (!la.valid || !lb.valid || la.lane == lb.lane || (lz.valid && (lz.lane == la.lane || lz.lane == lb.lane)))
      { st.noLane = true; return; }
      static const uint8_t phase[4] = { 0, 3, 1, 2 };   // (A << 1 | B): 00 10 11 01
      const int sign = c.quadDir ? -1 : 1;
      int ab = (la.Lv(0) << 1) | lb.Lv(0), z = lz.valid ? lz.Lv(0) : 0;
      int32_t pos = 0, runSteps = 0, lastZ = 0, firstPos = 0;
      int runDir = 0;
      uint32_t runBegin = 0, runEnd = 0, firstStep = 0, lastStep = 0;
      bool inRun = false, haveFirst = false;
      auto flush = [&]() {
        if (inRun && runSteps && o.n < o.cap - 1)
          o.Add(runBegin, runEnd, QuadRun, (uint32_t)runSteps, la.lane);
        inRun = false; runSteps = 0; runDir = 0;
      };
      for (int i = 1; i < n; i++)
      {
        uint32_t t = La::Time(ev[i]);
        int nab = (la.Lv(i) << 1) | lb.Lv(i);
        if (nab != ab)
        {
          int d = (phase[nab] - phase[ab]) & 3;
          ab = nab;
          if (d == 2)
          {
            st.qErrors++;
            flush();
            if (st.qErrors <= 16 && o.n < o.cap - 1) o.Add(t, t, QuadErr, 0, lb.lane, FlErr | FlPoint);
          }
          else
          {
            int dir = (d == 1 ? 1 : -1) * sign;
            pos += dir;
            st.qEdges++;
            if (!haveFirst) { haveFirst = true; firstStep = t; firstPos = pos - dir; }
            lastStep = t;
            if (dir != runDir) { flush(); runDir = dir; runBegin = t; inRun = true; }
            runEnd = t;
            runSteps += dir;
          }
        }
        if (lz.valid)
        {
          int nz = lz.Lv(i);
          if (nz && !z)
          {
            flush();
            st.zCount++;
            if (o.n < o.cap - 1)
              o.Add(t, t, QuadZ, (uint32_t)(st.haveZ ? pos - lastZ : pos), lz.lane, FlPoint, st.haveZ ? 1 : 0);
            if (st.haveZ) { st.perRev = pos - lastZ; st.havePerRev = true; }
            lastZ = pos;
            st.haveZ = true;
          }
          z = nz;
        }
      }
      flush();
      st.steps = pos;
      st.inRev = pos - lastZ;
      if (lastStep > firstStep)
        st.qRate = (float)(pos - firstPos) * fs / (float)(lastStep - firstStep);
      if (st.qErrors > st.qEdges / 8 && st.qErrors > 2) st.slow = true;
      o.overflow = false;
    }

    void Run()
    {
      switch (c.proto)
      {
        case PUart: Uart(false); break;
        case PLin: { int from = o.n; if (Uart(true)) PostLin(from); break; }
        case PI2c: I2c(); break;
        case PSpi: Spi(); break;
        case POneWire: OneWire(); break;
        case PCan: Can(); break;
        case PWs2812: Ws2812(); break;
        case PNec: Nec(); break;
        case PPs2: Ps2(); break;
        case PPulse: PulseW(); break;
        case PCounter: Counter(); break;
        case PEncoder: Encoder(); break;
        case PUsb: UsbDecode(); break;
      }
    }
  };

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

  // events -> sorted frames; returns the number of frames
  inline int Decode(const uint32_t* ev, int n, uint32_t end, float rate, const TCfg& c,
                    TFrame* frames, int nMax, TStats& st, bool& overflow)
  {
    memset(&st, 0, sizeof(st));
    TOut o(frames, nMax);
    if (c.proto > POff && c.proto < Protos && n >= 2)
    {
      CDecoder d(ev, n, end, rate, c, o, st);
      d.Run();
    }
    Sort(frames, o.n);
    overflow = o.overflow;
    return o.n;
  }

  // ------------------------------------------------------------ text
  struct TText
  {
    char* p;
    char* e;
    void C(char ch) { if (p < e) *p++ = ch; }
    void S(const char* s) { while (*s) C(*s++); }
    void Hex(uint32_t v, int digits) { for (int i = digits - 1; i >= 0; i--) C("0123456789ABCDEF"[(v >> (i * 4)) & 15]); }
    void Dec(uint32_t v) { char t[11]; int k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) C(t[--k]); }
    void Char(uint32_t v)
    {
      C('\'');
      if (v == '\n') S("\\n");
      else if (v == '\r') S("\\r");
      else if (v == '\t') S("\\t");
      else if (v >= 32 && v < 127) C((char)v);
      else { S("\\x"); Hex(v, 2); }
      C('\'');
    }
    // 3 significant digits with a unit
    void Time(float t)
    {
      if (t < 0) { C('-'); t = -t; }
      const char* unit;
      float v;
      if (t < 1e-6f) { v = t * 1e9f; unit = "ns"; }
      else if (t < 1e-3f) { v = t * 1e6f; unit = "us"; }
      else if (t < 1.0f) { v = t * 1e3f; unit = "ms"; }
      else { v = t; unit = "s"; }
      Num3(v);
      S(unit);
    }
    void Num3(float v)
    {
      int dec = v < 9.995f ? 2 : v < 99.95f ? 1 : 0;
      int scale = dec == 2 ? 100 : dec == 1 ? 10 : 1;
      uint32_t nn = (uint32_t)(v * scale + 0.5f);
      Dec(nn / scale);
      if (dec) { C('.'); int fr = nn % scale; if (dec == 2 && fr < 10) C('0'); Dec(fr); }
    }
    void Freq(float f)
    {
      const char* unit = "Hz";
      if (f < 1.0f) { f *= 1e3f; unit = "mHz"; }
      else if (f >= 1e6f) { f /= 1e6f; unit = "MHz"; }
      else if (f >= 1e3f) { f /= 1e3f; unit = "kHz"; }
      Num3(f);
      S(unit);
    }
  };

  inline const char* RomCommand(uint32_t v)
  {
    switch (v)
    {
      case 0x33: return "Read ROM"; case 0x55: return "Match ROM"; case 0xF0: return "Search ROM";
      case 0xCC: return "Skip ROM"; case 0xEC: return "Alarm search"; case 0x3C: return "Overdrive skip";
      case 0x69: return "Overdrive match";
    }
    return nullptr;
  }

  inline const char* FunctionCommand(uint32_t v)
  {
    switch (v)
    {
      case 0x44: return "Convert T"; case 0xBE: return "Read scratchpad"; case 0x4E: return "Write scratchpad";
      case 0x48: return "Copy scratchpad"; case 0xB8: return "Recall EEPROM"; case 0xB4: return "Read power supply";
      case 0xF0: return "Read memory"; case 0x0F: return "Write scratchpad"; case 0xAA: return "Read scratchpad";
      case 0x5A: return "Copy scratchpad";
    }
    return nullptr;
  }

  inline int QuadScale(int32_t steps, const TCfg& c) { return steps >> (c.quadRes == 2 ? 2 : c.quadRes == 1 ? 1 : 0); }

  // text of frame i (the list: long; under the waveform: short)
  inline int Format(const TFrame* fr, int i, int nFrames, const TCfg& c, float fs,
                    char* buf, int size, bool lng)
  {
    const TFrame& a = fr[i];
    TText w{ buf, buf + size - 1 };
    bool err = a.flags & FlErr, nak = a.flags & FlNack;
    int proto = c.proto;
    switch (a.kind)
    {
      case Data:
      {
        int bits = a.aux & 0xff;
        if (bits == 0 || bits > 32) bits = 8;
        int digits = (bits + 3) / 4;
        if (digits < 2) digits = 2;
        if (proto == POneWire && (a.aux & AuxBadSlot)) { w.S(lng ? "bad slot" : "?"); break; }
        if (lng)
        {
          w.S("0x"); w.Hex(a.v, digits); w.C(' '); w.Dec(a.v);
          if (bits <= 8) { w.C(' '); w.Char(a.v); }
          if (proto == PI2c) w.S(nak ? " NACK" : " ACK");
          if (a.flags & FlPar) w.S(" parity err");
          if (err && (proto == PUart || proto == PLin)) w.S(" frame err");
          if (err && proto == PPs2) w.S(" parity/stop err");
          if (err && proto == PSpi) w.S(" cut by CS");
          if (proto == PSpi && (a.flags & FlFirst)) w.S(" (first)");
          if (proto == POneWire)
          {
            const char* nm = (a.aux & AuxRom) ? RomCommand(a.v) : (a.aux & AuxFunc) ? FunctionCommand(a.v) : nullptr;
            if (nm) { w.C(' '); w.S(nm); }
          }
          break;
        }
        if (c.fmt == 1) w.Dec(a.v);
        else if (c.fmt == 2 && bits <= 8) w.Char(a.v);
        else w.Hex(a.v, digits);
        if (nak) w.C('~');
        if (err || (a.flags & FlPar)) w.C('!');
        break;
      }
      case AddrW: case AddrR:
        if (lng) { w.S("addr 0x"); w.Hex(a.v, 2); w.S(a.kind == AddrR ? " read" : " write"); w.S(nak ? " NACK" : " ACK"); }
        else { w.C(a.kind == AddrR ? 'R' : 'W'); w.Hex(a.v, 2); if (nak) w.C('~'); }
        break;
      case Start: w.S(lng ? "start" : "S"); break;
      case Restart: w.S(lng ? "repeated start" : "Sr"); break;
      case Stop: w.S(lng ? "stop" : "P"); break;
      case Break: w.S(lng ? "break" : "BRK"); break;
      case Sync:
        if (lng) { w.S("sync 0x"); w.Hex(a.v, 2); if (err) w.S(" bad"); }
        else w.S(err ? "SYN!" : "SYN");
        break;
      case Pid:
        if (lng) { w.S("PID 0x"); w.Hex(a.v, 2); w.S(" id 0x"); }
        else w.S("I:");
        w.Hex(a.v & 0x3f, 2);
        if (err) w.S(lng ? " parity err" : "!");
        break;
      case Checksum:
        if (lng) { w.S("checksum 0x"); w.Hex(a.v, 2); w.S(a.aux == 1 ? " ok classic" : a.aux == 2 ? " ok enhanced" : " bad"); }
        else w.S(err ? "CS!" : "CS");
        break;
      case Reset: w.S(lng ? (err ? "reset, no presence" : "reset") : "RST"); break;
      case Presence: w.S(lng ? "presence" : "PRS"); break;
      case CanId:
        w.S(lng ? "ID 0x" : "I:");
        w.Hex(a.v, (a.aux & 2) ? 8 : 3);
        if (lng) w.S((a.aux & 2) ? " ext" : " std");
        if (a.aux & 1) w.S(lng ? " RTR" : "R");
        break;
      case CanDlc: w.S(lng ? "DLC " : "L:"); w.Dec(a.v); break;
      case CanCrc:
        if (lng) { w.S("CRC 0x"); w.Hex(a.v, 4); w.S(err ? " bad" : " ok"); }
        else w.S(err ? "CRC!" : "CRC");
        break;
      case CanAck: w.S(a.v ? "ACK" : "NACK"); break;
      case Color:
        if (err) { w.S(lng ? "incomplete LED data" : "?"); break; }
        if (lng)
        {
          w.S("LED "); w.Dec(a.aux); w.S(" #"); w.Hex(a.v, 6);
          w.S(" R"); w.Dec((a.v >> 16) & 0xff); w.S(" G"); w.Dec((a.v >> 8) & 0xff); w.S(" B"); w.Dec(a.v & 0xff);
        }
        else { w.C('#'); w.Hex(a.v, 6); }
        break;
      case Leader: w.S(lng ? "leader" : "LEAD"); break;
      case Repeat: w.S(lng ? "repeat" : "REP"); break;
      case NecAddr:
        w.S(lng ? "address 0x" : "A:");
        w.Hex(a.v, a.aux == 16 ? 4 : 2);
        if (lng && a.aux == 16) w.S(" (extended)");
        break;
      case NecCmd:
        w.S(lng ? "command 0x" : "C:");
        w.Hex(a.v, 2);
        if (err) w.S(lng ? " inverse mismatch" : "!");
        break;
      case Pulse:
      {
        // period: to the next pulse of the lane
        uint32_t period = 0;
        for (int j = i + 1; j < nFrames; j++)
          if (fr[j].kind == Pulse && fr[j].lane == a.lane) { period = fr[j].t0 - a.t0; break; }
        if (lng) w.S(c.inv[PPulse] ? "low " : "high ");
        w.Time(a.v / fs);
        if (period)
        {
          uint32_t pm = (uint32_t)(((uint64_t)a.v * 1000 + period / 2) / period);
          if (lng) { w.S(" T "); w.Time(period / fs); w.C(' '); w.Freq(fs / period); }
          w.C(' '); w.Dec(pm / 10); w.C('.'); w.Dec(pm % 10); w.C('%');
        }
        break;
      }
      case Count:
        if (lng)
        {
          w.S(c.cntEdge == EdgeBoth ? "edge #" : c.cntEdge == EdgeFall ? "low pulse #" : "high pulse #");
          w.Dec(a.v);
          if (!(a.flags & FlPoint)) { w.C(' '); w.Time((a.t1 - a.t0 + 1) / fs); }
        }
        else w.Dec(a.v);
        break;
      case QuadRun:
      {
        int32_t s = (int32_t)a.v, m = s < 0 ? -s : s;
        if (lng)
        {
          w.S(s < 0 ? "backward " : "forward ");
          w.Dec((uint32_t)QuadScale(m, c));
          w.S(c.quadRes == 0 ? " steps " : " pulses ");
          w.Time((a.t1 - a.t0 + 1) / fs);
        }
        else { w.C(s < 0 ? '-' : '+'); w.Dec((uint32_t)QuadScale(m, c)); }
        break;
      }
      case QuadZ:
        if (lng)
        {
          int32_t s = (int32_t)a.v;
          w.S(a.aux ? "Z, revolution " : "Z, since start ");
          if (s < 0) w.C('-');
          w.Dec((uint32_t)QuadScale(s < 0 ? -s : s, c));
        }
        else w.S("Z");
        break;
      case QuadErr: w.S(lng ? "A and B changed together" : "AB!"); break;
      case StuffErr: w.S(lng ? "stuff error" : "STF!"); break;
      case UsbItem:
        Usb::Text(w, a.aux, a.v, err, lng, a.t1 - a.t0 + 1, [fs](TText& ww, uint32_t n) { ww.Time(n / fs); });
        break;
    }
    *w.p = 0;
    return (int)(w.p - buf);
  }

  // ------------------------------------------------------------ trigger
  // value kinds of a condition
  enum { VNone, VHex, VUs, VNum };
  struct TCond { const char* name; uint8_t id; uint8_t val; };
  enum ECond { CAny, CData, CRx, CTx, CBreak, CError, CPid, CAddrW, CAddrR, CAddr, CNack,
    CStart, CMosi, CMiso, CCmd, CReset, CRom, CFunc, CCanId, CColor, CLed, CNecAddr, CNecCmd,
    CRepeat, CWidthGt, CWidthLt, CPeriodGt, CEdgeN, CZ, CQuadErr,
    CUsbPid, CUsbAddr, CUsbReq };

  // the conditions of a protocol, [0] is "off"
  inline const TCond* Conds(int proto, int& n)
  {
    static const TCond off[] = { { "off", 0, VNone } };
    static const TCond uart[] = { { "off", 0, VNone }, { "data", CData, VHex }, { "RX data", CRx, VHex },
      { "TX data", CTx, VHex }, { "break", CBreak, VNone }, { "error", CError, VNone } };
    static const TCond lin[] = { { "off", 0, VNone }, { "PID (id)", CPid, VHex }, { "data", CData, VHex },
      { "break", CBreak, VNone }, { "error", CError, VNone } };
    static const TCond i2c[] = { { "off", 0, VNone }, { "write addr", CAddrW, VHex }, { "read addr", CAddrR, VHex },
      { "addr R/W", CAddr, VHex }, { "data", CData, VHex }, { "NACK", CNack, VNone }, { "start", CStart, VNone } };
    static const TCond spi[] = { { "off", 0, VNone }, { "MOSI word", CMosi, VHex }, { "MISO word", CMiso, VHex },
      { "command", CCmd, VHex }, { "cut by CS", CError, VNone } };
    static const TCond ow[] = { { "off", 0, VNone }, { "reset", CReset, VNone }, { "ROM command", CRom, VHex },
      { "function cmd", CFunc, VHex }, { "data", CData, VHex }, { "error", CError, VNone } };
    static const TCond can[] = { { "off", 0, VNone }, { "ID", CCanId, VHex }, { "data", CData, VHex },
      { "error", CError, VNone } };
    static const TCond ws[] = { { "off", 0, VNone }, { "color", CColor, VHex }, { "LED number", CLed, VNum },
      { "error", CError, VNone } };
    static const TCond nec[] = { { "off", 0, VNone }, { "address", CNecAddr, VHex }, { "command", CNecCmd, VHex },
      { "repeat", CRepeat, VNone }, { "error", CError, VNone } };
    static const TCond ps2[] = { { "off", 0, VNone }, { "data", CData, VHex }, { "error", CError, VNone } };
    static const TCond pulse[] = { { "off", 0, VNone }, { "width >", CWidthGt, VUs }, { "width <", CWidthLt, VUs },
      { "period >", CPeriodGt, VUs } };
    static const TCond cnt[] = { { "off", 0, VNone }, { "edge number", CEdgeN, VNum } };
    static const TCond enc[] = { { "off", 0, VNone }, { "index Z", CZ, VNone }, { "A/B error", CQuadErr, VNone } };
    static const TCond usb[] = { { "off", 0, VNone }, { "PID", CUsbPid, VHex }, { "token address", CUsbAddr, VHex },
      { "setup request", CUsbReq, VHex }, { "data", CData, VHex }, { "reset", CReset, VNone }, { "error", CError, VNone } };
    switch (proto)
    {
      case PUart: n = 6; return uart;
      case PLin: n = 5; return lin;
      case PI2c: n = 7; return i2c;
      case PSpi: n = 5; return spi;
      case POneWire: n = 6; return ow;
      case PCan: n = 4; return can;
      case PWs2812: n = 4; return ws;
      case PNec: n = 5; return nec;
      case PPs2: n = 3; return ps2;
      case PPulse: n = 4; return pulse;
      case PCounter: n = 2; return cnt;
      case PEncoder: n = 3; return enc;
      case PUsb: n = 7; return usb;
    }
    n = 1;
    return off;
  }

  inline const TCond& Cond(const TCfg& c)
  {
    int n;
    const TCond* t = Conds(c.proto, n);
    int k = c.proto >= 0 && c.proto < Protos ? c.tCond[(int)c.proto] : 0;
    return t[k >= 0 && k < n ? k : 0];
  }

  inline bool MatchFrame(const TFrame* fr, int i, int nFrames, const TCfg& c, float fs)
  {
    const TFrame& f = fr[i];
    const TCond& cd = Cond(c);
    bool hex = ((f.v ^ c.tValue) & c.tMask) == 0;
    const int8_t* ln = c.lane[(int)c.proto];
    switch (cd.id)
    {
      case CData: return f.kind == Data && !(f.aux & AuxBadSlot) && hex;
      case CRx: return f.kind == Data && f.lane == ln[0] && hex;
      case CTx: return f.kind == Data && f.lane == ln[1] && hex;
      case CBreak: return f.kind == Break;
      case CError: return (f.flags & (FlErr | FlPar)) != 0;
      case CPid: return f.kind == Pid && (((f.v & 0x3f) ^ c.tValue) & c.tMask) == 0;
      case CAddrW: return f.kind == AddrW && hex;
      case CAddrR: return f.kind == AddrR && hex;
      case CAddr: return (f.kind == AddrW || f.kind == AddrR) && hex;
      case CNack: return (f.flags & FlNack) != 0;
      case CStart: return f.kind == Start || f.kind == Restart;
      case CMosi: return f.kind == Data && f.lane == ln[1] && hex;
      case CMiso: return f.kind == Data && f.lane == ln[2] && hex;
      case CCmd: return f.kind == Data && (f.flags & FlFirst) && (ln[1] < 0 || f.lane == ln[1]) && hex;
      case CReset: return f.kind == Reset || (f.kind == UsbItem && f.aux == Usb::KReset);
      case CRom: return f.kind == Data && (f.aux & AuxRom) && hex;
      case CFunc: return f.kind == Data && (f.aux & AuxFunc) && hex;
      case CCanId: return f.kind == CanId && hex;
      case CColor: return f.kind == Color && !(f.flags & FlErr) && hex;
      case CLed: return f.kind == Color && f.aux == c.tValue;
      case CNecAddr: return f.kind == NecAddr && hex;
      case CNecCmd: return f.kind == NecCmd && hex;
      case CRepeat: return f.kind == Repeat;
      case CWidthGt: return f.kind == Pulse && f.v / fs * 1e6f > c.tValue;
      case CWidthLt: return f.kind == Pulse && f.v / fs * 1e6f < c.tValue;
      case CPeriodGt:
        if (f.kind != Pulse) return false;
        for (int j = i + 1; j < nFrames; j++)
          if (fr[j].kind == Pulse && fr[j].lane == f.lane)
            return (fr[j].t0 - f.t0) / fs * 1e6f > c.tValue;
        return false;
      case CEdgeN: return f.kind == Count && f.v == c.tValue;
      case CZ: return f.kind == QuadZ;
      case CQuadErr: return f.kind == QuadErr;
      // the PID (4 bits), address | endpoint << 7 of IN / OUT / SETUP / PING,
      // bRequest of a setup packet
      case CUsbPid: return f.kind == UsbItem && f.aux == Usb::KPid && !(f.flags & FlErr) && hex;
      case CUsbAddr: return f.kind == UsbItem && f.aux == Usb::KAddr && hex;
      case CUsbReq: return f.kind == UsbItem && f.aux == Usb::KRequest && (((f.v & 0xff) ^ c.tValue) & c.tMask) == 0;
    }
    return false;
  }

  // the frame of the trigger: the tNth frame that matches the condition
  // (and is followed on its lane by a frame with the value tThenValue when
  // tThen is on); -1: none
  inline int FindTrigger(const TFrame* fr, int nFrames, const TCfg& c, float fs)
  {
    const TCond& cd = Cond(c);
    if (cd.id == 0)
      return -1;
    const bool then = c.tThen && cd.val == VHex;
    int hits = 0, nth = c.tNth ? c.tNth : 1;
    for (int i = 0; i < nFrames; i++)
    {
      if (!MatchFrame(fr, i, nFrames, c, fs))
        continue;
      if (then)
      {
        // the next frame of the lane that has a value (no start / stop)
        int j = i + 1;
        while (j < nFrames && (fr[j].lane != fr[i].lane || (fr[j].flags & FlPoint))) j++;
        if (j >= nFrames || fr[j].v != c.tThenValue)
          continue;
      }
      if (++hits == nth)
        return i;
    }
    return -1;
  }

  // edge for the hardware trigger while waiting for the protocol trigger:
  // the start of activity on the main lane; false: no lane
  inline bool StartEdge(const TCfg& c, int& lane, bool& rise)
  {
    const int8_t* ln = c.lane[(int)c.proto];
    int inv = c.inv[(int)c.proto];
    switch (c.proto)
    {
      case PUart: lane = ln[0] >= 0 ? ln[0] : ln[1]; rise = inv; break;
      case PLin: case POneWire: lane = ln[0]; rise = inv && c.proto == PLin; break;
      case PCan: lane = ln[0]; rise = inv; break;
      case PI2c: lane = ln[1]; rise = false; break;
      case PSpi:
        if (ln[3] >= 0) { lane = ln[3]; rise = false; }
        else { lane = ln[0]; rise = !(c.spiMode & 2); }
        break;
      case PWs2812: lane = ln[0]; rise = true; break;
      case PNec: lane = ln[0]; rise = !inv; break;
      case PPs2: lane = ln[0]; rise = false; break;
      case PPulse: lane = ln[0]; rise = !inv; break;
      case PCounter: lane = ln[0]; rise = c.cntEdge != EdgeFall; break;
      case PEncoder: lane = ln[0]; rise = true; break;
      // SYNC: J -> K, D+ falls at full speed, rises at low speed
      case PUsb: lane = ln[0]; rise = c.usbSpeed == Usb::SpeedLow; break;
      default: return false;
    }
    return lane >= 0 && lane < 4;
  }
}

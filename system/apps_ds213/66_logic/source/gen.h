#pragma once
// Generator of the logic analyzer: the Wave Out connector as a logic output
// (PB6, 3.3 V push-pull, tied to the DAC pin on the board). Single pulse,
// square wave (TIM4 PWM), burst of pulses and test signals of the decoders
// (UART, LIN, CAN, 1-Wire, NEC IR, WS2812, pulses) built as a table of levels that
// DMA2 channel 4 writes to GPIOB_BSRR, paced by TIM7 (main.cpp). This file
// builds the tables only (host test: test/test_gen.cpp).

#include <stdint.h>
#include "decode.h"

namespace Gen
{
  enum EMode { GOff, GPulse, GSquare, GBurst, GTest, GModes };
  static const char* const ModeName[GModes] = { "Gen off", "Pulse", "Square", "Burst", "Test" };

  struct TCfg
  {
    uint32_t magic;
    int8_t mode;
    int8_t pol;               // 0: active high (idle low), 1: active low (idle high)
    int8_t autoFire;          // fire after every start of a capture
    int8_t payload;           // UART test: 0 text, 1 the 4 bytes of value;
                              // WS2812: 0 test colors, 1 the color value for all LEDs
    int8_t duty;              // %, square and burst
    int8_t pad[3];
    uint32_t pulseUs;         // single pulse
    uint32_t freqHz;          // square, burst
    uint32_t count;           // burst; WS2812: LEDs
    uint32_t value;           // test: UART bytes, LIN id / data, CAN id, 1-Wire function, NEC address / command
  };
  const uint32_t CfgMagic = 0x4c474e31;   // "LGN1"

  inline void Defaults(TCfg& c)
  {
    memset(&c, 0, sizeof(c));
    c.magic = CfgMagic;
    c.duty = 50;
    c.pulseUs = 1000;
    c.freqHz = 1000;
    c.count = 10;
    c.value = 0x44;
  }

  enum { MaxTab = 1024, MinDiv = 18 };   // DMA writes at most 4 MHz
  const uint32_t Clock = 72000000;

  // a table of levels (0 / 1) written at Clock / div; first: level set
  // at once when firing (the table starts one sample period later)
  struct TSeq
  {
    uint32_t div = 0;
    int n = 0;
    int first = -1;
    const char* err = nullptr;
    float real = 0;           // the generated frequency / baud / bit rate
    int perBit = 0;           // samples per bit (protocols)
  };

  struct TWriter
  {
    uint32_t* t;
    int n = 0;
    bool ovf = false;
    void Put(int level, int count)
    {
      for (int i = 0; i < count; i++)
      {
        if (n >= MaxTab) { ovf = true; return; }
        t[n++] = (uint32_t)level;
      }
    }
  };

  // samples per bit k and divider for a bit rate: the largest k (<= 16)
  // with the table size and the DMA rate and an error below 0.1 %, else the
  // smallest error
  inline bool BitPlan(float rate, int bits, TSeq& s)
  {
    int bestK = 0;
    uint32_t bestDiv = 0;
    float bestErr = 1e9f;
    for (int k = 16; k >= 1; k--)
    {
      if (bits * k > MaxTab) continue;
      float fd = Clock / (rate * k);
      if (fd < MinDiv) continue;
      uint32_t d = (uint32_t)(fd + 0.5f);
      float err = (Clock / (float)(d * k) - rate) / rate;
      if (err < 0) err = -err;
      if (err < 0.001f) { bestK = k; bestDiv = d; bestErr = err; break; }
      if (err < bestErr) { bestK = k; bestDiv = d; bestErr = err; }
    }
    if (!bestK) { s.err = bits > MaxTab ? "too long" : "too fast"; return false; }
    s.perBit = bestK;
    s.div = bestDiv;
    s.real = Clock / (float)(bestDiv * bestK);
    return true;
  }

  // ------------------------------------------------------------ protocols
  inline void UartFrame(TWriter& w, int v, int bits, int parity, int stop, int k, int inv)
  {
    auto L = [&](int l, int nb) { w.Put(l ^ inv, nb * k); };
    L(0, 1);
    int ones = 0;
    for (int b = 0; b < bits; b++) { int x = (v >> b) & 1; ones += x; L(x, 1); }
    if (parity) L(parity == 1 ? (ones & 1) : !(ones & 1), 1);
    L(1, stop);
  }

  inline int PayloadBytes(const TCfg& g, uint8_t* p)
  {
    if (g.payload == 0)
    {
      const char* txt = "Hello DS203!\r\n";
      int n = 0;
      while (txt[n]) { p[n] = (uint8_t)txt[n]; n++; }
      return n;
    }
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(g.value >> (24 - 8 * i));
    return 4;
  }

  inline bool Uart(const TCfg& g, const Dec::TCfg& d, uint32_t* tab, TSeq& s)
  {
    uint32_t baud = Dec::RateUart[(int)d.uartBaud];
    if (!baud) baud = 115200;
    uint8_t p[16];
    int nb = PayloadBytes(g, p);
    int frame = 1 + d.uartBits + (d.uartParity ? 1 : 0) + d.uartStop;
    if (!BitPlan((float)baud, nb * frame + 4, s)) return false;
    int inv = d.inv[Dec::PUart] ? 1 : 0;
    TWriter w{ tab };
    w.Put(1 ^ inv, 2 * s.perBit);
    for (int i = 0; i < nb; i++) UartFrame(w, p[i], d.uartBits, d.uartParity, d.uartStop, s.perBit, inv);
    w.Put(1 ^ inv, 2 * s.perBit);
    s.n = w.n;
    s.first = 1 ^ inv;
    return !w.ovf;
  }

  // break, sync, PID (value & 0x3F), two data bytes (value >> 8, >> 16),
  // enhanced checksum
  inline bool Lin(const TCfg& g, const Dec::TCfg& d, uint32_t* tab, TSeq& s)
  {
    uint32_t baud = Dec::RateLin[(int)d.linBaud];
    if (!baud) baud = 19200;
    if (!BitPlan((float)baud, 2 + 14 + 5 * 10 + 2, s)) return false;
    int inv = d.inv[Dec::PLin] ? 1 : 0, k = s.perBit;
    int id = g.value & 0x3f;
    int p0 = (id ^ (id >> 1) ^ (id >> 2) ^ (id >> 4)) & 1;
    int p1 = !(((id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id >> 5)) & 1);
    int pid = id | (p0 << 6) | (p1 << 7);
    int data[2] = { (int)((g.value >> 8) & 0xff), (int)((g.value >> 16) & 0xff) };
    int sum = pid;
    for (int x : data) { sum += x; if (sum > 255) sum -= 255; }
    TWriter w{ tab };
    w.Put(1 ^ inv, 2 * k);
    w.Put(0 ^ inv, 13 * k);
    w.Put(1 ^ inv, 1 * k);
    UartFrame(w, 0x55, 8, 0, 1, k, inv);
    UartFrame(w, pid, 8, 0, 1, k, inv);
    for (int x : data) UartFrame(w, x, 8, 0, 1, k, inv);
    UartFrame(w, (~sum) & 0xff, 8, 0, 1, k, inv);
    w.Put(1 ^ inv, 2 * k);
    s.n = w.n;
    s.first = 1 ^ inv;
    return !w.ovf;
  }

  // CAN data frame: ID = value (11 bit, 29 bit when > 0x7FF), DLC 4, data
  // 11 22 33 44, CRC, bit stuffing, ACK (dominant, as a receiver would)
  inline bool Can(const TCfg& g, const Dec::TCfg& d, uint32_t* tab, TSeq& s)
  {
    uint32_t rate = Dec::RateCan[(int)d.canRate];
    if (!rate) rate = 125000;
    uint8_t b[160];
    int n = 0;
    bool ext = g.value > 0x7ff;
    uint32_t id = g.value & 0x1fffffff;
    b[n++] = 0;
    if (!ext) { for (int k = 10; k >= 0; k--) b[n++] = (id >> k) & 1; b[n++] = 0; b[n++] = 0; b[n++] = 0; }
    else
    {
      for (int k = 28; k >= 18; k--) b[n++] = (id >> k) & 1;
      b[n++] = 1; b[n++] = 1;
      for (int k = 17; k >= 0; k--) b[n++] = (id >> k) & 1;
      b[n++] = 0; b[n++] = 0; b[n++] = 0;
    }
    const int data[4] = { 0x11, 0x22, 0x33, 0x44 };
    for (int k = 3; k >= 0; k--) b[n++] = (4 >> k) & 1;
    for (int x : data) for (int k = 7; k >= 0; k--) b[n++] = (x >> k) & 1;
    uint16_t crc = 0;
    for (int i = 0; i < n; i++) { int nx = b[i] ^ ((crc >> 14) & 1); crc = (crc << 1) & 0x7fff; if (nx) crc ^= 0x4599; }
    for (int k = 14; k >= 0; k--) b[n++] = (crc >> k) & 1;
    // stuffing
    uint8_t st[200];
    int m = 0, same = 0, last = -1;
    for (int i = 0; i < n; i++)
    {
      st[m++] = b[i];
      if (b[i] == last) same++; else { same = 1; last = b[i]; }
      if (same == 5) { st[m++] = !b[i]; last = !b[i]; same = 1; }
    }
    st[m++] = 1; st[m++] = 0; st[m++] = 1;              // CRC delimiter, ACK, ACK delimiter
    for (int i = 0; i < 7; i++) st[m++] = 1;            // end of frame
    if (!BitPlan((float)rate, m + 11 + 3, s)) return false;
    int inv = d.inv[Dec::PCan] ? 1 : 0;
    TWriter w{ tab };
    w.Put(1 ^ inv, 11 * s.perBit);                      // bus idle
    for (int i = 0; i < m; i++) w.Put(st[i] ^ inv, s.perBit);
    w.Put(1 ^ inv, 3 * s.perBit);
    s.n = w.n;
    s.first = 1 ^ inv;
    return !w.ovf;
  }

  // 1-Wire: reset, presence (as a device would answer), Skip ROM (CC), the
  // function command value & 0xFF; 3 us per sample
  inline bool OneWire(const TCfg& g, uint32_t* tab, TSeq& s)
  {
    s.div = 216;
    s.real = 0;
    TWriter w{ tab };
    auto us = [&](int l, int t) { w.Put(l, (t + 1) / 3); };
    us(1, 20);
    us(0, 480); us(1, 70); us(0, 120); us(1, 300);
    const int bytes[2] = { 0xcc, (int)(g.value & 0xff) };
    for (int v : bytes)
      for (int b = 0; b < 8; b++)
      {
        if ((v >> b) & 1) { us(0, 6); us(1, 64); }
        else { us(0, 60); us(1, 10); }
      }
    us(1, 20);
    s.n = w.n;
    s.first = 1;
    return !w.ovf;
  }

  // NEC: leader, address (value & 0xFF), command ((value >> 8) & 0xFF) with
  // their inverses, end mark; marks low when the decoder expects the
  // inverted output of an IR receiver (its default); 140.625 us per sample
  inline bool Nec(const TCfg& g, const Dec::TCfg& d, uint32_t* tab, TSeq& s)
  {
    s.div = 10125;
    s.real = 0;
    int on = d.inv[Dec::PNec] ? 0 : 1;
    TWriter w{ tab };
    auto mark = [&](int m, int sp) { w.Put(on, m); w.Put(!on, sp); };
    w.Put(!on, 8);
    mark(64, 32);
    int a = g.value & 0xff, c = (g.value >> 8) & 0xff;
    uint32_t v = (uint32_t)a | ((uint32_t)(~a & 0xff) << 8) | ((uint32_t)c << 16) | ((uint32_t)(~c & 0xff) << 24);
    for (int b = 0; b < 32; b++) mark(4, ((v >> b) & 1) ? 12 : 4);
    mark(4, 8);
    s.n = w.n;
    s.first = !on;
    return !w.ovf;
  }

  // WS2812 as the LED strip tester (apps_featured/test63_ws2812): every bit
  // is 3 slots of a stream, 1 = 110, 0 = 100 (there: SPI bytes from a table,
  // bit-banged to PB6 with the interrupts off, 444 ns per slot). Here the
  // slots are the table of the DMA at 2.4 MHz (72 MHz / 30): 0.417 us per
  // slot, T0H 0.42 us, T1H 0.83 us, 1.25 us per bit (800 kbit/s), exact by
  // TIM7. 24 bits per LED in the order of the decoder (GRB / RGB), at most
  // 14 LEDs (72 slots each, table of 1024); the line is low before (the
  // idle level, a reset for a strip that was quiet 50 us) and after the
  // frame (latch). Colors: value (0xRRGGBB) for all LEDs or 7 test colors
  // of 1/4 brightness (red, green, blue, yellow, cyan, magenta, white).
  enum { WsSlotDiv = 30, WsMaxLeds = (MaxTab - 1) / 72 };
  inline uint32_t WsColor(const TCfg& g, int i)
  {
    static const uint32_t test[7] = { 0x400000, 0x004000, 0x000040, 0x404000, 0x004040, 0x400040, 0x404040 };
    return g.payload ? (g.value & 0xffffff) : test[i % 7];
  }
  inline bool Ws2812(const TCfg& g, const Dec::TCfg& d, uint32_t* tab, TSeq& s)
  {
    if (g.count < 1 || g.count > WsMaxLeds) { s.err = "LEDs: 1 .. 14"; return false; }
    s.div = WsSlotDiv;
    s.perBit = 3;
    s.real = Clock / (float)(WsSlotDiv * 3);
    TWriter w{ tab };
    for (uint32_t i = 0; i < g.count; i++)
    {
      uint32_t c = WsColor(g, (int)i);
      uint32_t r = (c >> 16) & 0xff, gr = (c >> 8) & 0xff, b = c & 0xff;
      uint32_t v = d.wsOrder ? (r << 16 | gr << 8 | b) : (gr << 16 | r << 8 | b);
      for (int k = 23; k >= 0; k--)
      {
        w.Put(1, 1);
        w.Put((v >> k) & 1, 1);
        w.Put(0, 1);
      }
    }
    w.Put(0, 1);
    s.n = w.n;
    s.first = 0;
    return !w.ovf;
  }

  // ------------------------------------------------------------ pulses
  inline bool Pulse(const TCfg& g, uint32_t* tab, TSeq& s)
  {
    uint64_t div = (uint64_t)g.pulseUs * 72;
    if (div < MinDiv) { s.err = "too short"; return false; }
    if (div > 0xffffffffull) { s.err = "too long"; return false; }
    s.div = (uint32_t)div;
    int act = g.pol ? 0 : 1;
    tab[0] = !act;              // the active level is set at once, this ends it
    s.n = 1;
    s.first = act;
    return true;
  }

  inline bool Burst(const TCfg& g, uint32_t* tab, TSeq& s)
  {
    if (g.freqHz == 0 || g.count == 0) { s.err = "no pulses"; return false; }
    int n = g.count > 1000 ? 1000 : (int)g.count;
    int sp = (MaxTab - 1) / n;
    if (sp > 100) sp = 100;
    float fd = Clock / ((float)g.freqHz * sp);
    while (sp > 2 && fd < MinDiv) { sp--; fd = Clock / ((float)g.freqHz * sp); }
    if (sp < 2 || fd < MinDiv) { s.err = "too fast"; return false; }
    s.div = (uint32_t)(fd + 0.5f);
    s.real = Clock / (float)(s.div * sp);
    s.perBit = sp;
    int hi = (g.duty * sp + 50) / 100;
    if (hi < 1) hi = 1;
    if (hi > sp - 1) hi = sp - 1;
    int act = g.pol ? 0 : 1;
    TWriter w{ tab };
    for (int i = 0; i < n; i++) { w.Put(act, hi); w.Put(!act, sp - hi); }
    s.n = w.n;
    s.first = !act;
    return !w.ovf;
  }

  // the table of a one-shot mode (not Square); false: s.err says why
  inline bool Build1(const TCfg& g, const Dec::TCfg& d, uint32_t* tab, TSeq& s);
  inline bool Build(const TCfg& g, const Dec::TCfg& d, uint32_t* tab, TSeq& s)
  {
    s = TSeq();
    bool ok = Build1(g, d, tab, s);
    if (!ok && !s.err) s.err = "too long";
    return ok;
  }

  inline bool Build1(const TCfg& g, const Dec::TCfg& d, uint32_t* tab, TSeq& s)
  {
    switch (g.mode)
    {
      case GPulse: return Pulse(g, tab, s);
      case GBurst: return Burst(g, tab, s);
      case GTest:
        switch (d.proto)
        {
          case Dec::PUart: return Uart(g, d, tab, s);
          case Dec::PLin: return Lin(g, d, tab, s);
          case Dec::PCan: return Can(g, d, tab, s);
          case Dec::POneWire: return OneWire(g, tab, s);
          case Dec::PNec: return Nec(g, d, tab, s);
          case Dec::PPulse: case Dec::PCounter: return Burst(g, tab, s);
          case Dec::POff: s.err = "decoder off"; return false;
          case Dec::PWs2812: return Ws2812(g, d, tab, s);
          default: s.err = "needs 2+ lines"; return false;
        }
      default:
        s.err = "not a one-shot mode";
        return false;
    }
  }

  // square wave by TIM4: prescaler and period for a frequency
  inline bool SquarePlan(uint32_t f, uint32_t& psc, uint32_t& arr, float& real)
  {
    if (f == 0 || f > Clock / 4) return false;
    uint32_t total = (Clock + f / 2) / f;
    psc = (total - 1) / 65536 + 1;
    arr = (total + psc / 2) / psc;
    if (arr < 4) arr = 4;
    real = Clock / (float)(psc * arr);
    return true;
  }
}

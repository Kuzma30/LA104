// Host test of the protocol decoders: g++ -std=c++17 -I../source test_decode.cpp && ./a.out
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include "decode.h"

// state bits: 0 C, 1 D, 2 A, 3 B (lanes A 0, B 1, C 2, D 3)
enum { MC = 1, MD = 2, MA = 4, MB = 8 };

struct TWave
{
  struct TCh { double t; int mask, level; };
  std::vector<TCh> ch;
  int init = 15;
  void Set(double t, int mask, int level) { ch.push_back({ t, mask, level }); }
  // events at rate samples per second (jitter: random +-j samples)
  std::vector<uint32_t> Events(double rate, uint32_t& end, double j = 0)
  {
    std::stable_sort(ch.begin(), ch.end(), [](const TCh& a, const TCh& b) { return a.t < b.t; });
    std::vector<uint32_t> ev;
    int s = init;
    ev.push_back(La::Event(s, 0));
    for (auto& c : ch)
    {
      double tj = c.t * rate + (j ? (rand() / (double)RAND_MAX * 2 - 1) * j : 0);
      uint32_t t = (uint32_t)(tj < 1 ? 1 : tj);
      int s2 = c.level ? (s | c.mask) : (s & ~c.mask);
      if (s2 == s) continue;
      s = s2;
      if (La::Time(ev.back()) == t) { ev.back() = La::Event(s, t); continue; }
      ev.push_back(La::Event(s, t));
    }
    end = La::Time(ev.back()) + (uint32_t)(rate * 1e-3);
    return ev;
  }
};

int fails = 0;
void Check(bool c, const char* what)
{
  if (!c) { printf("FAIL: %s\n", what); fails++; }
}

Dec::TFrame frames[1024];

void UartByte(TWave& w, double& t, int mask, int v, double bit, int bits, int parity, int stop, bool inv = false)
{
  auto lv = [&](int l) { return inv ? !l : l; };
  w.Set(t, mask, lv(0)); t += bit;
  int ones = 0;
  for (int b = 0; b < bits; b++) { int x = (v >> b) & 1; ones += x; w.Set(t, mask, lv(x)); t += bit; }
  if (parity) { int p = parity == 1 ? ones & 1 : !(ones & 1); w.Set(t, mask, lv(p)); t += bit; }
  w.Set(t, mask, lv(1)); t += bit * stop;
}

void TestUart()
{
  const char* msg = "Hello, DS203!\x00\xff\x55";
  int len = 16;
  for (int cfg = 0; cfg < 4; cfg++)
  {
    double rate = cfg == 3 ? 1e6 : 24e6;
    uint32_t baud = cfg == 0 ? 115200 : cfg == 1 ? 9600 : cfg == 2 ? 921600 : 115200;
    int parity = cfg == 1 ? 1 : cfg == 2 ? 2 : 0, stop = cfg == 1 ? 2 : 1;
    bool inv = cfg == 2;
    TWave w;
    w.init = inv ? 15 & ~MC : 15;
    double t = 20e-6, bit = 1.0 / baud;
    for (int i = 0; i < len; i++) { UartByte(w, t, MC, (uint8_t)msg[i], bit, 8, parity, stop, inv); t += i == 5 ? 3 * bit : 0; }
    uint32_t end;
    auto ev = w.Events(rate, end, 0.4);
    uint32_t auto_ = Dec::AutoBaud(ev.data(), ev.size(), MC, rate);
    char s[80];
    sprintf(s, "uart cfg %d autobaud %u", cfg, auto_);
    Check(auto_ == baud, s);
    Dec::TOut o(frames, 1024);
    bool ok = Dec::Uart(ev.data(), ev.size(), end, MC, 2, rate, baud, 8, parity, stop, inv, o);
    sprintf(s, "uart cfg %d frames %d", cfg, o.n);
    Check(ok && o.n == len, s);
    for (int i = 0; i < o.n && i < len; i++)
    {
      sprintf(s, "uart cfg %d byte %d: %02x != %02x flags %d", cfg, i, o.f[i].v, (uint8_t)msg[i], o.f[i].flags);
      Check(o.f[i].v == (uint8_t)msg[i] && o.f[i].flags == 0 && o.f[i].lane == 2, s);
    }
  }
  // framing / parity errors: wrong settings
  {
    TWave w; double t = 10e-6, bit = 1.0 / 9600;
    for (int i = 0; i < 4; i++) UartByte(w, t, MD, 0x31 + i, bit, 8, 1, 1);
    uint32_t end; auto ev = w.Events(24e6, end);
    Dec::TOut o(frames, 1024);
    Dec::Uart(ev.data(), ev.size(), end, MD, 3, 24e6, 9600, 8, 2, 1, false, o);   // odd instead of even
    int par = 0; for (int i = 0; i < o.n; i++) par += (o.f[i].flags & Dec::FlPar) ? 1 : 0;
    Check(o.n == 4 && par == 4, "uart parity error");
  }
}

void SpiWord(TWave& w, double& t, int v, int vi, double half, int bits, bool cpol, bool cpha, bool msb)
{
  // clock idle cpol; cpha 0: data before the first edge
  for (int b = 0; b < bits; b++)
  {
    int k = msb ? bits - 1 - b : b;
    int bo = (v >> k) & 1, bi = (vi >> k) & 1;
    if (!cpha) { w.Set(t, MD, bo); w.Set(t, MA, bi); t += half; w.Set(t, MC, !cpol); t += half; w.Set(t, MC, cpol); }
    else { w.Set(t, MC, !cpol); w.Set(t + half * 0.1, MD, bo); w.Set(t + half * 0.1, MA, bi); t += half; w.Set(t, MC, cpol); t += half; }
  }
}

void TestSpi()
{
  for (int mode = 0; mode < 4; mode++)
  {
    bool cpol = mode & 2, cpha = mode & 1;
    TWave w;
    w.init = (cpol ? MC : 0) | MB;
    double t = 5e-6, half = 0.5e-6;
    int tx[] = { 0x9f, 0x00, 0xa5, 0x3c }, rx[] = { 0xff, 0xef, 0x40, 0x18 };
    w.Set(t, MB, 0); t += 1e-6;
    for (int i = 0; i < 4; i++) { SpiWord(w, t, tx[i], rx[i], half, 8, cpol, cpha, true); t += 1e-6; }
    w.Set(t, MB, 1); t += 2e-6;
    // a cut word
    w.Set(t, MB, 0); t += 1e-6; SpiWord(w, t, 0xf0, 0, half, 4, cpol, cpha, true); w.Set(t + 1e-6, MB, 1);
    uint32_t end; auto ev = w.Events(24e6, end);
    Dec::TOut o(frames, 1024);
    Dec::Spi(ev.data(), ev.size(), MC, MD, MA, MB, 3, 0, cpol, cpha, 8, true, o);
    Dec::Sort(o.f, o.n);
    char s[80];
    sprintf(s, "spi mode %d frames %d", mode, o.n);
    Check(o.n == 10, s);
    int imo = 0, imi = 0;
    for (int i = 0; i < o.n && i < 8; i++)
    {
      if (o.f[i].lane == 3) { sprintf(s, "spi mode %d mosi %d: %02x", mode, imo, o.f[i].v); Check(o.f[i].v == tx[imo++] && !o.f[i].flags, s); }
      else { sprintf(s, "spi mode %d miso %d: %02x", mode, imi, o.f[i].v); Check(o.f[i].v == rx[imi++] && !o.f[i].flags, s); }
    }
    Check(o.n == 10 && (o.f[8].flags & Dec::FlErr) && (o.f[9].flags & Dec::FlErr), "spi cut word");
  }
}

void I2cByte(TWave& w, double& t, int v, int ack, double q)
{
  for (int b = 8; b >= 0; b--)
  {
    int x = b ? (v >> (b - 1)) & 1 : ack;
    w.Set(t, MD, x); t += q; w.Set(t, MC, 1); t += 2 * q; w.Set(t, MC, 0); t += q;
  }
}

void TestI2c()
{
  TWave w; w.init = 15;
  double t = 10e-6, q = 1.25e-6;   // 200 kHz
  auto start = [&]() { w.Set(t, MD, 0); t += q; w.Set(t, MC, 0); t += q; };
  auto stop = [&]() { w.Set(t, MD, 0); t += q; w.Set(t, MC, 1); t += q; w.Set(t, MD, 1); t += 4 * q; };
  auto restart = [&]() { w.Set(t, MD, 1); t += q; w.Set(t, MC, 1); t += q; start(); };
  start(); I2cByte(w, t, 0x50 << 1, 0, q); I2cByte(w, t, 0x10, 0, q);
  restart(); I2cByte(w, t, (0x50 << 1) | 1, 0, q); I2cByte(w, t, 0xab, 0, q); I2cByte(w, t, 0xcd, 1, q);
  stop();
  start(); I2cByte(w, t, 0x3c << 1, 1, q); stop();
  uint32_t end; auto ev = w.Events(10e6, end);
  Dec::TOut o(frames, 1024);
  Dec::I2c(ev.data(), ev.size(), MC, MD, 3, o);
  struct { int kind, v, fl; } want[] = {
    { Dec::Start, 0, 0 }, { Dec::AddrW, 0x50, 0 }, { Dec::Data, 0x10, 0 }, { Dec::Restart, 0, 0 },
    { Dec::AddrR, 0x50, 0 }, { Dec::Data, 0xab, 0 }, { Dec::Data, 0xcd, Dec::FlNack }, { Dec::Stop, 0, 0 },
    { Dec::Start, 0, 0 }, { Dec::AddrW, 0x3c, Dec::FlNack }, { Dec::Stop, 0, 0 } };
  char s[80];
  sprintf(s, "i2c frames %d", o.n);
  Check(o.n == 11, s);
  for (int i = 0; i < o.n && i < 11; i++)
  {
    sprintf(s, "i2c %d: kind %d v %02x fl %d", i, o.f[i].kind, o.f[i].v, o.f[i].flags);
    Check(o.f[i].kind == want[i].kind && o.f[i].v == want[i].v && o.f[i].flags == want[i].fl, s);
  }
}

void TestOneWire()
{
  TWave w; w.init = 15;
  double t = 50e-6, us = 1e-6;
  // reset 480 us, presence after 30 us for 120 us
  w.Set(t, MC, 0); t += 480 * us; w.Set(t, MC, 1); t += 30 * us; w.Set(t, MC, 0); t += 120 * us; w.Set(t, MC, 1); t += 330 * us;
  int bytes[] = { 0xcc, 0x44, 0x01 };
  for (int v : bytes)
    for (int b = 0; b < 8; b++)
    {
      double low = ((v >> b) & 1) ? 6 * us : 64 * us;
      w.Set(t, MC, 0); w.Set(t + low, MC, 1); t += 70 * us;
    }
  uint32_t end; auto ev = w.Events(1e6, end);
  Dec::TOut o(frames, 1024);
  Dec::OneWire(ev.data(), ev.size(), MC, 2, 1e6, o);
  char s[80];
  sprintf(s, "1-wire frames %d", o.n);
  Check(o.n == 5, s);
  if (o.n == 5)
  {
    Check(o.f[0].kind == Dec::Reset && o.f[1].kind == Dec::Presence, "1-wire reset / presence");
    for (int i = 0; i < 3; i++) { sprintf(s, "1-wire byte %d: %02x", i, o.f[2 + i].v); Check(o.f[2 + i].v == bytes[i], s); }
  }
}

int main()
{
  srand(1);
  TestUart();
  TestSpi();
  TestI2c();
  TestOneWire();
  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails != 0;
}

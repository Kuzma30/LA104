// Host test of the protocol decoders and the protocol trigger:
//   g++ -std=c++17 -I../source test_decode.cpp && ./a.out
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <string>
#include "decode.h"
#include "../../common/test/usbsignal.h"

// state bits: 0 C, 1 D, 2 A, 3 B; lanes A 0, B 1, C 2, D 3
enum { MC = 1, MD = 2, MA = 4, MB = 8 };
enum { LA_ = 0, LB_ = 1, LC_ = 2, LD_ = 3 };

struct TWave
{
  struct TCh { double t; int mask, level; };
  std::vector<TCh> ch;
  int init = 15;
  void Set(double t, int mask, int level) { ch.push_back({ t, mask, level }); }
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
Dec::TStats st;
int nf;

void Run(TWave& w, double rate, const Dec::TCfg& c, double jitter = 0)
{
  uint32_t end;
  auto ev = w.Events(rate, end, jitter);
  bool ovf;
  nf = Dec::Decode(ev.data(), ev.size(), end, rate, c, frames, 1024, st, ovf);
}

Dec::TCfg Cfg(int proto)
{
  Dec::TCfg c;
  Dec::Defaults(c);
  c.proto = (int8_t)proto;
  return c;
}

int Trig(const Dec::TCfg& c, double rate) { return Dec::FindTrigger(frames, nf, c, rate); }

// ------------------------------------------------------------------ UART
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
    Dec::TCfg c = Cfg(Dec::PUart);
    c.lane[Dec::PUart][1] = -1;
    c.uartParity = parity; c.uartStop = stop; c.inv[Dec::PUart] = inv;
    Run(w, rate, c, 0.4);
    char s[80];
    sprintf(s, "uart cfg %d autobaud %u", cfg, st.baud);
    Check(st.baud == baud, s);
    sprintf(s, "uart cfg %d frames %d", cfg, nf);
    Check(nf == len, s);
    for (int i = 0; i < nf && i < len; i++)
    {
      sprintf(s, "uart cfg %d byte %d: %02x != %02x flags %d", cfg, i, frames[i].v, (uint8_t)msg[i], frames[i].flags);
      Check(frames[i].v == (uint8_t)msg[i] && frames[i].flags == 0 && frames[i].lane == LC_, s);
    }
    if (cfg == 0)
    {
      // trigger: the 2nd 'l', then ' '? no: 'l' then 'o'
      c.tCond[Dec::PUart] = 2; c.tValue = 'l'; c.tMask = 0xff; c.tNth = 2;
      Check(Trig(c, rate) == 3, "uart trigger 2nd l");
      c.tNth = 1; c.tThen = 1; c.tThenValue = 'o';
      Check(Trig(c, rate) == 3, "uart trigger l then o");
      c.tThenValue = 'x';
      Check(Trig(c, rate) == -1, "uart trigger l then x: none");
      c.tThen = 0; c.tValue = 0x40; c.tMask = 0xf0; c.tNth = 1;     // 0x4x: 'H' 0x48
      Check(Trig(c, rate) == 0, "uart trigger mask");
      c.tCond[Dec::PUart] = 3;                                     // TX: no TX lane
      Check(Trig(c, rate) == -1, "uart trigger TX none");
    }
  }
  // parity error and break
  {
    TWave w; double t = 10e-6, bit = 1.0 / 9600;
    for (int i = 0; i < 4; i++) UartByte(w, t, MD, 0x31 + i, bit, 8, 1, 1);
    w.Set(t, MD, 0); t += 20 * bit; w.Set(t, MD, 1); t += 3 * bit;
    UartByte(w, t, MD, 0x41, bit, 8, 1, 1);
    Dec::TCfg c = Cfg(Dec::PUart);
    c.lane[Dec::PUart][0] = -1; c.lane[Dec::PUart][1] = LD_;
    c.uartParity = 2; c.uartBaud = 6;                              // odd instead of even, 9600
    Run(w, 24e6, c);
    int par = 0, brk = 0;
    for (int i = 0; i < nf; i++) { par += (frames[i].flags & Dec::FlPar) ? 1 : 0; brk += frames[i].kind == Dec::Break; }
    char s[64]; sprintf(s, "uart parity / break: %d frames %d par %d brk", nf, par, brk);
    Check(nf == 6 && par == 5 && brk == 1, s);
    c.tCond[Dec::PUart] = 4;
    Check(Trig(c, 24e6) == 4, "uart trigger break");
    c.tCond[Dec::PUart] = 5;
    Check(Trig(c, 24e6) == 0, "uart trigger error");
  }
}

// ------------------------------------------------------------------ LIN
void TestLin()
{
  TWave w; double bit = 1.0 / 19200, t = 50e-6;
  int pid = 0x3c, id = pid & 0x3f;
  int p0 = (id ^ (id >> 1) ^ (id >> 2) ^ (id >> 4)) & 1, p1 = !(((id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id >> 5)) & 1);
  int pidb = id | (p0 << 6) | (p1 << 7);
  int data[] = { 0x10, 0x20, 0x30 };
  int sum = pidb;
  for (int d : data) { sum += d; if (sum > 255) sum -= 255; }
  int cs = (~sum) & 0xff;
  w.Set(t, MC, 0); t += 13 * bit; w.Set(t, MC, 1); t += 2 * bit;
  UartByte(w, t, MC, 0x55, bit, 8, 0, 1); UartByte(w, t, MC, pidb, bit, 8, 0, 1);
  for (int d : data) UartByte(w, t, MC, d, bit, 8, 0, 1);
  UartByte(w, t, MC, cs, bit, 8, 0, 1);
  Dec::TCfg c = Cfg(Dec::PLin);
  Run(w, 2e6, c);
  char s[80]; sprintf(s, "lin frames %d baud %u", nf, st.baud);
  Check(nf == 7 && st.baud == 19200, s);
  if (nf == 7)
  {
    Check(frames[0].kind == Dec::Break && frames[1].kind == Dec::Sync && frames[2].kind == Dec::Pid
          && frames[6].kind == Dec::Checksum && frames[6].aux == 2 && !frames[2].flags, "lin kinds / checksum");
    c.tCond[Dec::PLin] = 1; c.tValue = 0x3c; c.tMask = 0x3f;
    Check(Trig(c, 2e6) == 2, "lin trigger PID");
  }
}

// ------------------------------------------------------------------ SPI
void SpiWord(TWave& w, double& t, int v, int vi, double half, int bits, bool cpol, bool cpha, bool msb)
{
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
    w.Set(t, MB, 0); t += 1e-6; SpiWord(w, t, 0x03, 0, half, 8, cpol, cpha, true); SpiWord(w, t, 0xf0, 0, half, 4, cpol, cpha, true);
    w.Set(t + 1e-6, MB, 1);
    Dec::TCfg c = Cfg(Dec::PSpi);
    c.spiMode = mode;
    Run(w, 24e6, c);
    char s[80];
    sprintf(s, "spi mode %d frames %d", mode, nf);
    Check(nf == 12, s);
    int imo = 0, imi = 0;
    for (int i = 0; i < nf && i < 8; i++)
    {
      if (frames[i].lane == LD_) { sprintf(s, "spi mode %d mosi %d: %02x", mode, imo, frames[i].v); Check(frames[i].v == (uint32_t)tx[imo++] && !(frames[i].flags & Dec::FlErr), s); }
      else { sprintf(s, "spi mode %d miso %d: %02x", mode, imi, frames[i].v); Check(frames[i].v == (uint32_t)rx[imi++] && !(frames[i].flags & Dec::FlErr), s); }
    }
    Check(nf == 12 && (frames[10].flags & Dec::FlErr) && (frames[11].flags & Dec::FlErr), "spi cut word");
    // command: the first MOSI word after CS
    c.tCond[Dec::PSpi] = 3; c.tValue = 0x03; c.tMask = 0xff;
    sprintf(s, "spi mode %d trigger command 03: %d", mode, Trig(c, 24e6));
    Check(Trig(c, 24e6) >= 8 && frames[Trig(c, 24e6)].v == 3, s);
    c.tValue = 0x00;
    Check(Trig(c, 24e6) == -1, "spi command 00 (not first): none");
    c.tCond[Dec::PSpi] = 2; c.tValue = 0x40;
    int k = Trig(c, 24e6);
    Check(k >= 0 && frames[k].lane == LA_ && frames[k].v == 0x40, "spi trigger MISO");
  }
}

// ------------------------------------------------------------------ I2C
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
  double t = 10e-6, q = 1.25e-6;
  auto start = [&]() { w.Set(t, MD, 0); t += q; w.Set(t, MC, 0); t += q; };
  auto stop = [&]() { w.Set(t, MD, 0); t += q; w.Set(t, MC, 1); t += q; w.Set(t, MD, 1); t += 4 * q; };
  auto restart = [&]() { w.Set(t, MD, 1); t += q; w.Set(t, MC, 1); t += q; start(); };
  start(); I2cByte(w, t, 0x50 << 1, 0, q); I2cByte(w, t, 0x10, 0, q);
  restart(); I2cByte(w, t, (0x50 << 1) | 1, 0, q); I2cByte(w, t, 0xab, 0, q); I2cByte(w, t, 0xcd, 1, q);
  stop();
  start(); I2cByte(w, t, 0x3c << 1, 1, q); stop();
  start(); I2cByte(w, t, 0x50 << 1, 0, q); I2cByte(w, t, 0x20, 0, q); I2cByte(w, t, 0x77, 0, q); stop();
  Dec::TCfg c = Cfg(Dec::PI2c);
  Run(w, 10e6, c);
  struct { int kind, v, fl; } want[] = {
    { Dec::Start, 0, Dec::FlPoint }, { Dec::AddrW, 0x50, 0 }, { Dec::Data, 0x10, 0 }, { Dec::Restart, 0, Dec::FlPoint },
    { Dec::AddrR, 0x50, 0 }, { Dec::Data, 0xab, 0 }, { Dec::Data, 0xcd, Dec::FlNack }, { Dec::Stop, 0, Dec::FlPoint },
    { Dec::Start, 0, Dec::FlPoint }, { Dec::AddrW, 0x3c, Dec::FlNack }, { Dec::Stop, 0, Dec::FlPoint } };
  char s[80];
  sprintf(s, "i2c frames %d", nf);
  Check(nf == 16, s);
  for (int i = 0; i < 11 && i < nf; i++)
  {
    sprintf(s, "i2c %d: kind %d v %02x fl %d", i, frames[i].kind, frames[i].v, frames[i].flags);
    Check(frames[i].kind == want[i].kind && frames[i].v == (uint32_t)want[i].v && frames[i].flags == want[i].fl, s);
  }
  // write to 0x50 register 0x20 (not 0x10)
  c.tCond[Dec::PI2c] = 1; c.tValue = 0x50; c.tMask = 0x7f; c.tThen = 1; c.tThenValue = 0x20;
  Check(Trig(c, 10e6) == 12, "i2c trigger write 50 then 20");
  c.tThen = 0;
  Check(Trig(c, 10e6) == 1, "i2c trigger write 50");
  c.tCond[Dec::PI2c] = 2;
  Check(Trig(c, 10e6) == 4, "i2c trigger read 50");
  c.tCond[Dec::PI2c] = 5;
  Check(Trig(c, 10e6) == 6, "i2c trigger NACK");
}

// ------------------------------------------------------------------ 1-Wire
void TestOneWire()
{
  TWave w; w.init = 15;
  double t = 50e-6, us = 1e-6;
  w.Set(t, MC, 0); t += 480 * us; w.Set(t, MC, 1); t += 30 * us; w.Set(t, MC, 0); t += 120 * us; w.Set(t, MC, 1); t += 330 * us;
  int bytes[] = { 0xcc, 0x44, 0x01 };
  for (int v : bytes)
    for (int b = 0; b < 8; b++)
    {
      double low = ((v >> b) & 1) ? 6 * us : 64 * us;
      w.Set(t, MC, 0); w.Set(t + low, MC, 1); t += 70 * us;
    }
  Dec::TCfg c = Cfg(Dec::POneWire);
  Run(w, 1e6, c);
  char s[80];
  sprintf(s, "1-wire frames %d", nf);
  Check(nf == 5, s);
  if (nf == 5)
  {
    Check(frames[0].kind == Dec::Reset && frames[1].kind == Dec::Presence && !frames[0].flags, "1-wire reset / presence");
    for (int i = 0; i < 3; i++) { sprintf(s, "1-wire byte %d: %02x", i, frames[2 + i].v); Check(frames[2 + i].v == (uint32_t)bytes[i], s); }
    Check((frames[2].aux & Dec::AuxRom) && (frames[3].aux & Dec::AuxFunc), "1-wire ROM / function");
    c.tCond[Dec::POneWire] = 3; c.tValue = 0x44; c.tMask = 0xff;
    Check(Trig(c, 1e6) == 3, "1-wire trigger function 44");
  }
}

// ------------------------------------------------------------------ CAN
std::vector<int> CanBits(uint32_t id, bool ext, const std::vector<int>& data)
{
  std::vector<int> b;
  b.push_back(0);                                        // SOF
  if (!ext) { for (int k = 10; k >= 0; k--) b.push_back((id >> k) & 1); b.push_back(0); b.push_back(0); b.push_back(0); }
  else
  {
    for (int k = 28; k >= 18; k--) b.push_back((id >> k) & 1);
    b.push_back(1); b.push_back(1);                      // SRR, IDE
    for (int k = 17; k >= 0; k--) b.push_back((id >> k) & 1);
    b.push_back(0); b.push_back(0); b.push_back(0);      // RTR, r1, r0
  }
  int dlc = data.size();
  for (int k = 3; k >= 0; k--) b.push_back((dlc >> k) & 1);
  for (int d : data) for (int k = 7; k >= 0; k--) b.push_back((d >> k) & 1);
  uint16_t crc = 0;
  for (int x : b) { int nx = x ^ ((crc >> 14) & 1); crc = (crc << 1) & 0x7fff; if (nx) crc ^= 0x4599; }
  for (int k = 14; k >= 0; k--) b.push_back((crc >> k) & 1);
  // stuffing
  std::vector<int> s;
  int same = 0, last = -1;
  for (int x : b)
  {
    s.push_back(x);
    if (x == last) same++; else { same = 1; last = x; }
    if (same == 5) { s.push_back(!x); last = !x; same = 1; }
  }
  s.push_back(1); s.push_back(0); s.push_back(1);        // CRC delimiter, ACK, ACK delimiter
  for (int k = 0; k < 10; k++) s.push_back(1);
  return s;
}

void TestCan()
{
  TWave w; w.init = 15;
  double bit = 1.0 / 500000, t = 30e-6;
  auto send = [&](uint32_t id, bool ext, std::vector<int> d) {
    for (int x : CanBits(id, ext, d)) { w.Set(t, MC, x); t += bit; }
    t += 5 * bit;
  };
  send(0x123, false, { 0x11, 0x22, 0x33 });
  send(0x18daf110, true, { 0x02, 0x10, 0x03 });
  send(0x7df, false, { 0x02, 0x01, 0x0c, 0, 0, 0, 0, 0 });
  Dec::TCfg c = Cfg(Dec::PCan);
  Run(w, 8e6, c);
  int ids = 0, crcOk = 0, acks = 0;
  for (int i = 0; i < nf; i++)
  {
    ids += frames[i].kind == Dec::CanId;
    crcOk += frames[i].kind == Dec::CanCrc && !frames[i].flags;
    acks += frames[i].kind == Dec::CanAck && !frames[i].flags;
  }
  char s[80]; sprintf(s, "can frames %d ids %d crc %d ack %d baud %u", nf, ids, crcOk, acks, st.baud);
  Check(ids == 3 && crcOk == 3 && acks == 3 && st.baud == 500000, s);
  c.tCond[Dec::PCan] = 1; c.tValue = 0x18daf110; c.tMask = 0x1fffffff;
  int k = Trig(c, 8e6);
  Check(k >= 0 && frames[k].kind == Dec::CanId && frames[k].v == 0x18daf110 && (frames[k].aux & 2), "can trigger ext id");
  c.tValue = 0x7df; c.tMask = 0x7ff; c.tThen = 1; c.tThenValue = 8;   // then DLC 8
  k = Trig(c, 8e6);
  Check(k >= 0 && frames[k].v == 0x7df, "can trigger id then dlc");
}

// ------------------------------------------------------------------ WS2812, NEC, PS/2
void TestWs2812()
{
  TWave w; w.init = 0;
  double t = 10e-6, us = 1e-6;
  uint32_t grb[] = { 0xff0000, 0x00ff00, 0x0000ff, 0x123456 };
  for (uint32_t v : grb)
    for (int b = 23; b >= 0; b--) { double h = ((v >> b) & 1) ? 0.8 : 0.4; w.Set(t, MC, 1); w.Set(t + h * us, MC, 0); t += 1.25 * us; }
  Dec::TCfg c = Cfg(Dec::PWs2812);
  Run(w, 24e6, c);
  char s[80]; sprintf(s, "ws2812 frames %d", nf);
  Check(nf == 4, s);
  if (nf == 4)
  {
    Check(frames[0].v == 0x00ff00 && frames[1].v == 0xff0000 && frames[3].v == 0x341256, "ws2812 GRB -> RGB");
    c.tCond[Dec::PWs2812] = 2; c.tValue = 2;
    Check(Trig(c, 24e6) == 2, "ws2812 trigger LED 2");
  }
}

void TestNec()
{
  TWave w; w.init = 15;    // receiver output idle high (inverted)
  double t = 1e-3, us = 1e-6;
  auto mark = [&](double m, double s) { w.Set(t, MC, 0); t += m * us; w.Set(t, MC, 1); t += s * us; };
  mark(9000, 4500);
  uint32_t v = 0x04 | (0xfb << 8) | (0x08 << 16) | (0xf7u << 24);
  for (int b = 0; b < 32; b++) mark(562, ((v >> b) & 1) ? 1687 : 562);
  mark(562, 40000);
  mark(9000, 2250); mark(562, 1000);
  Dec::TCfg c = Cfg(Dec::PNec);
  Run(w, 100e3, c);
  char s[80]; sprintf(s, "nec frames %d", nf);
  Check(nf == 4, s);
  if (nf == 4)
  {
    Check(frames[0].kind == Dec::Leader && frames[1].kind == Dec::NecAddr && frames[1].v == 4 &&
          frames[2].kind == Dec::NecCmd && frames[2].v == 8 && !frames[2].flags && frames[3].kind == Dec::Repeat, "nec frames");
    c.tCond[Dec::PNec] = 2; c.tValue = 8; c.tMask = 0xff;
    Check(Trig(c, 100e3) == 2, "nec trigger command");
  }
}

void TestPs2()
{
  TWave w; w.init = 15;
  double t = 100e-6, half = 40e-6;
  auto frame = [&](int v) {
    int bits[11]; bits[0] = 0; int ones = 0;
    for (int b = 0; b < 8; b++) { bits[1 + b] = (v >> b) & 1; ones += bits[1 + b]; }
    bits[9] = !(ones & 1); bits[10] = 1;
    for (int b = 0; b < 11; b++) { w.Set(t, MD, bits[b]); t += half / 2; w.Set(t, MC, 0); t += half; w.Set(t, MC, 1); t += half / 2; }
    t += 500e-6;
  };
  frame(0x1c); frame(0xf0); frame(0x1c);
  Dec::TCfg c = Cfg(Dec::PPs2);
  Run(w, 1e6, c);
  char s[80]; sprintf(s, "ps2 frames %d", nf);
  Check(nf == 3 && frames[0].v == 0x1c && frames[1].v == 0xf0 && !frames[1].flags, s);
  c.tCond[Dec::PPs2] = 1; c.tValue = 0xf0; c.tMask = 0xff;
  Check(Trig(c, 1e6) == 1, "ps2 trigger f0");
}

// ------------------------------------------------------------------ Pulse, Counter, Encoder
void TestPulseCounter()
{
  TWave w; w.init = 0;
  double t = 10e-6;
  double widths[] = { 10, 20, 30, 150, 20 };   // us, period 200 us
  for (double wd : widths) { w.Set(t, MC, 1); w.Set(t + wd * 1e-6, MC, 0); t += 200e-6; }
  Dec::TCfg c = Cfg(Dec::PPulse);
  Run(w, 1e6, c);
  char s[96]; sprintf(s, "pulse frames %d", nf);
  sprintf(s, "pulse frames %d w3 %u", nf, nf > 3 ? frames[3].v : 0);
  Check(nf == 5 && frames[3].v >= 149 && frames[3].v <= 151, s);
  char txt[64];
  Dec::Format(frames, 0, nf, c, 1e6, txt, sizeof(txt), true);
  sprintf(s, "pulse text '%s'", txt);
  Check(strcmp(txt, "high 10.0us T 200us 5.00kHz 5.0%") == 0, s);
  c.tCond[Dec::PPulse] = 1; c.tValue = 100;
  Check(Trig(c, 1e6) == 3, "pulse trigger width > 100 us");
  c.tCond[Dec::PPulse] = 2; c.tValue = 15;
  Check(Trig(c, 1e6) == 0, "pulse trigger width < 15 us");

  c = Cfg(Dec::PCounter);
  Run(w, 1e6, c);
  Check(nf == 5 && st.edges == 5, "counter rising");
  c.cntEdge = Dec::EdgeBoth;
  Run(w, 1e6, c);
  Check(st.edges == 10, "counter both");
  c.tCond[Dec::PCounter] = 1; c.tValue = 7;
  Check(Trig(c, 1e6) == 6, "counter trigger edge 7");
}

void TestEncoder()
{
  TWave w; w.init = 0;
  double t = 10e-6, dt = 5e-6;
  int a = 0, b = 0;
  auto step = [&](int dir) {
    // forward: A leads B: 00 -> 10 -> 11 -> 01 -> 00
    int ph[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int k = 0; for (; k < 4; k++) if (ph[k][0] == a && ph[k][1] == b) break;
    k = (k + dir + 4) & 3; a = ph[k][0]; b = ph[k][1];
    w.Set(t, MC, a); w.Set(t, MD, b); t += dt;
  };
  for (int i = 0; i < 40; i++) step(1);
  w.Set(t - dt / 2, MA, 1); w.Set(t - dt / 4, MA, 0);   // Z between two steps
  for (int i = 0; i < 40; i++) step(1);
  w.Set(t - dt / 2, MA, 1); w.Set(t - dt / 4, MA, 0);
  for (int i = 0; i < 12; i++) step(-1);
  Dec::TCfg c = Cfg(Dec::PEncoder);
  c.lane[Dec::PEncoder][2] = LA_;
  Run(w, 1e6, c);
  char s[96]; sprintf(s, "encoder steps %d perRev %d z %u err %u", st.steps, st.perRev, st.zCount, st.qErrors);
  Check(st.steps == 68 && st.perRev == 40 && st.zCount == 2 && st.qErrors == 0, s);
  if (getenv("DBG")) for (int i = 0; i < nf; i++) printf("%d kind %d v %d t %u\n", i, frames[i].kind, (int)frames[i].v, frames[i].t0);
  c.tCond[Dec::PEncoder] = 1; c.tNth = 2;
  int k = Trig(c, 1e6);
  Check(k >= 0 && frames[k].kind == Dec::QuadZ && frames[k].v == 40, "encoder trigger 2nd Z");
}

// ------------------------------------------------------------------ USB
// D+ on C, D- on D; the samples of usbsignal.h as events
void RunUsb(const UsbSig& u, double rate, const Dec::TCfg& c)
{
  std::vector<uint32_t> ev;
  int prev = -1;
  for (size_t i = 0; i < u.st.size(); i++)
    if (u.st[i] != prev) { prev = u.st[i]; ev.push_back(La::Event(prev | 12, (uint32_t)i)); }
  bool ovf;
  nf = Dec::Decode(ev.data(), ev.size(), (uint32_t)u.st.size(), rate, c, frames, 1024, st, ovf);
}

std::vector<std::string> Texts(const Dec::TCfg& c, double rate, bool lng = false)
{
  std::vector<std::string> v;
  char b[96];
  for (int i = 0; i < nf; i++) { Dec::Format(frames, i, nf, c, rate, b, sizeof b, lng); v.push_back(b); }
  return v;
}

void TestUsb()
{
  const double rate = 72e6;
  UsbSig u(rate, 12e6, 1);
  u.Idle(20);
  u.Se0(12e6 * 10e-6);                    // reset 10 us
  u.Idle(30);
  u.Sof(0x2a5);
  u.Idle(30);
  u.Token(Usb::PidSetup, 0, 0);
  u.Idle(4);
  u.Data(Usb::PidData0, { 0x00, 0x05, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00 });
  u.Idle(4);
  u.Handshake(Usb::PidAck);
  u.Idle(30);
  u.Token(Usb::PidIn, 11, 0);
  u.Idle(4);
  u.Handshake(Usb::PidNak);
  u.Idle(30);
  u.Token(Usb::PidOut, 11, 2);
  u.Idle(4);
  u.Data(Usb::PidData1, { 'h', 'i' }, 0xbeef);
  u.Idle(40);
  Dec::TCfg c = Cfg(Dec::PUsb);
  RunUsb(u, rate, c);
  std::vector<std::string> e = { "RESET", "SOF", "F677", "CRC", "SETUP", "A0.0", "CRC", "DATA0", "OUT std dev",
    "SET_ADDRESS", "addr 11", "I:0000", "L:0", "CRC", "ACK", "IN", "A11.0", "CRC", "NAK", "OUT", "A11.2", "CRC",
    "DATA1", "68", "69", "CRC!" };
  auto v = Texts(c, rate);
  bool ok = v == e;
  if (!ok) { for (auto& x : v) printf("[%s] ", x.c_str()); printf("\n"); }
  Check(ok, "usb frames");
  Check(st.baud == 12000000, "usb rate");
  auto l = Texts(c, rate, true);
  Check(l[0] == "reset (SE0 10.0us)", "usb reset text");
  Check(l[10] == "wValue 0x000B: address 11", "usb set address text");
  // the trigger conditions
  c.tCond[Dec::PUsb] = 1; c.tValue = Usb::PidNak; c.tMask = 15;
  Check(Trig(c, rate) == 18, "usb trigger PID NAK");
  c.tCond[Dec::PUsb] = 2; c.tValue = 11 | 2 << 7; c.tMask = 0x7ff;
  Check(Trig(c, rate) == 20, "usb trigger address 11 endpoint 2");
  c.tMask = 0x7f; c.tNth = 2;
  Check(Trig(c, rate) == 20, "usb trigger address 11, 2nd");
  c.tNth = 1;
  c.tCond[Dec::PUsb] = 3; c.tValue = 5; c.tMask = 0xff;
  Check(Trig(c, rate) == 9, "usb trigger SET_ADDRESS");
  c.tCond[Dec::PUsb] = 5;
  Check(Trig(c, rate) == 0, "usb trigger reset");
  c.tCond[Dec::PUsb] = 6;
  Check(Trig(c, rate) == 25, "usb trigger error (CRC16)");
  c.tCond[Dec::PUsb] = 1; c.tValue = Usb::PidSetup; c.tMask = 15; c.tThen = 1; c.tThenValue = 0;
  Check(Trig(c, rate) == 4, "usb trigger SETUP then address 0");
  int lane; bool rise;
  Check(Dec::StartEdge(c, lane, rise) && lane == 2 && !rise, "usb start edge");
  // low speed at 12 MS/s; too slow at 24 MS/s for full speed
  UsbSig ls(12e6, 1.5e6, 1, true);
  ls.Idle(10);
  ls.Token(Usb::PidIn, 1, 1);
  ls.Idle(4);
  ls.Data(Usb::PidData0, { 1, 2, 3 });
  ls.Idle(10);
  Dec::TCfg c2 = Cfg(Dec::PUsb);
  RunUsb(ls, 12e6, c2);
  v = Texts(c2, 12e6);
  ok = v == std::vector<std::string>{ "IN", "A1.1", "CRC", "DATA0", "01", "02", "03", "CRC" };
  if (!ok) { for (auto& x : v) printf("[%s] ", x.c_str()); printf("\n"); }
  Check(ok && st.baud == 1500000, "usb low speed");
  UsbSig f(24e6, 12e6, 0);
  f.Idle(10); f.Handshake(Usb::PidAck); f.Idle(10);
  RunUsb(f, 24e6, c2);
  Check(st.slow, "usb full speed at 24 MS/s: too slow");
  c2.lane[Dec::PUsb][1] = 2;
  RunUsb(u, rate, c2);
  Check(st.noLane, "usb D+ and D- on one lane");
}

int main()
{
  srand(1);
  TestUart();
  TestLin();
  TestSpi();
  TestI2c();
  TestOneWire();
  TestCan();
  TestWs2812();
  TestNec();
  TestPs2();
  TestPulseCounter();
  TestEncoder();
  TestUsb();
  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails != 0;
}

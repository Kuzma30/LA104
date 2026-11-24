#pragma once
// Synthetic USB signals for the PC tests of the USB decoder: packets with
// SYNC, NRZI, bit stuffing and EOP, sampled at fs; D- may change a few
// samples after D+ (crossover glitches). st[i]: bit 0 D+, bit 1 D-.
#include <stdint.h>
#include <vector>
#include "../usbdecode.h"

struct UsbSig
{
  double fs, T;
  int skew;
  bool low;
  double t = 0;
  bool k = false;                 // current NRZI level: K
  struct Seg { double t0; int dp, dm; };
  std::vector<Seg> seg;
  std::vector<uint8_t> st;

  UsbSig(double sampleRate, double rate, int skewSamples, bool lowSpeed = false)
    : fs(sampleRate), T(sampleRate / rate), skew(skewSamples), low(lowSpeed) {}

  void Put(int dp, int dm, double bits)
  {
    seg.push_back({ t, dp, dm });
    t += bits * T;
    size_t n = (size_t)(t + 0.5);
    st.assign(n, 0);
    size_t a = 0, b = 0;
    for (size_t i = 0; i < n; i++)
    {
      while (a + 1 < seg.size() && seg[a + 1].t0 <= i) a++;
      while (b + 1 < seg.size() && seg[b + 1].t0 + skew <= i) b++;
      st[i] = (uint8_t)(seg[a].dp | (seg[b].dm << 1));
    }
  }
  void Level(bool kk, double bits)
  {
    int j = low ? 0 : 1;              // D+ of J
    int dp = kk ? !j : j;
    Put(dp, !dp, bits);
  }
  void Idle(double bits) { k = false; Level(false, bits); }
  void Se0(double bits) { Put(0, 0, bits); }
  void Eop() { Se0(2); Idle(1); }
  void KeepAlive() { Eop(); }

  // NRZI: 0 changes the level; stuffing after 6 ones
  void Bits(const std::vector<int>& bits, bool stuff = true)
  {
    int ones = 0;
    for (int b : bits)
    {
      if (!b) k = !k;
      Level(k, 1);
      ones = b ? ones + 1 : 0;
      if (stuff && ones == 6) { k = !k; Level(k, 1); ones = 0; }
    }
  }
  static void ByteBits(std::vector<int>& v, int x) { for (int i = 0; i < 8; i++) v.push_back((x >> i) & 1); }

  void Raw(const std::vector<int>& bytes, bool eop = true)
  {
    std::vector<int> v;
    for (int x : bytes) ByteBits(v, x);
    Bits(v);
    if (eop) Eop();
  }
  void Packet(int pid, const std::vector<int>& payload)
  {
    std::vector<int> b = { 0x80, pid | ((~pid & 15) << 4) };
    b.insert(b.end(), payload.begin(), payload.end());
    Raw(b);
  }
  void Token(int pid, int addr, int ep)
  {
    uint32_t w = (uint32_t)(addr | ep << 7);
    uint32_t c = Usb::Crc5(w, 11);
    Packet(pid, { (int)(w & 0xff), (int)((w >> 8) | c << 3) });
  }
  void Sof(int frame)
  {
    uint32_t c = Usb::Crc5((uint32_t)frame, 11);
    Packet(Usb::PidSof, { frame & 0xff, (int)((frame >> 8) | c << 3) });
  }
  void Data(int pid, std::vector<int> d, int crcOverride = -1)
  {
    uint32_t c = 0xffff;
    for (int x : d) c = Usb::Crc16Step(c, (uint32_t)x);
    c ^= 0xffff;
    if (crcOverride >= 0) c = (uint32_t)crcOverride;
    d.push_back((int)(c & 0xff));
    d.push_back((int)(c >> 8));
    Packet(pid, d);
  }
  void Handshake(int pid) { Packet(pid, {}); }
  // SYNC, PID OUT, then 8 ones without the stuffed bit
  void StuffError()
  {
    std::vector<int> v;
    ByteBits(v, 0x80);
    ByteBits(v, Usb::PidOut | ((~Usb::PidOut & 15) << 4));
    Bits(v);
    Bits({ 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 }, false);
    Eop();
  }
};

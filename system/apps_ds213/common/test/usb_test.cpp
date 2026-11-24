// PC test of the USB decoder core (usbdecode.h) with synthetic packets.
//   g++ -std=c++17 -O1 -Wall -I.. usb_test.cpp && ./a.out
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "usbdecode.h"
#include "usbsignal.h"

using namespace Usb;

struct TIn
{
  const std::vector<uint8_t>& st;   // bit 0 D+, bit 1 D-
  uint32_t Begin() { return 0; }
  uint32_t End() { return (uint32_t)st.size(); }
  int State(uint32_t t) { return st[t]; }
  uint32_t Next(uint32_t t)
  {
    uint32_t i = t + 1;
    while (i < st.size() && st[i] == st[t]) i++;
    return i;
  }
};

struct TOut
{
  std::vector<TItem> items;
  size_t cap = 100000;
  bool Add(const TItem& it) { if (items.size() >= cap) return false; items.push_back(it); return true; }
};

struct TW
{
  std::string s;
  void C(char c) { s += c; }
  void S(const char* t) { s += t; }
  void Hex(uint32_t v, int d) { for (int i = d - 1; i >= 0; i--) C("0123456789ABCDEF"[(v >> (4 * i)) & 15]); }
  void Dec(uint32_t v) { s += std::to_string(v); }
};

static std::string Str(const TItem& it, bool lng, float fs)
{
  TW w;
  if (it.kind == KData) { w.Hex(it.v, 2); return w.s; }
  Text(w, it.kind, it.v, it.err, lng, it.t1 - it.t0 + 1, [&](TW& ww, uint32_t n) {
    char b[32]; snprintf(b, sizeof b, "%.1fus", n / fs * 1e6); ww.S(b); });
  return w.s;
}

static int failures = 0;

static std::vector<std::string> Run(const std::vector<uint8_t>& st, float fs, int speed, bool lng = false,
                                    TResult* pr = nullptr, size_t cap = 100000)
{
  TIn in{ st };
  TOut out;
  out.cap = cap;
  CDecoder<TIn, TOut> d(in, out, fs);
  TResult r = d.Run(speed);
  if (pr) *pr = r;
  std::vector<std::string> v;
  for (auto& it : out.items) v.push_back(Str(it, lng, fs));
  return v;
}

static void Expect(const char* name, const std::vector<std::string>& got, const std::vector<std::string>& exp)
{
  bool ok = got == exp;
  printf("%-40s %s\n", name, ok ? "OK" : "FAIL");
  if (!ok)
  {
    printf("  got: ");
    for (auto& s : got) printf("[%s] ", s.c_str());
    printf("\n  exp: ");
    for (auto& s : exp) printf("[%s] ", s.c_str());
    printf("\n");
    failures++;
  }
}

static void Check(const char* name, bool ok)
{
  printf("%-40s %s\n", name, ok ? "OK" : "FAIL");
  failures += !ok;
}

int main()
{
  // catalog check values of CRC-5/USB and CRC-16/USB ("123456789")
  {
    const char* t = "123456789";
    uint32_t c5 = 0x1f, c16 = 0xffff;
    for (int i = 0; i < 9; i++)
    {
      for (int b = 0; b < 8; b++)
        c5 = (((c5 ^ (t[i] >> b)) & 1) ? (c5 >> 1) ^ 0x14 : c5 >> 1);
      c16 = Crc16Step(c16, (uint8_t)t[i]);
    }
    Check("CRC-5/USB check 0x19", (c5 ^ 0x1f) == 0x19);
    Check("CRC-16/USB check 0xB4C8", (c16 ^ 0xffff) == 0xb4c8);
    Check("token addr 0 ep 0 CRC5 = 2", Crc5(0, 11) == 2);
  }

  const float fs = 72e6f;
  // full speed: SETUP, DATA0 GET_DESCRIPTOR, ACK, SOF; skew of D- by 1 sample
  {
    UsbSig s(fs, 12e6, 1);
    s.Idle(20);
    s.Token(PidSetup, 0, 0);
    s.Idle(4);
    s.Data(PidData0, { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00 });
    s.Idle(4);
    s.Handshake(PidAck);
    s.Idle(10);
    s.Sof(1234);
    s.Idle(10);
    TResult r;
    Expect("FS setup", Run(s.st, fs, SpeedAuto, false, &r),
      { "SETUP", "A0.0", "CRC", "DATA0", "IN std dev", "GET_DESCRIPTOR", "DEVICE 0", "I:0000", "L:64", "CRC",
        "ACK", "SOF", "F1234", "CRC" });
    Check("FS detected 12 Mbit/s", r.rate == 12000000 && !r.slow);
    Expect("FS setup long", Run(s.st, fs, SpeedAuto, true),
      { "SETUP (PID 0x2D)", "address 0 endpoint 0", "CRC5 0x02 ok", "DATA0 (PID 0xC3)",
        "bmRequestType 0x80 IN std dev", "bRequest 0x06 GET_DESCRIPTOR", "wValue 0x0100: DEVICE 0",
        "wIndex 0x0000", "wLength 64", "CRC16 0x94DD ok", "ACK (PID 0xD2)", "SOF (PID 0xA5)", "frame 1234",
        "CRC5 0x00 ok" });
  }
  // stuffing: data with 0xFF bytes, IN token address 0x7F endpoint 15
  {
    UsbSig s(fs, 12e6, 2);
    s.Idle(10);
    s.Token(PidIn, 0x7f, 15);
    s.Idle(5);
    s.Data(PidData1, { 0xff, 0xff, 0xff, 0x7e, 0x3f });
    s.Idle(5);
    s.Handshake(PidNak);
    s.Idle(8);
    Expect("FS bit stuffing", Run(s.st, fs, SpeedAuto),
      { "IN", "A127.15", "CRC", "DATA1", "FF", "FF", "FF", "7E", "3F", "CRC", "NAK" });
  }
  // 36 MS/s: 3 samples per bit
  {
    const float f2 = 36e6f;
    UsbSig s(f2, 12e6, 0);
    s.Idle(10);
    s.Token(PidOut, 5, 1);
    s.Idle(5);
    s.Data(PidData0, { 0x12, 0x34, 0x56 });
    s.Idle(5);
    s.Handshake(PidStall);
    s.Idle(8);
    Expect("FS at 36 MS/s", Run(s.st, f2, SpeedAuto), { "OUT", "A5.1", "CRC", "DATA0", "12", "34", "56", "CRC", "STALL" });
    TResult r;
    Run(s.st, 24e6f, SpeedFull, false, &r);
    Check("FS at 24 MS/s: too slow", r.slow);
  }
  // low speed at 12 MS/s with slow edges (skew 2 samples), keep-alive, reset
  {
    const float f3 = 12e6f;
    UsbSig s(f3, 1.5e6, 2, true);
    s.Se0(40e-6 * 1.5e6);           // reset 40 us (in bits)
    s.Idle(30);
    s.KeepAlive();
    s.Idle(30);
    s.Token(PidSetup, 3, 0);
    s.Idle(4);
    s.Data(PidData0, { 0x00, 0x05, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00 });
    s.Idle(4);
    s.Handshake(PidAck);
    s.Idle(20);
    TResult r;
    Expect("LS reset, keep-alive, SET_ADDRESS", Run(s.st, f3, SpeedAuto, false, &r),
      { "RESET", "EOP", "SETUP", "A3.0", "CRC", "DATA0", "OUT std dev", "SET_ADDRESS", "addr 7", "I:0000", "L:0", "CRC", "ACK" });
    Check("LS detected 1.5 Mbit/s", r.rate == 1500000);
  }
  // errors: bad CRC16, bad PID, stuffing error, capture starting inside a packet
  {
    UsbSig s(fs, 12e6, 1);
    s.Idle(10);
    s.Data(PidData1, { 0x01, 0x02 }, 0x1234);      // wrong CRC
    s.Idle(10);
    s.Raw({ 0x80, 0x59 }, true);                     // PID IN with a bad check nibble (0x59)
    s.Idle(10);
    s.StuffError();
    s.Idle(10);
    s.Token(PidIn, 1, 1);
    s.Idle(10);
    Expect("errors", Run(s.st, fs, SpeedAuto),
      { "DATA1", "01", "02", "CRC!", "PID!", "STUFF!", "IN", "A1.1", "CRC" });
    // cut: the first 60 samples are inside the DATA1 packet
    std::vector<uint8_t> cut(s.st.begin() + 200, s.st.end());
    auto v = Run(cut, fs, SpeedAuto);
    bool ok = !v.empty() && v[0] == "PID!";
    Check("starts inside a packet: no SYNC error", ok);
    if (!ok) { for (auto& x : v) printf("[%s] ", x.c_str()); printf("\n"); }
  }
  // long data packet (64 bytes) is sent in parts, CRC ok
  {
    UsbSig s(fs, 12e6, 0);
    s.Idle(10);
    std::vector<int> d;
    for (int i = 0; i < 64; i++) d.push_back((i * 7) & 0xff);
    s.Data(PidData0, d);
    s.Idle(10);
    auto v = Run(s.st, fs, SpeedAuto);
    bool ok = v.size() == 66 && v[0] == "DATA0" && v[65] == "CRC";
    for (int i = 0; ok && i < 64; i++) { char b[4]; snprintf(b, 4, "%02X", (i * 7) & 0xff); ok = v[1 + i] == b; }
    Check("64 byte data packet", ok);
    if (!ok) { for (auto& x : v) printf("[%s] ", x.c_str()); printf("\n"); }
  }
  // swapped lines: decoded, with a warning
  {
    UsbSig s(fs, 12e6, 0);
    s.Idle(10);
    for (int k = 0; k < 3; k++) { s.Token(PidIn, 2, 1); s.Idle(10); s.Handshake(PidNak); s.Idle(10); }
    for (auto& x : s.st) x = (uint8_t)(((x & 1) << 1) | ((x >> 1) & 1));
    auto v = Run(s.st, fs, SpeedAuto);
    bool ok = v.size() == 13 && v[0] == "D+/D-?" && v[1] == "IN";
    Check("swapped D+ / D-: warning, decoded", ok);
    if (!ok) { for (auto& x : v) printf("[%s] ", x.c_str()); printf("\n"); }
  }
  // bit rate 0.5 % off, edges moved by +-1 sample, skew 2 samples
  {
    UsbSig s(fs, 12e6 * 1.005, 2);
    s.Idle(10);
    for (int k = 0; k < 4; k++)
    {
      s.Token(PidOut, 9, 2); s.Idle(3);
      s.Data(PidData0, { 0xff, 0x00, 0xaa, 0x55, 0xfe, 0x7f }); s.Idle(3);
      s.Handshake(PidAck); s.Idle(7);
    }
    srand(5);
    for (size_t i = 1; i + 1 < s.st.size(); i++)
      if (s.st[i] != s.st[i - 1] && (rand() & 1)) { s.st[i - 1] = s.st[i]; i++; }
    auto v = Run(s.st, fs, SpeedAuto);
    std::vector<std::string> e;
    for (int k = 0; k < 4; k++)
      for (const char* x : { "OUT", "A9.2", "CRC", "DATA0", "FF", "00", "AA", "55", "FE", "7F", "CRC", "ACK" }) e.push_back(x);
    Expect("FS 0.5% off, jitter, skew 2", v, e);
  }
  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures != 0;
}

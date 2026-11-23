// Host test of the generator tables: every test signal is decoded by the
// decoder of the logic analyzer.
//   g++ -std=c++17 -I../source test_gen.cpp && ./a.out
#include <cstdio>
#include <vector>
#include "gen.h"

int fails = 0;
void Check(bool c, const char* what) { if (!c) { printf("FAIL: %s\n", what); fails++; } }

uint32_t tab[Gen::MaxTab];
Dec::TFrame frames[1024];
Dec::TStats st;

// the output on lane C (state bit 0) sampled at rate: before the table the
// level s.first, sample i of the table from (i + 1) * div / 72 MHz
int Decode(const Gen::TSeq& s, const Dec::TCfg& d, double rate)
{
  std::vector<uint32_t> ev;
  int level = s.first;
  ev.push_back(La::Event(level ? 1 : 0, 0));
  uint32_t t0 = 100;    // fired at sample 100
  for (int i = 0; i < s.n; i++)
  {
    int l = (int)tab[i];
    if (l == level) continue;
    level = l;
    uint32_t t = t0 + (uint32_t)((double)(i + 1) * s.div / 72e6 * rate);
    if (La::Time(ev.back()) == t) ev.back() = La::Event(l, t);
    else ev.push_back(La::Event(l, t));
  }
  uint32_t end = t0 + (uint32_t)((double)(s.n + 2) * s.div / 72e6 * rate);
  bool ovf;
  return Dec::Decode(ev.data(), ev.size(), end, (float)rate, d, frames, 1024, st, ovf);
}

Dec::TCfg Dc(int proto)
{
  Dec::TCfg d;
  Dec::Defaults(d);
  d.proto = (int8_t)proto;
  return d;
}

int main()
{
  Gen::TCfg g;
  Gen::Defaults(g);
  g.mode = Gen::GTest;
  char s[96];

  // UART text, 115200 and 9600 even 2 stop, inverted
  for (int c = 0; c < 2; c++)
  {
    Dec::TCfg d = Dc(Dec::PUart);
    d.lane[Dec::PUart][1] = -1;
    if (c) { d.uartBaud = 6; d.uartParity = 1; d.uartStop = 2; d.inv[Dec::PUart] = 1; }
    Gen::TSeq q;
    bool ok = Gen::Build(g, d, tab, q);
    int n = ok ? Decode(q, d, 4e6) : 0;
    const char* txt = "Hello DS203!\r\n";
    bool same = n == 14;
    for (int i = 0; same && i < 14; i++) same = frames[i].v == (uint8_t)txt[i] && !frames[i].flags;
    sprintf(s, "uart %d: ok %d k %d div %u n %d frames %d baud %u", c, ok, q.perBit, q.div, q.n, n, st.baud);
    Check(ok && same, s);
  }
  // UART value bytes
  {
    Dec::TCfg d = Dc(Dec::PUart);
    g.payload = 1; g.value = 0x12345678;
    Gen::TSeq q;
    Gen::Build(g, d, tab, q);
    int n = Decode(q, d, 4e6);
    Check(n == 4 && frames[0].v == 0x12 && frames[3].v == 0x78, "uart value bytes");
    g.payload = 0;
  }
  // LIN
  {
    Dec::TCfg d = Dc(Dec::PLin);
    g.value = 0x00b0a03c;
    Gen::TSeq q;
    bool ok = Gen::Build(g, d, tab, q);
    int n = ok ? Decode(q, d, 1e6) : 0;
    sprintf(s, "lin frames %d", n);
    Check(n == 6 && frames[0].kind == Dec::Break && frames[2].kind == Dec::Pid && (frames[2].v & 0x3f) == 0x3c &&
          frames[3].v == 0xa0 && frames[4].v == 0xb0 && frames[5].kind == Dec::Checksum && frames[5].aux == 2, s);
  }
  // CAN standard and extended, 125k and 500k
  for (int c = 0; c < 2; c++)
  {
    Dec::TCfg d = Dc(Dec::PCan);
    d.canRate = c ? 9 : 7;
    g.value = c ? 0x18daf110 : 0x123;
    Gen::TSeq q;
    bool ok = Gen::Build(g, d, tab, q);
    int n = ok ? Decode(q, d, 8e6) : 0;
    int ids = 0, crc = 0, ack = 0, data = 0;
    for (int i = 0; i < n; i++)
    {
      if (frames[i].kind == Dec::CanId && frames[i].v == g.value) ids++;
      if (frames[i].kind == Dec::CanCrc && !frames[i].flags) crc++;
      if (frames[i].kind == Dec::CanAck && !frames[i].flags) ack++;
      if (frames[i].kind == Dec::Data) data++;
    }
    sprintf(s, "can %d: frames %d id %d crc %d ack %d data %d baud %u k %d", c, n, ids, crc, ack, data, st.baud, q.perBit);
    Check(ok && ids == 1 && crc == 1 && ack == 1 && data == 4, s);
  }
  // 1-Wire
  {
    Dec::TCfg d = Dc(Dec::POneWire);
    g.value = 0x44;
    Gen::TSeq q;
    Gen::Build(g, d, tab, q);
    int n = Decode(q, d, 1e6);
    sprintf(s, "1-wire frames %d", n); if (n != 4) for (int i = 0; i < n; i++) printf("  k %d v %x f %d aux %x t %u\n", frames[i].kind, frames[i].v, frames[i].flags, frames[i].aux, frames[i].t0);
    Check(n == 4 && frames[0].kind == Dec::Reset && !frames[0].flags && frames[1].kind == Dec::Presence &&
          frames[2].v == 0xcc && frames[3].v == 0x44 && (frames[3].aux & Dec::AuxFunc), s);
  }
  // NEC
  {
    Dec::TCfg d = Dc(Dec::PNec);
    g.value = 0x0804;
    Gen::TSeq q;
    Gen::Build(g, d, tab, q);
    int n = Decode(q, d, 100e3);
    sprintf(s, "nec frames %d", n);
    Check(n == 3 && frames[1].kind == Dec::NecAddr && frames[1].v == 4 && frames[2].kind == Dec::NecCmd && frames[2].v == 8 && !frames[2].flags, s);
  }
  // burst: counter and pulse decoders
  {
    Dec::TCfg d = Dc(Dec::PCounter);
    g.mode = Gen::GBurst; g.count = 25; g.freqHz = 10000; g.duty = 30;
    Gen::TSeq q;
    Gen::Build(g, d, tab, q);
    Decode(q, d, 1e6);
    sprintf(s, "burst: counter %u real %.1f", st.edges, q.real);
    Check(st.edges == 25 && q.real > 9990 && q.real < 10010, s);
    d = Dc(Dec::PPulse);
    int n = Decode(q, d, 1e6);
    sprintf(s, "burst: pulses %d width %u", n, n ? frames[0].v : 0);
    Check(n == 25 && frames[0].v >= 29 && frames[0].v <= 31, s);
  }
  // single pulse
  {
    g.mode = Gen::GPulse; g.pulseUs = 2500; g.pol = 0;
    Dec::TCfg d = Dc(Dec::PPulse);
    Gen::TSeq q;
    Gen::Build(g, d, tab, q);
    // fired: active at once, the table ends it after div
    std::vector<uint32_t> ev{ La::Event(0, 0), La::Event(1, 100), La::Event(0, 100 + (uint32_t)(q.div / 72.0)) };
    bool ovf;
    int n = Dec::Decode(ev.data(), ev.size(), 10000, 1e6f, d, frames, 1024, st, ovf);
    sprintf(s, "pulse %d width %u", n, n ? frames[0].v : 0);
    Check(n == 1 && frames[0].v == 2500, s);
  }
  // not possible
  {
    g.mode = Gen::GTest;
    Gen::TSeq q;
    Dec::TCfg d = Dc(Dec::PI2c);
    Check(!Gen::Build(g, d, tab, q) && q.err, "i2c: needs 2 lines");
    uint32_t psc, arr; float real;
    Check(Gen::SquarePlan(1000, psc, arr, real) && real > 999.9f && real < 1000.1f, "square 1 kHz");
    Check(Gen::SquarePlan(1, psc, arr, real) && real > 0.99f && real < 1.01f, "square 1 Hz");
  }
  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails != 0;
}

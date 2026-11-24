#pragma once
// USB 1.1 decoder of the sampled D+ / D- lines (low speed 1.5 Mbit/s, full
// speed 12 Mbit/s), shared by the oscilloscope (58_gabmini, Decode) and the
// logic analyzer (66_logic).
//
// After the USB analyzer of Petteri Aimonen for the DSO Quad
// (github.com/PetteriAimonen/dso-quad-usb-analyzer, GPL v3): there the FPGA
// samples the lines at 72 MHz (PHY of R. Usselmann: glitch filter, DPLL
// resynchronized on every edge, SYNC pattern, NRZI, bit unstuffing, SE0 as
// EOP, reset after 2.5 us of SE0) and a Pawn program shows the packets
// (PID check, address / endpoint of tokens, setup requests). Here the same
// steps run in software on a captured buffer:
//
// - the lines are J, K, SE0 or SE1 (J: D+ high at full speed, D- high at low
//   speed); J / K runs shorter than half a bit and SE0 / SE1 shorter than
//   0.8 bit are glitches (crossover of the two lines), the edge is put in
//   their middle
// - a packet starts with J -> K; every run of n bits is a 0 (the change)
//   and n - 1 ones (NRZI); the time base is resynchronized on every edge and
//   the bit time is refined by the 7 edges of the SYNC pattern
// - after 6 ones the stuffed 0 is dropped, 7 ones are a stuffing error
// - the bytes come LSB first: SYNC (0x80), PID (with its inverted copy),
//   then the fields of the packet type up to SE0 (EOP)
// - token (OUT, IN, SETUP, PING): address, endpoint, CRC5; SOF: frame
//   number, CRC5; data: bytes, CRC16; handshake: the PID only. The 8 data
//   bytes after a SETUP token are shown as the fields of the request.
// - SE0 of 2.5 us or more: reset; a single EOP outside of a packet:
//   keep-alive (low speed)
//
// The input is an adapter class (no hardware access, host tests:
// 58_gabmini/tests/decode_test.cpp, 66_logic/test/test_decode.cpp):
//   uint32_t Begin(), End()      samples [Begin, End)
//   int State(uint32_t t)        bit 0: D+, bit 1: D-
//   uint32_t Next(uint32_t t)    the first sample after t with another
//                                state (End(): none)
// The output: bool Add(const TItem&), false stops the decoder (full).

#include <stdint.h>

namespace Usb
{
  enum ESpeed { SpeedAuto, SpeedFull, SpeedLow };

  enum EKind {
    KPid,         // v: PID (4 bits); the box covers SYNC and PID
    KAddr,        // v: address | endpoint << 7 (tokens)
    KFrame,       // v: frame number (SOF)
    KData,        // v: byte
    KCrc5,        // v: received CRC
    KCrc16,
    KReqType,     // setup request, v: bmRequestType
    KRequest,     //   v: bRequest | bmRequestType << 8
    KValue,       //   v: wValue | bRequest << 16 | bmRequestType << 24
    KIndex,       //   v: wIndex | bRequest << 16 | bmRequestType << 24
    KLength,      //   v: wLength
    KReset,       // SE0 >= 2.5 us
    KKeepAlive,   // EOP without a packet
    KError        // v: EError
  };
  enum EError { ESync, EStuff, EBits, ELength, ENoEop, ESe1, ESwap };

  enum EPid { PidOut = 1, PidAck = 2, PidData0 = 3, PidPing = 4, PidSof = 5, PidNyet = 6,
    PidData2 = 7, PidSplit = 8, PidIn = 9, PidNak = 10, PidData1 = 11, PidPre = 12,
    PidSetup = 13, PidStall = 14, PidMdata = 15 };

  struct TItem
  {
    uint32_t t0, t1;    // samples, first and last
    uint32_t v;
    uint8_t kind;
    uint8_t err;
  };

  struct TResult
  {
    uint32_t rate;      // bit rate decoded with (0: no signal)
    bool slow;          // less than 3 samples per bit
    bool overflow;      // the output is full
  };

  inline bool IsToken(int pid) { return pid == PidOut || pid == PidIn || pid == PidSetup || pid == PidPing; }
  inline bool IsData(int pid) { return pid == PidData0 || pid == PidData1 || pid == PidData2 || pid == PidMdata; }
  inline bool IsHandshake(int pid) { return pid == PidAck || pid == PidNak || pid == PidStall || pid == PidNyet; }

  // CRC5 of n bits (LSB first), as sent in a token
  inline uint32_t Crc5(uint32_t data, int n)
  {
    uint32_t crc = 0x1f;
    for (int i = 0; i < n; i++)
      crc = (((crc ^ (data >> i)) & 1) ? (crc >> 1) ^ 0x14 : crc >> 1);
    return crc ^ 0x1f;
  }

  // CRC16 register (start 0xFFFF); the bytes of a data packet with its CRC
  // leave 0xB001
  inline uint32_t Crc16Step(uint32_t crc, uint32_t byte)
  {
    crc ^= byte;
    for (int k = 0; k < 8; k++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    return crc;
  }

  template <class In, class Out>
  class CDecoder
  {
    enum { SymSe0, SymJ, SymK, SymSe1 };
    struct TRun { uint32_t t0, t1; int sym; };
    enum { BufBytes = 24 };

    In& in;
    Out& out;
    float fs;
    uint32_t end;
    float T = 0;            // samples per bit, nominal
    uint32_t minRun = 1;    // shorter J / K runs are glitches
    uint32_t minSe = 1;     // shorter SE0 / SE1: crossover of the lines
    uint32_t resetLen = 0;  // SE0 this long is a reset
    int jState = 1;         // state of J: 1 (D+ high, full speed) or 2
    bool full = false;      // the output is full

    // run reader with one run of look-ahead
    uint32_t pos = 0;
    bool havePend = false;
    TRun pend;

    // packet being decoded
    float Tb = 0;           // bit time of this packet
    uint32_t pktT0 = 0;
    int nBits = 0, nByte = 0, ones = 0;
    uint32_t cur = 0, byteT0 = 0, lastBitT0 = 0, lastBitLen = 0;
    int pid = -1;
    bool pidOk = false;
    uint32_t pidT1 = 0;
    uint8_t buf[BufBytes];
    uint32_t bt0[BufBytes], bt1[BufBytes];
    int nb = 0;             // bytes in buf
    int flushed = 0;        // data bytes already sent (long packets)
    uint32_t crc16 = 0xffff;
    int lastToken = 0;

    int Sym(int st) const { return st == 0 ? SymSe0 : st == 3 ? SymSe1 : st == jState ? SymJ : SymK; }

    TRun Raw()
    {
      TRun r;
      r.t0 = pos;
      r.t1 = in.Next(pos);
      r.sym = Sym(in.State(pos));
      pos = r.t1;
      return r;
    }

    bool Get(TRun& r)
    {
      if (havePend) { r = pend; havePend = false; }
      else
      {
        if (pos >= end) return false;
        r = Raw();
      }
      while (pos < end)
      {
        TRun n = Raw();
        bool se = n.sym == SymSe0 || n.sym == SymSe1;
        if (n.t1 - n.t0 < (se ? minSe : minRun) && n.t1 < end)
        {
          if (pos >= end) { r.t1 = n.t1; break; }
          // a glitch: the run after it continues r, or the edge is in its middle
          TRun m = Raw();
          if (m.sym == r.sym) { r.t1 = m.t1; continue; }
          uint32_t mid = n.t0 + (n.t1 - n.t0) / 2;
          r.t1 = mid;
          m.t0 = mid;
          pend = m;
          havePend = true;
          return true;
        }
        if (n.sym == r.sym) { r.t1 = n.t1; continue; }
        pend = n;
        havePend = true;
        return true;
      }
      return true;
    }

    bool Add(uint32_t t0, uint32_t t1, int kind, uint32_t v, bool err = false)
    {
      if (full) return false;
      TItem it;
      it.t0 = t0;
      it.t1 = t1 < t0 ? t0 : t1;
      it.v = v;
      it.kind = (uint8_t)kind;
      it.err = err ? 1 : 0;
      if (!out.Add(it)) { full = true; return false; }
      return true;
    }

    // ---------------------------------------------------------- detection
    // idle state from the line after an SE0 (EOP, reset), else the state
    // of the longest run; the speed from the shortest runs
    void Detect(int speed, uint32_t begin)
    {
      const float fsBit = fs / 12e6f, lsBit = fs / 1.5e6f;
      uint32_t longest = 0, se0Min = fsBit > 2.0f ? (uint32_t)fsBit : 2;
      int longestSt = 1, afterSe0 = 0, shortRuns = 0, runs = 0, prevSt = -1;
      uint32_t prevLen = 0;
      for (uint32_t t = begin; t < end; )
      {
        int st = in.State(t);
        uint32_t n = in.Next(t), len = n - t;
        if (st == 1 || st == 2)
        {
          if (len > longest) { longest = len; longestSt = st; }
          if (prevSt == 0 && prevLen >= se0Min && !afterSe0) afterSe0 = st;
          bool inner = t != begin && n < end;
          if (inner && len >= fsBit * 0.6f && len < lsBit * 0.4f) shortRuns++;
          if (inner && len < lsBit * 8) runs++;
        }
        prevSt = st;
        prevLen = len;
        t = n;
      }
      // J: the idle state (after an EOP, else the longest); the speed: full
      // speed has runs of 1 .. 3 full speed bits
      jState = afterSe0 ? afterSe0 : longestSt;
      bool fast = speed == SpeedFull || (speed == SpeedAuto && shortRuns >= 4);
      if (speed == SpeedAuto && runs >= 8 && (fast ? jState == 2 : jState == 1))
        Add(begin, begin, KError, ESwap, true);   // decoded anyway: NRZI has no polarity
      T = fast ? fsBit : lsBit;
      rate = fast ? 12000000 : 1500000;
      minRun = (uint32_t)(T * 0.5f);
      if (minRun < 1) minRun = 1;
      minSe = (uint32_t)(T * 0.8f + 0.5f);
      if (minSe < minRun) minSe = minRun;
      resetLen = (uint32_t)(fs * 2.5e-6f);
      if (resetLen < 4 * T) resetLen = (uint32_t)(4 * T);
    }

    // ---------------------------------------------------------- packets
    void Reset1(uint32_t t0)
    {
      Tb = T;
      pktT0 = t0;
      nBits = nByte = ones = 0;
      cur = 0;
      pid = -1;
      pidOk = false;
      nb = flushed = 0;
      crc16 = 0xffff;
    }

    void SendData(int k)
    {
      Add(bt0[k], bt1[k], KData, buf[k]);
    }

    // long data packet: sends all but the last 2 bytes (maybe the CRC)
    void Flush()
    {
      int n = nb - 2;
      for (int k = 0; k < n; k++) SendData(k);
      for (int k = 0; k < 2; k++) { buf[k] = buf[n + k]; bt0[k] = bt0[n + k]; bt1[k] = bt1[n + k]; }
      nb = 2;
      flushed += n;
    }

    // a complete byte; false: not a packet (bad SYNC)
    bool Byte(uint32_t v, uint32_t t0, uint32_t t1)
    {
      int idx = nByte++;
      if (idx == 0)
      {
        if (v != 0x80) return false;
        // bit time from the SYNC edges (7 bits from the first to the last)
        float m = (float)(lastBitT0 - pktT0) / 7.0f;
        if (m > T * 0.9f && m < T * 1.1f) Tb = m;
        return true;
      }
      if (idx == 1)
      {
        pid = v & 15;
        pidOk = ((v >> 4) ^ 15) == (uint32_t)pid;
        pidT1 = t1;
        Add(pktT0, t1, KPid, pidOk ? pid : v, !pidOk);
        return true;
      }
      if (nb == BufBytes) Flush();
      buf[nb] = (uint8_t)v;
      bt0[nb] = t0;
      bt1[nb] = t1;
      nb++;
      crc16 = Crc16Step(crc16, v);
      return true;
    }

    // one bit: unstuffing, bytes; false: stop this packet
    bool Bit(int b, uint32_t t0, uint32_t len)
    {
      if (ones == 6)
      {
        ones = 0;
        if (b == 0) return true;        // the stuffed bit
        Add(t0, t0 + len - 1, KError, EStuff, true);
        return false;
      }
      ones = b ? ones + 1 : 0;
      if (nBits == 0) { byteT0 = t0; cur = 0; }
      cur |= (uint32_t)b << nBits;
      lastBitT0 = t0;
      lastBitLen = len;
      if (++nBits == 8)
      {
        nBits = 0;
        if (!Byte(cur, byteT0, t0 + len - 1))
        {
          if (!quietSync) Add(pktT0, t0 + len - 1, KError, ESync, true);
          return false;
        }
      }
      return true;
    }

    // time of the end of the first 'bits' bits of the bytes [k, k + 2)
    uint32_t Split(int k, int bits) const
    {
      return bt0[k] + (uint32_t)((float)(bt1[k + 1] + 1 - bt0[k]) * bits / 16.0f);
    }

    // the packet is complete (EOP at t)
    void Finish(uint32_t tEop)
    {
      if (nBits > 1 && nByte >= 2)
        Add(byteT0, tEop - 1, KError, EBits, true);
      if (nByte < 2)
        return;                         // only SYNC (bad SYNC is reported)
      bool rawAll = false;
      if (!pidOk || pid == PidSplit)
        rawAll = true;
      else if (IsToken(pid) || pid == PidSof)
      {
        if (nb == 2 && !flushed)
        {
          uint32_t w = buf[0] | (uint32_t)buf[1] << 8;
          uint32_t data = w & 0x7ff, crc = w >> 11;
          bool ok = Crc5(data, 11) == crc;
          uint32_t mid = Split(0, 11);
          Add(bt0[0], mid - 1, pid == PidSof ? KFrame : KAddr, data);
          Add(mid, bt1[1], KCrc5, crc, !ok);
          if (IsToken(pid)) lastToken = pid;
          return;
        }
        rawAll = true;
        Add(bt0[0], nb ? bt1[nb - 1] : pidT1, KError, ELength, true);
      }
      else if (IsData(pid))
      {
        if (nb + flushed >= 2)
        {
          bool ok = crc16 == 0xb001;
          int n = nb - 2;
          if (pid == PidData0 && lastToken == PidSetup && !flushed && n == 8)
          {
            const uint8_t* b = buf;
            uint32_t rq = (uint32_t)b[1] << 16 | (uint32_t)b[0] << 24;
            Add(bt0[0], bt1[0], KReqType, b[0]);
            Add(bt0[1], bt1[1], KRequest, b[1] | (uint32_t)b[0] << 8);
            Add(bt0[2], bt1[3], KValue, (b[2] | (uint32_t)b[3] << 8) | rq);
            Add(bt0[4], bt1[5], KIndex, (b[4] | (uint32_t)b[5] << 8) | rq);
            Add(bt0[6], bt1[7], KLength, b[6] | (uint32_t)b[7] << 8);
          }
          else
            for (int k = 0; k < n; k++) SendData(k);
          Add(bt0[nb - 2], bt1[nb - 1], KCrc16, buf[nb - 2] | (uint32_t)buf[nb - 1] << 8, !ok);
          lastToken = 0;
          return;
        }
        rawAll = true;
        Add(nb ? bt0[0] : pidT1, nb ? bt1[nb - 1] : pidT1, KError, ELength, true);
      }
      else
      {
        // handshake, PRE: the PID only
        if (nb)
        {
          Add(bt0[0], bt1[nb - 1], KError, ELength, true);
          rawAll = true;
        }
      }
      if (rawAll)
        for (int k = 0; k < nb; k++) SendData(k);
    }

    // skips to the end of the packet: after its SE0, or to a long idle
    void Skip(TRun& r, bool& have)
    {
      while ((have = Get(r)))
      {
        if (r.sym == SymSe0)
        {
          if (r.t1 - r.t0 >= resetLen) Add(r.t0, r.t1 - 1, KReset, r.t1 - r.t0);
          have = Get(r);
          return;
        }
        if (r.sym == SymJ && r.t1 - r.t0 > 8 * T) return;
      }
    }

    bool quietSync = false;

    // a packet from its first K run; leaves r at the run after it
    void Packet(TRun& r, bool& have)
    {
      Reset1(r.t0);
      while (true)
      {
        if (r.sym == SymSe0)
        {
          Finish(r.t0);
          if (r.t1 - r.t0 >= resetLen) Add(r.t0, r.t1 - 1, KReset, r.t1 - r.t0);
          have = Get(r);
          return;
        }
        if (r.sym == SymSe1)
        {
          Add(r.t0, r.t1 - 1, KError, ESe1, true);
          Skip(r, have);
          return;
        }
        uint32_t len = r.t1 - r.t0;
        int n = (int)(len / Tb + 0.5f);
        if (n < 1) n = 1;
        if (n > 7)
        {
          if (r.t1 >= end) { have = false; return; }   // cut by the end of the buffer
          if (nByte >= 2 || nBits) Add(r.t0, r.t0 + (uint32_t)(7 * Tb), KError, r.sym == SymJ ? ENoEop : EStuff, true);
          if (r.sym == SymK) Skip(r, have);
          return;                                      // J: idle, r starts the next search
        }
        float bl = (float)len / n;
        for (int k = 0; k < n; k++)
        {
          uint32_t t = r.t0 + (uint32_t)(k * bl + 0.5f);
          uint32_t tn = r.t0 + (uint32_t)((k + 1) * bl + 0.5f);
          if (!Bit(k == 0 ? 0 : 1, t, tn - t))
          {
            Skip(r, have);
            return;
          }
        }
        if (!(have = Get(r)))
          return;                                      // cut by the end of the buffer
      }
    }

  public:
    uint32_t rate = 0;

    CDecoder(In& input, Out& output, float sampleRate) : in(input), out(output), fs(sampleRate) {}

    TResult Run(int speed)
    {
      TResult res = { 0, false, false };
      uint32_t begin = in.Begin();
      end = in.End();
      if (end <= begin + 2) return res;
      Detect(speed, begin);
      res.rate = rate;
      if (T < 3.0f) { res.slow = true; return res; }
      pos = begin;
      havePend = false;
      TRun r;
      bool have = Get(r);
      // the buffer may start inside a packet: up to the first SE0 or idle
      if (have && r.sym != SymJ && r.sym != SymSe0)
      {
        quietSync = true;
        Skip(r, have);
      }
      quietSync = false;
      bool first = true;
      while (have && !full)
      {
        if (r.sym == SymK)
        {
          // a J at the start shorter than a packet gap: maybe inside a packet
          quietSync = first && r.t0 - begin < 8 * T;
          first = false;
          Packet(r, have);
          quietSync = false;
          continue;
        }
        if (r.sym == SymSe0)
        {
          uint32_t len = r.t1 - r.t0;
          if (len >= resetLen) Add(r.t0, r.t1 - 1, KReset, len);
          else if (len >= T && r.t0 > begin && r.t1 < end) Add(r.t0, r.t1 - 1, KKeepAlive, len);
        }
        else if (r.sym == SymSe1 && r.t1 - r.t0 >= T)
          Add(r.t0, r.t1 - 1, KError, ESe1, true);
        if (r.sym != SymJ) first = false;
        have = Get(r);
      }
      res.overflow = full;
      return res;
    }
  };

  // ------------------------------------------------------------ text
  inline const char* PidName(int pid)
  {
    static const char* const n[16] = { "PID0", "OUT", "ACK", "DATA0", "PING", "SOF", "NYET", "DATA2",
      "SPLIT", "IN", "NAK", "DATA1", "PRE", "SETUP", "STALL", "MDATA" };
    return n[pid & 15];
  }

  inline const char* RequestName(int type, int req)
  {
    if (((type >> 5) & 3) == 0)
      switch (req)
      {
        case 0: return "GET_STATUS"; case 1: return "CLEAR_FEATURE"; case 3: return "SET_FEATURE";
        case 5: return "SET_ADDRESS"; case 6: return "GET_DESCRIPTOR"; case 7: return "SET_DESCRIPTOR";
        case 8: return "GET_CONFIGURATION"; case 9: return "SET_CONFIGURATION";
        case 10: return "GET_INTERFACE"; case 11: return "SET_INTERFACE"; case 12: return "SYNCH_FRAME";
      }
    if (((type >> 5) & 3) == 1)
      switch (req)
      {
        // HID, CDC, mass storage
        case 0x01: return "GET_REPORT"; case 0x02: return "GET_IDLE"; case 0x03: return "GET_PROTOCOL";
        case 0x09: return "SET_REPORT"; case 0x0a: return "SET_IDLE"; case 0x0b: return "SET_PROTOCOL";
        case 0x20: return "SET_LINE_CODING"; case 0x21: return "GET_LINE_CODING";
        case 0x22: return "SET_CONTROL_LINE_STATE"; case 0x23: return "SEND_BREAK";
        case 0xfe: return "GET_MAX_LUN"; case 0xff: return "MASS_STORAGE_RESET";
      }
    return nullptr;
  }

  inline const char* DescName(int type)
  {
    switch (type)
    {
      case 1: return "DEVICE"; case 2: return "CONFIGURATION"; case 3: return "STRING";
      case 4: return "INTERFACE"; case 5: return "ENDPOINT"; case 6: return "DEVICE_QUALIFIER";
      case 7: return "OTHER_SPEED"; case 8: return "INTERFACE_POWER"; case 15: return "BOS";
      case 0x21: return "HID"; case 0x22: return "REPORT"; case 0x29: return "HUB";
    }
    return nullptr;
  }

  inline const char* ErrorName(int e, bool lng)
  {
    static const char* const s[] = { "SYNC!", "STUFF!", "BITS!", "LEN!", "EOP!", "SE1!", "D+/D-?" };
    static const char* const l[] = { "bad SYNC", "bit stuffing error", "incomplete byte before EOP",
      "wrong packet length", "no EOP", "SE1 (D+ and D- high)", "D+ / D- swapped?" };
    if (e < 0 || e > ESwap) return lng ? "error" : "!";
    return lng ? l[e] : s[e];
  }

  // text of an item (not KData: the applications format bytes); W: C(char),
  // S(text), Hex(value, digits), Dec(value)
  template <class W, class TTime>
  void Text(W& w, int kind, uint32_t v, bool err, bool lng, uint32_t samples, TTime time)
  {
    int type = (v >> 24) & 0xff, req = (v >> 16) & 0xff, val = v & 0xffff;
    bool std = ((type >> 5) & 3) == 0;
    switch (kind)
    {
      case KPid:
        if (err)
        {
          w.S(lng ? "bad PID 0x" : "PID!");
          if (lng) w.Hex(v, 2);
          break;
        }
        w.S(PidName(v));
        if (lng) { w.S(" (PID 0x"); w.Hex(v | ((~v & 15) << 4), 2); w.C(')'); }
        break;
      case KAddr:
        w.S(lng ? "address " : "A");
        w.Dec(v & 0x7f);
        w.S(lng ? " endpoint " : ".");
        w.Dec((v >> 7) & 15);
        break;
      case KFrame:
        w.S(lng ? "frame " : "F");
        w.Dec(v);
        break;
      case KCrc5: case KCrc16:
        if (lng)
        {
          w.S(kind == KCrc5 ? "CRC5 0x" : "CRC16 0x");
          w.Hex(v, kind == KCrc5 ? 2 : 4);
          w.S(err ? " bad" : " ok");
        }
        else w.S(err ? "CRC!" : "CRC");
        break;
      case KReqType:
        if (lng) { w.S("bmRequestType 0x"); w.Hex(v, 2); w.C(' '); }
        w.S((v & 0x80) ? "IN" : "OUT");
        w.C(' ');
        {
          static const char* const t[4] = { "std", "class", "vendor", "rsvd" };
          static const char* const r[4] = { "dev", "ifc", "ep", "other" };
          w.S(t[(v >> 5) & 3]);
          w.C(' ');
          w.S((v & 31) < 4 ? r[v & 31] : "?");
        }
        break;
      case KRequest:
      {
        const char* nm = RequestName((v >> 8) & 0xff, v & 0xff);
        if (lng) { w.S("bRequest 0x"); w.Hex(v & 0xff, 2); if (nm) w.C(' '); }
        if (nm) w.S(nm);
        else if (!lng) { w.S("R:"); w.Hex(v & 0xff, 2); }
        break;
      }
      case KValue:
      {
        if (lng) { w.S("wValue 0x"); w.Hex(val, 4); }
        const char* d = std && (req == 6 || req == 7) ? DescName(val >> 8) : nullptr;
        if (std && (req == 6 || req == 7))
        {
          w.S(lng ? ": " : "");
          if (d) w.S(d);
          else { w.S("type 0x"); w.Hex(val >> 8, 2); }
          w.C(' ');
          w.Dec(val & 0xff);
        }
        else if (std && req == 5) { w.S(lng ? ": address " : "addr "); w.Dec(val); }
        else if (std && req == 9) { w.S(lng ? ": configuration " : "cfg "); w.Dec(val); }
        else if (!lng) { w.S("V:"); w.Hex(val, 4); }
        break;
      }
      case KIndex:
        w.S(lng ? "wIndex 0x" : "I:");
        w.Hex(val, 4);
        if (lng && std && req == 6 && (type & 31) == 0 && val) w.S(" (language)");
        break;
      case KLength:
        w.S(lng ? "wLength " : "L:");
        w.Dec(v);
        break;
      case KReset:
        if (lng) { w.S("reset (SE0 "); time(w, samples); w.C(')'); }
        else w.S("RESET");
        break;
      case KKeepAlive:
        w.S(lng ? "keep-alive (EOP)" : "EOP");
        break;
      case KError:
        w.S(ErrorName((int)v, lng));
        break;
    }
  }
}

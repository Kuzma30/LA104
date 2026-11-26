// MINICAL.CFG: zero and gain calibration of the analog channels, shared by
// the oscilloscope (58_gabmini) and this editor. Same arithmetic as the
// Calibrator of gabmini (Framework/Calibration.h):
//   Q = line through (qin[i], qout[i]) at the vertical position
//   pixel / ADC units: Q + raw * K = 0 at 0 V, K = 2048 is the gain 1
//   volts = (Q + raw * K) * VoltsPerDiv / 65536
//
// File, one sector, little endian:
//   u32 magic "GMCA", u32 version 3, u32 checksum (djb2 *33 of the data)
//   2 channels x 8 ranges: s16 qin[3], s32 qout[3], s32 K
// Version 2 (the oscilloscope before the gain) has no K, it is read with
// K = 2048.
//
// DAC (Wave Out) calibration, a trailer right after the ADC data:
//   u32 magic "GDAC", s16 code[2], s32 uv[2], u32 checksum (djb2 of the
//   code/uv bytes). The line through (code[i], uv[i]) gives the output
//   voltage (microvolts) of a 12 bit DAC code. The oscilloscope checks the
//   checksum of the ADC data only, so it ignores the trailer; a file without
//   the trailer has the default DAC calibration.

#pragma once
#include <stdint.h>
#include <string.h>

namespace Cal
{
  enum { Ranges = 8, Points = 3, KOne = 2048, KMin = 1024, KMax = 4096 };
  enum { Magic = 0x41434d47, Version = 3 };
  // DAC: 12 bit code, reference and the supply of the output amplifier 2.8 V
  enum { DacMax = 4095, DacMagic = 0x43414447, DacTail = 20, DacMinSpan = 256 };
  enum { DacDefaultFullUv = 2800000, DacSlopeMinUv = 150, DacSlopeMaxUv = 2200 };
  static const char* const FileName = "MINICAL.CFG";
  static const char* const RangeName[Ranges] = {"50mV", "100mV", "200mV", "500mV", "1V", "2V", "5V", "10V"};
  static const int32_t RangeMv[Ranges] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};

  struct TCurve
  {
    int16_t qin[Points];
    int32_t qout[Points];
    int32_t k;
  };

  // output voltage of the DAC: line through (code[0], uv[0]), (code[1], uv[1])
  struct TDac
  {
    int16_t code[2];
    int32_t uv[2];          // microvolts, measured with an external voltmeter
  };

  struct TData
  {
    TCurve c[2][Ranges];
    TDac dac;
  };

  // DS203 defaults of the oscilloscope (Settings.cpp): 0 V at the position
  // -24 is the ADC value 0, at 249 the value 256
  inline void Default(TCurve& c)
  {
    const int q0 = -24, q2 = 249, q1 = (q0 + q2) / 2;
    c.qin[0] = q0; c.qin[1] = q1; c.qin[2] = q2;
    c.qout[0] = 0;
    c.qout[1] = -(int32_t)((q1 - q0) * (256 << 11) / (q2 - q0));
    c.qout[2] = -(256 << 11);
    c.k = KOne;
  }

  inline void Default(TDac& d)
  {
    d.code[0] = 0;
    d.code[1] = DacMax;
    d.uv[0] = 0;
    d.uv[1] = DacDefaultFullUv;
  }

  inline void Default(TData& d)
  {
    for (int ch = 0; ch < 2; ch++)
      for (int r = 0; r < Ranges; r++)
        Default(d.c[ch][r]);
    Default(d.dac);
  }

  // as CalibrationCurve::Get of gabmini (also outside the points)
  inline int32_t Q(const TCurve& c, int pos)
  {
    int b = 0, e = Points - 1;
    while (e - b > 1)
    {
      int m = (b + e) >> 1;
      if (pos >= c.qin[m])
        b = m;
      else
        e = m;
    }
    int32_t t = (int32_t)(pos - c.qin[b]);
    t *= c.qout[e] - c.qout[b];
    t /= c.qin[e] - c.qin[b];
    return t + c.qout[b];
  }

  // ADC value of 0 V at a position
  inline float Zero(const TCurve& c, int pos)
  {
    return -(float)Q(c, pos) / c.k;
  }

  // volts of a raw value (also fractional, a mean)
  inline float Volts(const TCurve& c, int pos, int range, float raw)
  {
    return ((float)Q(c, pos) + raw * c.k) * (RangeMv[range] / 1000.0f) / 65536.0f;
  }

  // 0 V moved by d/256 of an ADC step (positive: up) at every position
  inline void MoveZero(TCurve& c, int32_t d256)
  {
    for (int i = 0; i < Points; i++)
      c.qout[i] -= (int32_t)((int64_t)d256 * c.k / 256);
  }

  // new gain, the zero stays where it is
  inline void SetGain(TCurve& c, int32_t k)
  {
    if (k < KMin) k = KMin;
    if (k > KMax) k = KMax;
    for (int i = 0; i < Points; i++)
      c.qout[i] = (int32_t)((int64_t)c.qout[i] * k / c.k);
    c.k = k;
  }

  // zero from measured points: pos[i], mean ADC value in 1/2048 (n of them,
  // ascending positions). With fewer than three the line goes through the
  // measured ones; with one point the slope of the old curve is kept.
  inline void SetZero(TCurve& c, const int16_t* pos, const int32_t* mean2048, int n)
  {
    if (n <= 0)
      return;
    int32_t q[Points];
    for (int i = 0; i < n; i++)
      q[i] = -(int32_t)((int64_t)mean2048[i] * c.k / 2048);
    if (n == Points)
    {
      for (int i = 0; i < Points; i++)
      {
        c.qin[i] = pos[i];
        c.qout[i] = q[i];
      }
      return;
    }
    int16_t p0 = pos[0], p1;
    int32_t q0 = q[0], q1;
    if (n >= 2)
    {
      p1 = pos[n - 1];
      q1 = q[n - 1];
    } else
    {
      // slope of the old curve through the measured point
      int32_t slope = (c.qout[Points - 1] - c.qout[0]) / (c.qin[Points - 1] - c.qin[0]);
      p1 = p0 < 128 ? p0 + 128 : p0 - 128;
      q1 = q0 + slope * (p1 - p0);
      if (p1 < p0)
      {
        int16_t tp = p0; p0 = p1; p1 = tp;
        int32_t tq = q0; q0 = q1; q1 = tq;
      }
    }
    for (int i = 0; i < Points; i++)
    {
      int16_t p = p0 + (p1 - p0) * i / (Points - 1);
      c.qin[i] = p;
      c.qout[i] = q0 + (int32_t)((int64_t)(q1 - q0) * (p - p0) / (p1 - p0));
    }
  }

  inline bool Valid(const TCurve& c)
  {
    for (int i = 1; i < Points; i++)
      if (c.qin[i] <= c.qin[i - 1])
        return false;
    return c.k >= KMin && c.k <= KMax;
  }

  // ---- DAC: code <-> output voltage -------------------------------------
  // The two points can be anywhere on the linear part of the output (the
  // amplifier does not reach the rails, so 0 and the full scale are not the
  // best choice); in any order of the codes.
  inline bool Valid(const TDac& d)
  {
    if (d.code[0] < 0 || d.code[0] > DacMax || d.code[1] < 0 || d.code[1] > DacMax)
      return false;
    int64_t span = (int64_t)d.code[1] - d.code[0];
    int64_t dv = (int64_t)d.uv[1] - d.uv[0];
    if (span < 0)
    {
      span = -span;
      dv = -dv;
    }
    if (span < DacMinSpan)
      return false;
    if (d.uv[0] < -1000000 || d.uv[0] > 4000000 || d.uv[1] < -1000000 || d.uv[1] > 4000000)
      return false;
    return dv >= span * DacSlopeMinUv && dv <= span * DacSlopeMaxUv;
  }

  // output voltage in microvolts of a DAC code (also outside the points)
  inline int32_t DacUv(const TDac& d, int code)
  {
    int64_t n = (int64_t)(d.uv[1] - d.uv[0]) * (code - d.code[0]);
    int64_t den = d.code[1] - d.code[0];
    if (den < 0)
    {
      n = -n;
      den = -den;
    }
    return d.uv[0] + (int32_t)(n >= 0 ? (n + den / 2) / den : -((-n + den / 2) / den));
  }

  // DAC code for the wanted voltage in microvolts, limited to 0..DacMax
  inline int DacCode(const TDac& d, int32_t uv)
  {
    int64_t n = (int64_t)(uv - d.uv[0]) * (d.code[1] - d.code[0]);
    int64_t den = (int64_t)d.uv[1] - d.uv[0];
    if (den < 0)
    {
      n = -n;
      den = -den;
    }
    int64_t c = d.code[0] + (n >= 0 ? (n + den / 2) / den : -((-n + den / 2) / den));
    return c < 0 ? 0 : c > DacMax ? DacMax : (int)c;
  }

  // microvolts per DAC code
  inline float DacSlope(const TDac& d)
  {
    return (float)((int64_t)d.uv[1] - d.uv[0]) / ((int64_t)d.code[1] - d.code[0]);
  }

  // point idx (0 low, 1 high) from the code on the output and the voltage
  // measured there; false (nothing changed): points too close or a gain that
  // is not plausible
  inline bool SetDacPoint(TDac& d, int idx, int code, int32_t uv)
  {
    TDac t = d;
    t.code[idx] = (int16_t)code;
    t.uv[idx] = uv;
    if (!Valid(t))
      return false;
    d = t;
    return true;
  }

  inline uint32_t Sum(const uint8_t* p, int n)
  {
    uint32_t s = 5381;
    for (int i = 0; i < n; i++)
      s = s * 33 + p[i];
    return s;
  }

  // to a sector (512 bytes), returns the length of the file
  inline int Write(const TData& d, uint8_t* p)
  {
    memset(p, 0, 512);
    uint8_t* q = p + 12;
    for (int ch = 0; ch < 2; ch++)
      for (int r = 0; r < Ranges; r++)
      {
        const TCurve& c = d.c[ch][r];
        memcpy(q, c.qin, sizeof(c.qin)); q += sizeof(c.qin);
        memcpy(q, c.qout, sizeof(c.qout)); q += sizeof(c.qout);
        memcpy(q, &c.k, 4); q += 4;
      }
    uint32_t head[3] = {Magic, Version, Sum(p + 12, (int)(q - p - 12))};
    memcpy(p, head, 12);
    // DAC trailer, outside the checksum of the ADC data
    uint32_t magic = DacMagic;
    memcpy(q, &magic, 4); q += 4;
    uint8_t* body = q;
    memcpy(q, d.dac.code, sizeof(d.dac.code)); q += sizeof(d.dac.code);
    memcpy(q, d.dac.uv, sizeof(d.dac.uv)); q += sizeof(d.dac.uv);
    uint32_t sum = Sum(body, (int)(q - body));
    memcpy(q, &sum, 4); q += 4;
    return (int)(q - p);
  }

  // from a sector, false: d not changed
  inline bool Read(TData& d, const uint8_t* p)
  {
    uint32_t head[3];
    memcpy(head, p, 12);
    if (head[0] != Magic || (head[1] != 2 && head[1] != 3))
      return false;
    int item = (int)(sizeof(int16_t) * Points + sizeof(int32_t) * Points) + (head[1] == 3 ? 4 : 0);
    if (Sum(p + 12, 2 * Ranges * item) != head[2])
      return false;
    TData t;
    const uint8_t* q = p + 12;
    for (int ch = 0; ch < 2; ch++)
      for (int r = 0; r < Ranges; r++)
      {
        TCurve& c = t.c[ch][r];
        memcpy(c.qin, q, sizeof(c.qin)); q += sizeof(c.qin);
        memcpy(c.qout, q, sizeof(c.qout)); q += sizeof(c.qout);
        c.k = KOne;
        if (head[1] == 3)
        {
          memcpy(&c.k, q, 4); q += 4;
        }
        if (!Valid(c))
          Default(c);
      }
    // DAC trailer after the ADC data (absent or damaged: default DAC)
    Default(t.dac);
    uint32_t magic;
    memcpy(&magic, q, 4);
    if (magic == DacMagic && (q - p) + DacTail <= 512)
    {
      uint32_t sum;
      memcpy(&sum, q + DacTail - 4, 4);
      if (Sum(q + 4, DacTail - 8) == sum)
      {
        TDac dac;
        memcpy(dac.code, q + 4, sizeof(dac.code));
        memcpy(dac.uv, q + 4 + sizeof(dac.code), sizeof(dac.uv));
        if (Valid(dac))
          t.dac = dac;
      }
    }
    d = t;
    return true;
  }
}

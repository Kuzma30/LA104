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

#pragma once
#include <stdint.h>
#include <string.h>

namespace Cal
{
  enum { Ranges = 8, Points = 3, KOne = 2048, KMin = 1024, KMax = 4096 };
  enum { Magic = 0x41434d47, Version = 3 };
  static const char* const FileName = "MINICAL.CFG";
  static const char* const RangeName[Ranges] = {"50mV", "100mV", "200mV", "500mV", "1V", "2V", "5V", "10V"};
  static const int32_t RangeMv[Ranges] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};

  struct TCurve
  {
    int16_t qin[Points];
    int32_t qout[Points];
    int32_t k;
  };

  struct TData
  {
    TCurve c[2][Ranges];
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

  inline void Default(TData& d)
  {
    for (int ch = 0; ch < 2; ch++)
      for (int r = 0; r < Ranges; r++)
        Default(d.c[ch][r]);
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
    d = t;
    return true;
  }
}

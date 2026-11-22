#pragma once
// Frequency response: plans of the generator and of the sampling, single
// bin DFT, math without libm. No hardware access (host tests).

#include <stdint.h>

namespace Fra
{
  // ------------------------------------------------------------ math
  const float Pi = 3.14159265358979f;

  inline float Abs(float x) { return x < 0 ? -x : x; }

  // sin on [-pi, pi] after reduction, error < 2e-7
  inline float Sin(float x)
  {
    const float TwoPi = 2 * Pi;
    float k = x / TwoPi;
    int n = (int)(k < 0 ? k - 0.5f : k + 0.5f);
    x -= n * TwoPi;
    // to [-pi/2, pi/2]
    if (x > Pi / 2) x = Pi - x;
    else if (x < -Pi / 2) x = -Pi - x;
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.6666667e-1f + x2 * (8.3333331e-3f + x2 * (-1.9841270e-4f + x2 * 2.7557314e-6f))));
  }

  inline float Cos(float x) { return Sin(x + Pi / 2); }

  inline float Sqrt(float v)
  {
    if (v <= 0) return 0;
    float r = v > 1 ? v : 1;
    for (int i = 0; i < 30; i++)
    {
      float n = 0.5f * (r + v / r);
      if (Abs(n - r) <= r * 1e-7f) { r = n; break; }
      r = n;
    }
    return r;
  }

  // atan2 in radians, error ~1e-5
  inline float Atan2(float y, float x)
  {
    if (x == 0 && y == 0) return 0;
    float ax = Abs(x), ay = Abs(y);
    bool swap = ay > ax;
    float z = swap ? ax / ay : ay / ax;
    float z2 = z * z;
    float a = z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
    if (swap) a = Pi / 2 - a;
    if (x < 0) a = Pi - a;
    if (y < 0) a = -a;
    return a;
  }

  // log2 of v > 0, error ~1e-5
  inline float Log2(float v)
  {
    if (v <= 0) return -100;
    int e = 0;
    while (v >= 2) { v *= 0.5f; e++; }
    while (v < 1) { v *= 2; e--; }
    // v in [1, 2): ln((1+t)/(1-t)) series with t = (v-1)/(v+1)
    float t = (v - 1) / (v + 1), t2 = t * t;
    float ln = 2 * t * (1 + t2 * (1.0f / 3 + t2 * (1.0f / 5 + t2 * (1.0f / 7 + t2 * (1.0f / 9)))));
    return e + ln * 1.44269504f;
  }

  inline float Log10(float v) { return Log2(v) * 0.30102999f; }

  inline float Pow10(float x)
  {
    // 10^x = 2^(x log2 10)
    float y = x * 3.32192809f;
    int i = (int)y;
    if (y < i) i--;
    float r = (y - i) * 0.69314718f;  // e^r, r in [0, ln 2)
    float p = 1 + r * (1 + r * (0.5f + r * (1.0f / 6 + r * (1.0f / 24 + r * (1.0f / 120 + r / 720)))));
    for (; i > 0; i--) p *= 2;
    for (; i < 0; i++) p *= 0.5f;
    return p;
  }

  // ------------------------------------------------------------ generator
  // DAC: table of N samples with M whole periods, sample rate 72 MHz / D
  enum { DacMax = 2000, DacMinDiv = 72 };      // table length, 1 MS/s

  struct TGen
  {
    int div;          // TIM7 prescaler * reload
    int n;            // samples in the table
    int m;            // periods in the table
    float f;          // exact frequency
  };

  inline TGen PlanGen(float f)
  {
    TGen g;
    // one period in the whole table at the lowest frequencies; at least
    // DacMinDiv (1 MS/s, 5 samples per period at 200 kHz)
    float d = 72e6f / (f * DacMax);
    g.div = d < DacMinDiv ? DacMinDiv : (int)(d + 0.999f);
    if (g.div > 65536 * 16) g.div = 65536 * 16;
    float fd = 72e6f / g.div;
    float spc = fd / f;                          // samples per period
    g.m = (int)(DacMax / spc);
    if (g.m < 1) g.m = 1;
    g.n = (int)(g.m * spc + 0.5f);
    if (g.n > DacMax) { g.m--; g.n = (int)(g.m * spc + 0.5f); }
    if (g.n < 2) g.n = 2;
    g.f = g.m * fd / g.n;
    return g;
  }

  // TIM7 PSC / ARR of a divider (psc+1)*(arr+1) = div, arr <= 65535
  inline void SplitDiv(int div, int& psc, int& arr)
  {
    int p = 1;
    while (div / p > 65536) p++;
    while (div % p && p < 64) p++;   // exact if possible
    psc = p - 1;
    arr = div / p - 1;
  }

  // ------------------------------------------------------------ sampling
  // rates of the oscilloscope time bases: 72 MHz / divider
  const uint32_t RateDiv[] = { 1, 2, 3, 5, 12, 24, 48, 120, 240, 480, 1200, 2400, 4800,
    12000, 24000, 48000, 120000, 240000, 480000, 1200000, 2400000 };
  const float RateTime[] = { 200e-9f, 500e-9f, 1e-6f, 2e-6f, 5e-6f, 10e-6f, 20e-6f, 50e-6f,
    100e-6f, 200e-6f, 500e-6f, 1e-3f, 2e-3f, 5e-3f, 10e-3f, 20e-3f, 50e-3f, 100e-3f,
    200e-3f, 500e-3f, 1.0f };
  const int Rates = sizeof(RateDiv) / sizeof(RateDiv[0]);
  enum { SkipSamples = 200, MaxSamples = 3800 };

  struct TSample
  {
    int rate;         // index
    float fs;
    int ns;           // samples used: whole periods
    float cycles;
  };

  // the slowest rate with at most 64 samples per period (most periods in
  // the buffer), but the buffer (4096 samples) filled within 0.7 s at low
  // frequencies (then fewer periods, at least 2); samples cut to whole periods
  inline TSample PlanSample(float f)
  {
    TSample s;
    s.rate = Rates - 1;
    for (int i = 0; i < Rates; i++)
      if (72e6f / RateDiv[i] / f <= 64.0f) { s.rate = i; break; }
    while (s.rate > 0 && 4096.0f * RateDiv[s.rate] / 72e6f > 0.7f &&
           72e6f / RateDiv[s.rate - 1] / f <= MaxSamples / 2.0f)
      s.rate--;
    s.fs = 72e6f / RateDiv[s.rate];
    float spc = s.fs / f;
    int k = (int)(MaxSamples / spc);
    if (k < 1) k = 1;
    s.ns = (int)(k * spc + 0.5f);
    if (s.ns > MaxSamples) s.ns = MaxSamples;
    s.cycles = s.ns / spc;
    return s;
  }

  // ------------------------------------------------------------ DFT
  struct TCplx { float re, im; };

  inline TCplx Div(TCplx a, TCplx b)
  {
    float d = b.re * b.re + b.im * b.im;
    TCplx r = { 0, 0 };
    if (d > 0) { r.re = (a.re * b.re + a.im * b.im) / d; r.im = (a.im * b.re - a.re * b.im) / d; }
    return r;
  }
  inline float Mag(TCplx a) { return Sqrt(a.re * a.re + a.im * a.im); }
  inline float PhaseDeg(TCplx a) { return Atan2(a.im, a.re) * (180.0f / Pi); }

  // amplitude (peak, in input units) and phase of the component at f of
  // x[0..n) sampled at fs, Hann window, the mean removed; get(i) -> sample
  template <class F>
  inline TCplx Dft(F get, int n, float f, float fs)
  {
    float mean = 0;
    for (int i = 0; i < n; i++) mean += get(i);
    mean /= n;
    float w = 2 * Pi * f / fs, wh = 2 * Pi / n;
    float c = Cos(w), s = Sin(w), ch = Cos(wh), sh = Sin(wh);
    float pr = 1, pi = 0;             // e^{-j w i}
    float hr = 1, hi = 0;             // e^{j wh i}, Hann = 0.5 - 0.5 hr
    float sr = 0, si = 0, sw = 0;
    for (int i = 0; i < n; i++)
    {
      float win = 0.5f - 0.5f * hr;
      float v = (get(i) - mean) * win;
      sr += v * pr;
      si += v * pi;
      sw += win;
      float t = pr * c + pi * s;      // multiply by e^{-j w}
      pi = pi * c - pr * s;
      pr = t;
      t = hr * ch - hi * sh;          // multiply by e^{j wh}
      hi = hi * ch + hr * sh;
      hr = t;
      if ((i & 255) == 255)
      {
        // keep the phasors on the unit circle
        float m = Sqrt(pr * pr + pi * pi);
        pr /= m; pi /= m;
        m = Sqrt(hr * hr + hi * hi);
        hr /= m; hi /= m;
      }
    }
    // x = A cos(w i + p): sum = A/2 e^{jp} * sum(win)
    TCplx r = { 2 * sr / sw, 2 * si / sw };
    return r;
  }
}

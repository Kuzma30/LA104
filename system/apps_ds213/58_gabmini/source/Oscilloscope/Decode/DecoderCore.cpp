#include "DecoderCore.h"

namespace Decoder
{
    int Count = 0;
    bool Overflow = false;
    uint32_t DetectedRate = 0;

    const uint32_t RateUart[] = {0, 300, 1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600,
        76800, 115200, 230400, 250000, 460800, 500000, 921600, 1000000, 2000000};
    const char* const RateUartText[] = {"Auto", "300", "1200", "2400", "4800", "9600", "14400",
        "19200", "38400", "57600", "76800", "115200", "230400", "250000", "460800", "500000",
        "921600", "1M", "2M"};
    const uint32_t RateCan[] = {0, 10000, 20000, 33333, 50000, 83333, 100000, 125000, 250000,
        500000, 800000, 1000000};
    const char* const RateCanText[] = {"Auto", "10k", "20k", "33.3k", "50k", "83.3k", "100k",
        "125k", "250k", "500k", "800k", "1M"};
    const uint8_t BitsUart[] = {5, 6, 7, 8, 9};
    const char* const BitsUartText[] = {"5", "6", "7", "8", "9"};
    const uint8_t BitsSpi[] = {4, 8, 9, 10, 12, 16, 24, 32};
    const char* const BitsSpiText[] = {"4", "8", "9", "10", "12", "16", "24", "32"};

    const char* const ProtocolText[] = {"Off", "UART", "LIN", "I2C", "SPI", "1-Wire", "CAN",
        "WS2812", "NEC IR", "PS/2", "Pulse"};
    const char* const ChannelText[] = {"CH1", "CH2", "CH3", "CH4", "Off"};

    static const char* const g_roles[ProtocolCount][MaxRoles] = {
        {nullptr, nullptr, nullptr, nullptr},
        {"RX", "TX", nullptr, nullptr},
        {"Bus", nullptr, nullptr, nullptr},
        {"SCL", "SDA", nullptr, nullptr},
        {"CLK", "MOSI", "MISO", "CS"},
        {"DQ", nullptr, nullptr, nullptr},
        {"RX", nullptr, nullptr, nullptr},
        {"DIN", nullptr, nullptr, nullptr},
        {"IR", nullptr, nullptr, nullptr},
        {"CLK", "DATA", nullptr, nullptr},
        {"Input", nullptr, nullptr, nullptr},
    };

    const char* RoleName(int protocol, int role)
    {
        return g_roles[protocol][role];
    }

    bool RoleOptional(int protocol, int role)
    {
        return (protocol == Uart && role == 1) || (protocol == Spi && role >= 2);
    }

    void SetDefaults(TConfig& cfg)
    {
        cfg.protocol = Off;
        for (int p = 0; p < ProtocolCount; p++)
        {
            cfg.invert[p] = 0;
            for (int r = 0; r < MaxRoles; r++)
                cfg.ch[p][r] = ChOff;
            if (g_roles[p][0])
                cfg.ch[p][0] = Ch3;
            if (g_roles[p][1] && !RoleOptional(p, 1))
                cfg.ch[p][1] = Ch4;
        }
        cfg.ch[Spi][1] = Ch4;
        cfg.invert[Nec] = 1; // IR receivers have active low output
        cfg.uartBaud = 0;
        cfg.uartBits = 3; // 8
        cfg.uartParity = 0;
        cfg.uartStop = 0;
        cfg.linBaud = 0;
        cfg.spiMode = 0;
        cfg.spiBits = 1; // 8
        cfg.spiOrder = 0;
        cfg.canRate = 0;
        cfg.wsOrder = 0;
        cfg.format = Hex;
        cfg.band = Top;
    }

    // ------------------------------------------------------------------
    // sample access

    static uint32_t* s_samples;
    static int s_first;
    static int s_count;
    static float s_fs;

    static const uint8_t g_chBit[4] = {18, 19, 16, 17};

    struct TLine
    {
        uint8_t bit;
        uint8_t inv;
        bool valid;

        void Set(int ch, bool invert)
        {
            valid = ch >= Ch1 && ch <= Ch4;
            bit = valid ? g_chBit[ch] : 16;
            inv = invert ? 1 : 0;
        }
        inline int L(int i) const
        {
            return ((s_samples[i] >> bit) & 1) ^ inv;
        }
    };

    // index of the first sample after 'from' with different level (or s_count)
    static int NextChange(const TLine& ln, int from)
    {
        if (from >= s_count)
            return s_count;
        int level = ln.L(from);
        int i = from + 1;
        while (i < s_count && ln.L(i) == level)
            i++;
        return i;
    }

    // first index >= from with given level (or s_count)
    static int Find(const TLine& ln, int from, int level)
    {
        int i = from;
        while (i < s_count && ln.L(i) != level)
            i++;
        return i;
    }

    // Converts analog channel to logic level (bits 18/19), automatic threshold
    static void PrepareAnalog(int ch)
    {
        int shift = ch == Ch1 ? 0 : 8;
        uint32_t bit = 1u << g_chBit[ch];
        int nMin = 255, nMax = 0;
        for (int i = s_first; i < s_count; i++)
        {
            int v = (s_samples[i] >> shift) & 0xff;
            if (v < nMin) nMin = v;
            if (v > nMax) nMax = v;
        }
        int thr = (nMin + nMax) / 2;
        int hyst = (nMax - nMin) / 8;
        bool flat = nMax - nMin < 12;
        int level = 0;
        for (int i = s_first; i < s_count; i++)
        {
            int v = (s_samples[i] >> shift) & 0xff;
            if (!flat)
            {
                if (level && v < thr - hyst)
                    level = 0;
                else if (!level && v > thr + hyst)
                    level = 1;
            }
            if (level)
                s_samples[i] |= bit;
            else
                s_samples[i] &= ~bit;
        }
    }

    // ------------------------------------------------------------------
    // annotations, stored in the top byte of the samples

    static_assert(sizeof(TAnnotation) == 12, "annotation size");
    enum { AnnBytes = sizeof(TAnnotation) };
    static int s_capacity;

    TAnnotation Get(int i)
    {
        TAnnotation a;
        uint8_t* p = (uint8_t*)&a;
        const uint8_t* src = (const uint8_t*)s_samples + 3 + i * AnnBytes * 4;
        for (int k = 0; k < AnnBytes; k++)
            p[k] = src[k * 4];
        return a;
    }

    static void Set(int i, const TAnnotation& a)
    {
        const uint8_t* p = (const uint8_t*)&a;
        uint8_t* dst = (uint8_t*)s_samples + 3 + i * AnnBytes * 4;
        for (int k = 0; k < AnnBytes; k++)
            dst[k * 4] = p[k];
    }

    static bool Add(int begin, int end, int type, uint32_t value, int flags = 0, int aux = 0)
    {
        if (Count >= s_capacity - 1)
        {
            Overflow = true;
            return false;
        }
        if (begin < s_first) begin = s_first;
        if (end >= s_count) end = s_count - 1;
        if (end < begin) end = begin;
        TAnnotation a;
        a.begin = (uint16_t)begin;
        a.end = (uint16_t)end;
        a.value = value;
        a.type = (uint8_t)type;
        a.flags = (uint8_t)flags;
        a.aux = (uint16_t)aux;
        Set(Count++, a);
        return true;
    }

    static void Warn(int warning)
    {
        Add(s_first, s_count - 1, TWarning, warning, FError);
    }

    static int Popcount(uint32_t v)
    {
        int n = 0;
        while (v)
        {
            n += v & 1;
            v >>= 1;
        }
        return n;
    }

    // ------------------------------------------------------------------
    // bit rate estimation from the shortest pulses

    static float EstimateBit(const TLine& ln)
    {
        int nLimit = 1; // runs of a single sample are treated as glitches
        for (int iter = 0; iter < 8; iter++)
        {
            int nMin = 0x7fffffff;
            int p = NextChange(ln, s_first);
            while (true)
            {
                int q = NextChange(ln, p);
                if (q >= s_count)
                    break;
                int len = q - p;
                if (len > nLimit && len < nMin)
                    nMin = len;
                p = q;
            }
            if (nMin == 0x7fffffff)
                return 0;

            int nHigh = nMin + nMin * 3 / 10 + 1;
            int nRuns = 0, nSum = 0;
            p = NextChange(ln, s_first);
            while (true)
            {
                int q = NextChange(ln, p);
                if (q >= s_count)
                    break;
                int len = q - p;
                if (len >= nMin && len <= nHigh)
                {
                    nRuns++;
                    nSum += len;
                }
                p = q;
            }
            if (nRuns >= 2)
            {
                // refine with all runs up to 10 bits long
                float fBit = (float)nSum / nRuns;
                float fSumL = 0;
                int nSumN = 0;
                p = NextChange(ln, s_first);
                while (true)
                {
                    int q = NextChange(ln, p);
                    if (q >= s_count)
                        break;
                    int len = q - p;
                    int k = (int)(len / fBit + 0.5f);
                    if (k >= 1 && k <= 10)
                    {
                        fSumL += len;
                        nSumN += k;
                    }
                    p = q;
                }
                return nSumN ? fSumL / nSumN : fBit;
            }
            nLimit = nMin;
        }
        return 0;
    }

    // Returns samples per bit for given rate, auto detects when rate is 0
    static float BitLength(const TLine& ln, uint32_t rate, const uint32_t* table, int tableSize)
    {
        if (rate)
        {
            DetectedRate = rate;
            return s_fs / rate;
        }
        float fBit = EstimateBit(ln);
        if (fBit <= 0)
            return 0;
        float fRate = s_fs / fBit;
        // snap to standard rate
        for (int i = 1; i < tableSize; i++)
        {
            float r = (float)table[i];
            if (fRate > r * 0.95f && fRate < r * 1.05f)
            {
                DetectedRate = table[i];
                return s_fs / r;
            }
        }
        DetectedRate = (uint32_t)(fRate + 0.5f);
        return fBit;
    }

    // ------------------------------------------------------------------
    // UART

    static void DecodeUartLine(const TLine& ln, int rowFlag, int nBits, int nParity, int nStop, float fBit)
    {
        int nFrame = 1 + nBits + (nParity ? 1 : 0) + nStop;
        int i = s_first;
        while (!Overflow)
        {
            i = Find(ln, i, 1);
            i = Find(ln, i, 0);
            if (i >= s_count)
                break;
            int st = i;
            auto at = [&](int k) { return st + (int)((k + 0.5f) * fBit); };
            if (at(nFrame - 1) >= s_count)
                break;
            if (ln.L(at(0)))
            {
                i = st + 1; // glitch, not a start bit
                continue;
            }
            uint32_t v = 0;
            for (int k = 0; k < nBits; k++)
                if (ln.L(at(1 + k)))
                    v |= 1u << k;
            int k = 1 + nBits;
            bool bParityErr = false;
            int nParityBit = 0;
            if (nParity)
            {
                nParityBit = ln.L(at(k++));
                int ones = Popcount(v) + nParityBit;
                bParityErr = nParity == 1 ? (ones & 1) : !(ones & 1);
            }
            bool bStopOk = true;
            for (int j = 0; j < nStop; j++)
                if (!ln.L(at(k + j)))
                    bStopOk = false;

            if (v == 0 && !bStopOk && !nParityBit)
            {
                // line held low for the whole frame: break
                int e = Find(ln, at(k), 1);
                Add(st, e - 1, TBreak, 0, rowFlag);
                i = e;
                continue;
            }
            int end = st + (int)(nFrame * fBit) - 1;
            int aux = nBits | (bStopOk ? 0 : 0x100) | (bParityErr ? 0x200 : 0);
            Add(st, end, TData, v, rowFlag | ((bParityErr || !bStopOk) ? FError : 0), aux);
            i = at(k + nStop - 1); // continue from the middle of last stop bit
        }
    }

    static bool DecodeUart(const TConfig& cfg, int protocol)
    {
        TLine rx, tx;
        rx.Set(cfg.ch[protocol][0], cfg.invert[protocol]);
        tx.Set(protocol == Uart ? (int)cfg.ch[protocol][1] : (int)ChOff, cfg.invert[protocol]);
        if (!rx.valid)
        {
            Warn(WNoChannel);
            return false;
        }
        uint32_t rate = RateUart[protocol == Uart ? cfg.uartBaud : cfg.linBaud];
        float fBit = BitLength(rx, rate, RateUart, RateUartMax + 1);
        if (fBit == 0 && tx.valid)
            fBit = BitLength(tx, rate, RateUart, RateUartMax + 1);
        if (fBit == 0)
            return true; // no signal
        if (fBit < 2.5f)
        {
            Warn(WSampleRate);
            return false;
        }
        if (protocol == Lin)
        {
            DecodeUartLine(rx, 0, 8, 0, 1, fBit);
            return true;
        }
        int nBits = BitsUart[cfg.uartBits];
        int nStop = cfg.uartStop ? 2 : 1;
        DecodeUartLine(rx, 0, nBits, cfg.uartParity, nStop, fBit);
        if (tx.valid)
            DecodeUartLine(tx, FRow1, nBits, cfg.uartParity, nStop, fBit);
        return true;
    }

    // ------------------------------------------------------------------
    // LIN: UART frames, labels assigned afterwards

    static uint8_t LinChecksum(int from, int to, int seed)
    {
        int sum = seed;
        for (int j = from; j < to; j++)
        {
            TAnnotation a = Get(j);
            if (a.type != TData)
                continue;
            sum += a.value & 0xff;
            if (sum > 255)
                sum -= 255;
        }
        return (uint8_t)(~sum);
    }

    static void LinCloseFrame(int nFirstData, int nLast, int nPid, bool bClosed)
    {
        if (nFirstData < 0 || nLast < nFirstData)
            return;
        TAnnotation a = Get(nLast);
        uint8_t cs = a.value & 0xff;
        int aux = 0;
        if (LinChecksum(nFirstData, nLast, 0) == cs)
            aux = 1;
        else if (nPid >= 0 && LinChecksum(nFirstData, nLast, nPid) == cs)
            aux = 2;
        if (aux || bClosed)
        {
            a.type = TChecksum;
            a.aux = aux;
            if (!aux)
                a.flags |= FError;
            Set(nLast, a);
        }
    }

    static void PostLin(int nStart)
    {
        int state = 0, nPid = -1, nFirstData = -1, nLast = -1;
        for (int j = nStart; j < Count; j++)
        {
            TAnnotation a = Get(j);
            if (a.type == TBreak)
            {
                if (state == 3)
                    LinCloseFrame(nFirstData, nLast, nPid, true);
                state = 1;
                nPid = -1;
                nFirstData = -1;
                nLast = -1;
                continue;
            }
            if (a.type != TData)
                continue;
            switch (state)
            {
            case 1:
                a.type = TSync;
                if ((a.value & 0xff) != 0x55)
                    a.flags |= FError;
                state = 2;
                break;
            case 2:
            {
                a.type = TPid;
                int id = a.value & 0x3f;
                int b0 = id & 1, b1 = (id >> 1) & 1, b2 = (id >> 2) & 1, b3 = (id >> 3) & 1,
                    b4 = (id >> 4) & 1, b5 = (id >> 5) & 1;
                int p0 = b0 ^ b1 ^ b2 ^ b4;
                int p1 = !(b1 ^ b3 ^ b4 ^ b5);
                if ((int)(a.value & 0xff) != (id | (p0 << 6) | (p1 << 7)))
                    a.flags |= FError;
                nPid = a.value & 0xff;
                state = 3;
                break;
            }
            case 3:
                if (nFirstData < 0)
                    nFirstData = j;
                nLast = j;
                break;
            }
            Set(j, a);
        }
        if (state == 3)
            LinCloseFrame(nFirstData, nLast, nPid, false);
    }

    // ------------------------------------------------------------------
    // I2C

    static void DecodeI2c(const TConfig& cfg)
    {
        TLine scl, sda;
        scl.Set(cfg.ch[I2c][0], false);
        sda.Set(cfg.ch[I2c][1], false);
        if (!scl.valid || !sda.valid || cfg.ch[I2c][0] == cfg.ch[I2c][1])
        {
            Warn(WNoChannel);
            return;
        }
        bool bStarted = false, bAddr = false;
        int nBits = 0, nByteBegin = 0, nLastFall = -1;
        uint32_t v = 0;
        int pscl = scl.L(s_first), psda = sda.L(s_first);
        for (int i = s_first + 1; i < s_count && !Overflow; i++)
        {
            int c = scl.L(i), d = sda.L(i);
            if (c && pscl && d != psda)
            {
                if (!d)
                {
                    Add(i, i, bStarted ? TRepStart : TStart, 0, FPoint);
                    bStarted = true;
                    bAddr = true;
                } else
                {
                    Add(i, i, TStop, 0, FPoint);
                    bStarted = false;
                }
                nBits = 0;
            }
            else if (c && !pscl && bStarted)
            {
                if (nBits == 0)
                {
                    nByteBegin = nLastFall >= 0 ? nLastFall : i;
                    v = 0;
                }
                if (nBits < 8)
                {
                    v = (v << 1) | d;
                    nBits++;
                } else
                {
                    int half = nLastFall >= 0 ? i - nLastFall : 1;
                    int flags = d ? FNak : 0;
                    if (bAddr)
                        Add(nByteBegin, i + half, (v & 1) ? TAddrRead : TAddrWrite, v, flags);
                    else
                        Add(nByteBegin, i + half, TData, v, flags, 8);
                    bAddr = false;
                    nBits = 0;
                }
            }
            if (!c && pscl)
                nLastFall = i;
            pscl = c;
            psda = d;
        }
    }

    // ------------------------------------------------------------------
    // SPI

    static void DecodeSpi(const TConfig& cfg)
    {
        TLine clk, mosi, miso, cs;
        clk.Set(cfg.ch[Spi][0], false);
        mosi.Set(cfg.ch[Spi][1], false);
        miso.Set(cfg.ch[Spi][2], false);
        cs.Set(cfg.ch[Spi][3], false);
        if (!clk.valid || !mosi.valid || cfg.ch[Spi][0] == cfg.ch[Spi][1])
        {
            Warn(WNoChannel);
            return;
        }
        int nWordBits = BitsSpi[cfg.spiBits];
        bool bLsb = cfg.spiOrder != 0;
        int cpol = (cfg.spiMode >> 1) & 1, cpha = cfg.spiMode & 1;
        int nSampleLevel = cpol == cpha ? 1 : 0; // clock level after the sampling edge

        int nBits = 0, nWordFirst = 0, nLastEdge = 0, nPeriod = 0;
        uint32_t vo = 0, vi = 0;
        int pclk = clk.L(s_first);
        bool bPrevActive = cs.valid ? !cs.L(s_first) : true;
        // without CS the first word may be cut, start at the first clock idle gap
        for (int i = s_first + 1; i < s_count && !Overflow; i++)
        {
            bool bActive = cs.valid ? !cs.L(i) : true;
            int c = clk.L(i);
            if (bActive && !bPrevActive)
                nBits = 0;
            bPrevActive = bActive;
            if (!bActive)
            {
                if (nBits > 0)
                    Add(nWordFirst, i, TData, vo, FError, nBits);
                nBits = 0;
                pclk = c;
                continue;
            }
            if (c != pclk && c == nSampleLevel)
            {
                if (nBits > 0 && !cs.valid && nPeriod > 0 && i - nLastEdge > 4 * nPeriod + 2)
                    nBits = 0; // gap in clock, new word
                if (nBits == 0)
                {
                    nWordFirst = i;
                    vo = vi = 0;
                } else
                    nPeriod = i - nLastEdge;
                int bo = mosi.L(i), bi = miso.valid ? miso.L(i) : 0;
                if (bLsb)
                {
                    vo |= (uint32_t)bo << nBits;
                    vi |= (uint32_t)bi << nBits;
                } else
                {
                    vo = (vo << 1) | bo;
                    vi = (vi << 1) | bi;
                }
                nBits++;
                nLastEdge = i;
                if (nBits == nWordBits)
                {
                    int half = nPeriod / 2;
                    if (half < 1)
                        half = 1;
                    Add(nWordFirst - half, i + half - 1, TData, vo, 0, nWordBits);
                    if (miso.valid)
                        Add(nWordFirst - half, i + half - 1, TData, vi, FRow1, nWordBits);
                    nBits = 0;
                }
            }
            pclk = c;
        }
    }

    // ------------------------------------------------------------------
    // 1-Wire

    static void DecodeOneWire(const TConfig& cfg)
    {
        TLine dq;
        dq.Set(cfg.ch[OneWire][0], false);
        if (!dq.valid)
        {
            Warn(WNoChannel);
            return;
        }
        if (s_fs < 250e3f)
        {
            Warn(WSampleRate);
            return;
        }
        float fUs = s_fs / 1e6f; // samples per microsecond
        int nBits = -1, nByte = 0, nByteBegin = 0, nFuncPos = -1;
        uint32_t v = 0;
        int i = Find(dq, s_first, 1);
        while (!Overflow)
        {
            int f = Find(dq, i, 0);
            int r = Find(dq, f, 1);
            if (r >= s_count)
                break;
            float w = (r - f) / fUs;
            if (w >= 380)
            {
                // reset pulse, check for presence pulse
                int f2 = Find(dq, r, 0);
                int r2 = Find(dq, f2, 1);
                float d = (f2 - r) / fUs, w2 = (r2 - f2) / fUs;
                bool bPresence = r2 < s_count && d >= 10 && d <= 80 && w2 >= 50 && w2 <= 300;
                Add(f, r - 1, TReset, 0, bPresence ? 0 : FError);
                if (bPresence)
                {
                    Add(f2, r2 - 1, TPresence, 0);
                    r = r2;
                }
                nBits = 0;
                nByte = 0;
                nFuncPos = -1;
                v = 0;
            }
            else if (w < 120 && nBits >= 0)
            {
                int bit = w < 15 ? 1 : 0;
                if (nBits == 0)
                {
                    nByteBegin = f;
                    v = 0;
                }
                v |= (uint32_t)bit << nBits;
                if (++nBits == 8)
                {
                    int end = f + (int)(60 * fUs);
                    if (end < r)
                        end = r;
                    int aux = nByte & 0xff;
                    if (nByte == 0)
                    {
                        aux |= 0x100;   // ROM command
                        nFuncPos = v == 0xcc ? 1 : v == 0x55 ? 9 : -1;
                    } else if (nByte == nFuncPos)
                        aux |= 0x200;   // function command
                    Add(nByteBegin, end, TData, v, 0, aux);
                    nByte++;
                    nBits = 0;
                }
            }
            else if (w >= 120)
            {
                Add(f, r - 1, TData, 0, FError, 0x400);
                nBits = -1;
            }
            i = r;
        }
    }

    // ------------------------------------------------------------------
    // CAN 2.0A/B

    static int DecodeCanFrame(const TLine& ln, int nSof, float fBit)
    {
        enum { MaxBits = 128 };
        uint8_t bits[MaxBits];
        uint16_t pos[MaxBits];
        const float fSamplePoint = 0.7f;
        float fBitStart = (float)nSof;

        // reads one raw bit, resynchronizes on recessive to dominant edges
        auto readRaw = [&](int& nBegin) -> int
        {
            nBegin = (int)fBitStart;
            int sp = (int)(fBitStart + fSamplePoint * fBit);
            if (sp >= s_count)
                return -1;
            int b = ln.L(sp);
            float fNext = fBitStart + fBit;
            if (b)
            {
                int to = (int)(fNext + fBit * 0.5f);
                for (int k = sp + 1; k < to && k < s_count; k++)
                    if (!ln.L(k))
                    {
                        fNext = (float)k;
                        break;
                    }
            }
            fBitStart = fNext;
            return b;
        };

        int n = 0, nSame = 0, nLast = -1, nTotal = MaxBits;
        int nIde = 0, nDlcPos = 0, nRtrPos = 0, nBytes = 0;
        while (n < nTotal)
        {
            int p;
            int b = readRaw(p);
            if (b < 0)
                return s_count;
            if (nSame == 5)
            {
                if (b == nLast)
                {
                    Add(p, p + (int)fBit, TWarning, WStuffError, FError);
                    return p + 1;
                }
                nSame = 1;
                nLast = b;
                continue;
            }
            if (b == nLast)
                nSame++;
            else
            {
                nSame = 1;
                nLast = b;
            }
            bits[n] = (uint8_t)b;
            pos[n] = (uint16_t)p;
            n++;
            if (n == 14)
            {
                nIde = bits[13];
                nRtrPos = nIde ? 32 : 12;
                nDlcPos = nIde ? 35 : 15;
            }
            if (n >= 14 && n == nDlcPos + 4)
            {
                int dlc = (bits[nDlcPos] << 3) | (bits[nDlcPos + 1] << 2) | (bits[nDlcPos + 2] << 1) | bits[nDlcPos + 3];
                nBytes = bits[nRtrPos] ? 0 : (dlc > 8 ? 8 : dlc);
                nTotal = nDlcPos + 4 + nBytes * 8 + 15;
            }
        }
        auto get = [&](int from, int cnt)
        {
            uint32_t v = 0;
            for (int k = 0; k < cnt; k++)
                v = (v << 1) | bits[from + k];
            return v;
        };
        int nBitLen = (int)fBit;
        auto endOf = [&](int k) { return pos[k] + nBitLen - 1; };

        uint32_t id = get(1, 11);
        if (nIde)
            id = (id << 18) | get(14, 18);
        int rtr = bits[nRtrPos];
        Add(pos[1], endOf(nRtrPos), TCanId, id, 0, rtr | (nIde << 1));
        Add(pos[nDlcPos], endOf(nDlcPos + 3), TCanDlc, get(nDlcPos, 4));
        int nData = nDlcPos + 4;
        for (int k = 0; k < nBytes; k++)
            Add(pos[nData + k * 8], endOf(nData + k * 8 + 7), TData, get(nData + k * 8, 8), 0, 8);
        int nCrcPos = nData + nBytes * 8;
        uint16_t crc = 0;
        for (int k = 0; k < nCrcPos; k++)
        {
            int nxt = bits[k] ^ ((crc >> 14) & 1);
            crc = (crc << 1) & 0x7fff;
            if (nxt)
                crc ^= 0x4599;
        }
        uint32_t rxCrc = get(nCrcPos, 15);

        // crc delimiter, ack slot, ack delimiter are not stuffed
        int pDel, pAck, pAckDel;
        int bDel = readRaw(pDel);
        int bAck = readRaw(pAck);
        int bAckDel = readRaw(pAckDel);
        bool bCrcOk = crc == rxCrc && bDel == 1;
        Add(pos[nCrcPos], bDel >= 0 ? pDel + nBitLen - 1 : endOf(nCrcPos + 14), TCanCrc, rxCrc, bCrcOk ? 0 : FError);
        if (bAck < 0)
            return s_count;
        Add(pAck, pAck + nBitLen - 1, TCanAck, bAck == 0, (bAck == 0 && bAckDel == 1) ? 0 : FError);
        return bAckDel >= 0 ? pAckDel + nBitLen : s_count;
    }

    static void DecodeCan(const TConfig& cfg)
    {
        TLine ln;
        ln.Set(cfg.ch[Can][0], cfg.invert[Can]);
        if (!ln.valid)
        {
            Warn(WNoChannel);
            return;
        }
        float fBit = BitLength(ln, RateCan[cfg.canRate], RateCan, RateCanMax + 1);
        if (fBit == 0)
            return;
        if (fBit < 3.0f)
        {
            Warn(WSampleRate);
            return;
        }
        int nIdle = (int)(7 * fBit);
        int i = s_first;
        while (!Overflow && i < s_count)
        {
            // bus idle: at least 7 recessive bits, then start of frame
            int nRun = 0;
            while (i < s_count && nRun < nIdle)
            {
                nRun = ln.L(i) ? nRun + 1 : 0;
                i++;
            }
            i = Find(ln, i, 0);
            if (i >= s_count)
                break;
            i = DecodeCanFrame(ln, i, fBit);
        }
    }

    // ------------------------------------------------------------------
    // WS2812

    static void DecodeWs2812(const TConfig& cfg)
    {
        TLine ln;
        ln.Set(cfg.ch[Ws2812][0], false);
        if (!ln.valid)
        {
            Warn(WNoChannel);
            return;
        }
        if (s_fs < 3e6f)
        {
            Warn(WSampleRate);
            return;
        }
        float fUs = s_fs / 1e6f;
        float fThr = 0.6f * fUs;    // T0H 0.4us, T1H 0.8us
        int nGap = (int)(9 * fUs);  // longer low time = reset / end of frame
        int nLed = 0, nBits = 0, nLedBegin = 0;
        uint32_t v = 0;
        int i = Find(ln, s_first, 0);
        while (!Overflow)
        {
            int r = Find(ln, i, 1);
            int f = Find(ln, r, 0);
            if (f >= s_count)
                break;
            int r2 = Find(ln, f, 1);
            if (nBits == 0)
                nLedBegin = r;
            v = (v << 1) | ((f - r) > fThr ? 1 : 0);
            nBits++;
            bool bGap = r2 - f > nGap;
            if (nBits == 24)
            {
                uint32_t a = (v >> 16) & 0xff, b = (v >> 8) & 0xff, c = v & 0xff;
                uint32_t rgb = cfg.wsOrder ? (a << 16) | (b << 8) | c : (b << 16) | (a << 8) | c;
                Add(nLedBegin, bGap ? f + (int)(0.45f * fUs) : r2 - 1, TColor, rgb, 0, nLed++);
                nBits = 0;
                v = 0;
            }
            if (bGap)
            {
                if (nBits)
                    Add(nLedBegin, f, TColor, 0, FError, nLed);
                nBits = 0;
                v = 0;
                nLed = 0;
            }
            i = r2;
        }
    }

    // ------------------------------------------------------------------
    // NEC infrared

    static void DecodeNec(const TConfig& cfg)
    {
        TLine ln;
        ln.Set(cfg.ch[Nec][0], cfg.invert[Nec]);
        if (!ln.valid)
        {
            Warn(WNoChannel);
            return;
        }
        if (s_fs < 5e3f)
        {
            Warn(WSampleRate);
            return;
        }
        float fUsPer = 1e6f / s_fs; // microseconds per sample
        auto in = [&](int n, float lo, float hi) { float us = n * fUsPer; return us >= lo && us <= hi; };
        int nBits = -1, nSeg = 0;
        uint32_t v = 0;
        int i = Find(ln, s_first, 0);
        while (!Overflow)
        {
            int r = Find(ln, i, 1);
            int f = Find(ln, r, 0);
            if (f >= s_count)
                break;
            int r2 = Find(ln, f, 1);
            int mark = f - r, space = r2 - f;
            if (in(mark, 7000, 11000))
            {
                nBits = -1;
                if (in(space, 3500, 5500))
                {
                    Add(r, r2 - 1, TLeader, 0);
                    nBits = 0;
                    v = 0;
                } else if (in(space, 1700, 2900))
                    Add(r, f + (int)(562 / fUsPer), TRepeat, 0);
            }
            else if (nBits >= 0 && nBits < 32 && in(mark, 300, 900))
            {
                int bit = in(space, 300, 900) ? 0 : in(space, 1300, 2100) ? 1 : -1;
                if (bit < 0)
                    nBits = -1;
                else
                {
                    if ((nBits & 15) == 0)
                        nSeg = r;
                    v |= (uint32_t)bit << nBits;
                    nBits++;
                    if (nBits == 16)
                    {
                        uint32_t a = v & 0xff, na = (v >> 8) & 0xff;
                        if ((a ^ na) == 0xff)
                            Add(nSeg, r2 - 1, TNecAddr, a, 0, 8);
                        else
                            Add(nSeg, r2 - 1, TNecAddr, v & 0xffff, 0, 16);
                    }
                    if (nBits == 32)
                    {
                        uint32_t c = (v >> 16) & 0xff, nc = (v >> 24) & 0xff;
                        Add(nSeg, r2 - 1, TNecCmd, c, (c ^ nc) == 0xff ? 0 : FError, 8);
                        nBits = -1;
                    }
                }
            }
            else
                nBits = -1;
            i = r2;
        }
    }

    // ------------------------------------------------------------------
    // PS/2 (device to host)

    static void DecodePs2(const TConfig& cfg)
    {
        TLine clk, dat;
        clk.Set(cfg.ch[Ps2][0], false);
        dat.Set(cfg.ch[Ps2][1], false);
        if (!clk.valid || !dat.valid || cfg.ch[Ps2][0] == cfg.ch[Ps2][1])
        {
            Warn(WNoChannel);
            return;
        }
        if (s_fs < 100e3f)
        {
            Warn(WSampleRate);
            return;
        }
        int nGap = (int)(s_fs * 150e-6f);
        int nBits = 0, nBegin = 0, nLastFall = -100000, nHalf = 1, nParity = 0;
        uint32_t v = 0;
        int pc = clk.L(s_first);
        for (int i = s_first + 1; i < s_count && !Overflow; i++)
        {
            int c = clk.L(i);
            if (pc && !c)
            {
                if (nBits > 0 && i - nLastFall > nGap)
                    nBits = 0;
                else if (nBits > 0)
                    nHalf = (i - nLastFall) / 2;
                int d = dat.L(i);
                if (nBits == 0)
                {
                    if (d == 0)
                    {
                        nBegin = i;
                        v = 0;
                        nBits = 1;
                    }
                } else if (nBits <= 8)
                {
                    v |= (uint32_t)d << (nBits - 1);
                    nBits++;
                } else if (nBits == 9)
                {
                    nParity = d;
                    nBits++;
                } else
                {
                    bool bOk = d == 1 && ((Popcount(v) + nParity) & 1) == 1;
                    Add(nBegin - nHalf, i + nHalf, TData, v, bOk ? 0 : FError, 8);
                    nBits = 0;
                }
                nLastFall = i;
            }
            pc = c;
        }
    }

    // ------------------------------------------------------------------
    // Pulse width

    static void DecodePulse(const TConfig& cfg)
    {
        TLine ln;
        ln.Set(cfg.ch[Pulse][0], cfg.invert[Pulse]);
        if (!ln.valid)
        {
            Warn(WNoChannel);
            return;
        }
        int i = Find(ln, s_first, 0);
        while (!Overflow)
        {
            int r = Find(ln, i, 1);
            int f = Find(ln, r, 0);
            if (f >= s_count)
                break;
            int r2 = Find(ln, f, 1);
            int period = r2 < s_count ? r2 - r : 0;
            if (period > 0xffff)
                period = 0;
            Add(r, f - 1, TPulse, f - r, 0, period);
            i = f;
        }
    }

    // ------------------------------------------------------------------

    static void Sort()
    {
        // insertion sort by begin, stable
        for (int i = 1; i < Count; i++)
        {
            TAnnotation t = Get(i);
            int j = i - 1;
            TAnnotation prev;
            while (j >= 0 && (prev = Get(j)).begin > t.begin)
            {
                Set(j + 1, prev);
                j--;
            }
            if (j + 1 != i)
                Set(j + 1, t);
        }
    }

    void Run(const TConfig& cfg, uint32_t* samples, int first, int count, float sampleRate)
    {
        Count = 0;
        Overflow = false;
        DetectedRate = 0;
        s_samples = samples;
        s_fs = sampleRate;
        s_first = first;
        // cut off samples that were not acquired yet
        int n = first;
        while (n < count && samples[n] != 0xffffffff)
            n++;
        s_count = n;
        s_capacity = s_count / AnnBytes;
        if (cfg.protocol == Off || cfg.protocol >= ProtocolCount || s_count - s_first < 16)
            return;

        // bits 18..23 are free: 18/19 hold the logic levels of analog channels,
        // clearing 20..23 makes sure a sample never looks like NoData
        for (int i = 0; i < s_count; i++)
            samples[i] &= 0xff03ffff;

        // analog channels -> logic levels
        bool bUsed[2] = {false, false};
        for (int r = 0; r < MaxRoles; r++)
        {
            int ch = cfg.ch[cfg.protocol][r];
            if (g_roles[cfg.protocol][r] && (ch == Ch1 || ch == Ch2) && !bUsed[ch])
            {
                bUsed[ch] = true;
                PrepareAnalog(ch);
            }
        }

        switch (cfg.protocol)
        {
        case Uart: DecodeUart(cfg, Uart); break;
        case Lin:
            if (DecodeUart(cfg, Lin))
                PostLin(0);
            break;
        case I2c: DecodeI2c(cfg); break;
        case Spi: DecodeSpi(cfg); break;
        case OneWire: DecodeOneWire(cfg); break;
        case Can: DecodeCan(cfg); break;
        case Ws2812: DecodeWs2812(cfg); break;
        case Nec: DecodeNec(cfg); break;
        case Ps2: DecodePs2(cfg); break;
        case Pulse: DecodePulse(cfg); break;
        }
        Sort();
        if (Overflow)
        {
            // the last slot is reserved for this message
            TAnnotation a;
            a.begin = a.end = (uint16_t)(s_count - 1);
            a.value = WOverflow;
            a.type = TWarning;
            a.flags = FError | FPoint;
            a.aux = 0;
            Set(Count++, a);
        }
        if (Count == 0)
            Warn(WNoSignal);
    }

    // ------------------------------------------------------------------
    // text formatting

    struct TWriter
    {
        char* p;
        char* end;

        void C(char c)
        {
            if (p < end)
                *p++ = c;
        }
        void S(const char* s)
        {
            while (*s)
                C(*s++);
        }
        void Hex(uint32_t v, int digits)
        {
            for (int i = digits - 1; i >= 0; i--)
                C("0123456789ABCDEF"[(v >> (i * 4)) & 15]);
        }
        void Dec(uint32_t v)
        {
            char tmp[11];
            int n = 0;
            do
            {
                tmp[n++] = (char)('0' + v % 10);
                v /= 10;
            } while (v);
            while (n)
                C(tmp[--n]);
        }
        void Char(uint32_t v)
        {
            C('\'');
            if (v == '\n') S("\\n");
            else if (v == '\r') S("\\r");
            else if (v == '\t') S("\\t");
            else if (v >= 32 && v < 127) C((char)v);
            else
            {
                S("\\x");
                Hex(v, 2);
            }
            C('\'');
        }
    };

    int FormatTime(float t, char* buf)
    {
        TWriter w{buf, buf + 15};
        if (t < 0)
        {
            w.C('-');
            t = -t;
        }
        const char* unit;
        float v;
        if (t < 1e-6f) { v = t * 1e9f; unit = "ns"; }
        else if (t < 1e-3f) { v = t * 1e6f; unit = "us"; }
        else if (t < 1.0f) { v = t * 1e3f; unit = "ms"; }
        else { v = t; unit = "s"; }
        int dec = v < 9.995f ? 2 : v < 99.95f ? 1 : 0;
        int scale = dec == 2 ? 100 : dec == 1 ? 10 : 1;
        int n = (int)(v * scale + 0.5f);
        w.Dec(n / scale);
        if (dec)
        {
            w.C('.');
            int frac = n % scale;
            if (dec == 2 && frac < 10)
                w.C('0');
            w.Dec(frac);
        }
        w.S(unit);
        *w.p = 0;
        return (int)(w.p - buf);
    }

    static const char* RomCommand(uint32_t v)
    {
        switch (v)
        {
        case 0x33: return "Read ROM";
        case 0x55: return "Match ROM";
        case 0xF0: return "Search ROM";
        case 0xCC: return "Skip ROM";
        case 0xEC: return "Alarm search";
        case 0x3C: return "Overdrive skip";
        case 0x69: return "Overdrive match";
        }
        return nullptr;
    }

    static const char* FunctionCommand(uint32_t v)
    {
        switch (v)
        {
        case 0x44: return "Convert T";
        case 0xBE: return "Read scratchpad";
        case 0x4E: return "Write scratchpad";
        case 0x48: return "Copy scratchpad";
        case 0xB8: return "Recall EEPROM";
        case 0xB4: return "Read power supply";
        case 0xF0: return "Read memory";
        case 0x0F: return "Write scratchpad";
        case 0xAA: return "Read scratchpad";
        case 0x5A: return "Copy scratchpad";
        }
        return nullptr;
    }

    static const char* const g_warnings[] = {"Select channels", "Sample rate too low",
        "No data", "Too many items", "Stuff error"};

    int Format(const TAnnotation& a, char* buf, int bufSize, bool longForm,
               const TConfig& cfg, float sampleRate)
    {
        TWriter w{buf, buf + bufSize - 1};
        bool bErr = (a.flags & FError) != 0;
        bool bNak = (a.flags & FNak) != 0;
        int proto = cfg.protocol;

        // row prefix for protocols with two data rows
        if (longForm && (proto == Uart || proto == Spi) && a.type == TData)
        {
            const char* role = proto == Uart ? RoleName(Uart, (a.flags & FRow1) ? 1 : 0)
                                             : RoleName(Spi, (a.flags & FRow1) ? 2 : 1);
            w.S(role);
            w.C(' ');
        }

        switch (a.type)
        {
        case TData:
        {
            int nBits = a.aux & 0xff;
            if (nBits == 0 || nBits > 32)
                nBits = 8;
            int nDigits = (nBits + 3) / 4;
            if (nDigits < 2)
                nDigits = 2;
            if (proto == OneWire && (a.aux & 0x400))
            {
                w.S(longForm ? "Bad slot" : "?");
                break;
            }
            if (longForm)
            {
                w.S("0x");
                w.Hex(a.value, nDigits);
                w.C(' ');
                w.Dec(a.value);
                if (nBits <= 8)
                {
                    w.C(' ');
                    w.Char(a.value);
                }
                if (proto == I2c)
                    w.S(bNak ? " NAK" : " ACK");
                if (proto == Uart || proto == Lin)
                {
                    if (a.aux & 0x100) w.S(" frame err");
                    if (a.aux & 0x200) w.S(" parity err");
                }
                if (proto == Ps2 && bErr)
                    w.S(" parity/stop err");
                if (proto == Spi && bErr)
                    w.S(" incomplete");
                if (proto == OneWire)
                {
                    const char* name = (a.aux & 0x100) ? RomCommand(a.value)
                                     : (a.aux & 0x200) ? FunctionCommand(a.value) : nullptr;
                    if (name)
                    {
                        w.S(" ");
                        w.S(name);
                    }
                }
                break;
            }
            if (cfg.format == Dec)
                w.Dec(a.value);
            else if (cfg.format == Ascii && nBits <= 8)
                w.Char(a.value);
            else
                w.Hex(a.value, nDigits);
            if (bNak)
                w.C('~');
            if (bErr)
                w.C('!');
            break;
        }
        case TAddrWrite:
        case TAddrRead:
            if (longForm)
            {
                w.S("Addr 0x");
                w.Hex(a.value >> 1, 2);
                w.S(a.type == TAddrRead ? " read" : " write");
                w.S(bNak ? " NAK" : " ACK");
            } else
            {
                w.C(a.type == TAddrRead ? 'R' : 'W');
                w.C(':');
                w.Hex(a.value >> 1, 2);
                if (bNak)
                    w.C('~');
            }
            break;
        case TStart: w.S(longForm ? "Start" : "S"); break;
        case TRepStart: w.S(longForm ? "Repeated start" : "Sr"); break;
        case TStop: w.S(longForm ? "Stop" : "P"); break;
        case TBreak: w.S(longForm ? "Break" : "BRK"); break;
        case TSync:
            if (longForm)
            {
                w.S("Sync 0x");
                w.Hex(a.value, 2);
                if (bErr)
                    w.S(" bad");
            } else
                w.S(bErr ? "SYN!" : "SYN");
            break;
        case TPid:
            w.S(longForm ? "PID 0x" : "I:");
            if (longForm)
            {
                w.Hex(a.value, 2);
                w.S(" id 0x");
            }
            w.Hex(a.value & 0x3f, 2);
            if (bErr)
                w.S(longForm ? " parity err" : "!");
            break;
        case TChecksum:
            if (longForm)
            {
                w.S("Checksum 0x");
                w.Hex(a.value, 2);
                w.S(a.aux == 1 ? " ok classic" : a.aux == 2 ? " ok enhanced" : " bad");
            } else
                w.S(bErr ? "CS!" : "CS");
            break;
        case TReset: w.S(longForm ? (bErr ? "Reset, no presence" : "Reset") : "RST"); break;
        case TPresence: w.S(longForm ? "Presence" : "PRS"); break;
        case TCanId:
            w.S(longForm ? "ID 0x" : "I:");
            w.Hex(a.value, (a.aux & 2) ? 8 : 3);
            if (longForm)
                w.S((a.aux & 2) ? " ext" : " std");
            if (a.aux & 1)
                w.S(longForm ? " RTR" : "R");
            break;
        case TCanDlc:
            w.S(longForm ? "DLC " : "L:");
            w.Dec(a.value);
            break;
        case TCanCrc:
            if (longForm)
            {
                w.S("CRC 0x");
                w.Hex(a.value, 4);
                w.S(bErr ? " bad" : " ok");
            } else
                w.S(bErr ? "CRC!" : "CRC");
            break;
        case TCanAck: w.S(a.value ? "ACK" : "NAK"); break;
        case TColor:
            if (bErr)
            {
                w.S(longForm ? "Incomplete LED data" : "?");
                break;
            }
            if (longForm)
            {
                w.S("LED ");
                w.Dec(a.aux);
                w.S(" #");
                w.Hex(a.value, 6);
                w.S(" R");
                w.Dec((a.value >> 16) & 0xff);
                w.S(" G");
                w.Dec((a.value >> 8) & 0xff);
                w.S(" B");
                w.Dec(a.value & 0xff);
            } else
            {
                w.C('#');
                w.Hex(a.value, 6);
            }
            break;
        case TLeader: w.S(longForm ? "Leader" : "LEAD"); break;
        case TRepeat: w.S(longForm ? "Repeat" : "REP"); break;
        case TNecAddr:
            w.S(longForm ? "Address 0x" : "A:");
            w.Hex(a.value, a.aux == 16 ? 4 : 2);
            if (longForm && a.aux == 16)
                w.S(" (extended)");
            break;
        case TNecCmd:
            w.S(longForm ? "Command 0x" : "C:");
            w.Hex(a.value, 2);
            if (bErr)
                w.S(longForm ? " inverse mismatch" : "!");
            break;
        case TPulse:
        {
            char tmp[16];
            if (longForm)
                w.S("High ");
            FormatTime(a.value / sampleRate, tmp);
            w.S(tmp);
            if (longForm && a.aux)
            {
                float fPeriod = a.aux / sampleRate;
                w.S(" T ");
                FormatTime(fPeriod, tmp);
                w.S(tmp);
                w.C(' ');
                // frequency with 3 digits
                float f = 1.0f / fPeriod;
                const char* unit = "Hz";
                if (f < 1.0f) { f *= 1e3f; unit = "mHz"; }
                else if (f >= 1e6f) { f /= 1e6f; unit = "MHz"; }
                else if (f >= 1e3f) { f /= 1e3f; unit = "kHz"; }
                FormatTime(f, tmp);           // reuse the digit formatting...
                int n = 0;
                while (tmp[n] && (tmp[n] == '.' || (tmp[n] >= '0' && tmp[n] <= '9')))
                    n++;
                tmp[n] = 0;                   // ...without the time unit
                w.S(tmp);
                w.S(unit);
                w.C(' ');
                uint32_t permille = (a.value * 1000 + a.aux / 2) / a.aux;
                w.Dec(permille / 10);
                w.C('.');
                w.Dec(permille % 10);
                w.C('%');
            }
            break;
        }
        case TWarning:
            if (a.value < sizeof(g_warnings) / sizeof(g_warnings[0]))
                w.S(g_warnings[a.value]);
            break;
        }
        *w.p = 0;
        return (int)(w.p - buf);
    }
}

// PC test of the protocol decoders with synthetic signals.
//   g++ -O1 -Wall -Isource tests/decode_test.cpp source/Oscilloscope/Decode/DecoderCore.cpp
//       source/Oscilloscope/Decode/DecodeBand.cpp && ./a.out [screenshot dir]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "Oscilloscope/Decode/DecoderCore.h"
#include "Oscilloscope/Decode/DecodeBand.h"

using namespace Decoder;

static const int N = 4096;
static float fs;
static uint32_t buf[N];          // signals in the BIOS::ADC::TSample layout
static uint8_t pk[N * 3];        // packed like Sampler::memory
static int valid = N;

// packs buf like Sampler::Copy() and decodes it
static void Decode(const TConfig& cfg)
{
    for (int i = 0; i < N; i++)
    {
        pk[i * 3] = buf[i] & 0xff;
        pk[i * 3 + 1] = (buf[i] >> 8) & 0xff;
        pk[i * 3 + 2] = (buf[i] >> 16) & 3;
    }
    Run(cfg, pk, N, 0, valid, fs);
}
static int failures = 0;

// ---- signal builder, time in seconds ----
struct Sig
{
    int ch;           // 0 = CH1 analog, 1 = CH2 analog, 2 = CH3, 3 = CH4
    double t = 0;     // current time
    int level = 1;
    Sig(int c, int idle = 1) : ch(c), level(idle) { Fill(0, N, idle); }

    void Fill(int from, int to, int lv)
    {
        for (int i = from; i < to && i < N; i++)
        {
            if (i < 0) continue;
            if (ch >= 2)
            {
                uint32_t bit = 1u << (16 + ch - 2);
                buf[i] = lv ? (buf[i] | bit) : (buf[i] & ~bit);
            } else
            {
                // analog: 40 / 200 with noise
                int v = (lv ? 200 : 40) + (rand() % 21) - 10;
                int sh = ch * 8;
                buf[i] = (buf[i] & ~(0xffu << sh)) | ((uint32_t)v << sh);
            }
        }
    }
    void Hold(double dur, int lv)
    {
        int a = (int)(t * fs + 0.5), b = (int)((t + dur) * fs + 0.5);
        Fill(a, b, lv);
        t += dur;
        level = lv;
    }
    void Rest() { Fill((int)(t * fs + 0.5), N, level); }
};

static std::vector<std::string> Texts(bool longForm, const TConfig& cfg)
{
    std::vector<std::string> v;
    for (int i = 0; i < Count; i++)
    {
        char tmp[64];
        Format(Get(i), tmp, sizeof(tmp), longForm, cfg, fs);
        v.push_back(tmp);
    }
    return v;
}

static void Expect(const char* name, const TConfig& cfg, std::vector<std::string> expect)
{
    Decode(cfg);
    auto got = Texts(false, cfg);
    bool ok = got == expect;
    printf("%-14s %s  rate=%u\n", name, ok ? "OK  " : "FAIL", DetectedRate);
    auto lng = Texts(true, cfg);
    for (size_t i = 0; i < got.size(); i++)
        printf("    %4d..%4d  %-12s %s\n", Get(i).begin, Get(i).end, got[i].c_str(), lng[i].c_str());
    if (!ok)
    {
        failures++;
        printf("    expected:");
        for (auto& s : expect) printf(" [%s]", s.c_str());
        printf("\n");
    }
}

static void Clear() { memset(buf, 0, sizeof(buf)); }

// ---- screenshot of the graph area (300x200) with traces and decoded band ----
static const char* shotDir = nullptr;

static void Shot(const char* name, const TConfig& cfg, int viewBegin, int selected = -1)
{
    if (!shotDir)
        return;
    const int W = 300, H = 200;
    static uint16_t img[H][W];
    if (viewBegin < 0)
        viewBegin = Count ? Get(0).begin > 20 ? Get(0).begin - 20 : 0 : 0;
    if (viewBegin > N - W) viewBegin = N - W;
    bool band = DecodeBand::Begin(cfg, fs, viewBegin, W, selected);
    uint16_t column[H];
    int prev[4] = {-1, -1, -1, -1};
    for (int x = 0; x < W; x++)
    {
        int idx = viewBegin + x;
        for (int y = 0; y < H; y++)
            column[y] = (x % 30 == 0 && y % 5 == 0) || (y % 25 == 0 && x % 6 == 0) ? 0x8410 : 0x0101;
        // traces of the channels used by the protocol: CH1 yellow, CH2 cyan, CH3 magenta, CH4 green
        static const uint16_t clr[4] = {0x07ff, 0xffe0, 0xf81f, 0x07e0};
        for (int r = 0; r < MaxRoles; r++)
        {
            int ch = cfg.ch[cfg.protocol][r];
            if (!RoleName(cfg.protocol, r) || ch > Ch4)
                continue;
            int base = 20 + r * 40;
            int y;
            if (ch >= Ch3)
                y = base + (((buf[idx] >> (16 + ch - Ch3)) & 1) ? 25 : 0);
            else
                y = base + ((buf[idx] >> (ch * 8)) & 0xff) * 25 / 255;
            int a = prev[r] < 0 ? y : prev[r];
            for (int k = a < y ? a : y; k <= (a < y ? y : a); k++)
                column[k] = clr[ch];
            prev[r] = y;
        }
        if (band)
            DecodeBand::Column(column, H, idx);
        for (int y = 0; y < H; y++)
            img[H - 1 - y][x] = column[y];
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", shotDir, name);
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            uint16_t c = img[y][x];   // LA104 layout: red low bits, blue high bits
            uint8_t rgb[3] = {(uint8_t)((c & 31) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c >> 11) << 3)};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

// ---- protocol generators ----
static void UartByte(Sig& s, double bit, int v, int bits = 8, int parity = 0, int stop = 1)
{
    s.Hold(bit, 0);
    int ones = 0;
    for (int i = 0; i < bits; i++) { int b = (v >> i) & 1; ones += b; s.Hold(bit, b); }
    if (parity) s.Hold(bit, parity == 1 ? (ones & 1) : !(ones & 1));
    s.Hold(bit * stop, 1);
}

static void TestUart()
{
    Clear();
    fs = 2.4e6f;
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = Uart;
    cfg.ch[Uart][0] = Ch3;
    cfg.ch[Uart][1] = Ch1;      // TX on analog channel with noise
    double bit = 1.0 / 115200;
    Sig rx(2), tx(0);
    rx.Hold(20e-6, 1);
    for (const char* p = "Hi\n"; *p; p++) UartByte(rx, bit, *p);
    rx.Hold(30e-6, 1);
    UartByte(rx, bit, 0x00);
    rx.Hold(bit * 12, 0);        // break
    rx.Hold(bit * 2, 1);
    UartByte(rx, bit, 0xA5);
    rx.Rest();
    tx.Hold(50e-6, 1);
    UartByte(tx, bit, 'O');
    UartByte(tx, bit, 'K');
    tx.Rest();
    Expect("uart auto", cfg, {"48", "4F", "69", "4B", "0A", "00", "BRK", "A5"});
    Shot("uart", cfg, 30);
    cfg.format = Ascii;
    Expect("uart ascii", cfg, {"'H'", "'O'", "'i'", "'K'", "'\\n'", "'\\x00'", "BRK", "'\\xA5'"});

    // 7E2 at 9600, fixed rate, slow sample rate
    Clear();
    fs = 72e6f / 1200;   // 100us/div
    SetDefaults(cfg);
    cfg.protocol = Uart;
    cfg.uartBaud = 5; cfg.uartBits = 2; cfg.uartParity = 1; cfg.uartStop = 1;
    Sig r2(3);
    r2.Hold(1e-3, 1);
    double b96 = 1.0 / 9600;
    UartByte(r2, b96, 'A', 7, 1, 2);
    UartByte(r2, b96, 'B', 7, 2, 2); // odd parity -> error
    UartByte(r2, b96, 'C', 7, 1, 2);
    r2.Rest();
    cfg.ch[Uart][0] = Ch4;
    Expect("uart 7E2", cfg, {"41", "42!", "43"});
}

static void TestLin()
{
    Clear();
    fs = 72e6f / 240; // 50us/div... 300 kHz
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = Lin;
    double bit = 1.0 / 19200;
    Sig s(2);
    s.Hold(200e-6, 1);
    s.Hold(bit * 13, 0);
    s.Hold(bit * 1, 1);
    UartByte(s, bit, 0x55);
    int id = 0x3C;
    int b[6]; for (int i = 0; i < 6; i++) b[i] = (id >> i) & 1;
    int pid = id | ((b[0] ^ b[1] ^ b[2] ^ b[4]) << 6) | ((!(b[1] ^ b[3] ^ b[4] ^ b[5])) << 7);
    UartByte(s, bit, pid);
    int data[] = {0x12, 0x34};
    int sum = pid;
    for (int d : data) { UartByte(s, bit, d); sum += d; if (sum > 255) sum -= 255; }
    UartByte(s, bit, (~sum) & 0xff);
    s.Hold(300e-6, 1);
    s.Hold(bit * 13, 0);
    s.Hold(bit * 1, 1);
    UartByte(s, bit, 0x55);
    UartByte(s, bit, 0xBC); // bad parity
    s.Rest();
    Expect("lin", cfg, {"BRK", "SYN", "I:3C", "12", "34", "CS", "BRK", "SYN", "I:3C!"});
    Shot("lin", cfg, 380);
}

static void TestI2c()
{
    Clear();
    fs = 72e6f / 24;   // 3 MHz
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = I2c;
    Sig scl(2), sda(3);
    double q = 1.25e-6;   // quarter of 100 kHz... 200 kHz bit
    auto tick = [&](double d) { scl.t += 0; sda.t += 0; (void)d; };
    (void)tick;
    // helper writing both lines with common time base
    double t = 10e-6;
    auto set = [&](int c, int d, double dur)
    {
        scl.t = t; sda.t = t;
        scl.Hold(dur, c); sda.Hold(dur, d);
        t += dur;
    };
    set(1, 1, 10e-6);
    set(1, 0, q * 2);            // start
    auto byte = [&](int v, int ack)
    {
        for (int i = 7; i >= -1; i--)
        {
            int d = i >= 0 ? (v >> i) & 1 : !ack;
            set(0, d, q * 2);
            set(1, d, q * 2);
        }
    };
    byte(0xA0, 1);
    byte(0x10, 1);
    set(0, 1, q * 2);
    set(1, 1, q * 2);
    set(1, 0, q * 2);            // repeated start
    byte(0xA1, 1);
    byte(0x42, 0);
    set(0, 0, q * 2);
    set(1, 0, q * 2);
    set(1, 1, q * 2);            // stop
    set(1, 1, 50e-6);
    scl.Rest(); sda.Rest();
    Expect("i2c", cfg, {"S", "W:50", "10", "Sr", "R:50", "42~", "P"});
    Shot("i2c", cfg, 40);
    Shot("i2c_b", cfg, 340, 5);
}

static void TestSpi()
{
    Clear();
    fs = 72e6f / 12;  // 6 MHz
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = Spi;
    cfg.ch[Spi][0] = Ch3; cfg.ch[Spi][1] = Ch4; cfg.ch[Spi][2] = Ch1; cfg.ch[Spi][3] = Ch2;
    Sig clk(2, 0), mosi(3, 0), miso(0, 0), cs(1, 1);
    double t = 20e-6, h = 1e-6;
    auto set = [&](int c, int o, int i, int s, double d)
    {
        clk.t = mosi.t = miso.t = cs.t = t;
        clk.Hold(d, c); mosi.Hold(d, o); miso.Hold(d, i); cs.Hold(d, s);
        t += d;
    };
    set(0, 0, 0, 1, 5e-6);
    set(0, 0, 0, 0, 2e-6);
    int out[] = {0x9F, 0x00}, in[] = {0xFF, 0xEF};
    for (int k = 0; k < 2; k++)
        for (int i = 7; i >= 0; i--)
        {
            int o = (out[k] >> i) & 1, ii = (in[k] >> i) & 1;
            set(0, o, ii, 0, h);
            set(1, o, ii, 0, h);
        }
    set(0, 0, 0, 0, 2e-6);
    set(0, 0, 0, 1, 10e-6);
    clk.Rest(); mosi.Rest(); miso.Rest(); cs.Rest();
    Expect("spi mode0 cs", cfg, {"9F", "FF", "00", "EF"});
    Shot("spi", cfg, 120);

    // mode 3, no CS, 16 bit LSB first
    Clear();
    SetDefaults(cfg);
    cfg.protocol = Spi; cfg.spiMode = 3; cfg.spiBits = 5; cfg.spiOrder = 1;
    Sig c2(2, 1), d2(3, 0);
    t = 10e-6;
    auto set2 = [&](int c, int o, double d) { c2.t = d2.t = t; c2.Hold(d, c); d2.Hold(d, o); t += d; };
    set2(1, 0, 5e-6);
    int words[] = {0x1234, 0xBEEF};
    for (int w : words)
    {
        for (int i = 0; i < 16; i++)
        {
            int o = (w >> i) & 1;
            set2(0, o, h);
            set2(1, o, h);
        }
        set2(1, 0, 20e-6);
    }
    c2.Rest(); d2.Rest();
    Expect("spi mode3 lsb", cfg, {"1234", "BEEF"});
}

static void TestOneWire()
{
    Clear();
    fs = 1.5e6f;
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = OneWire;
    cfg.ch[OneWire][0] = Ch4;
    Sig s(3);
    s.Hold(30e-6, 1);
    s.Hold(480e-6, 0);
    s.Hold(30e-6, 1);
    s.Hold(120e-6, 0);   // presence
    s.Hold(330e-6, 1);
    auto byte = [&](int v)
    {
        for (int i = 0; i < 8; i++)
        {
            if ((v >> i) & 1) { s.Hold(6e-6, 0); s.Hold(64e-6, 1); }
            else { s.Hold(60e-6, 0); s.Hold(10e-6, 1); }
        }
    };
    byte(0xCC);
    byte(0x44);
    s.Rest();
    Expect("1-wire", cfg, {"RST", "PRS", "CC", "44"});
    Shot("onewire", cfg, 1400);
}

static void CanFrame(Sig& s, double bit, uint32_t id, bool ext, std::vector<int> data, bool ack = true)
{
    std::vector<int> b;
    b.push_back(0); // SOF
    auto put = [&](uint32_t v, int n) { for (int i = n - 1; i >= 0; i--) b.push_back((v >> i) & 1); };
    if (ext)
    {
        put(id >> 18, 11); put(1, 1); put(1, 1); put(id & 0x3ffff, 18); put(0, 1); put(0, 2);
    } else
    {
        put(id, 11); put(0, 1); put(0, 1); put(0, 1);
    }
    put(data.size(), 4);
    for (int d : data) put(d, 8);
    uint16_t crc = 0;
    for (int x : b) { int nx = x ^ ((crc >> 14) & 1); crc = (crc << 1) & 0x7fff; if (nx) crc ^= 0x4599; }
    put(crc, 15);
    // stuffing
    std::vector<int> st;
    int same = 0, last = -1;
    for (int x : b)
    {
        st.push_back(x);
        if (x == last) same++; else { same = 1; last = x; }
        if (same == 5) { st.push_back(!x); same = 1; last = !x; }
    }
    for (int x : st) s.Hold(bit, x);
    s.Hold(bit, 1);          // crc delimiter
    s.Hold(bit, ack ? 0 : 1);
    s.Hold(bit, 1);
    s.Hold(bit * 7, 1);      // EOF
    s.Hold(bit * 3, 1);
}

static void TestCan()
{
    Clear();
    fs = 72e6f / 12; // 6 MHz
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = Can;
    Sig s(2);
    double bit = 1.0 / 500000;
    s.Hold(30e-6, 1);
    CanFrame(s, bit, 0x123, false, {0xDE, 0xAD});
    CanFrame(s, bit * 1.01, 0x18FF50E5, true, {0x00, 0x00, 0x00}, false);  // 1% slower clock, no ack
    s.Rest();
    Expect("can", cfg, {"I:123", "L:2", "DE", "AD", "CRC", "ACK", "I:18FF50E5", "L:3", "00", "00", "00", "CRC", "NAK"});
    Shot("can", cfg, 330);
}

static void TestWs2812()
{
    Clear();
    fs = 72e6f / 12; // 6 MHz (5us/div)
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = Ws2812;
    Sig s(2, 0);
    s.Hold(60e-6, 0);
    uint32_t leds[] = {0xFF0000, 0x00FF00, 0x123456}; // GRB on wire
    for (uint32_t v : leds)
        for (int i = 23; i >= 0; i--)
        {
            if ((v >> i) & 1) { s.Hold(0.8e-6, 1); s.Hold(0.45e-6, 0); }
            else { s.Hold(0.4e-6, 1); s.Hold(0.85e-6, 0); }
        }
    s.Hold(60e-6, 0);
    s.Rest();
    Expect("ws2812", cfg, {"#00FF00", "#FF0000", "#341256"});
    Shot("ws2812", cfg, 500);
}

static void TestNec()
{
    Clear();
    fs = 72e6f / (100 * 120); // 6 kHz (5ms/div)
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = Nec;
    Sig s(2, 1); // receiver output: idle high, burst low
    s.Hold(20e-3, 1);
    s.Hold(9e-3, 0); s.Hold(4.5e-3, 1);
    uint32_t frame = 0x04 | (0xFB << 8) | (0x1F << 16) | (0xE0u << 24);
    for (int i = 0; i < 32; i++)
    {
        s.Hold(562e-6, 0);
        s.Hold(((frame >> i) & 1) ? 1687e-6 : 562e-6, 1);
    }
    s.Hold(562e-6, 0);
    s.Hold(40e-3, 1);
    s.Hold(9e-3, 0); s.Hold(2.25e-3, 1); s.Hold(562e-6, 0);
    s.Hold(10e-3, 1);
    s.Rest();
    Expect("nec", cfg, {"LEAD", "A:04", "C:1F", "REP"});
    Shot("nec", cfg, 100);
}

static void TestPs2()
{
    Clear();
    fs = 72e6f / 240; // 300 kHz
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = Ps2;
    Sig clk(2), dat(3);
    double t = 100e-6;
    auto set = [&](int c, int d, double dur) { clk.t = dat.t = t; clk.Hold(dur, c); dat.Hold(dur, d); t += dur; };
    auto frame = [&](int v, bool badParity)
    {
        int ones = 0;
        int bits[11];
        bits[0] = 0;
        for (int i = 0; i < 8; i++) { bits[1 + i] = (v >> i) & 1; ones += bits[1 + i]; }
        bits[9] = !(ones & 1) ^ badParity;
        bits[10] = 1;
        for (int i = 0; i < 11; i++) { set(1, bits[i], 20e-6); set(0, bits[i], 40e-6); set(1, bits[i], 20e-6); }
        set(1, 1, 300e-6);
    };
    frame(0x1C, false);
    frame(0xF0, false);
    frame(0x1C, true);
    clk.Rest(); dat.Rest();
    Expect("ps2", cfg, {"1C", "F0", "1C!"});
    Shot("ps2", cfg, 0);
}

static void TestPulse()
{
    Clear();
    fs = 72e6f / (10 * 120); // 60 kHz (500us/div)
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = Pulse;
    Sig s(2, 0);
    s.Hold(1e-3, 0);
    for (int i = 0; i < 3; i++) { s.Hold(1.5e-3, 1); s.Hold(18.5e-3, 0); }
    s.Rest();
    Expect("pulse", cfg, {"1.50ms 7.5%", "1.50ms 7.5%", "1.50ms"});
    float fFreq, fDuty, fWidth;
    bool ok = PulseStats(fs, fFreq, fDuty, fWidth) && fFreq > 49.9f && fFreq < 50.1f &&
        fDuty > 7.45f && fDuty < 7.55f && fWidth > 1.49e-3f && fWidth < 1.51e-3f;
    printf("%-14s %s  %.3f Hz %.2f %% %.4f ms\n", "pulse stats", ok ? "OK  " : "FAIL", fFreq, fDuty, fWidth * 1e3f);
    failures += !ok;
    // PWM 1 kHz 25 %, measured as low pulses
    Clear();
    fs = 72e6f / 240; // 300 kHz
    cfg.invert[Pulse] = 1;
    Sig p(2, 0);
    p.Hold(0.1e-3, 0);
    for (int i = 0; i < 12; i++) { p.Hold(0.25e-3, 1); p.Hold(0.75e-3, 0); }
    p.Rest();
    Decode(cfg);
    ok = PulseStats(fs, fFreq, fDuty, fWidth) && fFreq > 999 && fFreq < 1001 && fDuty > 74.9f && fDuty < 75.1f;
    printf("%-14s %s  %.3f Hz %.2f %%\n", "pwm low", ok ? "OK  " : "FAIL", fFreq, fDuty);
    failures += !ok;
    Shot("pulse", cfg, 0);
}

static void TestWarnings()
{
    Clear();
    fs = 6e3f;
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = Ws2812;
    Expect("ws slow", cfg, {"Sample rate too low"});
    Shot("warning", cfg, 0);
    cfg.protocol = I2c;
    cfg.ch[I2c][1] = Ch3;
    Expect("i2c same ch", cfg, {"Select channels"});
    cfg.protocol = Uart;
    Expect("uart flat", cfg, {"No data"});
    // only part of the buffer was acquired
    valid = 100;
    Expect("partial buf", cfg, {"No data"});
    valid = N;
}

static void TestOverflow()
{
    // dense pulses: more items than the storage can hold
    Clear();
    fs = 1e6f;
    TConfig cfg; SetDefaults(cfg);
    cfg.protocol = Pulse;
    for (int i = 0; i < N; i++)
        buf[i] = 0x5aa5u | (((i / 2) & 1) << 16);
    Decode(cfg);
    TAnnotation last = Get(Count - 1);
    bool ok = Overflow && Count == N / 24 && last.type == TWarning && last.value == WOverflow;
    // the waveform must survive the storage of annotations
    bool noFake = true;
    for (int i = 0; i < N; i++)
        noFake &= pk[i * 3] == 0xa5 && pk[i * 3 + 1] == 0x5a && (pk[i * 3 + 2] & 1) == ((i / 2) & 1);
    // the decoded levels must survive the storage in the top byte
    bool firstOk = Get(0).type == TPulse && Get(0).value == 2 && Get(0).aux == 4;
    printf("%-14s %s  count=%d\n", "overflow", ok && noFake && firstOk ? "OK" : "FAIL", Count);
    failures += !(ok && noFake && firstOk);
}

int main(int argc, char** argv)
{
    srand(1);
    if (argc > 1) shotDir = argv[1];
    TestUart();
    TestLin();
    TestI2c();
    TestSpi();
    TestOneWire();
    TestCan();
    TestWs2812();
    TestNec();
    TestPs2();
    TestPulse();
    TestWarnings();
    TestOverflow();
    char tmp[32];
    FormatTime(0.0015f, tmp); printf("time %s ", tmp);
    FormatTime(9.999e-6f, tmp); printf("%s ", tmp);
    FormatTime(-123e-9f, tmp); printf("%s\n", tmp);
    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures != 0;
}

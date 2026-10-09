#pragma once
#include <stdint.h>

// Protocol decoders for the oscilloscope sample buffer.
//
// The core does not depend on the BIOS or on the GUI, it works with the
// sample buffer of the oscilloscope and a sample rate, so it can be built and
// tested on a PC as well.
//
// Sample layout (3 bytes per sample, see Sampler.h): byte 0 CH1, byte 1 CH2,
// byte 2: bit 0 CH3 (C), bit 1 CH4 (D). Analog channels used by a decoder are
// converted to logic levels (automatic threshold in the middle of the signal
// range, with hysteresis) and stored in bit 2 (CH1) and bit 3 (CH2) of byte 2.
//
// RAM is very tight (the application runs on the OS stack), so the decoded
// items are stored in the free bits 4..7 of byte 2: one 12 byte annotation
// uses 24 samples, the 4096 sample buffer holds 170 items. The annotations
// are valid only until the sample buffer is overwritten.

namespace Decoder
{
    enum EProtocol {
        Off, Uart, Lin, I2c, Spi, OneWire, Can, Ws2812, Nec, Ps2, Pulse,
        ProtocolCount, ProtocolMax = ProtocolCount - 1
    };
    enum EChannel { Ch1, Ch2, Ch3, Ch4, ChOff, ChMax = ChOff, ChMaxRequired = Ch4 };
    enum EFormat { Hex, Dec, Ascii, FormatMax = Ascii };
    enum EBand { Top, Bottom, BandMax = Bottom };

    enum { MaxRoles = 4 };

    struct TConfig
    {
        uint8_t protocol;
        uint8_t ch[ProtocolCount][MaxRoles]; // channel for every role of every protocol
        uint8_t invert[ProtocolCount];       // signal polarity
        uint8_t uartBaud;                    // index to RateUart, 0 = auto
        uint8_t uartBits;                    // index to BitsUart
        uint8_t uartParity;                  // none, even, odd
        uint8_t uartStop;                    // 1, 2
        uint8_t linBaud;                     // index to RateUart, 0 = auto
        uint8_t spiMode;                     // 0..3
        uint8_t spiBits;                     // index to BitsSpi
        uint8_t spiOrder;                    // MSB, LSB first
        uint8_t canRate;                     // index to RateCan, 0 = auto
        uint8_t wsOrder;                     // GRB, RGB
        uint8_t format;                      // EFormat
        uint8_t band;                        // EBand
    };

    enum EType {
        TData,          // value = word, aux = number of bits
        TAddrWrite,     // I2C address byte, value = 7 bit address
        TAddrRead,
        TStart, TRepStart, TStop,
        TBreak,         // UART / LIN break
        TSync,          // LIN sync byte
        TPid,           // LIN protected id, value = raw byte
        TChecksum,      // LIN checksum, value = byte
        TReset, TPresence, // 1-Wire
        TCanId,         // value = id, aux bit0 = RTR, bit1 = extended
        TCanDlc,
        TCanCrc,        // value = crc
        TCanAck,
        TColor,         // WS2812, value = 0xRRGGBB, aux = led index
        TLeader, TRepeat, TNecAddr, TNecCmd, // NEC IR, aux = number of bits
        TPulse,         // value = width in samples, aux = period in samples (0 = unknown)
        TWarning        // value = EWarning
    };

    enum EFlags { FRow1 = 1, FError = 2, FPoint = 4, FNak = 8 };

    enum EWarning { WNoChannel, WSampleRate, WNoSignal, WOverflow, WStuffError };

    struct TAnnotation
    {
        uint16_t begin;     // sample index
        uint16_t end;       // sample index, inclusive
        uint32_t value;
        uint8_t type;       // EType
        uint8_t flags;      // EFlags
        uint16_t aux;
    };

    extern int Count;
    TAnnotation Get(int i);
    extern bool Overflow;
    extern uint32_t DetectedRate;   // baud / bit rate used by the last decode (0 = n/a)

    // option tables (also used by the menu)
    extern const uint32_t RateUart[];
    extern const char* const RateUartText[];
    enum { RateUartMax = 18 };
    extern const uint32_t RateCan[];
    extern const char* const RateCanText[];
    enum { RateCanMax = 11 };
    extern const uint8_t BitsUart[];
    extern const char* const BitsUartText[];
    enum { BitsUartMax = 4 };
    extern const uint8_t BitsSpi[];
    extern const char* const BitsSpiText[];
    enum { BitsSpiMax = 7 };

    extern const char* const ProtocolText[];
    extern const char* const ChannelText[];

    // Role names for every protocol (nullptr = role not used) and whether the
    // role may be switched off
    const char* RoleName(int protocol, int role);
    bool RoleOptional(int protocol, int role);

    void SetDefaults(TConfig& cfg);

    // Decodes samples [first, count) of the buffer, sampleRate in Hz
    enum { BytesPerSample = 3 };
    // Decodes samples [first, count) of the packed buffer with nBufferSamples
    // samples, sampleRate in Hz
    void Run(const TConfig& cfg, uint8_t* samples, int nBufferSamples, int first, int count, float sampleRate);

    // Text of an annotation; short form for the band above the waveform,
    // long form for the list. Returns length.
    int Format(const TAnnotation& a, char* buf, int bufSize, bool longForm,
               const TConfig& cfg, float sampleRate);

    // Formats time in seconds with 3 significant digits ("1.25ms")
    int FormatTime(float t, char* buf);
}

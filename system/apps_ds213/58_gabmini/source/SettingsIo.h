#pragma once
#include <stdint.h>

// Byte stream for SettingsStore: computes a checksum, writes sectors to the
// open file or reads them back. Values are written and read by the same
// code, Read assigns a value only when it is in the allowed range, Verify
// only checks the file. BIOS::FAT has to be declared before including.
template <int SectorSize> struct TSettingsIo
{
    enum EMode { Checksum, Write, Verify, Read } mode;
    uint8_t* pSector;
    int nPos;
    int nLength;        // bytes processed
    uint32_t nSum;
    bool bOk;
    bool bValid;        // Read: all values in range

    void Sum(uint8_t b)
    {
        nSum = (nSum << 5) + nSum + b;
        nLength++;
    }

    void PutByte(uint8_t b)
    {
        if ( mode == Checksum )
        {
            Sum(b);
            return;
        }
        pSector[nPos++] = b;
        if ( nPos == SectorSize )
        {
            if ( BIOS::FAT::Write( pSector ) != BIOS::FAT::EOk )
                bOk = false;
            nPos = 0;
        }
        nLength++;
    }

    uint8_t GetByte()
    {
        if ( nPos == SectorSize )
        {
            if ( BIOS::FAT::Read( pSector ) != BIOS::FAT::EOk )
                bOk = false;
            nPos = 0;
        }
        uint8_t b = pSector[nPos++];
        Sum(b);
        return b;
    }

    bool Reading() const { return mode == Verify || mode == Read; }

    // value of the stream (also in Verify mode), reading only
    template <class T> void Fetch(T& v)
    {
        uint8_t* p = (uint8_t*)&v;
        for ( int i = 0; i < (int)sizeof(T); i++ )
            p[i] = GetByte();
    }

    // raw value, Verify only checks
    template <class T> void Raw(T& v)
    {
        if ( Reading() )
        {
            T tmp;
            Fetch(tmp);
            if ( mode == Read )
                v = tmp;
        } else
        {
            uint8_t* p = (uint8_t*)&v;
            for ( int i = 0; i < (int)sizeof(T); i++ )
                PutByte(p[i]);
        }
    }

    // enumeration stored in one byte, assigned only when in range
    template <class T> void Enum(T& v, int nMax)
    {
        uint8_t b = (uint8_t)(int)v;
        if ( !Reading() )
        {
            PutByte(b);
            return;
        }
        b = GetByte();
        if ( b > nMax )
        {
            bValid = false;
            return;
        }
        if ( mode == Read )
            v = (T)b;
    }

    // integer with range check
    template <class T> void Int(T& v, int nMin, int nMax)
    {
        int32_t n = (int32_t)v;
        if ( !Reading() )
        {
            Raw(n);
            return;
        }
        Fetch(n);
        if ( n < nMin || n > nMax )
        {
            bValid = false;
            return;
        }
        if ( mode == Read )
            v = (T)n;
    }

    // float with range check (also rejects NaN)
    void Float(float& v, float fMin, float fMax)
    {
        float f = v;
        if ( !Reading() )
        {
            Raw(f);
            return;
        }
        Fetch(f);
        if ( !(f >= fMin && f <= fMax) )
        {
            bValid = false;
            return;
        }
        if ( mode == Read )
            v = f;
    }
};

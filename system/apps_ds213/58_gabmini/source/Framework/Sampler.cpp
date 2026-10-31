#include <library.h>
#include "Sampler.h"

namespace Sampler
{
	static uint8_t memory[Samples * BytesPerSample];
	static int nValid = 0;
	static uint32_t nSequence = 0;
	static int nLength = Samples;

	static inline void _Store(uint8_t* p, uint32_t value)
	{
		p[0] = value & 0xff;
		p[1] = (value >> 8) & 0xff;
		p[2] = (value >> 16) & 0x03;
	}

	void SetLength(int n)
	{
		if ( n < 16 ) n = 16;
		if ( n > Samples ) n = Samples;
		nLength = n;
		if ( nValid > nLength )
			nValid = nLength;
	}

	int Length()
	{
		return nLength;
	}

	uint8_t* HistoryArea(int& nBytes)
	{
		nBytes = (Samples - nLength) * BytesPerSample;
		return memory + nLength * BytesPerSample;
	}

	void RollStart(int nFirst)
	{
		if ( nFirst > nLength ) nFirst = nLength;
		nValid = nFirst;
		memset( memory, 0x80, nFirst * BytesPerSample );
	}

	void RollAppend(const uint32_t* pSamples, int nCount, int nFirst, int nWidth)
	{
		int nEnd = nFirst + nWidth;
		if ( nEnd > nLength || nCount <= 0 )
			return;
		if ( nValid < nFirst )
			RollStart( nFirst );
		if ( nCount > nWidth )
		{
			// only the newest fit to the window
			pSamples += nCount - nWidth;
			nCount = nWidth;
		}
		int nOver = nValid + nCount - nEnd;
		if ( nOver > 0 )
		{
			memmove( memory + nFirst * BytesPerSample,
				memory + (nFirst + nOver) * BytesPerSample,
				(nValid - nFirst - nOver) * BytesPerSample );
			nValid -= nOver;
		}
		for ( int i = 0; i < nCount; i++ )
			_Store( memory + (nValid++) * BytesPerSample, pSamples[i] );
	}

	void Copy(bool bNew)
	{
		uint8_t* p = memory;
		nValid = nLength;
		if ( bNew )
			nSequence++;
		for (int i=0; i<nLength; i++, p += BytesPerSample)
		{
			uint32_t value = BIOS::ADC::Get();
			if ( value == BIOS::ADC::NoData && nValid == nLength )
				nValid = i;
			_Store( p, value );
		}
	}

	uint8_t* GetBuffer()
	{
		return memory;
	}

	int GetValid()
	{
		return nValid;
	}

	uint32_t GetSequence()
	{
		return nSequence;
	}

	BIOS::ADC::TSample::SampleType GetAt(int i)
	{
		i = (unsigned)i % Samples;
		if ( i >= nValid )
			return BIOS::ADC::NoData;
		const uint8_t* p = memory + i * BytesPerSample;
		return p[0] | (p[1] << 8) | ((p[2] & 0x03) << 16);
	}
}

#include <library.h>
#include "Sampler.h"

namespace Sampler
{
	static uint8_t memory[Samples * BytesPerSample];
	static int nValid = 0;

	void Copy()
	{
		uint8_t* p = memory;
		nValid = Samples;
		for (int i=0; i<Samples; i++, p += BytesPerSample)
		{
			uint32_t value = BIOS::ADC::Get();
			if ( value == BIOS::ADC::NoData && nValid == Samples )
				nValid = i;
			p[0] = value & 0xff;
			p[1] = (value >> 8) & 0xff;
			p[2] = (value >> 16) & 0x03;
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

	BIOS::ADC::TSample::SampleType GetAt(int i)
	{
		i = (unsigned)i % Samples;
		if ( i >= nValid )
			return BIOS::ADC::NoData;
		const uint8_t* p = memory + i * BytesPerSample;
		return p[0] | (p[1] << 8) | ((p[2] & 0x03) << 16);
	}
}

#pragma once

// Copy of the FPGA sample buffer, packed to 3 bytes per sample to save RAM:
//   byte 0: CH1, byte 1: CH2,
//   byte 2: bit 0 CH3, bit 1 CH4, bits 2..7 are used by the protocol decoders
namespace Sampler
{
	enum { Samples = BIOS::ADC::NumSamples, BytesPerSample = 3 };

	void Copy();
	// sample in the BIOS::ADC::TSample layout, NoData after the valid part
	BIOS::ADC::TSample::SampleType GetAt(int);
	uint8_t* GetBuffer();
	int GetValid();     // samples received by the last Copy()
}

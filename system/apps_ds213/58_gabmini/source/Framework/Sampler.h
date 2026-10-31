#pragma once

// Copy of the FPGA sample buffer, packed to 3 bytes per sample to save RAM:
//   byte 0: CH1, byte 1: CH2,
//   byte 2: bit 0 CH3, bit 1 CH4, bits 2..7 are used by the protocol decoders
namespace Sampler
{
	enum { Samples = BIOS::ADC::NumSamples, BytesPerSample = 3 };
	// one screen with the pre-trigger part and room to move the window
	enum { ScreenSamples = 480 };

	// samples taken by one acquisition (Samples or ScreenSamples); the
	// memory after them is free for the persistence history
	void SetLength(int nLength);
	int Length();
	uint8_t* HistoryArea(int& nBytes);

	// bNew: a new acquisition, false when the same (unfinished) acquisition
	// is read again
	void Copy(bool bNew = true);
	// sample in the BIOS::ADC::TSample layout, NoData after the valid part
	BIOS::ADC::TSample::SampleType GetAt(int);
	uint8_t* GetBuffer();
	int GetValid();     // samples received by the last Copy()
	uint32_t GetSequence(); // incremented by every new acquisition

	// roll mode: samples are appended to the window [nFirst, nFirst+nWidth),
	// when it is full the older samples move to the left
	void RollStart(int nFirst);
	void RollAppend(const uint32_t* pSamples, int nCount, int nFirst, int nWidth);
}

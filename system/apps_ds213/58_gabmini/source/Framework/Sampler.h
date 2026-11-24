#pragma once

// Copy of the FPGA sample buffer, packed to 3 bytes per sample to save RAM:
//   byte 0: CH1, byte 1: CH2,
//   byte 2: bit 0 CH3, bit 1 CH4, bits 2..7 are used by the protocol decoders
namespace Sampler
{
	enum { Samples = BIOS::ADC::NumSamples, BytesPerSample = 3 };
	// one screen with the pre-trigger part and room to move the window
	enum { ScreenSamples = 480 };
	// the sample memory as scratch before the first capture
	uint8_t* Scratch(int& size);

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
	// moves the level crossing nearest to the trigger position (edge trigger
	// of CH1..CH4, rising / falling, level in ADC units) to the trigger
	// position; returns the shift in samples
	int AlignTrigger(int nSource, bool bRising, int nLevel);
	// shift of the last AlignTrigger, AlignNone: not done, AlignNotFound: no
	// crossing near the trigger position (diagnostic, shown next to TRIG)
	enum { AlignNone = 0x7fff, AlignNotFound = 0x7ffe };
	int LastAlign();
	uint8_t* GetBuffer();
	int GetValid();     // samples received by the last Copy()
	uint32_t GetSequence(); // incremented by every new acquisition

	// Time base of the buffer: the samples were taken at the time base of
	// the last Copy / roll. When the time base is changed later (stopped),
	// GetAt / GetValid show the buffer as if it were sampled at the new time
	// base (horizontal zoom around the trigger position): the trace, cursors,
	// measurements, FFT and CSV follow the new time base. The decoders work
	// on the raw buffer (GetBuffer, GetValidRaw) at CaptureRes(); their
	// positions are converted with PhysToVirt / VirtToPhys.
	bool Scaled();
	// the same for a new V/div or position of CH1 / CH2: GetAt converts the
	// stored ADC values to the current V/div and position
	bool VScaled();
	int CaptureRes();
	int GetValidRaw();
	int PhysToVirt(int p);
	int VirtToPhys(int v);

	// roll mode: samples are appended to the window [nFirst, nFirst+nWidth),
	// when it is full the older samples move to the left
	void RollStart(int nFirst);
	void RollAppend(const uint32_t* pSamples, int nCount, int nFirst, int nWidth);
}

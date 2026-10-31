#pragma once
#include <Framework.h>

class CCoreOscilloscope
{
public:
	static void ConfigureAdc();
	static void ConfigureTrigger();
	static void UpdateConstants();

	// samples of one acquisition: the whole buffer, or one screen (Buffer
	// Screen, or persistence that keeps its history after the samples)
	static int BufferLength();
	// sampler length and FPGA depth by BufferLength, window position
	static void ApplyBuffer();
	// Scan at 50 ms/div and slower: samples are shown as they come (roll)
	static bool Rolling();
	// incremented by ConfigureAdc / ConfigureTrigger
	static uint32_t ConfigGeneration();
};

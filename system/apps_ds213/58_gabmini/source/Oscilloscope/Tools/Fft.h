#pragma once
#include <stdint.h>

class CWndOscGraph;

// LA104: spectrum of 1024 samples (512 / 256 when the buffer is shorter),
// Hann window, bins up to fs/2, fixed point. The peak is interpolated
// between the bins. pScratch is not used any more (own 2 KB buffer).
namespace Fft
{
	void Paint( CWndOscGraph* pGraph, int nSource, uint8_t* pScratch );
}

#pragma once
#include <stdint.h>

class CWndOscGraph;

// LA104: spectrum of 256 samples (Hann window, 128 bins up to fs/2) in
// fixed point, computed in the 512 bytes of pScratch.
namespace Fft
{
	void Paint( CWndOscGraph* pGraph, int nSource, uint8_t* pScratch );
}

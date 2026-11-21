#pragma once
#include <stdint.h>

class CWndOscGraph;

// LA104: spectrum of 1024 samples (512 / 256 when the buffer is shorter),
// Hann window, bins up to fs/2, fixed point. The peak is interpolated
// between the bins. pScratch is not used any more (own 2 KB buffer).
// Cursors X1 / X2 (tab Cursor) are frequency lines while the FFT is shown:
// the column of a cursor (its sample - Time.Shift) is a frequency, the level
// is the highest bin of the column. Harmonics H2..H9 of the peak are marked
// and listed (dBc) with the THD.
namespace Fft
{
	void Paint( CWndOscGraph* pGraph, int nSource, uint8_t* pScratch );

	// FFT shown and computed (the last Paint had data)
	bool Shown();
	// frequency of a graph column (0 .. MaxX-1)
	float ColumnHz( int x );
	// level of a graph column, dB (0 = full scale sine); false: no data
	bool ColumnDb( int x, float& fDb );
	// the column of a time cursor
	int CursorColumn( int nSample );
}

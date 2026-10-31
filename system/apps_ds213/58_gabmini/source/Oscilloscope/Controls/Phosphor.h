#pragma once
#include <stdint.h>

// History of the last acquisitions for the persistence and phosphor modes.
//
// There is no RAM for an intensity map of the screen, the history is kept
// in the sample buffer after the samples of one acquisition (Buffer: one
// screen leaves ~10 kB free):
//   T-Y: per frame and channel the Y of every column (300 bytes)
//   X-Y: per frame up to 256 points (x, y)
// Up to 16 frames. Rendering sums the frames that pass through a pixel with
// a weight by age (newest 16 ... oldest 1): a trace present in every frame
// gets 136, a single old excursion 1. The sum selects the color: dim ->
// channel color -> towards white, like the afterglow and the brightness of
// an analog phosphor screen.
//
// The core does not use the BIOS or the GUI, it is tested on a PC.
namespace Phosphor
{
    enum { Width = 300, Height = 200, MaxFrames = 16, MaxPoints = 256, None = 0xff };
    enum { LutSize = 32, MaxSum = 136 };
    // the stack of the application is ~1.8 kB on the DS203, the buffers of
    // the rendering are taken from the end of the history area
    enum { ScratchBytes = 2 * MaxPoints };
    uint8_t* Scratch();             // ScratchBytes, valid after Begin*

    // area: free memory for the history; key: settings that change the
    // meaning of the stored values (clears the history when different);
    // bNew: a new acquisition (the oldest frame is reused); bNew is set to
    // true also when the history was cleared: the caller stores the frame
    // when bNew is true on return
    // returns false when there is no room for at least 2 frames
    bool BeginTY(uint8_t* pArea, int nBytes, uint32_t nKey, bool& bNew);
    void StoreTY(int x, int nChannel, int y);       // y 0..Height-1 or None
    // intensity of a column of one channel, bGraded false = only the hits
    // (envelope)
    void SumColumn(int x, int nChannel, bool bLines, bool bGraded, uint8_t* pSum);

    bool BeginXY(uint8_t* pArea, int nBytes, uint32_t nKey, bool& bNew);
    void StoreXY(int nPoints, const uint8_t* pX, const uint8_t* pY);
    int Frames();                                   // valid frames
    // frame by age (0 = newest): points as x0, y0, x1, y1...
    const uint8_t* FrameXY(int nAge, int& nPoints);

    void Clear();

    // color table for one channel: 1..20 dark to the channel color,
    // 21..31 from the channel color towards white
    void MakeLut(uint16_t clr, uint16_t* pLut);
    // T-Y: index by the weighted sum, square law: brightness follows the
    // density of the hits (a trace in every frame 31, only in the newest
    // frame 10, one old excursion 2)
    int LutIndex(int nSum);
    // X-Y frames: by age only, the newest full color (20), the oldest dim
    inline int AgeIndex(int nAge) { return 20 - nAge * 19 / (MaxFrames - 1); }

}

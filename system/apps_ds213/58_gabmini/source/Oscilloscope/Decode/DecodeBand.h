#pragma once
#include <stdint.h>
#include "DecoderCore.h"

// Draws the decoded items as labelled boxes into the waveform columns.
// The oscilloscope renders the graph column by column (one sample per
// column, column[0] is the bottom pixel), the band is drawn into the same
// column buffer, so it does not flicker.
namespace DecodeBand
{
    bool Begin(const Decoder::TConfig& cfg, float fSampleRate, int nViewBegin, int nViewWidth, int nSelected);
    void Column(uint16_t* column, int nHeight, int nIndex);
}

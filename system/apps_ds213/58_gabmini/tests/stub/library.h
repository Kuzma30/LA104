#pragma once
// BIOS stubs for the PC tests (DecodeBand.cpp includes Framework/Sampler.h)
#include <stdint.h>
namespace BIOS { namespace ADC {
    enum { NumSamples = 4096 };
    struct TSample { typedef uint32_t SampleType; };
} }

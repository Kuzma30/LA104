#pragma once
#include <stdint.h>

// Converts float to double using integer bit operations only.
//
// Passing a float into printf-like varargs promotes it to double through
// __aeabi_f2d. On Cortex-M3 that single call links the whole soft double
// library object from libgcc (~1.6 kB of ROM), which these apps can not
// afford. Denormals are flushed to zero, which is fine for display purposes.
inline double F2D(float f)
{
	union { float f; uint32_t u; } in;
	union { double d; uint32_t w[2]; } out;
	in.f = f;

	uint32_t sign = in.u & 0x80000000u;
	uint32_t exp = (in.u >> 23) & 0xff;
	uint32_t mant = in.u & 0x7fffffu;

	if (exp == 0)
	{
		out.w[0] = 0;
		out.w[1] = sign;
		return out.d;
	}

	uint32_t exp64 = exp == 0xff ? 0x7ff : exp - 127 + 1023;
	out.w[1] = sign | (exp64 << 20) | (mant >> 3);
	out.w[0] = mant << 29;
	return out.d;
}

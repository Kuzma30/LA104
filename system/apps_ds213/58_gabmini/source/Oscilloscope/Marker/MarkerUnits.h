#pragma once

// LA104: units of the time cursors follow the time base: ns below 1 us/div,
// us below 1 ms/div, ms below 1 s/div, s at 1 s/div (the old code printed
// ms everywhere, 0.006 ms at 200 ns/div). Frequencies in Hz, kHz or MHz.

struct CMarkerUnit
{
	float fScale;
	const char* strUnit;
};

inline CMarkerUnit MarkerTimeUnit()
{
	float fDiv = Settings.Runtime.m_fTimeRes;   // seconds per division
	if ( fDiv < 0.99e-6f )
		return { 1e9f, "ns" };
	if ( fDiv < 0.99e-3f )
		return { 1e6f, "us" };
	if ( fDiv < 0.99f )
		return { 1e3f, "ms" };
	return { 1.0f, "s" };
}

inline CMarkerUnit MarkerFreqUnit( float fHz )
{
	float a = fHz < 0 ? -fHz : fHz;
	if ( a >= 999.5e3f )
		return { 1e-6f, "MHz" };
	if ( a >= 999.5f )
		return { 1e-3f, "kHz" };
	return { 1.0f, "Hz" };
}

// value already scaled to its unit: 2 decimals below 10, 1 below 100,
// none above (Printf truncates, so it is rounded here)
inline int MarkerPrint( int x, int y, ui16 clr, float f )
{
	float a = f < 0 ? -f : f;
	if ( a < 9.995f )
		return UI::Printf( x, y, clr, RGBTRANS, "%.2f", F2D( f < 0 ? f - 0.005f : f + 0.005f ) );
	if ( a < 99.95f )
		return UI::Printf( x, y, clr, RGBTRANS, "%.1f", F2D( f < 0 ? f - 0.05f : f + 0.05f ) );
	int n = (int)( f < 0 ? f - 0.5f : f + 0.5f );
	return UI::Printf( x, y, clr, RGBTRANS, "%d", n );
}

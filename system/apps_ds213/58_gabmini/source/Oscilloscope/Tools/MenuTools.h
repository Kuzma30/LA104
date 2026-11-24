#pragma once
#include <Framework.h>
#include <Settings.h>

// LA104: tab Tools
//   Math     Off, A+B, A-B, B-A, A*B (A = CH1, B = CH2, in volts), Left / Right
//   Math V/d scale of the math trace (V/div, for A*B V^2/div)
//   FFT      Off, CH1, CH2: spectrum of 1024 samples from the screen start
//            (256 with the buffer Buf:Scr), the peak interpolated between bins
//   Ref      Off / On; Enter stores the CH1 trace as the reference
//   CSV      Enter saves the samples to WAVnnn.CSV
//   BMP      Enter saves the screen to SCRnnn.BMP
//   Zero     Enter twice: zero calibration of CH1 / CH2 for all V/div
//            (inputs disconnected or grounded), stored in MINICAL.CFG

// one menu item with all six tools (RAM: one window instead of six);
// Up / Down move between the tools inside it
class CItemTools : public CWndMenuItem
{
public:
	// Sleep: display off after minutes without keys and signal (SLEEP.CFG)
	enum { Math, MathRes, Fft, Ref, Csv, Bmp, Zero, SleepTime, Count };
	int m_nSel;
	char m_strInfo[3][32];   // result of the last save (CSV, BMP), calibration
	bool m_bArmed;           // Zero: first Enter asks, the second runs
	void Create( CWnd* pParent );
	virtual void OnPaint() override;
	virtual void OnKey( int nKey ) override;
};

class CWndMenuTools : public CWnd
{
public:
	CItemTools m_itmTools;

	virtual void Create( CWnd* pParent, int dwFlags );
	virtual void OnMessage( CWnd* pSender, int code, uintptr_t data ) override;
};

namespace Tools
{
	enum { RefWidth = 300 };
	// y of the reference trace (pixels) for every second column, 0xff none
	extern uint8_t arrRef[RefWidth / 2];

	bool SaveCsv( char* pName );        // pName gets the file name
	bool SaveBmp( char* pName );
	void StoreRef();

	// zero of CH1 / CH2 for every V/div: the ADC value of 0 V at two
	// positions gives the offset curve (Calibrator Q); K is kept
	bool ZeroCalibrate( char* strResult );   // strResult: 32 bytes (UTF-8)
	bool LoadCalibration();             // at start, false: defaults kept
}

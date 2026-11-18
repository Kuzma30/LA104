#pragma once
#include <Framework.h>
#include "GraphBase.h"
#include <Settings.h>

class CWndOscGraph : public CWndGraph
{
	ui16			m_arrAverageBuf[DivsX*BlkX];
	bool			m_bPersistReset;
	bool			m_bNeedRedraw;
	uint32_t		m_nPhosphorSeq;
	uint32_t		m_nXyKey;

	uint32_t		_PersistKey();
	void			_PhosphorColumn( ui16* column, int x, bool bLines, bool bGraded,
						bool en1, bool en2, ui16 clr1, ui16 clr2 );
	void			_XyGrid( const CRect& rc );
	void			_XyLine( const CRect& rc, int x1, int y1, int x2, int y2, ui16 clr );
	void			_PrepareColumn( ui16 *column, ui16 n, ui16 clr );
	ui16			_Interpolate( ui16 clrA, ui16 clrB );

public:
	CWndOscGraph();
	virtual void	Create(CWnd *pParent, int dwFlags);
	virtual void	OnMessage(CWnd* pSender, int code, uintptr_t data) override;
	virtual void	OnPaint() override;
	void			SetupMarkers( CSettings::Calibrator::FastCalc& Ch1fast, CSettings::Calibrator::FastCalc& Ch2fast,
		int& nMarkerT1, int& nMarkerT2, int& nMarkerY1, int& nMarkerY2 );
	void			SetupSelection( bool& bSelection, int& nMarkerT1, int& nMarkerT2 );
	void			GetCurrentRange(int& nBegin, int& nEnd);
	void			ClearAverage();
	void			ClearPersist();

	void			OnPaintTY();
	// LA104: "WAIT" / "TRIG" in Norm and Single (Trigger::State Run or Wait)
	void			PaintTrigState();
	void			OnPaintXY();

	// LA104: 600 bytes for the FFT and the file output (the averaging buffer;
	// call ClearAverage after use)
	uint8_t*		GetScratch() { return (uint8_t*)m_arrAverageBuf; }
};

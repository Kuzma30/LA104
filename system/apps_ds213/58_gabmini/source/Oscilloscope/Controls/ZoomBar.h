class CWndZoomBar : public CWnd
{
public:
	virtual void Create(CWnd* pParent, ui16 nFlags, CWnd* pGraph)
	{
		CRect rcClient( pGraph->m_rcClient );
		rcClient.top = rcClient.bottom + 4;
		rcClient.bottom = rcClient.top + 7;
		CWnd::Create("CWndZoomBar", nFlags | WsNoActivate | WsListener, rcClient, pParent);
	}

	virtual void OnMessage(CWnd* pSender, int code, uintptr_t data) override
	{
		// new waveform captured, refresh the preview
		if ( pSender == NULL && code == WmBroadcast && data == ToWord('d', 'g') && IsVisible() )
			Invalidate();
	}

	virtual void OnPaint()
	{
		ui16 clr = RGB565(808080);
		CRect rcBorder = m_rcClient;
		rcBorder.Inflate(1, 0, 1, 0);
		BIOS::LCD::Bar( rcBorder, clr );

		CRect rc = rcBorder;
		ui16 nBegin = Settings.Time.Shift;
		ui16 nView = CWndGraph::DivsX*CWndGraph::BlkX;

		ui32 nXBegin = (nBegin*m_rcClient.Width()) >> 12;
		ui32 nXEnd = ((nBegin+nView)*m_rcClient.Width()) >> 12;

		rc.top++;
		rc.bottom--;
		rc.left = m_rcClient.left + (si16)nXBegin;
		rc.right = m_rcClient.left + (si16)nXEnd;

		if ( rc.right > rcBorder.right )
			rc.right = rcBorder.right;
		BIOS::LCD::Bar( rc, RGB565(000000) );

		// Preview of the whole sample buffer: min/max envelope of CH1 in every
		// column shows where in the buffer the interesting part of signal is
		if ( Settings.CH1.Enabled != CSettings::AnalogChannel::_YES )
			return;

		int nWidth = m_rcClient.Width();
		int nHeight = m_rcClient.Height() - 2;
		ui16 clrWave = Settings.CH1.u16Color;
		for ( int x = 0; x < nWidth; x++ )
		{
			int nFirst = x * BIOS::ADC::NumSamples / nWidth;
			int nLast = (x+1) * BIOS::ADC::NumSamples / nWidth;
			int nMin = 255, nMax = 0;
			for ( int i = nFirst; i < nLast; i++ )
			{
				BIOS::ADC::TSample::SampleType value = Sampler::GetAt(i);
				if ( value == BIOS::ADC::NoData )
					continue;
				int nValue = value & 0xff;
				nMin = min(nMin, nValue);
				nMax = max(nMax, nValue);
			}
			if ( nMin > nMax )
				continue;
			int nBottom = m_rcClient.bottom - 2 - ((nMin * nHeight) >> 8);
			int nTop = m_rcClient.bottom - 2 - ((nMax * nHeight) >> 8);
			BIOS::LCD::Bar( m_rcClient.left + x, nTop, m_rcClient.left + x + 1, nBottom + 1, clrWave );
		}
	}

};

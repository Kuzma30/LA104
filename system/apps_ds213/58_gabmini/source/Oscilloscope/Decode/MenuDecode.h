#pragma once
#include <Framework.h>
#include <Settings.h>
#include <Oscilloscope/Disp/ItemDisp.h>
#include "DecoderCore.h"

// Glue between the decoder core and the oscilloscope
namespace DecodeUi
{
    float SampleRate();
    void Update();                   // decode current sample buffer
    bool Hold();                     // acquisition paused (list window open)

    // Band with decoded data drawn over the waveform, column by column
    bool BeginFrame(int nViewBegin, int nViewWidth);
    void RenderColumn(ui16* column, int nHeight, int nIndex);

    int Selected();
    void Select(int n);
}

class CItemDecodeButton : public CWndMenuItem
{
	ui16 m_nCode;

public:
	void Create(const char* pszId, ui16 nCode, CWnd* pParent)
	{
		m_nCode = nCode;
		CWndMenuItem::Create(pszId, RGB565(808080), 1, pParent);
	}

	virtual void OnKey(int nKey) override
	{
		if ( nKey == BIOS::KEY::Enter )
			SendMessage(m_pParent, m_nCode, 0);
		CWnd::OnKey(nKey);
	}
};

// Left/Right jumps to previous/next decoded item
class CItemDecodeBrowse : public CWndMenuItem
{
public:
	void Create(CWnd* pParent)
	{
		CWndMenuItem::Create("Browse", RGB565(808080), 2, pParent);
	}
	virtual void OnPaint() override;
	virtual void OnKey(int nKey) override;
};

class CItemDecodeInfo : public CWnd
{
public:
	void Create(CWnd* pParent);
	virtual void OnPaint() override;
};

// Protocol settings: channels and parameters
class CWndListDecode : public CListBox
{
	enum { MaxItems = 8 };
	CProviderEnum	m_pro[MaxItems];
	CLPItem			m_itm[MaxItems];
	int				m_nItems;

	void _Add(const char* pszName, const char* const* ppszText, uint8_t* pValue, int nMax);

public:
	void Create(CWnd* pParent);
	CWnd* First() { return &m_itm[0]; }
};

// List of all decoded items in the buffer
class CWndDecodeList : public CWnd
{
	int m_nFirst;
	int m_nSel;
	enum { Lines = 12 };

public:
	void Create(CWnd* pParent);
	virtual void OnPaint() override;
	virtual void OnKey(int nKey) override;
};

class CWndMenuDecode : public CWnd
{
public:
	CProviderEnum		m_proProtocol;
	CProviderEnum		m_proFormat;

	CMPItem				m_itmProtocol;
	CItemDecodeButton	m_itmSetup;
	CItemDecodeBrowse	m_itmBrowse;
	CItemDecodeButton	m_itmList;
	CMPItem				m_itmFormat;
	CItemDecodeInfo		m_itmInfo;

	CWndListDecode		m_wndSetup;
	CWndDecodeList		m_wndList;

	virtual void Create(CWnd *pParent, int dwFlags);
	virtual void OnMessage(CWnd* pSender, int code, uintptr_t data) override;

	void ShowItem(int n);     // moves the view to decoded item n
	void Refresh();           // after new decode
};

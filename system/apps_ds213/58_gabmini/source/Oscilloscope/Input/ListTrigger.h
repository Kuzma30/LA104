#ifndef __LISTTRIGGER_H__
#define __LISTTRIGGER_H__

class CWndListTrigger : public CListBox
{
public:
	CProviderEnum	m_proSync;
	CProviderEnum	m_proType;
	CProviderEnum	m_proSource;
	CProviderNum	m_proLevel;
	CProviderNum	m_proTime;
	CProviderNum	m_proHoldoff;
	CProviderBtn	m_proCenter;

	CLPItem			m_itmSync;
	CLPItem			m_itmType;
	CLPItem			m_itmSource;
	CLPItem			m_itmLevel;
	CLPItem			m_itmTime;
	CLPItem			m_itmHoldoff;
	CLPItem			m_itmCenter;
	// DSO does not allow to change the time offset, trigger is always
	// at CSettings::Trigger::Position

public:
	void Create( CWnd* pParent )
	{
		CListBox::Create( "Trigger", WsVisible | WsModal, CRect(120, 55, 319, 201), RGB565(404040), pParent );

		m_proSync.Create( (const char**)CSettings::Trigger::ppszTextSync,
			(NATIVEENUM*)&Settings.Trig.Sync, CSettings::Trigger::_SyncMax );
		m_proType.Create( (const char**)CSettings::Trigger::ppszTextType,
			(NATIVEENUM*)&Settings.Trig.Type, CSettings::Trigger::_TypeMax );
		m_proSource.Create( (const char**)CSettings::Trigger::ppszTextSource,
			(NATIVEENUM*)&Settings.Trig.Source, CSettings::Trigger::_SourceMax );
		m_proLevel.Create( &Settings.Trig.nLevel, 0, 255 );
		m_proTime.Create( &Settings.Trig.nTime, 0, 4096 );
		m_proHoldoff.Create( &Settings.Trig.nHoldOff, 0, 4096 );
		m_proCenter.Create( "Set" );

		m_itmSync.Create( "Mode", CWnd::WsVisible, &m_proSync, this );
		m_itmType.Create( "Type", CWnd::WsVisible, &m_proType, this );
		m_itmSource.Create( "Source", CWnd::WsVisible, &m_proSource, this );
		m_itmLevel.Create( "Threshold", CWnd::WsVisible, &m_proLevel, this );
		// pulse width limit, used only by the "<dT" / ">dT" trigger types
		m_itmTime.Create( "Pulse dT", CWnd::WsVisible, &m_proTime, this );
		m_itmHoldoff.Create( "HoldOff ms", CWnd::WsVisible, &m_proHoldoff, this );
		// sets threshold to the middle of the signal in current view
		m_itmCenter.Create( "Level 50%", CWnd::WsVisible, &m_proCenter, this );
	}
};

#endif
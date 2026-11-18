#pragma once

// LA104: automatic setup of the oscilloscope (F3). Chooses V/div and the
// vertical position of CH1 / CH2 so that the signal takes about 4..6 of the
// 8 divisions in the middle of the screen, then the time base for about
// 2.5..6 periods on the screen, and sets a rising edge trigger at the middle
// of the channel with the largest signal (Auto mode).
namespace AutoSet
{
	enum EResult { Ok, NoSignal };
	EResult Run();
}

#pragma once

namespace juce
{
	class PropertySet;
}

namespace mdJucePlugin
{
	// iOS: makes the editor window's Settings button open the full-screen settings page (iosSettingsSheet.cpp)
	void installIosSettingsSheet();
	// iOS: applies the audio cushion saved by the settings page (Audio and MIDI), before the audio device starts
	void loadIosOffloadCushion(const juce::PropertySet& _config);
}

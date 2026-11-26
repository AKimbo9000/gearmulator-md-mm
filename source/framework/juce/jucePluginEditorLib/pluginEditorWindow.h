#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "editorWindowScaleRestore.h"

namespace juceRmlUi
{
	class RmlComponent;
}

namespace jucePluginEditorLib
{
	class PluginEditorState;

	//==============================================================================
#if JUCE_IOS
	// Set by the instrument (mdJucePlugin/iosSettingsSheet.cpp): opens its full-screen settings over _window, the
	// editor window. _openEditorSettings opens the editor's own settings pages. Unset, the Settings button opens those.
	extern std::function<void(juce::Component& _window, std::function<void()> _openEditorSettings)> g_iosOpenSettingsSheet;
#endif

	class EditorWindow : public juce::AudioProcessorEditor, juce::Timer
	{
	public:
	    explicit EditorWindow (juce::AudioProcessor& _p, PluginEditorState& _s, juce::PropertiesFile& _config);
	    ~EditorWindow() override;

		void paint(juce::Graphics& g) override {}

		void resized() override;

		int getControlParameterIndex(Component&) override;

		// A combined product may host this editor inside a larger editor. In that
		// case the parent owns sizing and the normal standalone parent-size repair
		// must not expand the entire composite to this panel's saved scale.
		void setEmbedded(bool _embedded);

#if JUCE_IOS
		// test hook: taps the Settings button once the editor has loaded; false while it has not
		bool clickSettingsIfReady();
#endif

	private:
		void setGuiScale(float _percent);
		void setUiRoot(juce::Component* _component);
		void updateSizeConstrainer();
		void onSkinSizeChanged();

		juceRmlUi::RmlComponent* getRmlComponent() const;
		bool isFreeWindowMode() const;
		juce::Point<int> constrainSize(const juce::Point<int>& _size) const;
		void setSizeIfDifferent(const juce::Point<int>& _size);

		juce::Point<int> getSizeForScale(float _percent) const;
		juce::Point<int> getSizeForSkinScale(float _scale) const;
		bool isMinimumSize(int _width, int _height) const;

		void timerCallback() override;
		void fixParentWindowSize() const;

		PluginEditorState& m_state;
		juce::PropertiesFile& m_config;

	    juce::ComponentBoundsConstrainer m_sizeConstrainer;
		EditorWindowScaleRestore m_scaleRestore;
		// editor pixels per skin pixel, which is the GUI scale in percent times the skin's root scale. In the free
		// window mode it is the zoom and does not depend on the window size.
		float m_skinScale = 0.0f;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
	#if JUCE_IOS
		std::unique_ptr<juce::TextButton> m_gearButton;	// always visible on iOS: opens the settings
#endif
	};
}

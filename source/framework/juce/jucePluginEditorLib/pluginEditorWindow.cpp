#include "pluginEditorWindow.h"

#include <cstdlib>

#include "pluginEditor.h"
#include "pluginEditorState.h"

#include "dsp56kBase/logging.h"

#include "juceRmlPlugin/rmlParameterBinding.h"

#include "juceRmlUi/juceRmlComponent.h"

#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace jucePluginEditorLib
{
#if JUCE_IOS
namespace
{
	void openSettingsWhenReady(juce::Component::SafePointer<EditorWindow> _window, int _triesLeft);
}
#endif

//==============================================================================
EditorWindow::EditorWindow(juce::AudioProcessor& _p, PluginEditorState& _s, juce::PropertiesFile& _config)
	: AudioProcessorEditor(&_p), m_state(_s), m_config(_config)
{
	addMouseListener(this, true);

	m_state.evSkinLoaded = [&](juce::Component* _component)
	{
		setUiRoot(_component);
	};

	m_state.evSetGuiScale = [&](const int _scale)
	{
		if(getNumChildComponents() > 0)
			setGuiScale(static_cast<float>(_scale));
	};

	m_state.evSkinSizeChanged = [&]
	{
		if(getNumChildComponents() > 0)
			onSkinSizeChanged();
	};

	m_state.evFreeWindowSizeRestored = [&]
	{
		if(getNumChildComponents() > 0 && isFreeWindowMode())
			setSizeIfDifferent(constrainSize({m_state.getFreeWindowSize().width, m_state.getFreeWindowSize().height}));
	};

	setUiRoot(m_state.getUiRoot());
#if JUCE_IOS
	// iOS has no right click: an always-visible gear opens the settings (the long press opens the context menu)
	m_gearButton = std::make_unique<juce::TextButton>("Settings");
	m_gearButton->setTooltip("Settings");
	m_gearButton->onClick = [this]
	{
		auto* editor = m_state.getEditor();
		if (!editor)
			return;
		if (editor->settingsOpened())
		{
			editor->showSettings(false);
			return;
		}
		if (!g_iosOpenSettingsSheet)
		{
			editor->showSettings(true);
			return;
		}
		juce::Component::SafePointer<EditorWindow> self(this);
		g_iosOpenSettingsSheet(*this, [self]
		{
			if (self)
				if (auto* e = self->m_state.getEditor())
					e->showSettings(true);
		});
	};
	addAndMakeVisible(*m_gearButton);
	// landscape only, as in the Info.plist (source/cmake/juce.cmake): the view controller reports these orientations
	juce::Desktop::getInstance().setOrientationsEnabled(juce::Desktop::rotatedClockwise | juce::Desktop::rotatedAntiClockwise);
	// test hook (device screenshots of the settings page, which cannot be tapped from the Mac)
	if (std::getenv("GEARMULATOR_IOS_OPEN_SETTINGS"))
		openSettingsWhenReady(juce::Component::SafePointer<EditorWindow>(this), 30);
#endif
}

EditorWindow::~EditorWindow()
{
	m_state.evSetGuiScale = [&](int){};
	m_state.evSkinSizeChanged = [&]{};
	m_state.evFreeWindowSizeRestored = [&]{};
	m_state.evSkinLoaded = [&](juce::Component*){};

	setUiRoot(nullptr);
}

#if JUCE_IOS
std::function<void(juce::Component&, std::function<void()>)> g_iosOpenSettingsSheet;

namespace
{
	// The part of the screen the panel may use: the display minus its safe-area insets (notch, Dynamic Island,
	// home indicator). Rotation swaps it, so it is read whenever it is needed.
	juce::Rectangle<int> iosScreenArea()
	{
		if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
			return display->totalArea;
		return {};
	}

	juce::Rectangle<int> iosSafeArea()
	{
		if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
			return display->safeAreaInsets.subtractedFrom(display->totalArea);
		return {};
	}

	// On iOS the editor covers the whole window (full screen, no title bar). resized() fits the panel into the safe
	// area and centres it, which leaves room for the Settings button beside or below it. A panel-sized editor was
	// placed top-left by the standalone window and left the button no room but the panel's corner.
	// test hook: tap the Settings button once the editor exists (it loads after the window on a device)
	void openSettingsWhenReady(juce::Component::SafePointer<EditorWindow> _window, const int _triesLeft)
	{
		juce::Timer::callAfterDelay(1000, [_window, _triesLeft]
		{
			if (!_window)
				return;
			if (_window->clickSettingsIfReady() || _triesLeft <= 0)
				return;
			openSettingsWhenReady(_window, _triesLeft - 1);
		});
	}

	juce::Point<int> iosEditorSize(const juce::Component& _editor)
	{
		if (const auto* top = _editor.getTopLevelComponent(); top && top != &_editor && !top->getLocalBounds().isEmpty())
			return {top->getWidth(), top->getHeight()};
		if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
			return {display->totalArea.getWidth(), display->totalArea.getHeight()};
		return {};
	}
}
#endif

#if JUCE_IOS
bool EditorWindow::clickSettingsIfReady()
{
	const auto* editor = m_state.getEditor();
	if (!m_gearButton || !editor || !editor->getRmlComponent() || !isShowing())
		return false;
	// triggerClick() posts a command message that never reached the button here; call the handler directly
	if (m_gearButton->onClick)
		m_gearButton->onClick();
	return true;
}
#endif

void EditorWindow::resized()
{
	AudioProcessorEditor::resized();

	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	const auto w = getWidth();
	const auto h = getHeight();

	// in the free window mode a new size is more or less room, the scale stays
	if (isFreeWindowMode())
	{
		if (!m_state.resizeEditor(w,h))
			return;
		// same reason as below for not remembering the minimum
		if (!isMinimumSize(w, h))
			m_state.setFreeWindowSize({w, h});
		startTimer(1);
		return;
	}

	const auto scaleX = static_cast<float>(w) / static_cast<float>(m_state.getWidth());
	const auto scaleY = static_cast<float>(h) / static_cast<float>(m_state.getHeight());

	// A size that the current scale produces keeps that scale. Taking it from the pixels again would round it, and a
	// skin that changes its own size would then drift by a pixel each time. Any other size is the user's or the host's.
	const auto expected = getSizeForSkinScale(m_skinScale);
	if (std::abs(expected.x - w) > 1 || std::abs(expected.y - h) > 1)
		m_skinScale = std::min(scaleX, scaleY);

#if JUCE_IOS
	// iOS (from charlesvestal/gearmulator 78ba807c, 2dc6f3dc): fit the fixed-aspect skin INSIDE the given bounds and
	// centre it, instead of handing the editor raw bounds that are wider than the panel and clip it at one edge.
	// The constraints are recomputed here because rotating the device swaps the screen bounds.
	updateSizeConstrainer();
	{
		// the safe area of the screen, in our coordinates: clear of the notch, Dynamic Island and home indicator
		const auto safe = getLocalArea(nullptr, iosSafeArea()).getIntersection(getLocalBounds());
		const auto area = safe.isEmpty() ? getLocalBounds() : safe;

		// The panel takes the whole safe area. The Settings button goes beside the panel or below it where the panel
		// leaves that much room (portrait, and an iPad in landscape), and otherwise, as on an iPhone in landscape, over
		// the panel's top right corner, compact: on the Monomachine that is its decorative connector strip ("DC IN",
		// "POWER"), not a control. A strip of its own there cost the panel 12% of its size.
		constexpr int bw = 96, bh = 40, gap = 8;
		const auto skinW = static_cast<float>(m_state.getWidth());
		const auto skinH = static_cast<float>(m_state.getHeight());
		const auto fitIn = [&](const juce::Rectangle<int>& _r)
		{
			return std::max(0.01f, std::min(static_cast<float>(_r.getWidth()) / skinW, static_cast<float>(_r.getHeight()) / skinH));
		};
		auto fit = fitIn(area);

		// Where the height limits the panel (an iPhone in landscape), it reaches down to the screen's bottom edge, so the
		// home indicator sits on the panel's chin: the strip below the step buttons and their numbers, decorative screws
		// only. That is allowed (no control under the home indicator) as long as the chin covers the inset: on the
		// Monomachine it is 5.8% of the panel's height, 25 pt against a 21 pt inset; 5.5% is the margin used here.
		constexpr float chinShare = 0.055f;
		// the screen's own bottom edge: the standalone window makes this component 30 pt taller than the screen
		const auto screen = getLocalArea(nullptr, iosScreenArea()).getIntersection(getLocalBounds());
		const auto bottomInset = screen.getBottom() - area.getBottom();
		bool onChin = false;
		if (bottomInset > 0 && static_cast<float>(area.getWidth()) / skinW > static_cast<float>(area.getHeight()) / skinH)
		{
			const auto f = fitIn(area.withBottom(screen.getBottom()));
			if (skinH * f * chinShare >= static_cast<float>(bottomInset))
			{
				fit = f;
				onChin = true;
			}
		}
		m_skinScale = fit;
		const auto fitW = static_cast<int>(skinW * fit);
		const auto fitH = static_cast<int>(skinH * fit);
		if (!m_state.resizeEditor(fitW, fitH))
			return;
		auto panel = juce::Rectangle<int>(fitW, fitH).withCentre(area.getCentre());
		if (onChin)
			panel.setY(screen.getBottom() - fitH);
		if (auto* root = m_state.getUiRoot())
			root->setTopLeftPosition(panel.getPosition());
		if (m_gearButton)
		{
			const auto roomBeside = area.getRight() - panel.getRight();
			const auto roomBelow = area.getBottom() - panel.getBottom();
			const juce::Colour background(0xff2b3138);
			if (roomBelow >= bh + 2 * gap)
			{
				m_gearButton->setBounds(std::min(panel.getRight(), area.getRight() - gap) - bw, panel.getBottom() + gap, bw, bh);
				m_gearButton->setColour(juce::TextButton::buttonColourId, background);
			}
			else if (roomBeside >= bw + 2 * gap)
			{
				m_gearButton->setBounds(panel.getRight() + gap, panel.getY(), bw, bh);
				m_gearButton->setColour(juce::TextButton::buttonColourId, background);
			}
			else
			{
				// the connector strip is about 6% of the panel's height; inset past the corner screw
				const auto h = juce::jlimit(24, bh, juce::roundToInt(static_cast<float>(fitH) * 0.06f));
				const auto w = juce::roundToInt(static_cast<float>(h) * 3.4f);
				m_gearButton->setBounds(panel.getRight() - w - juce::roundToInt(static_cast<float>(fitW) * 0.025f), panel.getY() + 2, w, h);
				m_gearButton->setColour(juce::TextButton::buttonColourId, background.withAlpha(0.85f));
			}
			m_gearButton->toFront(false);
		}
	}
#else
	if (!m_state.resizeEditor(w,h))
		return;
#endif

	// Any size that is too small ends up at the minimum, and hosts, window managers and display changes ask for those
	// without the user doing anything. A size at the minimum therefore says nothing about the scale the user wants,
	// and remembering it opened every later window at the minimum. Fork: neither are the placeholder sizes a
	// standalone host imposes before the delayed scale restore ran.
	if (m_scaleRestore.shouldPersistResize() && !isMinimumSize(w, h))
	{
		const auto percent = 100.f * m_skinScale / m_state.getRootScale();
		m_config.setValue("scale", percent);
		m_config.saveIfNeeded();
	}

	// Prettymuch unbelievable Juce VST3 bug, but our root component is a child of the VST3 editor component
	// and that one is not resized! The host window is, the first child (our editor component) is, but the
	// root component is not! This is no drama as long as you do not have a juce OpenGL context, because
	// that one uses the "top level component" to set the clipping rectangle! W T F
	startTimer(m_scaleRestore.delayedRestorePending() ? 50 : 1);
}

int EditorWindow::getControlParameterIndex(Component& _component)
{
	// This code relies on the fact that getComponentAt() is called with a XY position
	// first and then the parameter is queried for that returned component afterwards.
	// As we do not have Juce components, we remember the last Rml element that was
	// under the mouse and query the parameter binding for that element here.
	// It would be better if there was a function like "getParameterForPosition" but unfortunately
	// Juce does not provide that.
	if (const auto* editor = m_state.getEditor())
	{
		if (const auto* comp = editor->getRmlComponent())
		{
			if (const auto* binding = editor->getRmlParameterBinding())
			{
				if (const auto* elem = comp->getLastElementByGetComponentAt())
				{
					if (const auto* param = binding->getParameterForElement(elem))
						return param->getParameterIndex();

					if (const auto* parent = elem->GetParentNode())
					{
						if (dynamic_cast<const Rml::ElementFormControlInput*>(parent))
						{
							if (const auto* param = binding->getParameterForElement(parent))
								return param->getParameterIndex();
						}
					}
				}
			}
		}
	}

	return AudioProcessorEditor::getControlParameterIndex(_component);
}

void EditorWindow::setEmbedded(const bool _embedded)
{
	m_scaleRestore.setEmbedded(_embedded);
	if(m_scaleRestore.isEmbedded())
	{
		stopTimer();
		setResizable(false, false);
		setConstrainer(nullptr);
	}
}

void EditorWindow::setGuiScale(const float _percent)
{
	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	m_skinScale = _percent / 100.0f * m_state.getRootScale();

	if (isFreeWindowMode())
	{
		// zoom: the window keeps its size, the skin gets less or more room in it
		getRmlComponent()->setFreeWindowScale(m_skinScale);
		updateSizeConstrainer();
		setSizeIfDifferent(constrainSize({getWidth(), getHeight()}));
	}
	else
	{
		auto size = getSizeForSkinScale(m_skinScale);
#if JUCE_IOS
		// the editor fills the window, resized() fits the panel inside it
		if (const auto full = iosEditorSize(*this); full.x > 0 && full.y > 0)
			size = full;
#endif
		setSize(size.x, size.y);
	}

	m_config.setValue("scale", _percent);
	m_config.saveIfNeeded();
}

juce::Point<int> EditorWindow::getSizeForScale(const float _percent) const
{
	return getSizeForSkinScale(_percent / 100.0f * m_state.getRootScale());
}

juce::Point<int> EditorWindow::getSizeForSkinScale(const float _scale) const
{
	return { static_cast<int>(static_cast<float>(m_state.getWidth()) * _scale),
		static_cast<int>(static_cast<float>(m_state.getHeight()) * _scale) };
}

bool EditorWindow::isMinimumSize(const int _width, const int _height) const
{
	return _width <= m_sizeConstrainer.getMinimumWidth() || _height <= m_sizeConstrainer.getMinimumHeight();
}

void EditorWindow::setUiRoot(juce::Component* _component)
{
	removeAllChildren();
	setConstrainer(nullptr);

	if(!_component)
		return;

	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	// versions before this one did save the minimum, and a config that holds it would keep every window at the minimum
	auto scale = static_cast<float>(m_config.getDoubleValue("scale", 100));

	if (isFreeWindowMode())
	{
		// the minimum of the fixed window mode, the one such a config holds; the skin's own minimum can be its default
		if (const auto s = getSizeForScale(scale); s.x <= m_state.getWidth() / 10 || s.y <= m_state.getHeight() / 10)
			scale = 100.0f;
		m_skinScale = scale / 100.0f * m_state.getRootScale();

		getRmlComponent()->setFreeWindowScale(m_skinScale);
		updateSizeConstrainer();

		const auto& saved = m_state.getFreeWindowSize();
		const auto size = constrainSize(saved.isValid() ? juce::Point<int>(saved.width, saved.height) : getSizeForSkinScale(m_skinScale));
		setSize(size.x, size.y);
	}
	else
	{
		updateSizeConstrainer();

		const auto size = getSizeForScale(scale);
		if (isMinimumSize(size.x, size.y))
			scale = 100.0f;

		// Fork: a standalone host can impose a small placeholder size after the editor loaded, the first attach arms
		// a delayed restore of this scale (see timerCallback). An embedded editor leaves sizing to its parent.
		m_scaleRestore.attachRoot(juce::JUCEApplicationBase::isStandaloneApp(), scale);
		if (!m_scaleRestore.isEmbedded())
			setGuiScale(scale);
	}

	_component->setSize(getWidth(), getHeight());

	addAndMakeVisible(_component);

	if(m_scaleRestore.isEmbedded())
	{
		setResizable(false, false);
		setConstrainer(nullptr);
		stopTimer();
	}
	else
	{
		setResizable(true, true);
		setConstrainer(&m_sizeConstrainer);
		startTimer(m_scaleRestore.delayedRestorePending() ? 50 : 1);
	}
}

void EditorWindow::updateSizeConstrainer()
{
	if (!isFreeWindowMode())
	{
		m_sizeConstrainer.setMinimumSize(m_state.getWidth() / 10, m_state.getHeight() / 10);
		auto maxW = m_state.getWidth() * 4;
		auto maxH = m_state.getHeight() * 4;
#if JUCE_IOS
		// the editor takes the window's shape (screen, rotation, iPad split view); the panel keeps its aspect ratio
		// inside it (resized()). A fixed ratio here made the standalone window shrink the editor to the panel.
		if (const auto full = iosEditorSize(*this); full.x > 0 && full.y > 0)
		{
			maxW = std::max(maxW, full.x);
			maxH = std::max(maxH, full.y);
		}
		m_sizeConstrainer.setMaximumSize(maxW, maxH);
		m_sizeConstrainer.setFixedAspectRatio(0.0);
		return;
#endif
		m_sizeConstrainer.setMaximumSize(maxW, maxH);

		m_sizeConstrainer.setFixedAspectRatio(static_cast<double>(m_state.getWidth()) / static_cast<double>(m_state.getHeight()));
		return;
	}

	// The limits come from min-width, min-height, max-width and max-height of the body, at the current zoom. Without
	// a minimum it is the one of the fixed window mode, a minimum that hosts and window managers ask for.
	const auto* rml = getRmlComponent();
	const auto minDoc = rml->getMinimumDocumentSize();
	const auto maxDoc = rml->getMaximumDocumentSize();

	constexpr float noMax = 0x3fffffff;

	const auto minW = minDoc.x > 0 ? juce::roundToInt(minDoc.x * m_skinScale) : m_state.getWidth() / 10;
	const auto minH = minDoc.y > 0 ? juce::roundToInt(minDoc.y * m_skinScale) : m_state.getHeight() / 10;
	const auto maxW = juce::roundToInt(std::min(maxDoc.x * m_skinScale, noMax));
	const auto maxH = juce::roundToInt(std::min(maxDoc.y * m_skinScale, noMax));

	m_sizeConstrainer.setFixedAspectRatio(0.0);
	m_sizeConstrainer.setMinimumSize(minW, minH);
	m_sizeConstrainer.setMaximumSize(std::max(minW, maxW), std::max(minH, maxH));
}

void EditorWindow::onSkinSizeChanged()
{
	updateSizeConstrainer();

	// the skin has new room, from the window or the zoom, or new limits that the window has to respect
	if (isFreeWindowMode())
	{
		setSizeIfDifferent(constrainSize({getWidth(), getHeight()}));
		return;
	}

	// the skin changed its own size, to fold part of itself away for example, which keeps the scale
#if JUCE_IOS
	// the editor keeps filling the window; fit the new panel size into it
	if (const auto full = iosEditorSize(*this); full.x > 0 && full.y > 0)
	{
		if (full.x == getWidth() && full.y == getHeight())
			resized();
		else
			setSize(full.x, full.y);
		return;
	}
#endif
	const auto size = getSizeForSkinScale(m_skinScale);

	setSize(size.x, size.y);
}

juceRmlUi::RmlComponent* EditorWindow::getRmlComponent() const
{
	const auto* editor = m_state.getEditor();
	return editor ? editor->getRmlComponent() : nullptr;
}

bool EditorWindow::isFreeWindowMode() const
{
	const auto* rml = getRmlComponent();
	return rml && rml->isFreeWindowMode();
}

juce::Point<int> EditorWindow::constrainSize(const juce::Point<int>& _size) const
{
	return { juce::jlimit(m_sizeConstrainer.getMinimumWidth(), m_sizeConstrainer.getMaximumWidth(), _size.x),
		juce::jlimit(m_sizeConstrainer.getMinimumHeight(), m_sizeConstrainer.getMaximumHeight(), _size.y) };
}

void EditorWindow::setSizeIfDifferent(const juce::Point<int>& _size)
{
	if (_size.x != getWidth() || _size.y != getHeight())
		setSize(_size.x, _size.y);
}

void EditorWindow::timerCallback()
{
	if(m_scaleRestore.isEmbedded())
	{
		stopTimer();
		return;
	}

	float restoreScale = 100.0f;
	if(m_scaleRestore.consumeDelayedRestore(restoreScale))
	{
		// A standalone host can impose its small placeholder size after the editor
		// has loaded. Reapply the configured size once that native parent exists,
		// and do not persist the placeholder resizes as the user's GUI scale.
		setGuiScale(restoreScale);
	}

	fixParentWindowSize();
	stopTimer();
}

void EditorWindow::fixParentWindowSize() const
{
#if JUCE_IOS
	// the parent chain ends at a view iOS sizes; growing it leaves the panel larger than the screen
	return;
#endif
	const auto w = getWidth();
	const auto h = getHeight();

	auto* parent = getParentComponent();

	while (parent)
	{
		if (parent->getWidth() < w || parent->getHeight() < h)
		{
			LOG("Parent " << parent->getName() << " has wrong size: " << parent->getName() <<
				", expected: " << w << "x" << h <<
				", actual: " << parent->getWidth() << "x" << parent->getHeight());
			parent->setSize(w, h);
		}

		parent = parent->getParentComponent();
	}
}
}

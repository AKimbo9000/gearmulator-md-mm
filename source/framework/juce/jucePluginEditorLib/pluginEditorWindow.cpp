#include "pluginEditorWindow.h"

#include "pluginEditor.h"
#include "pluginEditorState.h"

#include "dsp56kBase/logging.h"

#include "juceRmlPlugin/rmlParameterBinding.h"

#include "juceRmlUi/juceRmlComponent.h"

#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace jucePluginEditorLib
{

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
		if (auto* editor = m_state.getEditor())
			editor->showSettings(!editor->settingsOpened());
	};
	addAndMakeVisible(*m_gearButton);
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
namespace
{
	// The part of the screen the panel may use: the display minus its safe-area insets (notch, Dynamic Island,
	// home indicator). Rotation swaps it, so it is read whenever it is needed.
	juce::Rectangle<int> iosSafeArea()
	{
		if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
			return display->safeAreaInsets.subtractedFrom(display->totalArea);
		return {};
	}
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
		const auto fit = std::min(static_cast<float>(area.getWidth()) / static_cast<float>(m_state.getWidth()),
		                          static_cast<float>(area.getHeight()) / static_cast<float>(m_state.getHeight()));
		m_skinScale = fit;
		const auto fitW = static_cast<int>(static_cast<float>(m_state.getWidth()) * fit);
		const auto fitH = static_cast<int>(static_cast<float>(m_state.getHeight()) * fit);
		if (!m_state.resizeEditor(fitW, fitH))
			return;
		if (auto* root = m_state.getUiRoot())
			root->setTopLeftPosition(area.getX() + (area.getWidth() - fitW) / 2, area.getY() + (area.getHeight() - fitH) / 2);
		if (m_gearButton)
		{
			// never on top of the panel: beside it (landscape), else below it (portrait), else in the corner
			const int bw = 96, bh = 40;
			const int panelRight = area.getX() + (area.getWidth() + fitW) / 2;
			const int panelBottom = area.getY() + (area.getHeight() + fitH) / 2;
			if (area.getRight() - panelRight >= bw + 8)
				m_gearButton->setBounds(panelRight + (area.getRight() - panelRight - bw) / 2, area.getY() + 8, bw, bh);
			else if (area.getBottom() - panelBottom >= bh + 8)
				m_gearButton->setBounds(area.getRight() - bw - 8, panelBottom + 8, bw, bh);
			else
				m_gearButton->setBounds(area.getRight() - bw - 4, area.getY() + 4, bw, bh);
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
		// never ask for more room than the screen's safe area: an iOS view is clipped to it
		const auto avail = getParentComponent() ? getParentComponent()->getLocalBounds() : iosSafeArea();
		if (!avail.isEmpty() && size.x > 0 && size.y > 0)
		{
			const auto fit = std::min(static_cast<float>(avail.getWidth()) / static_cast<float>(size.x),
			                          static_cast<float>(avail.getHeight()) / static_cast<float>(size.y));
			if (fit < 1.0f)
				size = {static_cast<int>(static_cast<float>(size.x) * fit), static_cast<int>(static_cast<float>(size.y) * fit)};
		}
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
		// a fixed aspect ratio with a 4x maximum grows the width past the screen edge; there is no window to drag
		if (const auto safe = iosSafeArea(); !safe.isEmpty())
		{
			maxW = std::min(maxW, safe.getWidth());
			maxH = std::min(maxH, safe.getHeight());
		}
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

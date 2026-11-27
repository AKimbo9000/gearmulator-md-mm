// iOS settings: one full-screen, scrollable page with touch-sized controls, opened by the Settings button of the editor
// window. On an iPhone the editor's own settings pages are drawn inside the panel and are small, so this page holds
// what iOS needs (engine, audio, MIDI, firmware, state) and opens the editor pages for the rest of the desktop options.

#include "iosSettingsSheet.h"

#include "mdPluginProcessor.h"

#if JUCE_IOS

#include "iosEngine.h"

#include "jucePluginEditorLib/pluginEditorWindow.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/parameterlocking.h"

#include "mdLib/mdromloader.h"

#include "baseLib/filesystem.h"

#include "juce_audio_utils/juce_audio_utils.h"
#include "juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"

#include <cstdlib>
#include <thread>

// jucePluginLib/processor.cpp (iOS real-time meter)
void tusRealtimeLast(double& _rtf, double& _callbacksPerSecond, double& _peak);
void tusPlayTestTone(double _seconds);
void tusSetOffloadCushionMs(double _ms);
double tusOffloadCushionMs();
double tusOffloadActiveCushionMs();

namespace mdJucePlugin
{
	namespace
	{
		constexpr int g_buttonHeight = 48;
		constexpr int g_margin = 16;
		constexpr int g_spacing = 10;
		const juce::Colour g_background(0xff1d2227);
		const juce::Colour g_text(0xffeef1f4);
		const juce::Colour g_secondary(0xffaab3bc);
		const juce::Colour g_good(0xff5fd27a);
		const juce::Colour g_warn(0xffffa94d);

		AudioPluginAudioProcessor* processor()
		{
			auto* holder = juce::StandalonePluginHolder::getInstance();
			return holder ? dynamic_cast<AudioPluginAudioProcessor*>(holder->processor.get()) : nullptr;
		}

		bool isMonomachine()
		{
			const auto* p = processor();
			return !p || p->getModel() == md::MachineModel::Monomachine;
		}

		juce::String romName() { return isMonomachine() ? "Monomachine OS 1.32b" : "Machinedrum OS 1.63"; }

		constexpr const char* g_cushionKey = "iosOffloadCushionMs";
		constexpr double g_cushionDefaultMs = 10.0;
		// 0 leaves one block queued, the least the worker needs
		constexpr std::pair<double, const char*> g_cushions[] = {{0.0, "One block"}, {10.0, "10 ms"}, {25.0, "25 ms"}, {50.0, "50 ms"}};

		// Runs _change while no DSP runs: the audio callback keeps going and outputs silence meanwhile. The player
		// takes the callback's lock, so a block in progress finishes first.
		void withAudioStopped(const std::function<void()>& _change)
		{
			auto* holder = juce::StandalonePluginHolder::getInstance();
			if(holder)
				holder->stopPlaying();
			_change();
			if(holder)
				holder->startPlaying();
		}

		// Checks a ROM image (8 MiB, md::RomLoader fingerprint of this instrument's OS), copies it into the data folder's
		// roms/ and restarts the device with it. _message says what happened.
		bool importRomImage(const juce::MemoryBlock& _data, const juce::String& _name, juce::String& _message)
		{
			constexpr size_t romSize = 8 * 1024 * 1024;
			auto* p = processor();
			if(!p)
			{
				_message = "No instrument to import into.";
				return false;
			}
			if(_data.getSize() != romSize)
			{
				_message = "\"" + _name + "\" is " + juce::String(static_cast<int64_t>(_data.getSize())) +
					" bytes, not the 8 MiB (8,388,608 bytes) " + romName() + " image. Nothing was imported.";
				return false;
			}
			const auto* bytes = static_cast<const uint8_t*>(_data.getData());
			const std::vector<uint8_t> image(bytes, bytes + _data.getSize());
			if(!md::RomLoader::isRomForModel(image, p->getModel()))
			{
				_message = "\"" + _name + "\" is 8 MiB but not " + romName() + " (fingerprint mismatch). Nothing was imported.";
				return false;
			}
			const auto folder = juce::File(juce::String::fromUTF8(p->getDataFolder().c_str())).getChildFile("roms");
			const auto target = folder.getChildFile(isMonomachine() ? "mm-os1.32b.bin" : "md-os1.63.bin");
			if(!folder.createDirectory() || !target.replaceWithData(_data.getData(), _data.getSize()))
			{
				_message = "Could not write " + target.getFullPathName();
				return false;
			}
			bool rebooted = false;
			withAudioStopped([&] { rebooted = p->rebootDevice(); });
			_message = romName() + " ROM imported" + (rebooted ? ", the device restarted with it." : ". Restart the app to use it.");
			return true;
		}

		// wrapped text whose height follows the width
		class TextBlock : public juce::Component
		{
		public:
			TextBlock(juce::String _text, const float _size, const juce::Colour _colour, const bool _bold = false)
				: m_text(std::move(_text)), m_font(_size, _bold ? juce::Font::bold : juce::Font::plain), m_colour(_colour)
			{
				setInterceptsMouseClicks(false, false);
			}

			void setText(const juce::String& _text, const juce::Colour _colour)
			{
				if(_text == m_text && _colour == m_colour)
					return;
				m_text = _text;
				m_colour = _colour;
				repaint();
				if(getParentComponent() && getWidth() > 0 && heightForWidth(getWidth()) != getHeight())
					getParentComponent()->resized();
			}

			int heightForWidth(const int _width) const
			{
				if(m_text.isEmpty())
					return 0;
				juce::TextLayout layout;
				layout.createLayout(attributed(), static_cast<float>(_width));
				return static_cast<int>(std::ceil(layout.getHeight())) + 4;
			}

			void paint(juce::Graphics& _g) override
			{
				attributed().draw(_g, getLocalBounds().toFloat());
			}

		private:
			juce::AttributedString attributed() const
			{
				juce::AttributedString a;
				a.setWordWrap(juce::AttributedString::byWord);
				a.append(m_text, m_font, m_colour);
				return a;
			}

			juce::String m_text;
			juce::Font m_font;
			juce::Colour m_colour;
		};

		// the page: rows stacked top to bottom at the viewport's width
		class Column : public juce::Component
		{
		public:
			template<typename T> T& add(std::unique_ptr<T> _row, const int _gapAbove = g_spacing)
			{
				auto& r = *_row;
				addAndMakeVisible(r);
				m_rows.push_back({std::move(_row), _gapAbove});
				return r;
			}

			void layout(const int _width)
			{
				if(m_inLayout)
					return;
				const juce::ScopedValueSetter<bool> guard(m_inLayout, true);
				const int w = _width - 2 * g_margin;
				int y = g_margin;
				for(auto& row : m_rows)
				{
					auto* c = row.component.get();
					y += row.gapAbove;
					if(auto* t = dynamic_cast<TextBlock*>(c))
						c->setBounds(g_margin, y, w, t->heightForWidth(w));
					else if(dynamic_cast<juce::AudioDeviceSelectorComponent*>(c))
						c->setBounds(g_margin, y, w, std::max(c->getHeight(), 100));		// sizes its own height
					else
						c->setBounds(g_margin, y, w, g_buttonHeight);
					y = c->getBottom();
				}
				setSize(_width, y + g_margin * 3);
			}

			void resized() override { layout(getWidth()); }
			void childBoundsChanged(juce::Component*) override { if(getWidth() > 0) layout(getWidth()); }

		private:
			struct Row
			{
				std::unique_ptr<juce::Component> component;
				int gapAbove;
			};
			std::vector<Row> m_rows;
			bool m_inLayout = false;
		};

		class Sheet;
		juce::Component::SafePointer<Sheet> g_sheet;

		// the JIT switch's progress, message thread only; shown by the sheet whenever it is open
		juce::String g_jitStatusText;
		juce::Colour g_jitStatusColour = g_secondary;
		void setJitStatus(const juce::String& _text, const juce::Colour _colour)
		{
			g_jitStatusText = _text;
			g_jitStatusColour = _colour;
		}

		class Sheet : public juce::Component, juce::Timer
		{
		public:
			explicit Sheet(std::function<void()> _openEditorSettings) : m_openEditorSettings(std::move(_openEditorSettings))
			{
				setOpaque(true);

				m_title.setText("Settings", juce::dontSendNotification);
				m_title.setFont(juce::Font(26.0f, juce::Font::bold));
				m_title.setColour(juce::Label::textColourId, g_text);
				addAndMakeVisible(m_title);

				m_done.setButtonText("Done");
				m_done.onClick = [this] { close(); };
				addAndMakeVisible(m_done);

				m_viewport.setViewedComponent(&m_column, false);
				m_viewport.setScrollBarsShown(true, false);
				m_viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
				addAndMakeVisible(m_viewport);

				build();
				update();
				updateRom();
				startTimer(500);
			}

			void close()
			{
				setVisible(false);
				juce::MessageManager::callAsync([self = juce::Component::SafePointer<Sheet>(this)]
				{
					delete self.getComponent();
				});
			}

			void paint(juce::Graphics& _g) override { _g.fillAll(g_background); }

			void parentSizeChanged() override
			{
				if(auto* p = getParentComponent())
					setBounds(p->getLocalBounds());
			}

			void resized() override
			{
				// clear of the notch, Dynamic Island and home indicator
				auto area = getLocalBounds();
				if(const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
					area = getLocalArea(nullptr, d->safeAreaInsets.subtractedFrom(d->totalArea)).getIntersection(area);
				auto top = area.removeFromTop(60).reduced(g_margin, 8);
				m_done.setBounds(top.removeFromRight(110));
				m_title.setBounds(top);
				m_viewport.setBounds(area);
				m_column.layout(m_viewport.getMaximumVisibleWidth());
			}

			void setStatus(TextBlock* _block, const juce::String& _text, const juce::Colour _colour)
			{
				if(_block)
					_block->setText(_text, _colour);
			}

			TextBlock* jitStatus() { return m_jitStatus; }
			TextBlock* romStatus() { return m_romStatus; }

		private:
			TextBlock& heading(const juce::String& _text)
			{
				return m_column.add(std::make_unique<TextBlock>(_text, 22.0f, g_text, true), 28);
			}

			TextBlock& text(const juce::String& _text, const juce::Colour _colour = g_secondary, const float _size = 16.0f)
			{
				return m_column.add(std::make_unique<TextBlock>(_text, _size, _colour));
			}

			juce::TextButton& button(const juce::String& _text, std::function<void()> _onClick)
			{
				auto& b = m_column.add(std::make_unique<juce::TextButton>(_text));
				b.onClick = std::move(_onClick);
				return b;
			}

			void build()
			{
				auto* holder = juce::StandalonePluginHolder::getInstance();

				// --- engine
				heading("Engine");
				m_engineBadge = &text({}, g_text, 20.0f);
				m_engineReason = &text({});
				m_live = &text({}, g_text);
				m_warning = &text({}, g_warn);
				m_jitButton = &button("Relaunch with JIT (StikDebug)", [this] { relaunchWithJit(); });
				m_jitStatus = &text({});
				text("Without StikDebug the interpreter runs the DSPs, at about 0.5 to 0.6x real time on these devices. "
					"With StikDebug the JIT runs them at about 2.5x. StikDebug and LocalDevVPN must be running "
					"(doc/JIT_SETUP.md).");

				// --- audio and MIDI
				heading("Audio and MIDI");
				if(holder)
				{
					auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(holder->deviceManager,
						0, 0, 0, 2, true, true, true, false);
					selector->setItemHeight(40);
					m_selector = &m_column.add(std::move(selector));
				}
				text("Output, sample rate and buffer size, then the MIDI inputs and the MIDI output. Bluetooth MIDI "
					"devices appear there once paired.");
				m_cushionStatus = &text({});
				for(const auto& [ms, label] : g_cushions)
				{
					const double value = ms;
					const auto text = juce::String("Audio cushion: ") + label + (ms == g_cushionDefaultMs ? " (default)" : "");
					auto& b = button(text, [value]
					{
						tusSetOffloadCushionMs(value);
						if(auto* p = processor())
						{
							p->getConfig().setValue(g_cushionKey, value);
							p->getConfig().saveIfNeeded();
						}
						withAudioStopped([] {});		// the worker takes the new size when the audio device restarts
					});
					m_cushionButtons.push_back({value, text, &b});
				}
				button("Play a test tone (440 Hz, 2 s)", [] { tusPlayTestTone(2.0); });
				button("Pair a Bluetooth MIDI device", []
				{
					if(!juce::BluetoothMidiDevicePairingDialogue::open())
						juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Bluetooth MIDI",
							"Bluetooth MIDI pairing is not available on this device.");
				});

				// --- firmware
				heading("Firmware");
				m_romStatus = &text({}, g_text);
				button("Import the ROM from Files", [this] { importRom(); });
				text("The file is checked (8 MiB, " + romName() + " fingerprint) and copied into this app's folder; "
					"the app itself contains no firmware.");

				// --- the desktop editor's options
				heading("Editor options");
				button("Open the editor's settings pages", [this]
				{
					auto open = m_openEditorSettings;
					close();
					if(open)
						open();
				});
				text("Skin, GUI (panel feel, LCD rendering, machine storage), MIDI (ports, routing matrix, panic, reboot), "
					"MIDI Learn, Panel MIDI, DSP & Audio and DSP Bridge: the desktop settings pages. On an iPhone they "
					"are larger in landscape.");
				// --- the context menu's global entries (the desktop right-click menu); entries about one parameter
				// (its links, its region, patch manager actions on a selection) stay on the long-press
				heading("Panel menu");
				text("Long-press the panel or a control for its context menu, the desktop right-click menu: parameter "
					"links and the region of that control, the patch manager, MIDI ports and layout. Its global "
					"entries are here as well:");
				button("Unlock all parameter regions", [] { setAllRegionsLocked(false); });
				button("Lock all parameter regions", [] { setAllRegionsLocked(true); });
				m_diagButton = &button("Start performance capture", []
				{
					if(auto* p = processor())
						p->setPerformanceDiagnosticsEnabled(!p->performanceDiagnosticsActive());
				});
				m_diagStatus = &text({});
				if(!isMonomachine())
				{
					text("RAM recording (Machinedrum):", g_text);
					m_ramComplete = &button("Complete tails (recommended)", [] { setRamRecording(md::RamRecordingMode::CompleteTail); });
					m_ramOriginal = &button("Original finalization", [] { setRamRecording(md::RamRecordingMode::Original); });
				}

				// --- the standalone app's own menu
				heading("State");
				if(holder)
				{
					button("Save the current state", [] { if(auto* h = juce::StandalonePluginHolder::getInstance()) h->askUserToSaveState(); });
					button("Load a saved state", [] { if(auto* h = juce::StandalonePluginHolder::getInstance()) h->askUserToLoadState(); });
				}
				button("Reboot the device", []
				{
					if(auto* p = processor())
						withAudioStopped([p] { p->rebootDevice(); });
				});
				button("Reset to the default state", [this] { resetToDefault(); });
			}

			void timerCallback() override { update(); }

			// iOS picks the IO buffer itself (257 frames on the iPad at 48 kHz) and JUCE lists only powers of two, so its
			// buffer-size box showed nothing. Add the session's size to the box and select it, without a change message.
			void showSessionBufferSize()
			{
				auto* holder = juce::StandalonePluginHolder::getInstance();
				auto* device = holder ? holder->deviceManager.getCurrentAudioDevice() : nullptr;
				if(!m_selector || !device)
					return;
				const int current = device->getCurrentBufferSizeSamples();
				const double rate = device->getCurrentSampleRate();
				if(current <= 0 || rate <= 0)
					return;
				std::function<void(juce::Component&)> visit = [&](juce::Component& _parent)
				{
					for(auto* child : _parent.getChildren())
					{
						auto* box = dynamic_cast<juce::ComboBox*>(child);
						if(box && box->getNumItems() > 0 && box->getItemText(0).contains("samples") && box->getSelectedId() != current)
						{
							if(box->indexOfItemId(current) < 0)
								box->addItem(juce::String(current) + " samples (" + juce::String(current * 1000.0 / rate, 1) + " ms), set by iOS", current);
							box->setSelectedId(current, juce::dontSendNotification);
						}
						visit(*child);
					}
				};
				visit(*m_selector);
			}

			static void setAllRegionsLocked(const bool _lock)
			{
				auto* p = processor();
				if(!p)
					return;
				auto& controller = p->getController();
				const auto part = controller.getCurrentPart();
				auto& locking = controller.getParameterLocking();
				for(const auto& region : controller.getParameterDescriptions().getRegions())
				{
					if(_lock)
						locking.lockRegion(part, region.first);
					else
						locking.unlockRegion(part, region.first);
				}
			}

			static void setRamRecording(const md::RamRecordingMode _mode)
			{
				if(auto* p = processor())
					p->setRamRecordingMode(_mode);
			}

			void update()
			{
				const bool jit = tusEngine::jitActive();
				m_engineBadge->setText(jit ? "JIT (StikDebug)" : "Interpreter", jit ? g_good : g_warn);
				m_engineReason->setText(juce::String("Why: ") + tusEngineDescription(), g_secondary);

				double rtf = 0, callbacks = 0, peak = 0;
				tusRealtimeLast(rtf, callbacks, peak);
				m_live->setText(rtf > 0 ? juce::String::formatted("Real time: %.2fx   emulated blocks: %.0f/s   output peak: %.2f", rtf, callbacks, peak)
				                       : juce::String("Real time: measuring..."), g_text);
				m_warning->setText(!jit && rtf > 0 && rtf < 0.98
					? juce::String::formatted("Below real time (%.2fx): the tempo slows down and the sound breaks up. Relaunch with JIT.", rtf)
					: juce::String(), g_warn);
				m_jitStatus->setText(g_jitStatusText, g_jitStatusColour);
				showSessionBufferSize();
				const double cushion = tusOffloadActiveCushionMs();
				m_cushionStatus->setText(juce::String::formatted("Audio cushion in effect: %.1f ms. The emulation renders two audio "
					"buffers per block and runs ahead of the output by at least one block plus this much, so a block may take "
					"longer than its duration without a gap, such as when the JIT compiles code the firmware runs for the first "
					"time (about 2 ms at the first Play). The sound plays this much later.", cushion), g_secondary);
				for(const auto& [value, text, b] : m_cushionButtons)
				{
					const bool selected = value == tusOffloadCushionMs();
					b->setToggleState(selected, juce::dontSendNotification);
					b->setButtonText(selected ? text + " (selected)" : text);		// the button font has no check mark glyph
				}
				if(auto* p = processor())
				{
					m_diagButton->setButtonText(p->performanceDiagnosticsActive() ? "Stop performance capture" : "Start performance capture");
					m_diagStatus->setText(juce::String(p->performanceDiagnosticsStatus()) + " (Files app: " +
						p->performanceDiagnosticsFolder().getFileName() + ")", g_secondary);
					if(m_ramComplete && m_ramOriginal)
					{
						const bool available = p->isRamRecordingModeAvailable();
						const auto mode = p->getRamRecordingMode();
						m_ramComplete->setEnabled(available);
						m_ramOriginal->setEnabled(available);
						m_ramComplete->setToggleState(mode == md::RamRecordingMode::CompleteTail, juce::dontSendNotification);
						m_ramOriginal->setToggleState(mode == md::RamRecordingMode::Original, juce::dontSendNotification);
					}
				}
				m_jitButton->setEnabled(!jit);
				m_jitButton->setButtonText(jit ? "JIT is running" : "Relaunch with JIT (StikDebug)");
			}

			// reads the ROM folders and fingerprints the image: on opening and after an import, not on the timer
			void updateRom()
			{
				const auto rom = isMonomachine() ? md::RomLoader::findROM(md::MachineModel::Monomachine) : md::RomLoader::findROM(md::MachineModel::Machinedrum);
				m_romStatus->setText(rom.isValid()
					? romName() + " ROM: found (" + juce::String(baseLib::filesystem::getFilenameWithoutPath(rom.getFilename())) + ")"
					: romName() + " ROM: not found. Import it below.", rom.isValid() ? g_text : g_warn);
			}

			// StikDebug attaches to this process; then the executable region is requested and the device recreated
			// on the JIT, with the state carried over.
			// StikDebug attaches to this process; the JIT watcher (startJitWatcher) then switches the engine
			static void relaunchWithJit()
			{
				setJitStatus("Opening StikDebug...", g_secondary);
				tusEngine::requestJitFromStikDebug([](const bool _opened)
				{
					juce::MessageManager::callAsync([_opened]
					{
						setJitStatus(_opened ? "Waiting for StikDebug to attach..."
						                     : "StikDebug did not open. Is it installed? Or start this app from StikDebug.",
							_opened ? g_secondary : g_warn);
					});
				});
			}

			void importRom()
			{
				m_chooser = std::make_unique<juce::FileChooser>("Choose the " + romName() + " ROM", juce::File(), "*");
				m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
					[self = juce::Component::SafePointer<Sheet>(this)](const juce::FileChooser& _chooser)
				{
					const auto url = _chooser.getURLResult();
					if(url.isEmpty() || !self)
						return;
					juce::MemoryBlock data;
					if(auto in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)))
						in->readIntoMemoryBlock(data);
					else if(url.isLocalFile())
						url.getLocalFile().loadFileAsData(data);
					self->acceptRom(data, url.getFileName());
				});
			}

			void acceptRom(const juce::MemoryBlock& _data, const juce::String& _name)
			{
				juce::String message;
				const bool imported = importRomImage(_data, _name, message);
				updateRom();
				setStatus(m_romStatus, message, imported ? g_good : g_warn);
			}

			void resetToDefault()
			{
				juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Reset to the default state",
					"This replaces the current state of the instrument with the default one.", "Reset", "Cancel", nullptr,
					juce::ModalCallbackFunction::create([self = juce::Component::SafePointer<Sheet>(this)](const int _result)
					{
						if(_result == 0 || !self)
							return;
						auto* window = dynamic_cast<juce::StandaloneFilterWindow*>(self->getTopLevelComponent());
						self->close();
						// the window rebuilds the editor, which this page is part of: after this callback has returned
						juce::MessageManager::callAsync([w = juce::Component::SafePointer<juce::StandaloneFilterWindow>(window)]
						{
							if(w)
								w->resetToDefaultState();
						});
					}));
			}

			std::function<void()> m_openEditorSettings;
			juce::Label m_title;
			juce::TextButton m_done;
			juce::Viewport m_viewport;
			Column m_column;
			std::unique_ptr<juce::FileChooser> m_chooser;

			TextBlock* m_engineBadge = nullptr;
			TextBlock* m_engineReason = nullptr;
			TextBlock* m_live = nullptr;
			TextBlock* m_warning = nullptr;
			TextBlock* m_jitStatus = nullptr;
			TextBlock* m_romStatus = nullptr;
			juce::TextButton* m_jitButton = nullptr;
			juce::AudioDeviceSelectorComponent* m_selector = nullptr;
			juce::TextButton* m_diagButton = nullptr;
			TextBlock* m_diagStatus = nullptr;
			std::vector<std::tuple<double, juce::String, juce::TextButton*>> m_cushionButtons;
			TextBlock* m_cushionStatus = nullptr;
			juce::TextButton* m_ramComplete = nullptr;
			juce::TextButton* m_ramOriginal = nullptr;
		};
	}

	namespace
	{
		// JIT detection after launch. StikDebug can attach once the app runs (Relaunch with JIT, or its own Enable JIT on
		// the running app), or attach at launch but answer the prepare request only after the load-time handshake gave
		// up. Either way the DSPs move to the JIT: the region is requested, then the device is recreated with audio
		// stopped (its state carries over). One request per attach: CS_DEBUGGED (debugged()) is set by the first attach
		// and stays set after a detach, so a later attach shows as P_TRACED (traced()) turning on.
		//
		// StikDebug's Enable JIT list starts an app that has no script assigned, attaches and detaches at once
		// (doc/JIT_SETUP.md): the app is debugged at launch, nothing answered its request, and no debugger is attached
		// any more. Then the app asks StikDebug again itself, once per run, with the URL of Relaunch with JIT, which names
		// universal.js; StikDebug's second attach is a P_TRACED edge.
		void requestJitAgain()
		{
			tusEngine::log("auto-request: StikDebug launched this app without its script and detached; asking it again with universal.js");
			juce::MessageManager::callAsync([]
			{
				setJitStatus("StikDebug started this app without its universal.js script. Asking StikDebug again...", g_secondary);
				tusEngine::requestJitFromStikDebugWhenActive([](const bool _opened)
				{
					tusEngine::log(std::string("auto-request: StikDebug ") + (_opened ? "opened" : "did not open"));
					setJitStatus(_opened ? "Waiting for StikDebug to attach..." : "StikDebug did not open. Is it installed?",
						_opened ? g_secondary : g_warn);
				});
			});
		}

		void startJitWatcher()
		{
			static bool started = false;
			if(started)
				return;
			started = true;
			std::thread([]
			{
				bool tried = false;				// a request was made since CS_DEBUGGED turned on
				bool wasTraced = false;
				bool askedAgain = false;
				while(!tusEngine::jitActive())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					const bool traced = tusEngine::traced();
					const bool attached = traced && !wasTraced;
					wasTraced = traced;
					if(!tusEngine::debugged())
					{
						tried = false;
						continue;
					}
					if(!askedAgain && !traced && tusEngine::launchedWithoutScript())
					{
						askedAgain = true;
						tried = true;			// the load-time handshake asked already and the debugger has gone
						if(tusEngine::stikDebugRunsScripts())
						{
							requestJitAgain();
						}
						else
						{
							tusEngine::log("auto-request: not asking StikDebug again, it runs no scripts on this device");
							juce::MessageManager::callAsync([]
							{
								setJitStatus("StikDebug attached without its universal.js script, and it runs scripts only on TXM devices.", g_warn);
							});
						}
						continue;
					}
					if(tried && !attached)
						continue;
					tried = true;
					tusEngine::log(std::string("watcher: debugger attached (") + (attached ? "P_TRACED set" : "CS_DEBUGGED set") +
						"), requesting the region");
					std::string reason;
					const bool acquired = tusEngine::acquireJitRegion(reason);
					// a debugger that did not answer may still be attached: the next request waits for a new attach
					wasTraced = tusEngine::traced();
					tusEngine::log(std::string("watcher: region ") + (acquired ? "acquired, " : "not acquired, ") + reason);
					if(!acquired)
					{
						juce::MessageManager::callAsync([reason] { setJitStatus("JIT not enabled: " + juce::String(reason), g_warn); });
						continue;
					}
					const bool posted = juce::MessageManager::callAsync([reason]
					{
						auto* p = processor();
						tusEngine::log(std::string("switch: on the message thread, processor ") + (p ? "found" : "MISSING"));
						if(!p)
							return;
						bool rebooted = false;
						withAudioStopped([&] { tusEngine::activateJit(); rebooted = p->rebootDevice(); });
						tusEngine::log(std::string("switch: device ") + (rebooted ? "recreated on the JIT" : "NOT recreated"));
						setJitStatus(rebooted ? "Switched to the JIT (" + juce::String(reason) + ")." : "JIT enabled, but the device did not restart.",
							rebooted ? g_good : g_warn);
					});
					tusEngine::log(std::string("watcher: switch ") + (posted ? "posted to the message thread" : "NOT posted"));
					return;
				}
			}).detach();
		}
	}

	void loadIosOffloadCushion(const juce::PropertySet& _config)
	{
		tusSetOffloadCushionMs(_config.getDoubleValue(g_cushionKey, g_cushionDefaultMs));
	}

	namespace
	{
		// The emulator renders at 44.1 kHz (md::g_samplerate). A 48 kHz session resamples every block in the plugin
		// (MameResamplerHq, about 4% of the emulation thread on the iPhone); at 44.1 kHz the plugin passes the samples
		// through unchanged. Applied once, so a sample rate chosen later in Settings stays.
		constexpr const char* g_nativeRateKey = "iosNativeRateApplied";

		void preferNativeSampleRate(const int _triesLeft)
		{
			juce::Timer::callAfterDelay(1000, [_triesLeft]
			{
				auto* holder = juce::StandalonePluginHolder::getInstance();
				auto* p = processor();
				auto* device = holder ? holder->deviceManager.getCurrentAudioDevice() : nullptr;
				if(!holder || !p || !device)
				{
					if(_triesLeft > 0)
						preferNativeSampleRate(_triesLeft - 1);
					return;
				}
				if(p->getConfig().getBoolValue(g_nativeRateKey, false))
					return;
				const double rate = md::g_samplerate;
				const auto rates = device->getAvailableSampleRates();
				auto setup = holder->deviceManager.getAudioDeviceSetup();
				// the built-in speakers of the iPad Pro M4 and iPhone 17 Pro Max offer 48 kHz only: try again next launch
				if(!rates.contains(rate) || setup.sampleRate == rate)
				{
					tusEngine::log("audio: native rate not applied, session " + std::to_string(setup.sampleRate) + " Hz");
					return;
				}
				p->getConfig().setValue(g_nativeRateKey, true);
				p->getConfig().saveIfNeeded();
				const auto before = setup.sampleRate;
				setup.sampleRate = rate;
				const auto error = holder->deviceManager.setAudioDeviceSetup(setup, true);
				tusEngine::log("audio: sample rate " + std::to_string(before) + " -> " + std::to_string(rate) + " Hz, now " +
					std::to_string(holder->deviceManager.getAudioDeviceSetup().sampleRate) + (error.isEmpty() ? std::string() : " error: " + error.toStdString()));
			});
		}
	}

	void installIosSettingsSheet()
	{
		startJitWatcher();
		preferNativeSampleRate(30);

		// test hook: GEARMULATOR_IOS_IMPORT_TEST=<file>[:<file>...] (relative to Documents) imports each file as the
		// Files picker would, after the app has started, and writes the verdicts to engine.log
		if(const char* list = std::getenv("GEARMULATOR_IOS_IMPORT_TEST"))
		{
			const juce::String files(list);
			tusEngine::log(("import test scheduled: " + files).toStdString());
			std::thread([files]
			{
				std::this_thread::sleep_for(std::chrono::seconds(6));
				juce::MessageManager::callAsync([files]
				{
				const auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
				for(const auto& rel : juce::StringArray::fromTokens(files, ":", ""))
				{
					const auto file = docs.getChildFile(rel);
					juce::MemoryBlock data;
					juce::String message = "unreadable";
					const bool read = file.loadFileAsData(data);
					const bool imported = read && importRomImage(data, file.getFileName(), message);
					tusEngine::log(("import test " + rel + ": " + (imported ? "ACCEPTED " : "REJECTED ") + message).toStdString());
				}
				});
			}).detach();
		}
		jucePluginEditorLib::g_iosOpenSettingsSheet = [](juce::Component& _window, std::function<void()> _openEditorSettings)
		{
			delete g_sheet.getComponent();
			auto* sheet = new Sheet(std::move(_openEditorSettings));
			g_sheet = sheet;
			_window.addAndMakeVisible(sheet);
			sheet->setBounds(_window.getLocalBounds());
			sheet->toFront(true);
		};
	}
}

#endif

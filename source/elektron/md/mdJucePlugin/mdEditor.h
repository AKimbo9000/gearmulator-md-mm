#pragma once

#include <array>
#include <bitset>
#include <deque>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "jucePluginEditorLib/pluginEditor.h"

#include "mdFrontPanelPresentation.h"
#include "mdLcdGesture.h"
#include "mdLcdInteractionModel.h"
#include "mdPanelMidiController.h"
#include "mdPanelMidiService.h"
#include "mdPanelAffordances.h"
#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdsyseximport.h"
#include "mdLib/mdsysexdumpexport.h"
#include "mdLib/mdsysexrecorder.h"

#include "juceRmlUi/rmlTouchDrag.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace juce
{
	class Image;
	class Graphics;
}

namespace Rml
{
	class Element;
	class Event;
}

namespace juceRmlUi
{
	class ElemButton;
	class ElemCanvas;
	class ElemKnob;
}

namespace md
{
	class Hardware;
}

namespace mdJucePlugin
{
	class Controller;
	class PixelPerfectPanel;
	struct EditorIdentityTestAccess;

	class Editor final : public jucePluginEditorLib::Editor, juce::MultiTimer,
		private juce::FocusChangeListener
	{
	public:
		Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin);
		~Editor() override;

		Editor(Editor&&) = delete;
		Editor(const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;

		void create() override;

		std::unique_ptr<jucePluginEditorLib::SettingsDeviceSpecific> createDeviceSpecificSettings(
			const std::string& _templateName, Rml::Element* _root) override;
		std::string getSettingsTemplateSuffix() const override;
		void registerSettings(std::vector<std::unique_ptr<jucePluginEditorLib::SettingsPlugin>>& _plugins) override;

		// Reapplies the configured wheel/encoder drag-speed percentages to the
		// panel knobs. Called on create and from the settings page.
		void applyPanelSpeeds();
		// Panel MIDI: the bindings, which belong to the processor.
		panelMidi::Controller& getPanelMidi() { return getPanelMidiService().getController(); }
		// The machine's base channel (zero-based), read from its Global settings.
		// Above 15 means it has not been read yet.
		uint8_t getMachineBaseChannel() const;
		void applyPixelPerfectPanel();
		void applyLcdInteraction();
		void loadInstalledFactoryStorage();
		void chooseStorageImage();
		void restorePreviousStorage();
		void choosePlusDriveImport();
		void choosePlusDriveExport();
		void resetPlusDrive();
		void rebootMachinedrum();
		std::string plusDriveStatusText() const;
		bool hasStorageRecoveryImage() const;
		void chooseUserSysexFile();
		void cancelUserSysexTransfer();
		bool canResumeUserSysexTransfer() const;
		void resumeUserSysexTransfer();
		std::string getUserSysexMenuText() const;
		bool isUserSysexTransferActive() const;
		bool canCancelUserSysexTransfer() const;
		bool isSysexRecording() const;
		std::string getSysexRecordingMenuText() const;
		void startSysexRecording();
		void stopSysexRecordingAndSave();
		bool isSysexDumpExportRunning() const;
		void chooseSysexDumpExport(md::SysexDumpScope _scope);
		std::weak_ptr<void> getLifetimeToken() const { return m_lifetimeToken; }

		static constexpr int g_panelSpeedPercents[] = {50, 75, 100, 150, 200, 300};

	private:
		friend struct EditorIdentityTestAccess;

		void timerCallback(int _timerId) override;

		std::shared_ptr<md::FrontPanelPublisher> getFrontPanelPublisher() const;
		bool sendPanelEvent(uint8_t _command, uint8_t _argument) const;
		bool refreshFrontPanelState(double _nowMilliseconds);
		md::MachineModel getModel() const;
		void createLcd();
		void updateLcdInteractionState();
		std::optional<unsigned> lcdTargetAt(const Rml::Event& _event) const;
		std::optional<unsigned> lcdTargetAt(float _x, float _y) const;	// context pixels
		void updateLcdHover(const Rml::Event& _event);
		void clearLcdHover();
		void cancelLcdGesture();
		void emitEncoderSteps(md::PanelEncoder _encoder, int _steps) const;
		void createButtons();
		void createPanelAffordances();
		void bindPanelTarget(const char* _id, md::PanelControl _control);
		void bindPanelChord(const char* _id, md::PanelControl _control);
		void pressPanelButton(juceRmlUi::ElemButton* _button, md::PanelControl _control,
			const md::PanelPacket& _packet, bool _shiftDown);
		void releasePanelButton(juceRmlUi::ElemButton* _button, md::PanelControl _control,
			const md::PanelPacket& _packet);
		struct PanelButtonBinding
		{
			md::PanelControl control = md::PanelControl::Trigger1;
			juceRmlUi::ElemButton* button = nullptr;
			md::PanelPacket packet;
		};
		void panelButtonDown(const PanelButtonBinding& _binding, bool _shiftDown);
		void panelButtonUp(const PanelButtonBinding& _binding);
		void createPanelMidi();
		panelMidi::Service& getPanelMidiService() const;
		void turnKnobForPanelMidi(md::PanelEncoder _encoder, int _steps);
		void updatePanelMidiPresentation();
		void releaseActivePanelButtons();
		void beginPanelGesture(Rml::Element* _element,
			std::initializer_list<md::PanelControl> _controls);
		void endPanelGesture();
		void releasePanelButtonGestures();
		void releaseEncoderPress();
		void cancelPanelInputGestures();
		void releaseAllPanelInputs();

		// iOS multi-touch: each finger on a panel key, encoder, the master volume or an LCD target holds or turns
		// it until that finger lifts, as on the hardware. Called by juceRmlUi::RmlComponent's touch handler with
		// _phase 0 = down, 1 = move, 2 = up and the position in context pixels. Returns true for a down on a panel
		// control, which claims the finger for the panel.
		bool onPanelTouch(int _id, int _phase, float _x, float _y, double _timeSeconds);
		void releaseAllPanelTouches();
		void pressTouchKey(size_t _binding);
		void releaseTouchKey(size_t _binding);
		bool isTouchKeyHeld(size_t _binding) const;
		// the drag curve of an encoder for a finger, with the user's encoder or wheel speed setting applied
		juceRmlUi::touchDrag::Curve touchCurveFor(md::PanelEncoder _encoder);
		// The printed FUNCTION labels and other mouse shortcuts (panelAffordance elements): clickable on the
		// desktop; on iOS only while a mouse or trackpad is connected, so that fingers meet the panel of the real
		// machine, where FUNCTION is held and a key pressed.
		void setPanelAffordancesEnabled(bool _enabled);
		void globalFocusChanged(juce::Component* _focusedComponent) override;
		void queuePanelPulse(md::PanelControl _control, int _count = 1);
		void servicePanelQueue();
		void servicePanelNavigation();
		void selectMachinedrumTrack(int _track);
		void selectMachinedrumDataPage(int _page);
		void selectMonomachineDataPage(int _page);
		void selectMonomachineTrigMode(int _mode);
		void togglePatternBankLatch(juceRmlUi::ElemButton* _button, const md::PanelPacket& _packet);
		void releasePatternBankLatch();
		void createEncoders();
		void createMasterVolume();
		void configureEncoder(juceRmlUi::ElemKnob* _knob, md::PanelEncoder _encoder,
			float& _last, float& _accum);
		void onEncoderChanged(juceRmlUi::ElemKnob* _knob, md::PanelEncoder _encoder,
			float& _last, float& _accum);
		void createLeds();
		bool updateLeds();
		void paintLcd(const juce::Image& _target, juce::Graphics& _graphics) const;

		enum class StorageImageBookmark
		{
			None,
			Factory,
			Other
		};

		void chooseStorageImage(StorageImageBookmark _bookmark);
		void confirmStorageImage(const juce::File& _file,
			StorageImageBookmark _bookmark);
		void showStorageOperationResult(bool _success, const juce::String& _message);
		enum class PlusDriveFileOperation { Import, Export };
		void choosePlusDriveFile(PlusDriveFileOperation _operation);
		void confirmPlusDriveImport(const juce::File& _file);
		void showPlusDriveOperationResult(bool _success, const juce::String& _message);
		std::optional<md::SysexImportProgress> getUserSysexProgress() const;
		void sendUserSysexFile(const juce::File& _file, const md::SysexImportTicket& _ticket);
		void startUserSysexTransfer(const std::shared_ptr<md::PreparedMidiSysexTransfer>& _prepared,
			const juce::File& _file, const md::SysexImportTicket& _ticket, bool _receiveModeConfirmed);
		void launchUserSysexFileChooser(const md::SysexImportTicket& _ticket);
		void showUserSysexError(const juce::String& _message);
		std::optional<md::MidiSysexRecordingStatus> getSysexRecordingStatus() const;
		void confirmSysexRecordingSave(std::shared_ptr<std::vector<uint8_t>> _bytes,
			const md::MidiSysexRecordingStatus& _status, const juce::String& _label = {});
		void launchSysexRecordingSaveChooser(std::shared_ptr<std::vector<uint8_t>> _bytes,
			const juce::String& _label = {});
		std::optional<md::SysexDumpExportProgress> getSysexDumpExportProgress() const;
		void startSysexDumpExport(md::SysexDumpScope _scope, bool _saveKitFirst);
		void serviceSysexDumpExport();
		void serviceUserSysexProgress();

		enum class StorageImageFlow
		{
			None,
			Choosing,
			AwaitingConfirmation
		};

		Controller& m_controller;
		const md::MachineModel m_model;
		juceRmlUi::ElemCanvas* m_lcdCanvas = nullptr;
		std::unique_ptr<PixelPerfectPanel> m_pixelPerfectPanel;
		md::FrontPanel m_frontPanelSnapshot;
		bool m_frontPanelSnapshotValid = false;
		bool m_lcdChanged = true;
		bool m_lcdInteractionInputChanged = true;
		std::optional<lcdInteraction::State> m_lcdInteractionState;
		std::optional<unsigned> m_lcdHoverEncoder;
		std::optional<unsigned> m_lcdWheelEncoder;
		lcdInteraction::DragGesture m_lcdDragGesture;
		lcdInteraction::DetentAccumulator m_lcdWheelAccumulator;
		FrontPanelLedPresentation m_ledPresentation;
		bool m_ledsChanged = true;
		md::FrontPanelLedTransitionStatus m_ledTransitionStatus;
		bool m_ledTransitionStatusValid = false;
		bool m_ledResyncPending = false;
		uint64_t m_ledResyncSequence = 0;

		md::PanelRowMixer::Port m_panelRows;	// the mouse's share of the processor's panel rows
		juceRmlUi::ElemButton* m_patternBankButton = nullptr;
		std::optional<md::PanelPacket> m_patternBankPacket;
		Rml::Element* m_panelGestureElement = nullptr;
		std::vector<md::PanelPacket> m_panelGesturePackets;
		panelAffordances::ShiftPanelLatch m_shiftPanelLatch;
		panelAffordances::EncoderPressGesture m_encoderPress;
		juceRmlUi::ElemKnob* m_pressedEncoder = nullptr;

		struct ActivePanelButton
		{
			juceRmlUi::ElemButton* button = nullptr;
			md::PanelPacket packet;
		};
		std::vector<ActivePanelButton> m_activePanelButtons;
		std::vector<PanelButtonBinding> m_panelButtonBindings;

		struct PanelTouch
		{
			enum class Kind : uint8_t { Ignored, Key, Encoder, Volume, Lcd };
			Kind kind = Kind::Ignored;
			size_t binding = 0;								// Key: index into m_panelButtonBindings
			md::PanelEncoder encoder = md::PanelEncoder::DataEntryA;	// Encoder, Lcd
			uint64_t lcdIdentity = 0;						// Lcd: the LCD page the drag started on
			juceRmlUi::touchDrag::Tracker tracker;
			float pointsX = 1.0f;							// screen points per context pixel
			float pointsY = 1.0f;
			double pressMilliseconds = 0.0;					// Key
		};
		std::map<int, PanelTouch> m_panelTouches;			// by JUCE touch index
		std::vector<uint8_t> m_touchKeyHolds;				// per m_panelButtonBindings entry: fingers holding it
		uint32_t m_touchGeneration = 0;						// bumped when every touch is let go
		md::PanelRowMixer::Port m_touchRows;				// the fingers' share of the processor's panel rows
		std::vector<Rml::Element*> m_disabledAffordances;	// labels setPanelAffordancesEnabled(false) switched off
		// What updatePanelMidiPresentation last showed as held from panel MIDI.
		std::bitset<panelMidi::g_controlCount> m_midiHeldControls;
		uint8_t m_midiEncoderPushMask = 0;	// bit i: encoder i's switch

		struct PanelStep
		{
			md::PanelPacket packet;
			bool press = false;
		};
		std::deque<PanelStep> m_panelSteps;
		int m_panelSettleTicks = 0;
		panelAffordances::PendingTarget<panelAffordances::g_machinedrumDataPages.size()>
			m_machinedrumDataPageTarget;
		panelAffordances::PendingTarget<panelAffordances::g_monomachineDataPages.size()>
			m_monomachineDataPageTarget;
		panelAffordances::PendingTarget<panelAffordances::g_monomachineTrigModes.size()>
			m_monomachineTrigModeTarget;

		std::array<juceRmlUi::ElemKnob*, 8> m_encoders{};
		std::array<float, 8> m_encLast{};
		std::array<float, 8> m_encAccum{};
		juceRmlUi::ElemKnob* m_levelEncoder = nullptr;
		float m_levelLast = 0.0f;
		float m_levelAccum = 0.0f;
		juceRmlUi::ElemKnob* m_soundEncoder = nullptr;
		float m_soundLast = 0.0f;
		float m_soundAccum = 0.0f;
		juceRmlUi::ElemKnob* m_masterVolume = nullptr;

		std::array<Rml::Element*, 16> m_stepLeds{};
		std::array<Rml::Element*, 16> m_drumLeds{};

		struct StatusLedElem
		{
			Rml::Element* elem;
			uint8_t bit;	// md::FrontPanel::StatusLed
		};
		std::array<StatusLedElem, 5> m_statusLeds{};
		std::array<StatusLedElem, 6> m_mdModeLeds{};

		struct RawLedElem
		{
			Rml::Element* elem = nullptr;
			uint8_t bank = 0;
			uint8_t bit = 0;
		};
		std::array<RawLedElem, 4> m_mdPageLeds{};
		std::array<RawLedElem, 20> m_mmPanelLeds{};
		std::unique_ptr<juce::FileChooser> m_storageFileChooser;
		StorageImageFlow m_storageImageFlow = StorageImageFlow::None;
		std::unique_ptr<juce::FileChooser> m_sysexFileChooser;
		std::unique_ptr<juce::FileChooser> m_sysexSaveChooser;
		bool m_sysexDumpExportWasRunning = false;
		bool m_sysexDumpExportCancelled = false;
		bool m_sysexChooserOpen = false;
		bool m_sysexTransferWasActive = false;
		md::SysexImportTicket m_sysexMonitoredTicket;
		md::MidiSysexTransferState m_sysexLastState =
			md::MidiSysexTransferState::Idle;
		size_t m_sysexLastSent = 0;
		uint32_t m_sysexLastServiceSerial = 0;
		uint32_t m_sysexReceivePromptId = 0;
		size_t m_sysexReceivePromptStep = 0;
		double m_sysexLastAdvanceMilliseconds = 0.0;
		bool m_sysexStallWarningShown = false;
		std::shared_ptr<void> m_lifetimeToken = std::make_shared<int>(0);
	};
}

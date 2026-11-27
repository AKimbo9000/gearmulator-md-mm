#pragma once

// iOS DSP engine selection (iosEngine.mm): JIT when StikDebug attached and prepared an executable region, the
// interpreter otherwise. Used by the settings sheet (iosSettingsSheet.cpp) and the real-time meter.

#include <functional>
#include <string>

extern "C" const char* tusEngineDescription();

namespace tusEngine
{
	bool jitActive();
	// a debugger has attached to this process (CS_DEBUGGED); stays true after it detaches
	bool debugged();
	// a debugger is attached right now (P_TRACED)
	bool traced();
	// A debugger attached at launch but nothing answered the region request: StikDebug's Enable JIT list starts an app
	// that has no script assigned, attaches and detaches at once (doc/JIT_SETUP.md).
	bool launchedWithoutScript();

	// StikDebug runs scripts such as universal.js only on devices it counts as TXM devices (its ProcessInfo.hasTXM,
	// without the override in its settings); elsewhere it only attaches and detaches, which gives this app no region.
	bool stikDebugRunsScripts();

	// Opens StikDebug's enable-jit URL for this process; _done(false) if StikDebug is not installed.
	void requestJitFromStikDebug(std::function<void(bool)> _done);
	// The same, once the app is in the foreground, where openURL works: now, or when the app next becomes active.
	// Not opened (and _done not called) if by then the JIT is active or a debugger is attached. Call it from any
	// thread; _done runs on the main thread.
	void requestJitFromStikDebugWhenActive(std::function<void(bool)> _done);

	// Asks the attached StikDebug for the executable region (retries for up to 5 s) and installs it. Blocks: call it
	// off the message thread. Does not change the engine.
	bool acquireJitRegion(std::string& _reason);

	// Switches new DSPs to the JIT. Only call it while no DSP runs (audio stopped), then recreate the device.
	void activateJit();

	// appends a timestamped line to Documents/engine.log (engine detection and switch steps)
	void log(const std::string& _line);
}

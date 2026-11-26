#pragma once

// iOS DSP engine selection (iosEngine.mm): JIT when StikDebug attached and prepared an executable region, the
// interpreter otherwise. Used by the settings sheet (iosSettingsSheet.cpp) and the real-time meter.

#include <functional>
#include <string>

extern "C" const char* tusEngineDescription();

namespace tusEngine
{
	bool jitActive();
	bool debugged();

	// Opens StikDebug's enable-jit URL for this process; _done(false) if StikDebug is not installed.
	void requestJitFromStikDebug(std::function<void(bool)> _done);

	// Asks the attached StikDebug for the executable region (retries for up to 5 s) and installs it. Blocks: call it
	// off the message thread. Does not change the engine.
	bool acquireJitRegion(std::string& _reason);

	// Switches new DSPs to the JIT. Only call it while no DSP runs (audio stopped), then recreate the device.
	void activateJit();

	// appends a timestamped line to Documents/engine.log (engine detection and switch steps)
	void log(const std::string& _line);
}

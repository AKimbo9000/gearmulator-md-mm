// iOS: choose the DSP engine when the app loads, before any DSP exists. If StikDebug launched the app (the process is
// being debugged), ask it for a 16 MiB executable region, install it as the JIT code region and run the JIT;
// otherwise run the interpreter (below real time for the Monomachine). See doc/JIT_SETUP.md.
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#include "iosEngine.h"

#include "stikjit.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jitcoderegion.h"
#include "mdLib/mdhardware.h"

#include <cstdio>
#include <unistd.h>
#include <atomic>
#include <cstring>
#include <string>

namespace
{
	constexpr size_t g_regionSize = 16 * 1024 * 1024;

	// constant-initialised, so nothing depends on the order of static initialisers and load-time constructors; a
	// new description leaks the old one, which another thread may still be reading (at most one switch per run)
	std::atomic<const char*> g_engineDescription{""};
	void setEngineDescription(const std::string& _text)
	{
		g_engineDescription = strdup(_text.c_str());
	}

	uint32_t playPresses() { return md::g_playPresses.load(); }
}

// read by the real-time meter in jucePluginLib (plain pointers: constant-initialised, no order issue)
extern uint32_t (*g_tusTransportCounter)();

extern "C" const char* tusEngineDescription() { return g_engineDescription.load(); }

std::string tusAudioSessionDescription()
{
	AVAudioSession* s = [AVAudioSession sharedInstance];
	NSMutableString* outs = [NSMutableString string];
	for(AVAudioSessionPortDescription* p in s.currentRoute.outputs)
		[outs appendFormat:@"%@(%@) ", p.portType, p.portName];
	NSString* d = [NSString stringWithFormat:@"category=%@ mode=%@ options=0x%lx route=%@ samplerate=%.0f io_buffer=%.4fs (%.0f frames) output_volume=%.2f",
		s.category, s.mode, (unsigned long)s.categoryOptions, outs, s.sampleRate, s.IOBufferDuration, s.IOBufferDuration * s.sampleRate, s.outputVolume];
	return d.UTF8String;
}

__attribute__((constructor)) static void tusChooseEngine()
{
	g_tusTransportCounter = &playPresses;

	const bool debugged = stikIsDebugged();
	void* rx = nullptr;
	void* rw = nullptr;
	std::string reason;
	char buf[256];
	// StikDebug may attach before its script answers the prepare request (seen on the iPad: debugged=1, request not
	// serviced). An unserviced request is stepped over safely, so retry for up to 5 s before falling back.
	bool acquired = stikAcquireRegion(g_regionSize, rx, rw, reason);
	int attempts = 1;
	for(; !acquired && debugged && attempts < 50; ++attempts)
	{
		usleep(100 * 1000);
		acquired = stikAcquireRegion(g_regionSize, rx, rw, reason);
	}
	if(acquired)
		reason = "serviced after " + std::to_string(attempts) + " attempt(s)";
	if(acquired && dsp56k::JitCodeRegion::install(rx, rw, g_regionSize))
	{
		dsp56k::g_runtimeUseJIT = true;
		std::snprintf(buf, sizeof(buf), "JIT (StikDebug) use_jit=1 debugged=1 region=%p alias=%p 16MiB %s", rx, rw, reason.c_str());
	}
	else
	{
		dsp56k::g_runtimeUseJIT = false;
		std::snprintf(buf, sizeof(buf), "Interpreter (below real time) use_jit=0 debugged=%d reason=%s", debugged ? 1 : 0, reason.c_str());
	}
	setEngineDescription(buf);
}

namespace tusEngine
{
	bool jitActive() { return dsp56k::g_runtimeUseJIT; }
	bool debugged() { return stikIsDebugged(); }

	void requestJitFromStikDebug(std::function<void(bool)> _done)
	{
		// URL form from the StikDebug integration guide; universal.js is the script for TXM devices
		NSString* url = [NSString stringWithFormat:@"stikdebug://enable-jit?bundle-id=%@&pid=%d&script-name=universal.js",
			NSBundle.mainBundle.bundleIdentifier, getpid()];
		auto done = std::make_shared<std::function<void(bool)>>(std::move(_done));
		[UIApplication.sharedApplication openURL:[NSURL URLWithString:url] options:@{} completionHandler:^(BOOL ok) {
			if(*done)
				(*done)(ok);
		}];
	}

	bool acquireJitRegion(std::string& _reason)
	{
		if(dsp56k::JitCodeRegion::installed())
			return true;
		if(!stikIsDebugged())
		{
			_reason = "StikDebug is not attached";
			return false;
		}
		void* rx = nullptr;
		void* rw = nullptr;
		bool acquired = false;
		for(int attempts = 0; !acquired && attempts < 50; ++attempts)
		{
			if(attempts)
				usleep(100 * 1000);
			acquired = stikAcquireRegion(g_regionSize, rx, rw, _reason);
		}
		if(!acquired)
			return false;
		if(!dsp56k::JitCodeRegion::install(rx, rw, g_regionSize))
		{
			_reason = "the region could not be installed";
			return false;
		}
		char buf[160];
		std::snprintf(buf, sizeof(buf), "region=%p alias=%p 16MiB", rx, rw);
		_reason = buf;
		return true;
	}

	void activateJit()
	{
		dsp56k::g_runtimeUseJIT = true;
		setEngineDescription("JIT (StikDebug, switched in Settings) use_jit=1 debugged=1");
	}
}

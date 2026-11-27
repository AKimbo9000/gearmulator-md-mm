// iOS: choose the DSP engine when the app loads, before any DSP exists. If StikDebug launched the app (the process is
// being debugged), ask it for a 16 MiB executable region, install it as the JIT code region and run the JIT;
// otherwise run the interpreter (below real time for the Monomachine). See doc/JIT_SETUP.md.
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#include "iosEngine.h"

#include "stikjit.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jitcoderegion.h"
#include "dsp56kEmu/jitstats.h"
#include "mdLib/mdhardware.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <sys/utsname.h>
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
	std::atomic<bool> g_launchedWithoutScript{false};
	void setEngineDescription(const std::string& _text)
	{
		g_engineDescription = strdup(_text.c_str());
	}

	uint32_t playPresses() { return md::g_playPresses.load(); }
	uint64_t jitCodeBytes() { return dsp56k::JitCodeRegion::installed() ? dsp56k::JitCodeRegion::used() : 0; }
	void jitStats(uint64_t* _out)
	{
		const auto& s = dsp56k::g_jitStats;
		_out[0] = s.emitNs.load(std::memory_order_relaxed);
		_out[1] = s.emitBlocks.load(std::memory_order_relaxed);
		_out[2] = s.codegenNs.load(std::memory_order_relaxed);
		_out[3] = s.finalizeNs.load(std::memory_order_relaxed);
		_out[4] = s.addNs.load(std::memory_order_relaxed);
		_out[5] = s.chainNs.load(std::memory_order_relaxed);
		_out[6] = s.chains.load(std::memory_order_relaxed);
	}
}

// read by the real-time meter in jucePluginLib (plain pointers: constant-initialised, no order issue)
extern uint32_t (*g_tusTransportCounter)();
extern uint64_t (*g_tusJitCodeBytes)();
extern void (*g_tusJitStats)(uint64_t*);
// jucePluginLib/processor.cpp: run the emulation off the real-time audio thread when this returns true
extern bool (*g_tusOffloadProcessing)();
extern bool (*g_tusOffloadRealtime)();
namespace
{
	// Both engines run the emulation on the offload worker. The interpreter cannot meet the audio callback's deadline,
	// and an overrunning real-time thread gets demoted. The JIT compiles DSP code on first execution: the first Play
	// emits ~80 KB of code in one 5.35 ms block, which then takes 18 ms (IOS_PORT_STATUS), and the worker's cushion
	// absorbs that where the audio callback would play a gap.
	bool offloadEmulation() { return true; }
	// the JIT keeps up with a real-time period; the interpreter overruns every one and would be demoted
	bool offloadRealtime() { return dsp56k::g_runtimeUseJIT; }
}

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

// Profile-guided optimisation (GEARMULATOR_MDMM_APPLE_PGO_MODE=generate, elektron/md/optimization.cmake): an
// instrumented build writes its counters to Documents/mm-<pid>.profraw when GEARMULATOR_PGO_WRITE_SECONDS=N says how
// long after launch, since an app stopped by devicectl never reaches the runtime's own write at exit. The counters
// are call and branch counts of the emulator's code, nothing from the firmware. Only instrumented builds have the
// profile runtime (optimization.cmake defines GEARMULATOR_MDMM_PGO_GENERATE for them).
#if GEARMULATOR_MDMM_PGO_GENERATE
extern "C" void __llvm_profile_set_filename(const char*);
extern "C" int __llvm_profile_write_file(void);
__attribute__((constructor)) static void tusSchedulePgoWrite()
{
	const char* seconds = std::getenv("GEARMULATOR_PGO_WRITE_SECONDS");
	if(!seconds)
		return;
	NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	static std::string path;
	path = std::string(docs.UTF8String) + "/mm-" + std::to_string(getpid()) + ".profraw";
	__llvm_profile_set_filename(path.c_str());
	const int s = std::atoi(seconds);
	std::thread([s]
	{
		std::this_thread::sleep_for(std::chrono::seconds(s));
		const int r = __llvm_profile_write_file();
		tusEngine::log("pgo: wrote " + path + " result " + std::to_string(r));
	}).detach();
}
#endif

__attribute__((constructor)) static void tusChooseEngine()
{
	g_tusTransportCounter = &playPresses;
	g_tusJitCodeBytes = &jitCodeBytes;
	g_tusJitStats = &jitStats;
	g_tusOffloadProcessing = &offloadEmulation;
	g_tusOffloadRealtime = &offloadRealtime;

	const bool debugged = stikIsDebugged();
	const bool traced = stikIsTraced();		// before the first request: a request that is answered detaches the debugger
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
	// debugged at launch, nothing answered, and the debugger had gone before the first request: StikDebug's list without
	// a script (vAttach and D before the app resumes). A debugger still attached here is a session that did not answer.
	g_launchedWithoutScript = debugged && !acquired && !traced;
	if(acquired && dsp56k::JitCodeRegion::install(rx, rw, g_regionSize))
	{
		dsp56k::g_runtimeUseJIT = true;
		std::snprintf(buf, sizeof(buf), "JIT (StikDebug) use_jit=1 debugged=1 traced=%d region=%p alias=%p 16MiB %s", traced ? 1 : 0, rx,
			rw, reason.c_str());
	}
	else
	{
		dsp56k::g_runtimeUseJIT = false;
		std::snprintf(buf, sizeof(buf), "Interpreter (below real time) use_jit=0 debugged=%d traced=%d reason=%s", debugged ? 1 : 0,
			traced ? 1 : 0, reason.c_str());
	}
	setEngineDescription(buf);
	tusEngine::log(std::string("launch: ") + buf);
}

namespace tusEngine
{
	bool jitActive() { return dsp56k::g_runtimeUseJIT; }
	bool debugged() { return stikIsDebugged(); }
	bool traced() { return stikIsTraced(); }
	bool launchedWithoutScript() { return g_launchedWithoutScript; }

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

	bool stikDebugRunsScripts()
	{
		// StikDebug/Support/ProcessInfo+TXM.swift (commit 63713ea), including its way of turning "iPhone14,2" into 14.2
		utsname name{};
		uname(&name);
		const std::string machine = name.machine;
		NSProcessInfo* info = NSProcessInfo.processInfo;
		if([info isOperatingSystemAtLeastVersion:{27, 0, 0}])
			return machine != "iPad8,11" && machine != "iPad8,12";
		if(![info isOperatingSystemAtLeastVersion:{26, 0, 0}])
			return false;
		const bool iPad = machine.rfind("iPad", 0) == 0;
		const bool iPhone = machine.rfind("iPhone", 0) == 0;
		int major = 0, minor = 0;
		if((!iPad && !iPhone) || std::sscanf(machine.c_str() + (iPad ? 4 : 6), "%d,%d", &major, &minor) != 2)
			return false;
		const double version = major + minor / std::pow(10.0, static_cast<double>(std::to_string(minor).size()));
		return version >= (iPad ? 14.5 : 14.2);
	}

	void requestJitFromStikDebugWhenActive(std::function<void(bool)> _done)
	{
		auto done = std::make_shared<std::function<void(bool)>>(std::move(_done));
		// by the time the app is active StikDebug may have attached some other way (its own Enable JIT on the running
		// app): then opening it again would only switch apps, and leave a session waiting for a request that never comes
		const auto open = [done]
		{
			if(dsp56k::g_runtimeUseJIT || stikIsTraced())
			{
				tusEngine::log(std::string("auto-request: not opening StikDebug, ") + (dsp56k::g_runtimeUseJIT ? "the JIT is active" : "a debugger is attached"));
				return;
			}
			requestJitFromStikDebug(*done);
		};
		dispatch_async(dispatch_get_main_queue(), ^
		{
			if(UIApplication.sharedApplication.applicationState == UIApplicationStateActive)
			{
				open();
				return;
			}
			tusEngine::log("auto-request: waiting for the app to become active");
			__block id observer = nil;
			observer = [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidBecomeActiveNotification
				object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*)
			{
				if(!observer)
					return;
				[NSNotificationCenter.defaultCenter removeObserver:observer];
				observer = nil;
				open();
			}];
		});
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

	void log(const std::string& _line)
	{
		NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
		const std::string path = std::string(docs.UTF8String) + "/engine.log";
		if(FILE* f = std::fopen(path.c_str(), "a"))
		{
			std::fprintf(f, "%.3f %s\n", [NSDate date].timeIntervalSince1970, _line.c_str());
			std::fclose(f);
		}
	}

	void activateJit()
	{
		dsp56k::g_runtimeUseJIT = true;
		setEngineDescription("JIT (StikDebug, switched in Settings) use_jit=1 debugged=1");
	}
}

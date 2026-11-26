// iOS: choose the DSP engine when the app loads, before any DSP exists. If StikDebug launched the app (the process is
// being debugged), ask it for a 16 MiB executable region, install it as the JIT code region and run the JIT;
// otherwise run the interpreter (below real time for the Monomachine). See doc/JIT_SETUP.md.
#include "stikjit.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jitcoderegion.h"

#include <string>

namespace
{
	constexpr size_t g_regionSize = 16 * 1024 * 1024;
}

std::string g_tusEngineDescription = "Interpreter";

__attribute__((constructor)) static void tusChooseEngine()
{
	void* rx = nullptr;
	void* rw = nullptr;
	std::string reason;
	if(stikAcquireRegion(g_regionSize, rx, rw, reason) && dsp56k::JitCodeRegion::install(rx, rw, g_regionSize))
	{
		dsp56k::g_runtimeUseJIT = true;
		g_tusEngineDescription = "JIT (StikDebug)";
	}
	else
	{
		dsp56k::g_runtimeUseJIT = false;
		g_tusEngineDescription = "Interpreter (below real time): " + reason;
	}
}

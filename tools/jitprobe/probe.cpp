// JIT probe: the StikDebug TXM protocol, step by step (see doc/JIT_SETUP.md and the StikDebug integration guide,
// https://github.com/StikDebug/StikJIT/blob/main/INTEGRATION.md):
//   1. wait until the process is being debugged (CS_DEBUGGED)
//   2. ask the debugger for an RX region: brk #0xf00d with x16 = 1 (prepare), x0 = 0 (let it allocate), x1 = size
//   3. check the answer is a real, page-aligned, executable mapping of this process
//   4. map a writable alias of it, then detach (brk #0xf00d with x16 = 0)
//   5. write a tiny function through the alias, flush the icache, call it; then patch it and call it again
// Progress is written to a file before every step that could kill the process, so a crash still leaves a reason.

#include "probe.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>

#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_map.h>

extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);

namespace
{
	constexpr unsigned int g_csOpsStatus = 0;
	constexpr uint32_t g_csDebugged = 0x10000000;
	constexpr uintptr_t g_brkEncoding = 0xD43E01A0;		// brk #0xf00d, which an unserviced breakpoint may leave in x0
	constexpr uintptr_t g_regionSize = 16 * 1024 * 1024;
	constexpr uintptr_t g_pageMask = 16 * 1024 - 1;

	volatile sig_atomic_t g_trapped = 0;

	// An unanswered brk raises SIGTRAP in the process; step over it and report "not serviced" instead of dying
	void onTrap(int, siginfo_t*, void* _context)
	{
		auto* uc = static_cast<ucontext_t*>(_context);
		g_trapped = 1;
#if __has_feature(ptrauth_calls)
		__darwin_arm_thread_state64_set_pc_fptr(uc->uc_mcontext->__ss,
			reinterpret_cast<void*>(__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss) + 4));
#else
		uc->uc_mcontext->__ss.__pc += 4;
#endif
		uc->uc_mcontext->__ss.__x[0] = 0;
	}

	uintptr_t stikBreak(const uintptr_t _command, const uintptr_t _a0, const uintptr_t _a1)
	{
		register uintptr_t x0 __asm__("x0") = _a0;
		register uintptr_t x1 __asm__("x1") = _a1;
		register uintptr_t x16 __asm__("x16") = _command;
		__asm__ volatile("brk #0xf00d" : "+r"(x0) : "r"(x1), "r"(x16) : "memory");
		return x0;
	}

	class Probe
	{
	public:
		Probe(ProbeLog _log, void* _ctx, const char* _progressPath) : m_log(_log), m_ctx(_ctx), m_progressPath(_progressPath) {}

		void log(const char* _fmt, ...)
		{
			char buf[512];
			va_list args;
			va_start(args, _fmt);
			std::vsnprintf(buf, sizeof(buf), _fmt, args);
			va_end(args);
			m_log(buf, m_ctx);
		}

		// written before each step that could kill the process
		void progress(const char* _step)
		{
			if(FILE* f = std::fopen(m_progressPath, "w"))
			{
				std::fprintf(f, "%s\n", _step);
				std::fclose(f);
			}
		}

		bool fail(const char* _reason)
		{
			log("FAIL: %s", _reason);
			progress((std::string("finished FAIL: ") + _reason).c_str());
			return false;
		}

		bool run(const int _waitSeconds)
		{
			utsname u{};
			uname(&u);
			log("device %s, pid %d", u.machine, getpid());

			// 1. debugger
			uint32_t flags = 0;
			int waited = 0;
			for(;; ++waited)
			{
				flags = 0;
				csops(getpid(), g_csOpsStatus, &flags, sizeof(flags));
				if(flags & g_csDebugged)
					break;
				if(waited == 0)
					log("waiting for a debugger (CS_DEBUGGED). Enable JIT for \"JIT Probe\" in StikDebug, or tap the button.");
				if(waited >= _waitSeconds)
					return fail("no debugger attached within the wait time: CS_DEBUGGED never set (StikDebug did not attach)");
				sleep(1);
			}
			log("debugger attached after %d s, code-signing flags 0x%08x", waited, flags);

			struct sigaction sa{};
			sa.sa_sigaction = &onTrap;
			sa.sa_flags = SA_SIGINFO;
			sigemptyset(&sa.sa_mask);
			sigaction(SIGTRAP, &sa, nullptr);

			// 2. prepare: ask the debugger to allocate and map an RX region
			progress("step 2: brk #0xf00d prepare (x16=1) - a crash here means the breakpoint was not handled by a StikDebug script");
			const uintptr_t rx = stikBreak(1, 0, g_regionSize);
			log("prepare returned 0x%lx%s", static_cast<unsigned long>(rx), g_trapped ? " (breakpoint reached the app: not serviced)" : "");
			if(g_trapped || rx == 0 || rx == g_brkEncoding)
				return fail("TXM prepare not serviced: StikDebug attached without the universal.js script, or this iOS version is not supported");
			if(rx & g_pageMask)
				return fail("prepare returned an address that is not 16 KiB aligned");

			// 3. is it an executable mapping of this process?
			vm_address_t addr = rx;
			vm_size_t size = 0;
			vm_region_basic_info_data_64_t info{};
			mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
			mach_port_t object = MACH_PORT_NULL;
			const auto kr = vm_region_64(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64,
				reinterpret_cast<vm_region_info_t>(&info), &count, &object);
			if(kr != KERN_SUCCESS || addr > rx)
				return fail("prepare returned an address with no mapping behind it");
			log("region 0x%lx size %lu KiB protection %c%c%c (max %c%c%c)", static_cast<unsigned long>(addr),
				static_cast<unsigned long>(size / 1024),
				(info.protection & VM_PROT_READ) ? 'r' : '-', (info.protection & VM_PROT_WRITE) ? 'w' : '-', (info.protection & VM_PROT_EXECUTE) ? 'x' : '-',
				(info.max_protection & VM_PROT_READ) ? 'r' : '-', (info.max_protection & VM_PROT_WRITE) ? 'w' : '-', (info.max_protection & VM_PROT_EXECUTE) ? 'x' : '-');
			if(!(info.protection & VM_PROT_EXECUTE))
				return fail("the region StikDebug returned is not executable");

			// 4. writable alias, then detach
			vm_address_t rw = 0;
			vm_prot_t cur = 0, max = 0;
			auto r = vm_remap(mach_task_self(), &rw, g_regionSize, 0, VM_FLAGS_ANYWHERE, mach_task_self(), rx, FALSE, &cur, &max, VM_INHERIT_NONE);
			if(r != KERN_SUCCESS)
				return fail("vm_remap of the RX region failed");
			r = vm_protect(mach_task_self(), rw, g_regionSize, FALSE, VM_PROT_READ | VM_PROT_WRITE);
			if(r != KERN_SUCCESS)
				return fail("vm_protect of the alias to read/write failed");
			log("writable alias at 0x%lx", static_cast<unsigned long>(rw));

			progress("step 4: brk #0xf00d detach (x16=0)");
			stikBreak(0, 0, 0);
			log("detached from the debugger%s", g_trapped ? " (detach breakpoint was not serviced; continuing)" : "");

			// 5. generate, run, patch, run again
			const uint32_t addOne[] = {0x11000400, 0xD65F03C0};		// add w0, w0, #1 ; ret
			std::memcpy(reinterpret_cast<void*>(rw), addOne, sizeof(addOne));
			sys_icache_invalidate(reinterpret_cast<void*>(rx), sizeof(addOne));
			progress("step 5a: call generated code (add w0, w0, #1) - a crash here means the region is not really executable");
			auto* fn = reinterpret_cast<int (*)(int)>(rx);
			const int first = fn(41);
			log("generated code returned %d (expected 42)", first);
			if(first != 42)
				return fail("generated code ran but returned the wrong value");

			const uint32_t returnSeven[] = {0x528000E0, 0xD65F03C0};	// mov w0, #7 ; ret
			std::memcpy(reinterpret_cast<void*>(rw), returnSeven, sizeof(returnSeven));
			sys_icache_invalidate(reinterpret_cast<void*>(rx), sizeof(returnSeven));
			progress("step 5b: call patched code after detach (mov w0, #7)");
			const int second = fn(41);
			log("patched code returned %d (expected 7)", second);
			if(second != 7)
				return fail("patching code after detach did not take effect");

			log("PASS: JIT works on this device (16 MiB RX region, writable alias, code patched after detach)");
			progress("finished PASS");
			return true;
		}

	private:
		ProbeLog m_log;
		void* m_ctx;
		const char* m_progressPath;
	};
}

bool runJitProbe(ProbeLog _log, void* _ctx, const char* _progressPath, const int _waitSeconds)
{
	Probe probe(_log, _ctx, _progressPath);
	return probe.run(_waitSeconds);
}

// StikDebug TXM handshake for the MM benchmark: the same steps tools/jitprobe proved on both devices
// (prepare an RX region with brk #0xf00d x16=1, writable alias with vm_remap, detach with x16=0).
#include "stikjit.h"

#include <cstdint>
#include <signal.h>
#include <unistd.h>

#include <sys/proc.h>
#include <sys/sysctl.h>

#include <mach/mach.h>
#include <mach/vm_map.h>

extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);

namespace
{
	constexpr uint32_t g_csDebugged = 0x10000000;
	constexpr uintptr_t g_brkEncoding = 0xD43E01A0;
	volatile sig_atomic_t g_trapped = 0;

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
}

bool stikIsDebugged()
{
	uint32_t flags = 0;
	csops(getpid(), 0, &flags, sizeof(flags));
	return (flags & g_csDebugged) != 0;
}

bool stikIsTraced()
{
	int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
	kinfo_proc info{};
	size_t size = sizeof(info);
	if(sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size != sizeof(info))
		return false;
	return (info.kp_proc.p_flag & P_TRACED) != 0;
}

bool stikAcquireRegion(const size_t _size, void*& _rx, void*& _rw, std::string& _reason)
{
	if(!stikIsDebugged()) { _reason = "not debugged (StikDebug has not enabled JIT)"; return false; }

	struct sigaction sa{};
	sa.sa_sigaction = &onTrap;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTRAP, &sa, nullptr);

	// set by onTrap when no debugger answered; cleared per request, or one unanswered request (the load-time retries
	// after a list launch without a script) would reject every later answered one
	g_trapped = 0;
	const uintptr_t rx = stikBreak(1, 0, _size);
	if(g_trapped) { _reason = "StikDebug did not service the prepare request (no universal.js script?)"; return false; }

	// A debugger answered. Every failure from here on still sends the detach request: universal.js stays attached
	// until it gets one, and would prepare another region for each retry.
	const auto fail = [&_reason](const char* _why)
	{
		stikBreak(0, 0, 0);
		_reason = _why;
		return false;
	};
	if(rx == 0 || rx == g_brkEncoding)
		return fail("the debugger answered the prepare request without a region");
	if(rx & (16 * 1024 - 1))
		return fail("prepared region is not 16 KiB aligned");

	vm_address_t rw = 0;
	vm_prot_t cur = 0, max = 0;
	if(vm_remap(mach_task_self(), &rw, _size, 0, VM_FLAGS_ANYWHERE, mach_task_self(), rx, FALSE, &cur, &max, VM_INHERIT_NONE) != KERN_SUCCESS)
		return fail("vm_remap of the region failed");
	if(vm_protect(mach_task_self(), rw, _size, FALSE, VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS)
	{
		vm_deallocate(mach_task_self(), rw, _size);
		return fail("vm_protect of the alias failed");
	}

	stikBreak(0, 0, 0);		// detach: all executable memory has been prepared

	_rx = reinterpret_cast<void*>(rx);
	_rw = reinterpret_cast<void*>(rw);
	return true;
}

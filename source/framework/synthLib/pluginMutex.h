#pragma once

#include <mutex>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
#include <pthread.h>

namespace synthLib
{
	// The processing lock of Plugin, on iOS. There the interpreter runs the emulation back to back on a worker
	// (jucePluginLib, IosOffload), which takes this lock again right after releasing it. Darwin's default first-fit
	// mutexes let it, before a waiting thread wakes, and the message thread waited for seconds (a posted task ran 8.6 s
	// late; IOS_PORT_STATUS). The fair-share policy hands the lock to the thread that waits. Recursive like the
	// std::recursive_mutex it replaces.
	class PluginMutex
	{
	public:
		PluginMutex()
		{
			pthread_mutexattr_t attr;
			pthread_mutexattr_init(&attr);
			pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
			pthread_mutexattr_setpolicy_np(&attr, PTHREAD_MUTEX_POLICY_FAIRSHARE_NP);
			pthread_mutex_init(&m_mutex, &attr);
			pthread_mutexattr_destroy(&attr);
		}
		~PluginMutex() { pthread_mutex_destroy(&m_mutex); }

		PluginMutex(const PluginMutex&) = delete;
		PluginMutex& operator=(const PluginMutex&) = delete;

		void lock() { pthread_mutex_lock(&m_mutex); }
		void unlock() { pthread_mutex_unlock(&m_mutex); }
		bool try_lock() { return pthread_mutex_trylock(&m_mutex) == 0; }

	private:
		pthread_mutex_t m_mutex;
	};
}
#else
namespace synthLib
{
	using PluginMutex = std::recursive_mutex;
}
#endif

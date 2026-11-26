#pragma once

#include <cstddef>
#include <string>

// true when a debugger (StikDebug) is attached to this process
bool stikIsDebugged();

// Ask StikDebug for an executable region of _size bytes and map a writable alias of it; detaches afterwards.
bool stikAcquireRegion(size_t _size, void*& _rx, void*& _rw, std::string& _reason);

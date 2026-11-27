#pragma once

#include <cstddef>
#include <string>

// true once a debugger (StikDebug) has attached to this process: CS_DEBUGGED, which stays set after it detaches
bool stikIsDebugged();

// true while a debugger is attached to this process right now (P_TRACED); false again once it has detached
bool stikIsTraced();

// Ask StikDebug for an executable region of _size bytes and map a writable alias of it; detaches afterwards.
bool stikAcquireRegion(size_t _size, void*& _rx, void*& _rw, std::string& _reason);

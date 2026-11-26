#pragma once

// Called for every line the probe reports, from the probe's own thread.
using ProbeLog = void (*)(const char* _line, void* _ctx);

// Runs the StikDebug TXM JIT checks (see probe.cpp). Returns true on PASS.
bool runJitProbe(ProbeLog _log, void* _ctx, const char* _progressPath, int _waitSeconds);

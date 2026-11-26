# Services the app's StikDebug JIT requests from a Mac, through Xcode's LLDB and the device's debugserver, so a JIT run
# can be started without StikDebug on the device (development and testing; doc/JIT_SETUP.md).
#
# Same protocol as StikDebug's universal.js (StikDebug/Scripts/universal.js): the app executes brk #0xf00d with
#   x16 = 1: prepare a region, x0 = address or 0, x1 = size. With x0 == 0 the debugger allocates RX memory
#            (gdb-remote _M<size>,rx) and returns its address in x0.
#   x16 = 0: detach.
# PC is advanced past the brk before the request is handled. universal.js then calls StikDebug's native
# prepare_memory_region(), which is not part of the script; here every 16 KiB page is touched through the debugger
# (one byte read and written back) in its place.
#
# Use: GEARMULATOR_JIT_UDID=<device udid> GEARMULATOR_JIT_PID=<pid of a process launched with --start-stopped> \
#      xcrun lldb -b -o "command script import tools/jitprobe/lldb_jit_service.py"

import os
import struct
import sys
import time

import lldb

BRK_F00D = 0xD43E01A0
PAGE = 16 * 1024


def log(*args):
    print("[jit-service]", *args)
    sys.stdout.flush()


def run(debugger, command):
    result = lldb.SBCommandReturnObject()
    debugger.GetCommandInterpreter().HandleCommand(command, result)
    log(command, "->", (result.GetOutput() or "").strip()[:200], (result.GetError() or "").strip()[:200])
    return result.Succeeded()


def packet(debugger, text):
    result = lldb.SBCommandReturnObject()
    debugger.GetCommandInterpreter().HandleCommand('process plugin packet send "%s"' % text, result)
    out = (result.GetOutput() or "") + (result.GetError() or "")
    return out.split("response:")[-1].strip() if "response:" in out else out.strip()


# Registers are written with gdb-remote P packets, as universal.js does (x0 = 0x00, pc = 0x20, little-endian hex).
# LLDB's own register writes fail while the app is still in its load-time initialisers.
def write_register(debugger, thread, number, value):
    tid = "%x" % thread.GetThreadID()
    reply = packet(debugger, "P%x=%s;thread:%s;" % (number, struct.pack("<Q", value).hex(), tid))
    back = packet(debugger, "p%x;thread:%s;" % (number, tid))
    try:
        readback = struct.unpack("<Q", bytes.fromhex(back[:16]))[0]
    except ValueError:
        readback = None
    return reply, readback


def touch_pages(process, address, size):
    error = lldb.SBError()
    touched = 0
    for page in range(address, address + size, PAGE):
        data = process.ReadMemory(page, 1, error)
        if not error.Success():
            log("read failed at", hex(page), error.GetCString())
            return touched
        process.WriteMemory(page, data, error)
        if not error.Success():
            log("write failed at", hex(page), error.GetCString())
            return touched
        touched += 1
    return touched


def service(debugger):
    udid = os.environ["GEARMULATOR_JIT_UDID"]
    pid = os.environ["GEARMULATOR_JIT_PID"]
    timeout = float(os.environ.get("GEARMULATOR_JIT_TIMEOUT", "90"))
    debugger.SetAsync(False)
    run(debugger, "device select " + udid)		# prints nothing and does not report success either
    run(debugger, "device process attach --pid " + pid)
    process = debugger.GetSelectedTarget().GetProcess()
    if not process.IsValid():
        log("RESULT FAIL attach")
        return
    log("attached, state", process.GetState())
    # signals the app raises for itself go back to it without stopping here
    run(debugger, "process handle SIGPIPE SIGUSR1 SIGUSR2 SIGALRM -s false -p true -n false")

    deadline = time.time() + timeout
    prepared = False
    while time.time() < deadline:
        state = process.GetState()
        if state in (lldb.eStateExited, lldb.eStateDetached, lldb.eStateCrashed):
            log("RESULT FAIL process state", state, "exit", process.GetExitStatus())
            return
        if state == lldb.eStateStopped:
            for thread in process:
                if thread.GetStopReason() not in (lldb.eStopReasonException, lldb.eStopReasonBreakpoint,
                                                  lldb.eStopReasonSignal):
                    continue
                frame = thread.GetFrameAtIndex(0)
                pc = frame.GetPC()
                error = lldb.SBError()
                instruction = process.ReadUnsignedFromMemory(pc, 4, error)
                if instruction != BRK_F00D:
                    log("stop", thread.GetStopDescription(200), "at", hex(pc), "(not a JIT request), resuming")
                    continue
                x16 = frame.FindRegister("x16").GetValueAsUnsigned()
                x0 = frame.FindRegister("x0").GetValueAsUnsigned()
                x1 = frame.FindRegister("x1").GetValueAsUnsigned()
                reply, readback = write_register(debugger, thread, 0x20, pc + 4)
                log("pc", hex(pc), "->", hex(pc + 4), "reply", reply, "read back", hex(readback) if readback is not None else None)
                if x16 == 1:
                    address = x0
                    if address == 0 and x1:
                        address = process.AllocateMemory(x1, lldb.ePermissionsReadable | lldb.ePermissionsExecutable,
                                                         error)
                        if not error.Success():
                            log("RESULT FAIL _M allocation of", hex(x1), error.GetCString())
                            address = 0
                    touch = os.environ.get("GEARMULATOR_JIT_TOUCH", "0") == "1"
                    touched = touch_pages(process, address, x1) if address and touch else 0
                    reply, readback = write_register(debugger, thread, 0x00, address)
                    log("prepare size", hex(x1), "-> rx", hex(address), "pages touched", touched,
                        "x0 reply", reply, "read back", hex(readback) if readback is not None else None)
                    prepared = address != 0
                elif x16 == 0:
                    log("detach request; RESULT", "PASS" if prepared else "FAIL (no region prepared)")
                    process.Detach()
                    return
                else:
                    log("unknown request x16 =", x16)
        process.Continue()
    log("RESULT FAIL timeout after", timeout, "s")
    process.Detach()


def __lldb_init_module(debugger, internal_dict):
    service(debugger)

# PLAN — Gearmulator Monomachine on iPad and iPhone (standalone first)

Revised 2026-11-26 for a new target. The original phases 0–6 are summarised under "Done so far" and "Deferred";
their measurements are in `doc/IOS_PORT_STATUS.md`.

## Target

- **First product:** a **standalone Monomachine app**. Machinedrum, and AUv3 for both, come later.
- **Devices:** **iPad Pro 13" M4** (iPadOS 27.0) and **iPhone 17 Pro Max** (iOS 27.2.1). Both are TXM-class devices, with Developer Mode on. Their IDs are in `CLAUDE.local.md`.
- **No Mac at run time.** The app must launch and run with no Mac and no cable. A one-time setup that needs the Mac is fine: install, pairing file, ROM import.
- **Dual engine:**
  - **JIT** when StikDebug has attached and granted an executable region.
  - **Interpreter** otherwise.
  - An iOS update that breaks StikDebug may only make the app slower, never unusable.
  - The engine in use is shown to the user.
- **Accuracy rules unchanged:**
  - The JIT on the device must reproduce `doc/ios-baselines` exactly.
  - The desktop JIT keeps producing the same results.
  - The interpreter keeps its current, verified output: MD audio equals the JIT; MM does not yet.

## Why this shape (measurements in doc/IOS_PORT_STATUS.md)

- **MM interpreter speed on this Mac:** 0.661x (OPTIMIZE_DSP+ThinLTO, after the two fast-path speedups), about 0.73x with PGO.
  - Projected sustained: **0.47–0.62x** on the iPad, **0.40–0.55x** on the iPhone. Below real time on both.
- **MM JIT** runs 2.77x on this Mac. Projected sustained about **1.8–2.4x** on the iPad and **1.5–2.1x** on the iPhone, which is real time with margin.
- **Threads** could give MM at most 2.09x. MM's mixer meets the producer about every **103 DSP cycles**, which would make a timed threaded mode mostly synchronisation. So threading is deferred, not dropped.
- **StikDebug on TXM devices:**
  - **Protocol:** the app asks the debugger for an RX region (`brk #0xf00d`, x16 = 1), writes code through its own RW alias, and detaches.
  - **Entitlement:** only `get-task-allow`, which development-signed builds carry.
  - **Reliability:** fragile on 27.x. One report says StikDebug 3.2.2 cannot attach on 27.2.1.
  - **So:** the JIT is the fast path and the interpreter is the guaranteed path.

## Fix inventory: what is here, what to port

The old patches are not `git apply`'d. Each real fix is reimplemented, and its commit names the source.

### DSP core (submodule → AKimbo9000/dsp56300-md-mm, branch `ios-port`)

| Fix | Source | In this tree? | Action |
|---|---|---|---|
| `movep` EA routed to peripherals | scratch 9cb3112 | **yes** (8a654fd5, `dsp_ops_move.inl:366-383`, test `movepPeripheralEa`) | — |
| Unknown-opcode safety net | scratch 5d81aa1 | **yes**, done another way (falls back to `Illegal` → `op_Illegal`) | — |
| DO end-word / RTI into LA+1 | md-do-loop-2026-09-18 | **yes**, stronger (`fetchedLA`, `dsp.cpp:611-625`, upstream tests) | Keep. Do not port the patch's condition: it misses call-at-LA and DO FOREVER. |
| Bounded DO yield (`maxDoIterations`) | md-mm-ios | **no**: the whole loop runs inside one `exec()` (`dsp.cpp:615-645`) | Phase 1c: mirror the JIT's own semantics (`jitblock.cpp:822`) and reuse `fetchedLA` |
| DO charged once on bounded return | md-mm-ios / session | partial (charged before the loop, `dsp.cpp:596-601`) | Add an `instructionsBefore` guard in `execOp`, together with the yield |
| Interpreter PC guard | interpreter-pc-guard | **no** (`dsp.cpp:401-404`) | Check **before** `fetchPC()` (`dsp.h:320-323`), not after it as the patch did |
| Rn+displacement moves → peripherals, plus `Movexr_ea`/`Movexr_A`/`Moveyr_A`/`Movec_ea` write sides | START_HERE audit | **no** (`dsp_ops_move.inl:73,77,125,129,141,145,169,180,218,291`) | Port, with unit tests modelled on `movepPeripheralEa` |
| Rate-limited `LOG_ERR_BASE` | md-mm-ios | **no** (`error.h:10-15`) | Port |
| cowmemory: Mach VM on macOS only | md-mm-ios | **no** (`cowmemory.cpp:3-59`, `__APPLE__` only) | Port (`DSP56K_COW_MACH`); iOS takes the existing `return false` path |
| asmjit `OSCacheControl.h` on all Apple targets | cv asmjit f49161e | **no** (`virtmem.cpp:31-33` vs `762-763`) | Commit to AKimbo9000/asmjit. asmjit is always linked. |
| ARM64 trampoline `lea_` for the register offset | md-do-loop | **no**, latent (`jittrampoline.cpp:156,360`) | Port. Cheap, and needed for arm64 Debug. |
| Realtime window fallback 48k/128 | cv dsp56300 e42e4d93 | **no** (still 44100/2048, `threadtools.cpp:182-183`) | Phase 3 (threads use it) |
| `execMinimalStep`, `DSP_PC_HIST`, `MD_*` traces, write trap at 0x101327, `dma`/`hdi08` diagnostic accessors | md-mm-current-state / session | not present | **Don't port** (debris) |

### Parent repo (mdLib, build glue)

| Hunk | In this tree? | Action / phase |
|---|---|---|
| base.cmake Debug/NDEBUG; juce.cmake mic permission; `-ios` products suffix | covered, or unnecessary (`base.cmake:197`, `juce.cmake:175`, `GEARMULATOR_JUCE_PRODUCTS_ROOT`) | drop |
| lua `LUA_USE_IOS` | no | Phase 4 |
| 3rdparty only with the plugin | no; can't apply as written (synthLib needs libresample) | Phase 4: gate only freetype/lua/lunasvg/RmlUi |
| changelog skipped when cross-compiling | the mechanism exists (`TUS_CAN_RUN_BUILT_BINARIES`) but is off for iOS | Phase 4 |
| mdJucePlugin ad-hoc `codesign` under `if(APPLE)` | no (`mdJucePlugin/CMakeLists.txt:96-103`) | Phase 4: skip on iOS |
| juce.cmake AUv3 option; iOS → AUv3 + Standalone only | no | Phase 4 (cv 1c03315c is the reference) |
| juceRmlUi on iOS: no GL/Metal, fix `JUCE_MAC`, GL guards in `juceRmlComponent` | no | Phase 4 |
| `GL_R` → `GL_RED` (`RmlUi_Renderer_GL3.cpp:739`) | no; a real bug on every platform | Phase 4, own commit |
| optimization.cmake: PGO mismatch → warning | no | Phase 5, **iOS only** |
| mdBench + iOS bundle | absent; it also bundled ROMs from `/private/tmp` | replace with a render/bench mode in mdLibTest plus the app's `--selftest` |
| `MD_DSP_THREADS`, `HDI08_SLACK`/`POST_ONLY`, `HOST_BACKLOG`, `PORTC_*`, `DSP_LEAD`, `LIVE_UCCYCLE`, `TIMED_HOSTRX`, mutex codec staging | not present | **Don't port**: disproved, or clamps/heuristics. The Phase 3 timed mode supersedes them. |
| `panelReadiness` dumps, `MD_HOSTWORDS`, `MD_PROGRESS`, `STUCK*` | not present | **Don't port** |
| cv framework iOS commits 78ba807c (fit), 2dc6f3dc (rotate), 61ae79ac (long-press), 12e75660 (advanced options), generic part of 1ec91a01 (per-instance workgroup, reset, tail) | no | Phase 6 |

**New issues found that no patch covers:**
- `mdRamPackingTest` calls `execJit()` in interpreter builds (`mdRamPackingTest.cpp:85,117`). Fix in Phase 1.
- Root `CMakeLists.txt:7,13-17` caches `arm64;x86_64` and a 10.13 deployment target. iOS overrides both.
- `conditionvariable.cpp:86` uses the private `__ulock_wait`. That's fine for development builds; note it for App Store.

## Do not repeat (disproved in the prior docs)

- **Free-running thread per DSP (`MD_DSP_THREADS`).** It broke even the JIT (3,160 bytes).
- **Post-only HI08 writes without threads.** They deadlock.
- **HI08 drain clamp/slack sweeps.** 256 was worse than 2048, and clamps break with the producer upload.
- **Scheduler slices** `MD_MAX_INSTR_PER_BLOCK` 1–16, and fixed 32-instruction slices meant to imitate JIT blocks. JIT blocks end at branches.
- **`MD_MAX_DO_ITERATIONS=0`.** It deadlocks. Slice size was also never the fix.
- **Frame-sync fast-forward as the cause** (`MD_NO_FRAMESYNC_FF`). It is never taken during boot.
- **Rejected or no-effect mechanisms:** Port C edge queue (rejected); `PORTC_RELAX`+`HOST_BACKLOG` combined (worse); UC stall and host backlog (heuristics).
- **Excluded with controls:** HF2/HF3, TXDE/TRDY derivation, the RX latch, the ESSI clock source (already `Cycles`), and "more runtime".
- **Release builds for correctness diagnosis.** NDEBUG compiles out all 323 asserts, so use Debug. Debug and Release give byte-identical emulated state.
- **Apple threading practices that fail:** never `yield()` on Apple (it demotes to E-cores); `THREAD_AFFINITY_POLICY` is unsupported; `thermalState` is not evidence.

## Verify-script contract (all three scripts)

- **Output.** Lines of the form `VERIFY <key> … PASS|FAIL|INFO`, ending with `VERIFY RESULT PASS|FAIL`. Exit 0 only on PASS. Full logs and a JSON copy go to `build/verify/<script>/<timestamp>/`. Common code lives in `scripts/lib/verify-common.sh`.
- **ROMs.** Located by SHA-256 in `roms/` (names have spaces), overridable with `GEARMULATOR_MD_FIRMWARE_BIN` / `GEARMULATOR_MM_FIRMWARE_BIN`. Never printed, and never copied into a bundle.
- **Fixed scenarios.**
  - MD: cold boot in a fresh temp state directory, then Trig 1 press, hold 2 s, release, render 10 s.
  - MM: the same: cold boot, then Trig 1 press, hold 2 s, release, render 10 s.
  - Both run in `trigPressFirmwareTest` (fixed 256-frame `processAudio` blocks, 25 s boot). The
    existing audio tests drive randomized host blocks through the resampler, so they can't give
    an exact raw-word hash.
- **Hashes and counters.**
  - Hash the **raw 24-bit DSP output words**, not floats, so the hash doesn't depend on platform or flags.
  - Record counters every 0.25 s emulated: UC cycles, and each DSP's instructions, cycles and PC.
  - On a mismatch, print the first divergent checkpoint.
- **JIT baseline.** `doc/ios-baselines/jit-render.json` records the JIT hashes from the untouched tree. Every later phase must reproduce them, which is the "desktop JIT unchanged" rule.

## Done so far

| Phase | State |
|---|---|
| 0 Harness, verify scripts, JIT baseline | done |
| 1 Port fixes; interpreter == JIT on macOS | partly done. 1a fixes landed; DO-loop livelock fixed; MD interpreter audio == JIT. MM differs (stop-point granularity); its sine tests show link dropouts |
| 2 Interpreter speed on this Mac | done (`verify-interp.sh --perf` PASS) |
| Interpreter speedups | two fast paths, results bit-identical; MM +1.5%, MD +10% |

## Phase A — Both engines in one DSP core

**Work:**
- Make the engine a runtime choice per `dsp56k::DSP`, fixed at construction: JIT or interpreter.
- `exec()`, `execUntilCycles()`, `execInlinePeripheralCheck()` and the interpreter-only cycle accounting branch on a member, not on `g_useJIT`.
- A third CMake mode, `DSP56K_RUNTIME_ENGINE=ON`, compiles both. The existing `DSP56K_FORCE_INTERPRETER` and the default desktop JIT build keep their constant paths, unchanged.
- `trigPressFirmwareTest` gains `--engine jit|interp` in a runtime build.

**Done when:**
```
$ scripts/verify-interp.sh                     (existing checks, plus a runtime-engine build)
VERIFY jit.unchanged.md / .mm == doc/ios-baselines PASS
VERIFY runtime.jit md,mm == doc/ios-baselines PASS
VERIFY runtime.interp md,mm == interpreter build (hash and every counter checkpoint) PASS
VERIFY perf.mm.runtime.interp rtf within 2% of the interpreter-only build PASS
```

**Risk:** the per-call engine branch costs interpreter speed. Measure it; if it shows, specialise the scheduler's hot loop per engine.

## Phase B — JIT memory that works on TXM, proved on macOS first

**Work:**
- **Measure** MM's and MD's peak JIT code size on macOS from the asmjit allocator statistics, and set the region size to about 2× that.
- **Code allocator:** an `asmjit::Target` subclass backed by one pre-reserved region with an RX view and an RW alias:
  - sub-allocate from a free list, and free when P memory changes;
  - `relocateToBase(rx)`, then `copyFlattenedData(rw)`, then `sys_icache_invalidate(rx)`;
  - the JIT and trampolines use it instead of `JitRuntime`.
- **On macOS** the region comes from `mach_vm_allocate` + `mach_vm_remap` with MAP_JIT rules. This proves the allocator against the baseline before any device run.
- **iOS front end:**
  1. Check `CS_DEBUGGED` (`csops`).
  2. Run the StikDebug prepare handshake for that one region, with a SIGTRAP guard for an unanswered `brk`.
  3. Validate the address: 16 KiB aligned, and an RX mapping of this process.
  4. Create the `vm_remap` RW alias, then detach.
  5. On any failure, use the interpreter.

**Done when:**
```
$ scripts/verify-interp.sh
VERIFY jit.region.md,mm (custom allocator, dual-mapped region) == doc/ios-baselines PASS
VERIFY jit.region.peak_code md=<KB> mm=<KB> (region <MB>) INFO
```

**Risks:**
- Blocks that are invalidated and recompiled must not exhaust the fixed region. Compact or reset the region when it is full, without changing emulated results.
- `get-task-allow` builds only.

## Phase C — Standalone Monomachine app (simulator, then devices)

**Work:**
- **iOS build glue** (from the Parent-repo table): lua `LUA_USE_IOS`, gate 3rdparty, changelog skip, no ad-hoc codesign, JUCE Standalone only (AUv3 off for now), juceRmlUi without GL/Metal, `GL_RED`.
- **App:** bundle ID `com.aki.gearmulator.mm`, `DSP56K_RUNTIME_ENGINE=ON`, deployment target 17.4.
- **ROM import from the Files app:** a document picker, plus `UIFileSharingEnabled`/`LSSupportsOpeningDocumentsInPlace`. Validate the 8 MiB size and the FNV fingerprint, then copy into `Documents/ROMs`. Never bundle ROMs.
- **Engine shown to the user:**
  - a header badge, "Engine: JIT (StikDebug)" or "Interpreter";
  - a settings page with the reason (not debugged / handshake failed / region too small / disabled) and the live audio load;
  - "Relaunch with JIT" through `stikdebug://enable-jit?bundle-id=…`.
- **`--selftest`:** headless; records engine, hash, rtf and peak memory to `Documents/selftest-<id>.json`.
- **Port the iOS editor fixes:** fit, rotate, long press.

**Done when:**
```
$ scripts/verify-sim.sh                       (iPad Pro 13-inch (M5) simulator)
VERIFY sim.mm.selftest engine=interpreter hash == macOS interpreter PASS
$ scripts/verify-device.sh --device ipad   and   --device iphone
VERIFY preflight device=<name> devmode=enabled identity=present PASS
VERIFY device.mm.selftest engine=interpreter hash == macOS interpreter PASS rtf.sustained-60s=<x>
VERIFY device.mm.selftest engine=jit (StikDebug attached) hash == doc/ios-baselines PASS rtf.sustained-60s=<x>
VERIFY bundle.roms none PASS
```
Plus a manual check, run by you: no Mac attached, the app launches, imports a ROM from Files, and plays on both devices, on JIT and on the interpreter.

**Risks:**
- StikDebug may not attach on 27.2.1 (iPhone); that is a third-party report. The interpreter fallback covers it, slower.
- Thermal throttling on the phone.
- AUv3-style memory limits do not apply to a standalone app, but peak memory is recorded anyway.

## Phase D — One-time setup guide and engine fallback test

**Work:**
- **`doc/IOS_SETUP.md`:**
  - the one-time Mac steps: install the app (paid team, one-year profile), the StikDebug pairing file, StikDebug and LocalDevVPN;
  - the steps on each launch: LocalDevVPN on, StikDebug → our app, or the in-app "Relaunch with JIT";
  - re-mounting the disk image after a reboot.
- **Fallback test:** the app with StikDebug absent or failing must start on the interpreter and say so.

**Done when:** `verify-device.sh --fallback` shows `engine=interpreter reason=not-debugged` on both devices, and the app shows the badge.

## Deferred (revisit only on the measurement named)

- **Threads (old Phase 3)** — only if the JIT route fails on both devices. MM's bound is 2.09x, but its sync gap is about 103 DSP cycles.
- **Block-shaped interpreter (old 1b)** — needed only for MM interpreter == JIT exactness and the MM sine dropouts. Revisit if the interpreter has to be the shipping engine. Its next step is in the status file: count the link drops around frame 1975 of the sine test.
- **AUv3 and App Group (old Phase 6), and Machinedrum.** MD runs at 0.945x on the interpreter here, about the same as MM's JIT case for margin. They follow once the MM standalone app passes Phase C.
- **Phase 1.5 bench app:** superseded by Phase C's `--selftest` on both devices.

## Needs from you

1. **On both devices, one time:** install StikDebug and LocalDevVPN, and create the pairing file. The pairing file needs the Mac once; I will write the exact steps in Phase D.
2. **A signing identity** for the team in `CLAUDE.local.md`. There were still 0 when last checked, and Phase C's device runs need one.
3. **Keep both devices unlocked**, with auto-lock off, during device runs. Keep the Mac quiet during speed runs.
4. **Phase C's manual no-Mac check** on both devices.

## Verification (end to end)

- **Before every commit to the DSP core:** `verify-interp.sh`, with JIT baseline unchanged and interpreter hashes as before.
- **After any speed change:** `verify-interp.sh --perf`.
- **For the devices:** `verify-sim.sh` and `verify-device.sh`, recording the engine, hash and sustained rtf on both devices.
- **Workflow:** commit small steps to `ios-port`; push submodule work to the AKimbo9000 forks; record numbers in `doc/IOS_PORT_STATUS.md`.

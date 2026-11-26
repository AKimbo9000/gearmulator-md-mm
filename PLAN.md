# PLAN — Gearmulator MD/MM on iOS/iPadOS (standalone + AUv3)

## Context

This tree (alpha.16, branch `ios-port`) has no iOS support. Earlier work
(`../gearmulator-ios`, Sept 2026) got MD to boot under the interpreter once
`movep` was fixed, and got MM past its halt once the DO-loop end-word was fixed.
That work was done on alpha.12's old flat layout, and its patches mix real fixes
with a lot of diagnostics and timing heuristics.

The goal: MD and MM as an iPad app and an AUv3, with interpreter-only DSPs (no
JIT on iOS), the same emulated results as the JIT, and real time on the
user's iPad.

**Facts this plan rests on (all measured or read this session):**
- **DSP core location.** The core is a submodule: `source/cpu/dsp56300` points to joelanders/dsp56300-md-mm, and its nested `source/asmjit` points to dsp56300/asmjit.
- **Forks (approved).** Fork both to AKimbo9000, repoint `.gitmodules`, and push submodule work there.
- **Engines stop at different points today.** The JIT runs blocks of up to 32 instructions on MD and checks peripherals, interrupts and `execUntilCycles` targets only between blocks. The interpreter checks them before every instruction ([dsp.h:221-260](source/cpu/dsp56300/source/dsp56kEmu/dsp.h:221)). So whole-machine JIT and interpreter renders cannot match until the interpreter follows JIT block boundaries.
- **The JIT block planner is reusable.** [`JitBlock::getInfo`](source/cpu/dsp56300/source/dsp56kEmu/jitblock.h:47) is a static planner that generates no code, so the interpreter can call it.
- **MD's scheduler order is set by run order, not timestamps.**
  - It interleaves the ColdFire and both DSPs on one thread ([mdhardware.cpp:1439-2081](source/elektron/md/mdLib/mdhardware.cpp:1439)).
  - DSPs run inside ColdFire instructions and inside each other's callbacks.
  - Four "run until a condition" loops in `mddsp.cpp` overshoot by up to 100k cycles.
- **Threading decision (approved).** Phase 3 is a new opt-in *timed* mode. The desktop default stays serial.
- **Missing test tools.** There is no `mdBench`, no `trigPressFirmwareTest`, no WAV/render output and no JIT-vs-interpreter whole-machine test. CLAUDE.md's mention of `trigPressFirmwareTest` refers to the old fork.
- **ROM tooling to reuse.** Tests read `GEARMULATOR_MD_FIRMWARE_BIN` and `GEARMULATOR_MM_FIRMWARE_BIN`. Both local ROMs match the SHA-256s in `scripts/macos/build_mdmm.sh`.
- **Mac.** M4 Max (12P+4E), Xcode 27.2, iOS SDK 27.2, CMake 4.5.1, ninja available.
- **iPad.** **iPad Pro 13" M4**, connected. The prior iPad numbers were taken on an M5.
- **Signing.** There were 0 code-signing identities in the keychain; you are creating one. The team (ID in `CLAUDE.local.md`, never in this file) is paid, so App Groups are available.

**Bundle IDs** (also recorded in `CLAUDE.local.md`):

| Product | Bundle ID |
|---|---|
| MD app | `com.aki.gearmulator.md` |
| MD AUv3 | `com.aki.gearmulator.md.auv3` |
| MM app | `com.aki.gearmulator.mm` |
| MM AUv3 | `com.aki.gearmulator.mm.auv3` |
| App Group | `group.com.aki.gearmulator.mdmm` |
| Phase 1.5 bench app (my default; say if you want another) | `com.aki.gearmulator.bench` |

**Prior iPad numbers (M5, interpreter):** MD 0.576x real time, needing ~1.74x more. MM 0.71–0.79x. So threads (Phase 3) are needed for speed, not only for structure.

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

## Phase 0 — Commit CLAUDE.md and .gitignore, forks, harness, verify scripts

**Work:**
0. Add the bundle IDs above to `CLAUDE.local.md` (gitignored, not committed).
1. Commit CLAUDE.md and .gitignore as they are. Then make a separate commit correcting CLAUDE.md:
   - Apple Release is `-Ofast -fno-lto`, not `-flto`.
   - `trigPressFirmwareTest` is not in this tree.
   - Add the fork push targets.
2. Run `gh repo fork joelanders/dsp56300-md-mm` and `gh repo fork dsp56300/asmjit` under AKimbo9000. Set the submodule URLs (`git submodule set-url`), create `ios-port` branches, and commit.
3. Add the render and counters harness in **test code only**, so the JIT baseline is recorded before any emulator change:
   - Add `trigPressFirmwareTest md|mm [--render <out>] [--counters <out>]`, modelled on `encoderPressFirmwareTest`. (Done this way rather than extending `mdAudioFirmwareTest`/`mmAudioFirmwareTest`, whose randomized resampled host blocks can't give an exact raw-word hash.)
   - Reuse `benchmarkDevice()` (`mdUwFirmwareTest.cpp:268`) and the waveform diff (`mdBlockLinkFirmwareTest.cpp:187`).
4. Write `scripts/verify-interp.sh` (Ninja, Release, arm64-only, plugins off, other synths off):
   - Builds `build/mac-jit` and `build/mac-interp`, the latter with `-DDSP56K_FORCE_INTERPRETER=ON`.
   - Flags: `--debug` (builds the Debug variants), `--perf`, `--baseline`.
5. Write `verify-sim.sh` and `verify-device.sh` to the same contract. Until their phase lands, they fail at a named stage.

**Done when:**
```
$ scripts/verify-interp.sh --baseline
VERIFY build.jit OK / build.interp OK
VERIFY dsp56kTestRunner.jit exit=0 PASS        (.interp recorded as-is)
VERIFY md.jit.determinism run1==run2 PASS      VERIFY mm.jit.determinism PASS
VERIFY md.render jit=<h> interp=<h'> MISMATCH first-divergence=<ckpt> INFO
VERIFY baseline written doc/ios-baselines/jit-render.json
VERIFY RESULT PASS   (exit 0; baseline mode)
$ scripts/verify-sim.sh      → VERIFY stage=configure FAIL (expected until Phase 4), exit 1
$ scripts/verify-device.sh   → VERIFY preflight device=connected identity=MISSING FAIL, exit 1
```
`bash -n` must pass on all three scripts, plus shellcheck if it is installed. `.gitmodules` must point at the AKimbo9000 forks.

**Risks:**
- The JIT may not be deterministic run to run. If not, stop: a fixed reference is a precondition for everything else.
- MD's first boot formats +Drive and reboots, which is long but deterministic. If a factory-cache start is needed, use the existing `GEARMULATOR_MD_FACTORY_CACHE`.
- The interpreter build may not compile or may crash (`mdRamPackingTest`). In baseline mode that is recorded, not fixed.

## Phase 1 — Port fixes; MD and MM interpreter renders match the JIT on macOS

**Work:**
- **1a. Port the DSP-core fixes in the table**, each with a unit test in `dsp56kTestRunner`:
  - PC guard
  - Rn+displacement peripheral routing
  - `LOG_ERR_BASE` rate limit
  - cowmemory
  - asmjit include
  - trampoline `lea_`

  Fix `mdRamPackingTest` for interpreter builds. These DSP-core iOS fixes compile and are tested on macOS here; the parent build glue waits for Phase 4.
- **1b. Block-shaped interpreter** (the core of this phase). A DSP config flag, set only by mdLib, makes the interpreter follow the JIT's semantics:
  - It keeps a shadow block map (start PC → `JitBlock::getInfo` result, with the same `JitConfig`, the same `ExistingCode`/`VolatileP`/loop-start/loop-end bookkeeping, and invalidation on P writes).
  - It runs each block's instructions with `execOp`.
  - It services peripherals and interrupts only before a block, exactly as `execJitImpl` does.
  - It charges cycles where the JIT charges them.
  - `execUntilCycles` stops at the first block boundary at or after the target.
  - It mirrors `maxDoIterations=4` DO slicing, fast interrupts and `dynamicFastInterrupts`, and the counter effects of `nopLoopSkip`/`spinSkip`/`fastForward`.

  Other synths and `InterpreterUnitTests` keep the per-instruction interpreter.
- **1c. Iterate on the first divergent checkpoint** until MD and MM match. Use Debug (asserts on) for diagnosis.

**Done when:**
```
$ scripts/verify-interp.sh            (and once with --debug: same hashes, no assert)
VERIFY dsp56kTestRunner.jit exit=0 PASS   VERIFY dsp56kTestRunner.interp exit=0 PASS
VERIFY ctest.mdLib.jit N/N PASS   VERIFY ctest.mdLib.interp N/N PASS   (GEARMULATOR_REQUIRE_FIRMWARE_TESTS=1)
VERIFY jit.unchanged md,mm == doc/ios-baselines/jit-render.json PASS
VERIFY md.render jit=<h> interp=<h> MATCH   VERIFY md.counters 120/120 checkpoints MATCH
VERIFY mm.render jit=<h> interp=<h> MATCH   VERIFY mm.counters MATCH
VERIFY md.trig1 peak=<p> >0 PASS            VERIFY mm.trig1 peak=<p> >0 PASS
VERIFY RESULT PASS
```

**Risks:**
- **Biggest risk: a JIT behaviour the interpreter can't mirror** without changing JIT results, such as block history from compile order or cycle-charge timing visible to a peripheral read in the middle of a block. **Escalation:** stop and bring the first-divergence evidence. Never loosen the comparison or touch JIT output.
- **Turn limit.** If an exact JIT == interpreter match for both MD and MM isn't reached within 30 turns of Phase 1 work, stop and report:
  - the closest state reached (which checkpoints match, and for how long each instrument stays identical);
  - the first divergence (checkpoint, DSP, PC, instruction and cycle counts in both engines);
  - what was tried.
- Mirroring the JIT fast paths exactly.
- Sites that mix `exec()` and `execUntilCycles` (mddsp.cpp sites 4–7) must also follow block granularity.

**Upside:** one peripheral check per block instead of per instruction should make the interpreter faster.

## Phase 1.5 — Minimal device build: serial interpreter real-time factor on the iPad

This phase measures the serial interpreter on the real target before any threading design is committed to. It must finish, and its numbers must be in `IOS_PORT_STATUS.md`, before Phase 3 starts.

**Work:**
- **Share the render code.** Move the harness's render/bench routine into mdLib, so mdLibTest, this bench app and the later `--selftest` share one code path.
- **Add a headless bench app**, `com.aki.gearmulator.bench`, as an iOS app target next to mdLibTest:
  - It links only mdLib and the DSP core: no JUCE, no editor.
  - It runs as a real UIApplication with the work on a background queue (the cv `iosmain.mm` pattern), so the watchdog doesn't kill it.
  - It takes `--bench md|mm --seconds N --run-id <id>` through `devicectl` launch arguments.
  - It writes `Documents/bench-<id>.json`.
- **Bring forward only the build glue a library-only iOS build needs:**
  - arch and deployment-target overrides
  - 3rdparty gating (or the lua `LUA_USE_IOS` fix)
  - the changelog skip when cross-compiling
  - automatic signing, with the team read from `CLAUDE.local.md`

  The DSP-core iOS fixes are already in from Phase 1a.
- **Build configurations:** plain Release, and `OPTIMIZE_DSP` + ThinLTO. No PGO yet.
- **Give `verify-device.sh` a `--bench` mode:**
  1. Preflight.
  2. Install.
  3. `devicectl device copy to --domain-type appDataContainer` puts the ROMs in `Documents/ROMs`.
  4. Launch.
  5. Poll for that run-id's JSON.

**Done when:**
```
$ scripts/verify-interp.sh                 (Phase 1 still PASS, run first)
$ scripts/verify-device.sh --bench
VERIFY preflight iPad Pro 13 (M4) unlocked devmode=on identity=present PASS
VERIFY install bench.app PASS   VERIFY roms→container (IPA has none) PASS
VERIFY device.md.serial-interp.<cfg> run=<id> fresh PASS hash==macOS-interp PASS rtf=<x>x (best of 3×20 s) sustained-60s-min=<y>x mixer=<M> producer=<M> Minstr/s
VERIFY device.mm.serial-interp.<cfg> … PASS
VERIFY RESULT PASS
```
Results, including the device/Mac ratio, are recorded in `IOS_PORT_STATUS.md`.

**Risks:**
- A locked device refuses to launch (error 7).
- The watchdog kills a long run if the main thread isn't kept responsive.
- Thermal throttling, which is why the sustained 60 s minimum is reported.
- Provisioning the new bundle ID.
- If the device hash differs from macOS, that is a correctness finding to chase before Phase 3, not noise.

## Phase 2 — Measure interpreter real-time factor (RTF) on this Mac

**Work:**
- Add `--perf` to the harness: free-running, best of 3 × 20 s, refuses to run if the load average is too high.
- Per-component time split via per-slice `mach_absolute_time`, extending the existing `MD_TRANSPORT_DIAGNOSTICS` option.
- Count cross-component sync events per second and their interval histogram (HI08, link, block link, codec). These are the Phase 3 inputs.
- Build configurations: plain; `GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP` + `THINLTO` (the switch that covers dsp56kEmu); and PGO on top, trained on the interpreter.

**Done when:**
```
$ scripts/verify-interp.sh --perf
VERIFY perf.idle loadavg=<x> PASS
VERIFY perf.md.interp.<cfg> rtf=<x>x mixer=<M>Minstr/s producer=<M> split uc/mix/prod=<a>/<b>/<c>%   (per cfg)
VERIFY perf.mm.interp.<cfg> …        VERIFY perf.md.jit / perf.mm.jit (reference)
VERIFY perf.sync md hi08=<n>/s link=<n>/s min-gap=<cyc>   mm …
VERIFY perf.bound threads≤<sum/max>x   (upper bound for Phase 3)
VERIFY RESULT PASS
```
The numbers, and an iPad projection, are written to `IOS_PORT_STATUS.md`. The Mac M4 Max and the iPad M4 share a core generation, so expect the iPad's *sustained* rate to be lower.

**Risks:**
- Machine load swings results by up to 9×, so the idle check is enforced.
- Timers must be per slice, never per instruction.

## Phase 3 — Timed threaded mode (opt-in), JIT == interpreter

**Design:**
- **Clocks.** The UC, DSP1 and DSP2 each keep a local clock in one integer timebase (the `mdhostclock.h` conversions).
- **Timestamped events.** Every cross-component effect becomes an event delivered at the receiver's first block boundary at or after the timestamp. Phase 1 makes that point identical in both engines. The events are:
  - HI08 words, commands and IRQs
  - HOTX and flags
  - ESSI0 link frames
  - block-link DMA deliveries (today these write directly into the peer's memory)
  - Port C edges
  - codec frames to the host queue
- **Cross-component reads** (HI08 status, Port C, DDR4) return the peer's published state as of `t − L`. A reader waits only if the peer hasn't published that far: conservative sync with lookahead L.
- **Where L comes from.** L is derived from hardware latencies, such as at least one ESSI frame (2,304 DSP cycles) on the serial link. It is never tuned to make a test pass.
- **Removed in timed mode.** The run-until-condition loops (sites 4–7) and the nested catch-ups (sites 2–3). The UC waits in emulated time.
- **Executors.**
  - A *sequential* executor runs the timed mode on one thread.
  - A *threaded* executor runs it on three threads: spin then `__ulock`, no yield, the realtime policy with the 48k/128 window ported from cv.
  - The two must produce identical bits; that is the determinism proof.
- **Selection.** `GEARMULATOR_MDMM_TIMED=1` or a device option. Default off on desktop, on for iOS.

**Phase 3 runs as four separate goals, in this order.** Each ends with its script output and a short report. The next goal starts only after the previous one passes. Every goal also requires `VERIFY serial.jit.unchanged == baseline PASS`, so the desktop default is untouched.

### Goal 3a — Timed mode on one thread (sequential executor); firmware tests pass
```
$ scripts/verify-interp.sh --timed=seq
VERIFY serial.jit.unchanged == baseline PASS
VERIFY ctest.mdLib.timed-seq.jit N/N PASS   (GEARMULATOR_REQUIRE_FIRMWARE_TESTS=1)
VERIFY timed-seq.md.determinism run1==run2 PASS   timed-seq.mm … PASS
VERIFY timed-seq.md.trig1 peak=<p> >0 PASS   timed-seq.mm.trig1 peak=<p> >0 PASS
VERIFY RESULT PASS
```
**Risks:**
- The firmware handshakes (MD block link and rendezvous, MM backpressure) may not tolerate a hardware-justified L. If so, report; don't tune L.
- Tests that reach into the scheduler through friend access need porting.

### Goal 3b — JIT == interpreter in timed sequential mode
```
$ scripts/verify-interp.sh --timed=seq
VERIFY ctest.mdLib.timed-seq.interp N/N PASS
VERIFY timed-seq.md.jit==interp render+counters MATCH   timed-seq.mm … MATCH
VERIFY RESULT PASS
```
**Risk:** an event delivery point that differs between engines. Phase 1's block shaping should already cover it. The same 30-turn stop-and-report rule as Phase 1 applies.

### Goal 3c — Threaded executor bit-identical to sequential
```
$ scripts/verify-interp.sh --timed=threaded
VERIFY timed.md.seq==threaded {jit,interp} 5/5 runs (+1 under stress load) PASS   timed.mm … PASS
VERIFY ctest.mdLib.timed-threaded.{jit,interp} N/N PASS
VERIFY tsan.timed-threaded md,mm 0 reports PASS
VERIFY RESULT PASS
```
**Risks:** races in state that has a single owner today (the host audio queue, block-link direct memory writes), and lock-ordering deadlocks.

### Goal 3d — Speed
```
$ scripts/verify-interp.sh --timed=threaded --perf
VERIFY perf.idle PASS
VERIFY perf.timed.md.interp rtf=<x>x (×<s> vs serial, ≤ Phase 2 bound)   perf.timed.mm …
VERIFY RESULT PASS
```
**Target:** at least 1.6× over the serial interpreter on the Mac, and enough to reach real time on the iPad given the Phase 1.5 device/Mac ratio. If the mode is correct but below 1.3×, I report before Phase 4.

**Risk:** a small L makes sync dominate the run time.

**Overall size:** roughly 2,000 lines across mdhardware/mddsp/mdblocklink.

## Phase 4 — iOS Simulator build plus self-test

**Work:**
- Port the Phase 4 rows of the parent table, using cv 1c03315c as the reference.
- Configure with `-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_SYSROOT=iphonesimulator -DDSP56K_FORCE_INTERPRETER=ON`, a deployment target of 17.4, and only the Standalone and AUv3 formats.
- Bundle IDs come from the table in Context. Override JUCE's default appex ID so the extensions are `…md.auv3` and `…mm.auv3`.
- Add `--selftest --run-id <id> --mode serial|timed` to the MD and MM standalone apps. It runs the **same mdLib render routine as the harness and the Phase 1.5 bench app**, headless and without the editor.
  - It loads ROMs from `Documents/ROMs`.
  - It writes `Documents/selftest-<id>.json` with the hash, counters, RTF, peak level and peak resident memory.
  - It prints a marker line and exits.
- `verify-sim.sh`:
  1. Boot "iPad Pro 13-inch (M5)" in the Simulator.
  2. Install both apps.
  3. Copy ROMs into each data container (`simctl get_app_container … data`).
  4. Launch with `simctl launch --console-pty`.
  5. Compare against the macOS hashes.

**Done when:**
```
$ scripts/verify-sim.sh
VERIFY build.sim md.app mm.app (+appex) OK
VERIFY sim.md.selftest run=<id> serial=<h>==macOS PASS timed=<h>==macOS PASS rtf=<x> rss=<MB>
VERIFY sim.mm.selftest … PASS
VERIFY bundle.roms none PASS
VERIFY RESULT PASS
```

**Risks:**
- JUCE 7.0.10 with CMake 4.5 policy errors; may need `CMAKE_POLICY_VERSION_MINIMUM`.
- Software RmlUi rendering cost.
- `-ffp-contract=off` must apply to `mdhardware.cpp` on iOS too, since float frame maths decides run order.
- Memory: `m_opcodeCache` is sized by `sizeP()` for two DSPs. AUv3 extensions have tight memory limits, so check the RSS here.

## Phase 5 — Device build plus self-test

**Work:**
- Reuse the Phase 1.5 device plumbing (signing via `CLAUDE.local.md`, ROM copy, run-id polling) for the real MD and MM apps.
- PGO trained on macOS, with mismatches as warnings on iOS only.
- `verify-device.sh` launches each app with `devicectl device process launch … --selftest --run-id <id>`. It then polls `copy from` until that run-id's JSON exists; the run-id defeats the stale-file trap.

**Done when:**
```
$ scripts/verify-device.sh
VERIFY preflight iPad Pro 13 (M4) unlocked devmode=on identity=Apple Development PASS
VERIFY install md,mm PASS   VERIFY roms→container (IPA has none) PASS
VERIFY device.md.selftest run=<id> fresh PASS serial,timed hash==macOS PASS rtf.timed=<x>x sustained-60s-min=<y>x
VERIFY device.mm.selftest … PASS
VERIFY RESULT PASS
```

**Real-time target:** at least 1.0× sustained for 60 s, for both instruments in timed mode on the M4. If not met, the levers are PGO, `OPTIMIZE_DSP`/ThinLTO and the thread policy, each measured.

**Risks:**
- The M4 is slower than the M5 the earlier numbers came from.
- Thermal throttling.
- A locked device refuses to launch (error 7).
- The watchdog kills a long run if the UIApplication main thread isn't kept responsive.

## Phase 6 — AUv3, plus ROM import from the Files app

**Work:**
- **App Group** `group.com.aki.gearmulator.mdmm`, shared by the MD and MM apps and both `.appex` (JUCE `APP_GROUPS_ENABLED`/`APP_GROUP_IDS`). On iOS, the RomLoader search path adds the group's `ROMs/` folder.
- **Import ROM…** in the editor (`juce::FileChooser` → document picker):
  - Validate the size (8 MiB) and the `md::RomLoader` FNV fingerprint.
  - Copy into the group's `ROMs/` and reload.
  - `UIFileSharingEnabled` + `LSSupportsOpeningDocumentsInPlace` let ROMs dropped into Documents be adopted too.
- **Port the cv editor and AUv3 commits:** fit, rotate, long-press, advanced options, per-instance workgroup, a `reset()` that drops stale audio, and a real tail.
- **Make sure MD's `processAudio` path never blocks** (IOS_AUV3 lesson).
- **Self-tests:**
  - `--selftest-auv3` instantiates the app's own extension out of process via `AVAudioUnit` and renders offline.
  - `--selftest-import` exercises the accept and reject paths.

**Done when:**
```
$ scripts/verify-device.sh --auv3
VERIFY appgroup ROMs/ visible to md.app mm.app md.appex mm.appex PASS
VERIFY rom-import reject-size PASS reject-fingerprint PASS accept-MD PASS accept-MM PASS
VERIFY auv3.md out-of-process render peak=<p> stalls=0 PASS   auv3.mm … PASS
VERIFY bundle.roms none PASS
VERIFY RESULT PASS
```
You also confirm the manual checks in `IOS_PORT_STATUS.md`:
- Both AUv3s load in a host on the iPad.
- Import through the Files picker works.
- Rotation and backgrounding behave.

**Risks:**
- The AUv3 memory cap.
- Document pickers inside an extension may be limited. Fallback: import in the container app; the App Group serves the extension.
- Host buffer sizes, and RmlUi CPU competing with emulation.

## Needs from you

1. **A signing identity, now needed by Phase 1.5** (you're creating it). It's the Apple Development certificate for the team in `CLAUDE.local.md`. `verify-device.sh` preflight checks for it.
2. **The iPad during device runs (Phases 1.5, 5, 6):** Developer Mode on, this Mac trusted, unlocked, and auto-lock off.
3. **The bench app ID.** `com.aki.gearmulator.bench` is my default for Phase 1.5; say if you want another.
4. **A quiet Mac** (no builds, other jobs) when I flag benchmark runs in Phases 1.5, 2, 3d and 5.
5. **For Phase 6:** an AUv3 host on the iPad (AUM, Logic, GarageBand), and running the Files-picker and host checks by hand.
6. **Optional:** CLAUDE.local.md lists the ROMs without `.bin`. The scripts match ROMs by hash, so nothing breaks.

## Verification (end to end)

Each phase ends with its script passing exactly as shown, the numbers recorded in `doc/IOS_PORT_STATUS.md`, and small commits on `ios-port`. Those commits go to origin, and submodule commits go to the AKimbo9000 forks. `verify-interp.sh` (with the JIT baseline check) runs before every device run and after every DSP-core change.

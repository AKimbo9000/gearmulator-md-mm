# iOS port status

Living record for the iOS/iPadOS port of MD/MM. The plan, phases and done criteria
are in [`PLAN.md`](../PLAN.md). Every number here comes with the command that
produced it.

## Current phase

**Phase 1 in progress.** Stopped at the 30-turn limit on 2026-11-26 without an exact JIT == interpreter match. The closest state and the first divergence are below.

| Phase | State |
|---|---|
| 0 Commit, forks, harness, verify scripts | **done** |
| 1 Port fixes; interpreter == JIT on macOS | in progress: 1a done, livelock fixed, MD audio matches, MM diverges |
| 1.5 Minimal device build, serial interpreter RTF | not started |
| 2 Interpreter RTF on this Mac | **done** (`verify-interp.sh --perf` PASS) |
| 3a–3d Timed threaded mode | not started |
| 4 Simulator build + self-test | not started |
| 5 Device build + self-test | not started |
| 6 AUv3 + Files ROM import | not started |

## Environment

- Mac: Apple M4 Max (12 P + 4 E cores), Xcode 27.2, iOS SDK 27.2, CMake 4.5.1, ninja.
- Device: iPad Pro 13-inch (M4), `iPad16,6`. The prior work measured an M5.
- Firmware: both ROMs in `roms/` match the SHA-256s in `scripts/macos/build_mdmm.sh`.

## Log

### 2026-11-26 — survey and plan

- Compared `../gearmulator-ios/patches/*.patch`, the scratch-clone commits named in its
  docs, and `../gearmulator-ios/libs/gearmulator` (charlesvestal, already in this tree's
  layout) against alpha.16. The inventory is in `PLAN.md`.
- Already in this tree: `movep` peripheral EA routing, the unknown-opcode fallback, and the
  DO-loop end-word / RTI-into-LA+1 handling.
- Missing:
  - bounded DO yield
  - interpreter PC guard
  - Rn+displacement peripheral routing
  - rate-limited `LOG_ERR_BASE`
  - cowmemory and asmjit iOS fixes
  - trampoline `lea_`
  - all parent-repo iOS build glue
- Finding: the JIT checks peripherals, interrupts and run targets between blocks of up to
  32 instructions, while the interpreter checks them before every instruction
  (`dsp.h:221-260`). Whole-machine renders can therefore only match once the interpreter
  follows JIT block boundaries (Phase 1b).
- Finding: there is no `mdBench`, no `trigPressFirmwareTest` and no render output in this
  tree. Phase 0 adds the harness.

### 2026-11-26 — Phase 0 done

- Forks: `source/cpu/dsp56300` → AKimbo9000/dsp56300-md-mm `ios-port` (d8008e77), whose
  `source/asmjit` → AKimbo9000/asmjit `ios-port`. In both, `origin` is the fork and `upstream` the original.
- Harness: `trigPressFirmwareTest md|mm` (25 s cold boot, Trigger 1 press, 2 s hold, release,
  10 s tail; 1,631,700 frames). It hashes the raw 24-bit output words, and `--counters` writes per-DSP
  counters every 0.25 s.
- `scripts/verify-interp.sh --baseline` → `VERIFY RESULT PASS` (exit 0), logs
  `build/verify/verify-interp/20261126-195744`:

  | | JIT hash | JIT run1 == run2 | hold peak | rtf (4 parallel runs) |
  |---|---|---|---|---|
  | MD | `bdb35c9d2cdc6bb9` | PASS (hash + 148 checkpoints) | 0.223144 | 5.52x |
  | MM | `6adf0ea75100b8c3` | PASS | 0.109842 | 2.61x |

  The MD hold peak equals the 0.223144 the Sept 2026 MD interpreter run reported.
  The reference is in `doc/ios-baselines/` (JSON plus counter CSVs).
- `dsp56kTestRunner`: exit 0 in both builds.
- **Interpreter baseline: both MD and MM livelock at boot.** After 7 min at 100% CPU,
  `sample` put 855/855 samples inside one `DSP::op_Dor_S` hardware loop: MD entered through
  `Dsp::writeWordToDsp` (from a ColdFire HI08 write), MM through `schedStep`. Under the
  interpreter `maxDoIterations` has no effect, so the whole loop runs inside one `exec()` and
  the ColdFire it waits on never runs. This is the livelock in `MD_MM_IOS.md`. The bounded-DO
  yield (Phase 1) addresses it. The runs were stopped by hand (exit 143); no counters had been
  flushed. Phase 1 should flush the counter CSV per line so a stall is visible.
- `scripts/verify-sim.sh` → `VERIFY stage=configure … FAIL` (juce.cmake has no AUv3/iOS
  support yet), exit 1. Preflight passes: Xcode 27.2, iOS 27.2 runtime, iPad Pro 13-inch (M5)
  simulator, ROMs.
- `scripts/verify-device.sh` → `VERIFY stage=preflight … FAIL`, exit 1. The device is
  connected, Developer Mode is enabled and it is unlocked; **no Apple Development identity**
  yet (`security find-identity`: 0 valid).
- The scripts run under macOS `/bin/bash` 3.2. shellcheck is not installed; `bash -n` is clean.

### 2026-11-26 — Phase 1, stopped at the 30-turn limit

**Landed. Each fix is verified (dsp56kTestRunner exit 0 in both builds; JIT MD/MM renders identical to `doc/ios-baselines`, counters included) and committed:**
- `verify-interp.sh`:
  - Each render has a wall-clock limit (JIT 300 s, interpreter 900 s, Debug 1800/5400 s). Exceeding it reports FAIL with the last counter checkpoint.
  - Counters are flushed line by line.
  - Compare mode runs the whole ctest suite of both builds with `GEARMULATOR_REQUIRE_FIRMWARE_TESTS=1`.
- dsp56300 `ios-port` fixes:
  - asmjit `OSCacheControl.h` on every Apple target
  - `LOG_ERR_BASE` rate limit
  - CowMemory Mach VM on macOS only
  - trampoline `lea_`
  - Rn+displacement peripheral routing, with the new test `moveRnDisplacementPeripheral`
  - interpreter PC guard before the fetch
- `mdRamPackingTest` runs only its interpreter engine in an interpreter build. It segfaulted before; now exit 0 in both.
- **Interpreter DO loops step per instruction when `maxDoIterations` is set** (dsp56300 `ios-port`):
  - This removes the boot livelock.
  - `do_start()` opens a loop activation, and `execInterpreter()` applies the loop end after each instruction with the inline loop's `fetchedLA` rules.

**Interpreter renders after that fix** (Release, `trigPressFirmwareTest`, run in parallel):

| | interp hash | JIT baseline | boot | hold peak (interp / JIT) | rtf |
|---|---|---|---|---|---|
| MD | `bdb35c9d2cdc6bb9` | `bdb35c9d2cdc6bb9` | ready | 0.223144 / 0.223144 | 0.69x |
| MM | `503bd8af7d1bb400` | `6adf0ea75100b8c3` | ready | 0.109842 / 0.109842 | 0.54x |

**Closest state:**
- **MD.** The audio is bit-identical to the JIT over all 1,631,700 frames, and counters are identical through checkpoint 4 (1.0 s).
  - From checkpoint 5 the mixer differs by tens of instructions at the stop point (JIT PC 0x260, interpreter 0x26a). The JIT stops at block boundaries, the interpreter per instruction.
  - The gap then grows to −319,804 instructions by the end, with cycles within 1. The producer ends +384 instructions.
- **MM.** Counters are identical through checkpoint 2 (0.5 s).
  - **First divergence: checkpoint 3 (0.75 s).** The producer is at 8,264,307 instructions and 35,601,105 cycles, PC 0x196, against the JIT's 8,264,310 / 35,601,111, PC 0x195. The mixer is identical there.
  - From 2 s both DSPs drift steadily: the interpreter retires fewer instructions for the same cycles (−1.05M mixer and −0.51M producer instructions by 7.5 s).
  - Disassembly (`TRIGPRESS_DISASM=1`) places both in their main processing loops: the mixer in `do #$22,$7cf` (body P:0x7cd–0x7cf, `mpy`/`mpy`/`move`) and the producer in `do #$22,$7c3`. So **inside DO loop bodies the interpreter charges more cycles per iteration than the JIT.** MD's mixer shows the same pattern at 0x260/0x209.

**Next:**
1. Compare the interpreter's `getOpcodeCycles()` with the JIT's per-iteration charge (`JitBlock::getInfo` `m_opCycles` plus the in-block loop iteration) for P:0x7cb–0x7cf on MM's mixer. Fix the side that disagrees with the JIT, which stays the reference.
2. The block-shaped interpreter (PLAN.md 1b), for the stop-point and interrupt-granularity differences that remain once the cycle charge agrees.
3. A unit test for the iterative DO loops (slices, nested loops sharing LA, two-word end, RTI into LA+1). It is not written yet; the existing suites and firmware runs pass.
4. The full `scripts/verify-interp.sh` compare run started at 20:20 (`build/verify/verify-interp/20261126-202024`, including ctest for both engines). Its result was not in yet when this was written.

### 2026-11-26: MM cycle drift, MM interpreter ctests, ctest parsing (25-turn goal)

**Full compare run** (`build/verify/verify-interp/20261126-202024`, before this goal):
- `jit.unchanged` PASS for MD and MM.
- MD and MM renders: MISMATCH on counters. The MD hash matches.
- JIT ctest: 89/89 passed, but it was reported FAIL because of the parser bug fixed below.
- Interpreter ctest: 86/89. `mmSineFirmwareTest` and `mmSineMidiFirmwareTest` fail; `mmDigiproFirmwareTest` timed out at 600 s.

**Fixed and committed:**
- `verify-interp.sh` now parses ctest's all-passed summary (`100% tests passed out of 89`) as well as the failed form. Checked against both summary lines.
- `DSP::fastForward()` counts calls and skipped instructions; `trigPressFirmwareTest` prints them as `ff0=` / `ff1=`. Diagnostic only; JIT renders and counters are unchanged.

**MM DO-loop cycle drift: cause found, not fixed yet.**
- Per-instruction cycles are identical in both engines. Both call `calcCycles(inst, pc, op, bridged, 1)`, and the interpreter's per-PC cycle cache is invalidated on P writes. So `getOpcodeCycles` is not the cause.
- The cause is the JIT's serial-status spin skip. `jitops_jmp.cpp` `skipToFrameSync` fast-forwards a poll of an ESSI/ESAI status flag that branches to itself with `fastForward(n, n)`: n instructions and n cycles, which its own TODO notes. The interpreter runs the spin for real at its true cycle cost.
- In 3 s (`--boot-seconds 3`), the JIT fast-forwards 53,655 times (13.3M instructions) on the MD mixer, and 441,313 times (15.0M instructions) on the MM mixer and 505,788 times (22.4M instructions) on the MM producer. The interpreter fast-forwards 0 times.
- That matches the drift: equal cycles at every checkpoint, fewer instructions in the interpreter. At the end of the MM run the gap is −10.66M (mixer) and −10.81M (producer).
- A mirror of the skip is committed in dsp56300 (`serialStatusSpin` / `btstSerialStatusSpin`, hooked into `op_Jclr_qq`, `op_Jset_qq`, `op_Brclr_qq`, `op_Brset_qq`, `op_Btst_qq` and absolute `op_Btst_ea`; dispatch does reach those). **It never fires: 0 calls.** One of its match conditions fails on the real firmware loops.

**Interpreter ctests, not fixed:**
- `mmDigiproFirmwareTest` takes 149.8 s under the JIT, so at the interpreter's 0.3–0.5× it needs about 300–500 s. It timed out at 600 s while 4 tests ran in parallel. A slow run, not a hang.
- `mmSineFirmwareTest` / `mmSineMidiFirmwareTest` fail with `GND SIN waveform failed smoothness/pitch-scale check` (track RMS 0.118, roughness 0.00129). The interpreter run logs 54,972 `HOTX is full` overwrites against 0 for the JIT in this test. They probably share the MM divergence above.

**Closest state:** MD audio is bit-identical to the JIT. MM is not: `503bd8af7d1bb400` against `6adf0ea75100b8c3`, first divergence at checkpoint 3 (0.75 s, producer 3 instructions and 6 cycles behind). `jit.unchanged` PASS. Interpreter ctest 86/89.

**Next step:**
1. Log PC, opcode and n at each JIT `fastForward` call on MM, for example by building `trigPressFirmwareTest` against the JIT with a temporary print. Compare that against the conditions in `DSP::serialStatusSpin` / `btstSerialStatusSpin` to find which one fails. Candidates:
   - the extension-word read (`mem.get(P, pc+1)`) differs from the JIT's `m_opWordB`;
   - the spin form the firmware uses is one the mirror doesn't cover;
   - an MD block-link `spinSkip` or `nopLoopSkip` path, which also calls `fastForward`.
2. Once the mirror fires with the JIT's n, re-run `verify-interp.sh`. MM's counters and hash should then match, and probably the sine tests too.
3. Give `mmDigiproFirmwareTest` a timeout that fits interpreter builds, or run the interpreter ctest with less parallelism.

### 2026-11-26: the wait-loop skip in the interpreter (25-turn goal)

**What the logging showed** (`DSP_SPIN_LOG=1 trigPressFirmwareTest mm --boot-seconds 3`):
- The JIT's serial-status spin skip (`JitOps::serialStatusSpin`) **never fires on MM**: no `JITSPIN` lines at all. Last entry's diagnosis was wrong.
- Every JIT fast forward comes from **mdLib's `spinSkip`**, at four polling `movep` reads:

  | DSP | PC | instruction | JIT calls / skipped instructions in 3 s |
  |---|---|---|---|
  | mixer | 0x87 | `movep x:<<$ffffeb,a` | 151,350 / 2.63M |
  | mixer | 0x17f | `movep x:<<$ffffeb,a` | 289,963 / 12.35M |
  | producer | 0x18d | `movep x:<<$ffffee,a` | 264,052 / 12.23M |
  | producer | 0x195 | `movep x:<<$ffffbd,b` (Port C) | 241,736 / 10.19M |

**The two conditions the interpreter missed:**
1. `spinSkip` acts only when `DSP::getRunStopCycle() != 0`, and only the JIT's `execUntilCycles` set it.
2. It identifies the loop by `getPC()`. Inside a JIT block that is the block start, which here is the polling `movep`. The interpreter has already advanced the PC past it.

**Fixed and committed:**
- The interpreter's `execUntilCycles` sets and restores `m_runStopCycle` exactly as the JIT does (dsp56300).
- mdLib uses a new `md::dspPollPC()` at all four PC checks: `getPC()` in JIT builds, the current instruction's PC in interpreter builds. JIT output therefore cannot change.
- Verified: JIT MD/MM hashes and every counter checkpoint identical to `doc/ios-baselines`; `dsp56kTestRunner` exit 0 in both builds.
- The interpreter now fast-forwards at the same four loops, with call counts within 1–5% of the JIT's. Over the full MM run (mixer / producer):
  - JIT: 6.80M / 8.29M calls, 230M / 373M skipped instructions
  - interpreter: 6.94M / 8.47M calls, 205M / 359M skipped instructions

**Interpreter rtf, before → after** (Release, `trigPressFirmwareTest`, one run at a time):
- MD: 0.67x → 0.71x
- MM: 0.52x → 0.54x

The "before" runs shared the machine with two other jobs, so the gain is approximate.

**Closest state:**
- MD: the hash still equals the JIT baseline `bdb35c9d2cdc6bb9`. Counters first diverge at checkpoint 5.
- MM: the hash is now `0863f46b643b5555` against `6adf0ea75100b8c3`. First divergence at checkpoint 3: producer 8,258,955 instructions against the JIT's 8,264,310, cycles 6 apart.
- Interpreter ctests: 18/20 of the targeted set pass. `mmSineFirmwareTest` and `mmSineMidiFirmwareTest` still fail their GND SIN smoothness/pitch check. `mmDigiproFirmwareTest` was still running at the time of writing (log `build/check-poll/ctest-digipro.log`).

**What this rules out:** `spinSkip` is state-exact by design: skipping k iterations equals running them. So skipping differently cannot cause the instructions-per-cycle drift. The drift comes from when the polled value changes, which is the cross-DSP and ColdFire timing. That timing differs because the JIT stops `execUntilCycles` (and the run-until-condition `exec()` loops in `mddsp.cpp`) at block boundaries, while the interpreter stops at instructions. The skip sizes k differ for the same reason: their limit is the run-stop cycle.

**Next step:** PLAN.md 1b, the block-shaped interpreter. Run each JIT block's extent, from `JitBlock::getInfo` with the same `JitConfig`, and stop and service peripherals and interrupts only at those boundaries. Then the scheduler sees the same stop points as with the JIT. Start with the MM producer at P:0x195 around checkpoint 3: compare the `execUntilCycles` targets and stop cycles of both engines there.

**MM sine tests: cause narrowed** (`MM_AUDIO_DUMP=<file> mmAudioFirmwareTest --sine` dumps the measured blocks):
- **Correction to earlier entries.** The "54,972 HOTX-full lines under the interpreter, 0 under the JIT" comparison was an artifact: ctest prints output only for failing tests. Run directly, the JIT logs 21,939 and the interpreter 18,318. HOTX overwrites happen in both engines and are not the cause.
- Same RMS in both engines (0.1180 JIT, 0.1177 interpreter), but roughness 1.9e-6 (JIT) against 2.5e-3 (interpreter).
- Over the first 16,384 frames:
  - JIT: a clean 261.6 Hz sine (half periods 84/85 frames) with no discontinuities.
  - Interpreter: the same pitch, but **dropouts**, 19 jumps in clusters about 380–1700 frames apart. At frame ~1975 the level halves (0.202 → 0.103), stays at 0.0 for 13 frames, then resumes at a shifted phase.
- So the mixer intermittently receives no producer frames: its ESSI link input underruns, and `blockingPop` returns silence when its ring is empty. This is the same cross-DSP timing difference as the counter drift, here audible.
- `mmDigiproFirmwareTest` (interpreter, 1200 s limit, run alone): 1/1 Test #87: mmDigiproFirmwareTest ............***Timeout 600.04 sec 0% tests passed, 1 tests failed out of 1 (run alone; the test's own TIMEOUT 600 property overrides `ctest --timeout`, so it never got more than 600 s. Whether it passes given ~2x the JIT's 150 s is still unmeasured: run the binary directly, or raise its TIMEOUT for interpreter builds)

**Next step, refined:** before the full block-shaped interpreter, instrument the MM link in an interpreter run. Count `blockingPop` empty reads and purges and `pushToInput` drops per reason (receiver disabled, window opened during catch-up, strobe epoch, overrun) around frame 1975 of the sine test, and compare with the JIT, which has none. The reason that fires names which scheduler or catch-up path the interpreter's instruction-granular stops break. Then make that path stop where the JIT would. PLAN.md 1b is the general form of that fix.

### 2026-11-26: Phase 2, interpreter speed on this Mac

`scripts/verify-interp.sh --perf` → **`VERIFY RESULT PASS`** (exit 0, logs `build/verify/verify-interp/20261126-212208`).
- **Machine:** Apple M4 Max.
- **Scenario:** `trigPressFirmwareTest` (25 s boot, Trig 1 press), then 3 windows of 20 s emulated time each. The best window counts; within each run the windows agree within ~2%.
- **One run at a time:** an idle check (1-minute load ≤ 3) passed before every run, at load 1.7–2.4.
- **Configurations:** all Release, arm64:
  - plain: `-Ofast`
  - opt: `GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP` + `GEARMULATOR_MDMM_APPLE_THINLTO`
  - pgo: opt plus a profile trained on the MD and MM `trigPressFirmwareTest` scenario (`build/perf-pgo/mdmm-interp.profdata`)

| | JIT | interp plain | interp opt | interp PGO | PGO / plain |
|---|---|---|---|---|---|
| **MD** rtf | 6.34x | 0.74x | 0.86x | **0.99x** | +34% |
| MD mixer / producer Minstr/s | 503 / 524 | 58.6 / 61.1 | 68.2 / 71.1 | 78.7 / 82.1 | |
| **MM** rtf | 2.77x | 0.57x | 0.65x | **0.72x** | +27% |
| MM mixer / producer Minstr/s | 228 / 223 | 46.7 / 45.7 | 53.4 / 52.3 | 59.3 / 58.0 | |

*Caveat:* the profile was trained on the same scenario that is measured, so the PGO column is an upper estimate for this workload.

**Host time per component and the thread bound** (`MD_TRANSPORT_DIAGNOSTICS` interpreter build, full scenario). The timer switches owner around every DSP run, so DSP runs nested inside ColdFire instructions or inside the other DSP are attributed to the DSP that ran.

| | ColdFire + scheduler | mixer | producer | perf.bound (total / largest) |
|---|---|---|---|---|
| MD | 3.8% | 63.6% | 32.6% | **≤ 1.57x** |
| MM | 6.0% | 47.9% | 46.1% | **≤ 2.09x** |

**Synchronisation events, interpreter.** Each is a point where one component must see another's state at a given time. The mean gap is in DSP cycles (101.6 MHz) of emulated time.

| path | MD /s | MD mean gap | MM /s | MM mean gap |
|---|---|---|---|---|
| ColdFire → mixer catch-up | 206k | 493 | 329k | 309 |
| ColdFire → producer catch-up | 44k | 2,299 | 393k | 258 |
| DSP → DSP catch-up, mixer side | 222k | 457 | 983k | **103** |
| DSP → DSP catch-up, producer side | 336 | 302,400 | 0 | – |
| HI08-bridge inline runs, mixer | 117k | 867 | 217k | 469 |
| HI08-bridge inline runs, producer | 21k | 4,780 | 225k | 452 |
| scheduler slices (ColdFire / mixer / producer) | 33k / 31k / 35k | ~3,000 | 33k / 29k / 32k | ~3,000 |
| ESSI link frames, producer → mixer | 1.5k | – | 1.04M (36.4M accepted, 1.9M purged) | – |

MD uses the block link, so it sends hardly any serial link frames. MM runs the serial link at the frame rate.

**What this means for Phase 3:**
- **MD.** The mixer dominates, so three ideal threads give at most 1.57x.
- **MM.** The split is even (bound 2.09x), but the mixer meets the producer every ~103 DSP cycles (about 1 µs of emulated time), and the ColdFire touches each DSP every ~260–310 cycles. A timed mode's lookahead L has to sit at or below that, or MM's handshakes change. That is far below the one-ESSI-frame (2,304-cycle) lookahead the plan assumed.
- So the timed mode's speedup on MM will be limited by sync cost, not by the 2.09x split. Phase 3a should measure what L the firmware actually needs before committing to threads.

**iPad Pro M4 projection** (to be replaced by the Phase 1.5 measurement):
- **Peak.** The M4 Max and the iPad's M4 share the same P-core generation, at about 4.5 vs 4.4 GHz. So expect roughly the Mac numbers, ×0.95–1.0.
- **Sustained.** The earlier work measured an iPad M5 falling from 1.59x cool to 0.999x after 40 minutes, about ×0.63. Assume ×0.65–0.85.
- Serial interpreter, PGO:
  - **MD** peak ≈ 0.94–0.99x, sustained ≈ 0.65–0.85x
  - **MM** peak ≈ 0.69–0.72x, sustained ≈ 0.47–0.61x
- **Neither instrument reaches real time on the iPad without threads.** With the ideal thread bound:
  - MD ≤ ~1.5x peak / ~1.0–1.3x sustained
  - MM ≤ ~1.5x peak / ~1.0–1.3x sustained, before sync costs

**What is not measured yet:**
- the minimum gap between sync events (only the mean is computed)
- the PGO gain on a scenario other than the training one
- any number on the iPad itself (Phase 1.5)

## New target (2026-11-26): standalone Monomachine on iPad Pro M4 and iPhone 17 Pro Max

The app has to launch and run with no Mac and no cable; a one-time setup step that needs the Mac is acceptable.

- **Devices:** iPad Pro 13" M4 (iPad16,6, iPadOS 27.0, Developer Mode on) and iPhone 17 Pro Max (iPhone18,2, iOS 27.2.1). Both are A15+/M2+ class, so both have TXM.
- The iPhone is paired but was not connected, so its Developer Mode state is unread. Its IDs are in `CLAUDE.local.md`.

### Step 1: what JIT through StikDebug needs (research, 2026-11-26)

Sources:
- [S1] StikDebug's integration guide, https://github.com/StikDebug/StikJIT/blob/main/INTEGRATION.md
- [S2] StikDebug README, https://github.com/StikDebug/StikDebug
- [S3] Kernarium issue #71, https://github.com/imxieyi/Kernarium/issues/71 (third party, opened 2026-11-26, closed "not planned")
- [S4] Play--CodeGen PR #34, https://github.com/jpd002/Play--CodeGen/pull/34 (third party, unmerged)
- [S5] SideStore JIT docs, https://docs.sidestore.io/docs/advanced/jit ("as of June 17 2026")

None of these is Apple documentation.

**What the app needs**
- **Entitlement:** only `get-task-allow`. Per [S1], "The process receiving JIT must have `get-task-allow`." Our Xcode development-signed builds already carry it. `MAP_JIT` needs `dynamic-codesigning` [S4], which we cannot get; the TXM route does not use it.
- **Developer Mode** must be on [S1].
- **Optional:** `LSApplicationQueriesSchemes` containing `stikdebug`, so the app can offer to open `stikdebug://enable-jit?bundle-id=…&pid=…` [S1].

**Memory pattern on TXM devices (iOS 26+, A15+/M2+)**
- A debugged app still cannot execute memory it maps itself [S4]. Instead, after `CS_DEBUGGED` appears, it asks the debugger for each executable region:
  - `brk #0xf00d` with `x16 = 1` (prepare), `x0` = address or NULL ("asks the debug server to allocate the RX mapping"), `x1` = size; the RX address comes back in `x0` [S1].
  - It then creates its own writable alias of that region (`vm_remap` + `vm_protect` [S4]), writes code only through the alias, and executes at the RX address, which is execute-only.
- All regions must be prepared before detaching (`x16 = 0`). "A region introduced later cannot be prepared by a script that has already detached" [S1].
- The pages are 16 KiB. [S4] requests one 64 MB region.
- [S4] warns that passing a self-mapped address appears to succeed but never gets serviced, and that an unserviced `brk` can leave the instruction's encoding in `x0`. So the result must be validated: page-aligned, and inside an RX mapping of this process.
- StikDebug runs a per-app script, `universal.js` (recommended) or `legacy.js`. On non-TXM devices "debugger attachment alone enables JIT" and no script is used [S1].

**Detection**
- Wait for `CS_DEBUGGED` in the code-signing status before any `brk` [S1]. A `brk` with no script attached "will crash the process" [S1].
- StikJIT exposes a TXM check (Present / Not Present / Unknown) [S1].

**What needs a computer**
- **Once:** the pairing file, and installing StikDebug plus our app. With a paid team, our app is installed and signed for a year from Xcode/devicectl.
- **On-device afterwards** [S2]: StikDebug, LocalDevVPN, Wi-Fi (or cellular, then Airplane Mode [S1]).
- **The developer disk image** has to be mounted again after every reboot [S5]; StikDebug does that on the device.
- **Per process launch:** StikDebug attaches with the app's PID, so JIT is enabled again on every launch [S1][S5].

**Reliability on 26.x / 27.x**
- [S2]: "17.4 – 18.x … Stable"; "26.0+ … Limited App Availability". Apps have to implement the protocol.
- [S5]: "26.6 and 27 only work with a few apps."
- [S4]: verified on 26.4.2 and 26.6.2. A companion PR claims 26.7 and 27; only its title was seen.
- [S3]: on iPhone15,3 with iOS 27.2.1, StikDebug 3.2.2 "cannot attach on 27.2.1". That is the iPhone 17 Pro Max's exact version. The trapping in that report was the reporter's driver bug; the attach failure is unresolved.
- **Conclusion:** JIT on 27.x is possible but fragile, and can break with any iOS update. It cannot be the only engine.

**Can asmjit/dsp56300 adopt it?** Yes, with a custom code allocator; no source covers asmjit specifically.
- dsp56300 today creates `asmjit::JitRuntime` objects (`jit.cpp:123`, `jittrampoline.h:74`), which map their own memory. That memory will not execute on TXM.
- **Needed:** an `asmjit::Target` subclass whose `_add()`:
  1. sub-allocates from the one pre-prepared RX region (a free list with 16-byte granularity; blocks are freed when P memory changes);
  2. calls `CodeHolder::relocateToBase(rx)`, then `copyFlattenedData()` into the RW alias;
  3. calls `sys_icache_invalidate(rx, size)`. Our asmjit fork already includes `OSCacheControl.h` on iOS.
- The JIT and the trampolines then use that target instead of `JitRuntime`.
- The region size must be fixed before detaching. Measure MM's peak JIT code size on macOS first, then reserve about 2× that.

### Step 2: dual-engine design (one app, JIT when available, interpreter otherwise)

1. **Both engines in one binary.** Today the engine is fixed at compile time (`DSP56K_FORCE_INTERPRETER` → `g_jitSupported` → `constexpr g_useJIT`, `dsp.h:42`). Make it a per-DSP runtime choice, set at construction: `exec()`, `execUntilCycles()`, `execInlinePeripheralCheck()` and the interpreter-only cycle accounting branch on a member, not a constant. Those branches are once per call and predictable. The desktop JIT build keeps its constant path, so its results and speed cannot change.
2. **Decide once, at launch, before the machine is created.**
   - (a) Read `csops(getpid(), CS_OPS_STATUS)`. If `CS_DEBUGGED` is clear, use the interpreter.
   - (b) If it is set on a TXM device, do the prepare handshake for one fixed-size region, validate the returned address, create the RW alias, then detach. A SIGTRAP handler steps over an unanswered `brk`, as in [S4]. Any failure means the interpreter.
   - (c) On a non-TXM device, `CS_DEBUGGED` alone is enough, so plain asmjit `JitRuntime` works.
   - The decision lasts for the process. Switching engines needs a relaunch; the machine's state survives that through the existing state save/restore (sysex).
3. **Never unusable.** The interpreter is always compiled in and is the default. JIT is only tried when the OS says the process is debugged, and no executable page is touched unless the handshake has proved it is serviced. An iOS update that breaks StikDebug only puts the app back on the interpreter.
4. **Shown to the user.**
   - A badge in the editor's header: "Engine: JIT (StikDebug)" or "Engine: Interpreter".
   - A settings page with the reason: not debugged / TXM handshake failed / region too small / JIT disabled by the user. It also shows the live real-time load from the audio callback.
   - A "Relaunch with JIT" button that opens StikDebug's URL when `stikdebug` can be opened.
   - The same facts go into the self-test JSON, so a device run records which engine it measured.
5. **The same results either way.** The iOS JIT generates the same arm64 code as the desktop JIT, so it reproduces `doc/ios-baselines` exactly. A self-test hash on the device checks that. The interpreter matches the JIT for MD audio but not yet for MM (see the Phase 1 entries), which is another reason to prefer the JIT whenever it works.

### Step 0: devices

- **iPhone 17 Pro Max:** connected by cable, tunnel up, **Developer Mode enabled**, iOS 27.2.1. Its CoreDevice UUID and UDID are in `CLAUDE.local.md`.
- **iPad:** the CoreDevice UUID in `CLAUDE.local.md` had been the UDID; corrected.

### Step 3: MM interpreter profile and two speedups

**Profile.**
- **Setup:** `sample`, 15 s of steady-state MM after the 25 s boot. Build: `build/perf-interp-opt` (OPTIMIZE_DSP+ThinLTO). About 15,000 samples on the one emulation thread.
- **Tooling note:** `sample` has to run outside the command sandbox, which hides `task_for_pid`.

Self time, top of stack:

| rank | function | samples | share |
|---|---|---|---|
| 1 | `md::Hardware::advance` (scheduler, with `execInterpreter`/`execUntilCycles` inlined) | 2635 | ~18% |
| 2 | `DSP::processLoopActivations` (the per-instruction DO-loop step) | 1247 | ~8% |
| 3 | `DSP::alu_multiply` | 917 | ~6% |
| 4 | `DSP::op_Parallel` | 900 | ~6% |
| 5 | `dspExecPeripherals<Peripherals56303, …>` (indirect call per instruction) | 802 | ~5% |
| 6 | `Memory::get` | 459 | ~3% |
| 7–10 | `decode_ddddd_write`, `decode_XMove_MMRRR`, `op_Mover`, `m68k_execute_run` (ColdFire) | 190–300 each | 1–2% each |

The rest is spread over opcode handlers, `EsxiClock::exec`, DMA and HDI08. There is no single dominant cost after dispatch.

**Prototyped and committed** (dsp56300):
1. Test `isDue()` inline before the peripheral callback, as `execJitImpl<true>` does.
2. Call `processLoopActivations()` only when it can act: the instruction ends at LA, a loop end is pending, or the frame was popped.

Both are the same work in the same order.

**Verified:**
- MD and MM interpreter renders and **every counter checkpoint identical** to the interpreter before the change; MD hash `bdb35c9d2cdc6bb9` and MM hash `0863f46b643b5555` both unchanged.
- JIT MD/MM identical to `doc/ios-baselines`.
- `dsp56kTestRunner` exit 0 in both builds.

**Interpreter rtf**, opt build, M4 Max, idle check passed (load 2.5–2.9), best of 3 × 20 s:

| | before (Phase 2) | after | |
|---|---|---|---|
| MM | 0.651x | **0.661x** | +1.5% |
| MD | 0.860x | **0.945x** | +10% |

MM gains less than its 8% profile share suggests: its hot loops have three-instruction bodies (`do #$22,$7cf` on P:0x7cd–0x7cf), so every third instruction really is a loop end. Adding PGO's measured gain (+11% for MM in Phase 2) puts MM at about 0.73x on this Mac.

**Projection, MM, sustained** (an estimate to be replaced by device measurements; assumptions listed):

| device | interpreter (opt + PGO) | JIT (Mac JIT 2.77x) |
|---|---|---|
| iPad Pro M4 | peak ≈ 0.69–0.73x, sustained ≈ 0.47–0.62x | peak ≈ 2.6–2.8x, sustained ≈ 1.8–2.4x |
| iPhone 17 Pro Max | peak ≈ 0.69–0.77x, sustained ≈ 0.40–0.55x | peak ≈ 2.6–2.9x, sustained ≈ 1.5–2.1x |

**Assumptions behind the projection:**
- **iPad:** the iPad's M4 P-core matches this M4 Max's P-core within about 5% (same generation, about 4.4 vs 4.5 GHz).
- **iPhone:** the A19 Pro's single-core speed is assumed comparable to M4. Not verified here.
- **Sustained derating:** ×0.65–0.85 on the iPad, from the earlier work's M5 measurement (1.59x cool → 0.999x after 40 min), and ×0.55–0.75 on the phone, which has less thermal headroom.
- **JIT:** assumes the device JIT reaches the desktop JIT's speed, since it is the same arm64 code.

**Conclusion:** the MM interpreter stays below real time on both devices even at its best. Its remaining cost is spread thin, so another quick win of more than ~10% is unlikely. The MM thread bound (2.09x) would be needed to reach about 1x, and MM's sync gap of about 103 DSP cycles makes that bound hard to approach.

### Recommendation (2026-11-26)

**Build the standalone Monomachine app with both engines, and make the JIT the engine it aims for.** PLAN.md has been rewritten for this as Phases A–D.

1. **The JIT is the only measured route to real time for MM on both devices.**
   - The MM interpreter is 0.66x on the M4 Max (opt build), about 0.73x with PGO. Its projected sustained rate is 0.47–0.62x on the iPad and 0.40–0.55x on the iPhone.
   - The profile shows no remaining hot spot big enough to close that gap: the two fast paths gave +1.5%, and the rest is spread thinly.
   - The MM JIT is 2.77x on the Mac. Projected sustained, about 1.8–2.4x on the iPad and 1.5–2.1x on the iPhone.
   - The JIT also reproduces `doc/ios-baselines` exactly. The MM interpreter does not yet: it has link dropouts in the sine tests.
2. **The interpreter stays in as the guaranteed engine.**
   - StikDebug's TXM route is officially "limited app availability" on 26.x [S2], and "26.6 and 27 only work with a few apps" [S5].
   - There is a third-party report that StikDebug 3.2.2 cannot attach on 27.2.1 [S3], the iPhone's exact version.
   - The app must therefore start, play and be editable on the interpreter, clearly marked as such. Slower, and below real time for MM, but never broken.
3. **The JIT needs a new code allocator, not a new JIT.**
   - **The allocator:** dsp56300's asmjit code goes into one region that StikDebug prepares (`brk #0xf00d`, x16 = 1), written through a `vm_remap` RW alias, with the icache flushed, before detaching [S1][S4].
   - **Proving it:** the allocator can be exercised on macOS against the baseline before any device run.
   - **The only entitlement** is `get-task-allow`, which our development-signed builds carry [S1].
4. **Defer threads.**
   - MM's ideal bound is 2.09x, but its mixer meets the producer about every 103 DSP cycles, so a timed mode would be mostly synchronisation.
   - Threads are worth revisiting only if the JIT route fails on both devices.
5. **First thing to test on hardware**, as soon as a signing identity exists: whether StikDebug attaches to a development-signed test app on each device. That is iPadOS 27.0 on the iPad and iOS 27.2.1 on the iPhone, where [S3] reports trouble. It decides how much the JIT matters per device before Phase B is finished.

**Status of the inputs:**
- Signing identity: present.
- iPhone 17 Pro Max: connected, Developer Mode enabled.
- iPad Pro M4: Developer Mode enabled.

**What is measured and what is projected.**
- **Measured:** every Mac number above (rtf, profile, thread bound, sync gaps).
- **Projected:**
  - all device numbers (assumptions in the Step 3 projection table);
  - the A19 Pro's single-core speed relative to M4 (not verified);
  - StikDebug's behaviour on these two OS versions, known only from the cited reports, not tried.

### 2026-11-26: JIT route de-risking (before Phase B)

**Done from the Mac:**
- **`tools/jitprobe`** (`com.aki.gearmulator.jitprobe`): built, development-signed (`get-task-allow` = true), **installed and running on both devices**. Each logged "waiting for a debugger".
  - It runs the TXM handshake, checks the region, maps a writable alias, detaches, then runs a generated function and patches it.
  - The first build crashed at launch on iOS 27. UIKit traps apps that have not adopted the scene lifecycle (`_UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption`). The probe now has a scene manifest; **the Monomachine app will need one too.**
- **StikDebug, built from source** (commit `63713ea`), signed as `com.aki.stikdebug`, **installed on both devices**. The only change: an unused App Group entitlement belonging to another team was removed.
- **`doc/JIT_SETUP.md`** lists the steps that need you.
- `iloader.app` is already on this Mac.

**Blocked on you** (all on the device, or one click in iloader):
1. Install LocalDevVPN from the App Store.
2. Place a pairing file: in StikDebug (Settings → Pair a Device), or with iloader → Manage Pairing File → Place.
3. Mount the disk image: open StikDebug, force-quit, reopen.
4. Enable JIT for JIT Probe and report PASS/FAIL.

The first real answer on whether the JIT route works on 27.0 (iPad) and 27.2.1 (iPhone) comes from step 4.

**Pairing files from this Mac: blocked (2026-11-26).**
- There is no command-line pairing tool here (`idevicepair` and `pymobiledevice3` are not installed). StikDebug's documented Mac route is the iloader GUI, which is installed.
- Driving iloader through computer use failed: macOS has not given the Claude desktop app Screen Recording permission.
- **To unblock, either:**
  - grant it (System Settings → Privacy & Security → Screen Recording → Claude) and ask again, and I place both files with iloader; or
  - use StikDebug's on-device pairing (Settings → Pair a Device), which works on iOS/iPadOS 27 (`doc/JIT_SETUP.md`, step 2).

**JIT de-risking goal: stopped after the 25-turn limit (about 30 turns used), 2026-11-26.**
- **Done:**
  - `tools/jitprobe` built, development-signed, installed and running on the iPad and the iPhone, both waiting for a debugger.
  - StikDebug built from source (`63713ea`), signed as `com.aki.stikdebug`, installed on both.
  - `doc/JIT_SETUP.md` written.
  - LocalDevVPN identified: App Store, Coxson Engineering.
  - Pairing routes identified: on-device in StikDebug for iOS 27, or iloader on this Mac.
- **Blocked on the user:**
  1. Pairing files: grant the Claude app Screen Recording permission so iloader can be driven, or pair on each device in StikDebug.
  2. Install LocalDevVPN on both devices.
  3. Mount the disk image and enable JIT for JIT Probe. Only then can the probe run the handshake and the generated-code check and report PASS/FAIL.
- **Next once unblocked:** read `Documents/jitprobe.log` from both devices; then Phase B if at least one device passes.

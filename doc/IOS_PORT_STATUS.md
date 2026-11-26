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
| 2 Interpreter RTF on this Mac | not started |
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
- `mmDigiproFirmwareTest` (interpreter, 1200 s limit, run alone): 1/1 Test #87: mmDigiproFirmwareTest ............***Timeout 600.04 sec 0% tests passed, 1 tests failed out of 1 (run alone, 1200 s limit)

**Next step, refined:** before the full block-shaped interpreter, instrument the MM link in an interpreter run. Count `blockingPop` empty reads and purges and `pushToInput` drops per reason (receiver disabled, window opened during catch-up, strobe epoch, overrun) around frame 1975 of the sine test, and compare with the JIT, which has none. The reason that fires names which scheduler or catch-up path the interpreter's instruction-granular stops break. Then make that path stop where the JIT would. PLAN.md 1b is the general form of that fix.

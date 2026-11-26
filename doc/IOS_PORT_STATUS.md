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

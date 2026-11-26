# iOS port status

Living record for the iOS/iPadOS port of MD/MM. The plan, phases and done criteria
are in [`PLAN.md`](../PLAN.md). Every number here comes with the command that
produced it.

## Current phase

**Phase 0 done** (2026-11-26). Next: Phase 1 (port fixes; interpreter == JIT on macOS).

| Phase | State |
|---|---|
| 0 Commit, forks, harness, verify scripts | **done** |
| 1 Port fixes; interpreter == JIT on macOS | not started |
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

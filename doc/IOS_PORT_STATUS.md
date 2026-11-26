# iOS port status

Living record for the iOS/iPadOS port of MD/MM. The plan, phases and done criteria
are in [`PLAN.md`](../PLAN.md). Every number here comes with the command that
produced it.

## Current phase

**Phase 0**: harness, verify scripts and the JIT baseline.

| Phase | State |
|---|---|
| 0 Commit, forks, harness, verify scripts | in progress |
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

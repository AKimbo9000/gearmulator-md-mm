# CLAUDE.md — iOS/iPadOS port of Gearmulator MD/MM

This fork (branch `ios-port`) ports the Elektron Machinedrum (MD) and Monomachine (MM)
emulators to iOS/iPadOS as a standalone app + AUv3. Upstream is
`joelanders/gearmulator-md-mm`, itself a fork of TUS's Gearmulator. The original TUS
CLAUDE.md is in git history; TUS-internal process (their remotes, YouTrack, Jenkins,
release workflow) does not apply here.

## Read first: prior iOS work exists

`../gearmulator-ios` (charlesvestal, Sept 2026) already investigated MD/MM on iOS, on an
older alpha.12 tree with the pre-reorg flat `source/` layout:

- `docs/MD_MM_IOS.md` — status, measurements, and why MD's single-threaded scheduler breaks
  under the interpreter (the proposed fix is giving each MD/MM DSP its own thread)
- `docs/MD_INTERPRETER_START_HERE.md` — newest session first (DO-loop end-word fix); read the
  top boxes before anything older
- `docs/MD_DO_LOOP_INVESTIGATION.md`, `DSP56300_INTERPRETER_FINDINGS.md`, `IOS_AUV3.md`,
  `IOS_BENCH.md`, and `scripts/build_ios_dsp56k.sh` for the iOS toolchain and signing
- `patches/*.patch` — fixes to carry over. Paths predate the reorg (the DSP core is now
  `source/cpu/dsp56300/`). Check whether this tree already contains a fix before applying it.
- `docs/MD_INTERPRETER_HANDOFF.md` is ~2,100 lines: grep it or use a subagent, never read it whole.

Do not repeat experiments those docs list as disproved. Also see this repo's
`doc/mdmm-apple-optimization.md` and `doc/md_mm_performance_diagnostics.md`.

## Hard constraints

- **Dual engine on iOS.** Device builds use `DSP56K_RUNTIME_ENGINE=ON`: the JIT when StikDebug has
  prepared an executable region (`dsp56k::JitCodeRegion`, see `doc/JIT_SETUP.md`), the interpreter
  otherwise. The app must work on the interpreter alone. The desktop JIT build must keep compiling
  and produce the same results as before (`doc/ios-baselines`).
- **Accuracy over shortcuts.** The interpreter must reach the same emulated state as the JIT
  (instruction and cycle counts, audio). Never paper over timing with clamps, slack values or
  heuristics the JIT does not need.
- **Firmware.** Never commit, print, upload or bundle ROMs. Local copies live in `roms/`
  (gitignored). The iOS app imports ROMs at runtime through the Files app, so the IPA contains
  none. `md::RomLoader` accepts exactly MD OS 1.63 and MM OS 1.32b 8 MiB images.

## Workflow

- Work on `ios-port`. Make small, logical commits on this branch without asking. Push only to
  `origin` (AKimbo9000/gearmulator-md-mm) and, for submodule work, to the `ios-port` branches of
  the forks AKimbo9000/dsp56300-md-mm (`source/cpu/dsp56300`) and AKimbo9000/asmjit (its
  `source/asmjit`). In each of those, `origin` is the fork and `upstream` the original. Never push
  to any `upstream`.
- Keep `doc/IOS_PORT_STATUS.md` current: what was tried, measured numbers, what's next. Update
  it before ending a turn.
- Assume the session can stop at any moment (usage limits). Work in small steps: after each
  verified step, commit it and update the status file, so an interruption loses at most the
  step in progress. After a restart or resume, read the status file and `git log` first.
- Back every claim with command output (exit codes, real-time factor, peak levels, render diffs).
- Run the macOS interpreter check before every device run; it is faster to debug there.

## Verification (write these first if missing)

- `scripts/verify-interp.sh` — macOS arm64 build with the interpreter forced; MD and MM
  real-time factor, plus a render compared against the JIT build of the same pattern
- `scripts/verify-sim.sh` — iOS Simulator build plus headless self-test
- `scripts/verify-device.sh` — device build, `xcrun devicectl device install app`, launch with
  `--selftest`, pull results with `xcrun devicectl device copy from`

`source/elektron/md/mdLibTest` already has firmware tests (e.g. `encoderPressFirmwareTest`, and
`trigPressFirmwareTest`, which this port added for the render-hash/counter comparison):
build on them rather than writing new harnesses.

Team ID, device UDID and ROM file names: see `CLAUDE.local.md`.

# Codebase reference (trimmed from upstream)

## Build (macOS)

```bash
cmake -G Xcode -S . -B temp/cmake        # desktop build
```

MD and MM build with `-Dgearmulator_SYNTH_ELEKTRON=ON` (on by default). Other synths:
`-Dgearmulator_SYNTH_OSIRUS=ON`, `_OSTIRUS`, `_VAVRA`, `_XENIA`, `_NODALRED2X`, `_JE8086`;
turn them off for faster builds. Plugin formats: `gearmulator_BUILD_JUCEPLUGIN`, `_VST3`,
`_AU`, `_Standalone`, and so on. Tests: `ctest -C Release`. Iterate on device code with
`mdLibTest` before building plugins.

### Platform-specific build settings

Set in `source/cmake/base.cmake`:
- **Windows**: MSVC Release uses `/O2 /GS- /fp:fast /Oy /GT /GL /Zi /Oi /Ot`
- **Linux**: `-Ofast -fno-stack-protector`, LTO enabled (except GCC, due to compiler bugs)
- **macOS**: universal binaries (x86_64 + arm64), minimum 10.12/10.13, Release `-Ofast -fno-lto`
  (ThinLTO is opt-in through `GEARMULATOR_MDMM_APPLE_THINLTO`; `GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP`
  covers dsp56kEmu, see `doc/mdmm-apple-optimization.md`)
- **ARM**: special handling for ARMv8.1a+ atomics on known boards (rk3588, rock-5b, rpi-2712)
- C++17 required — the DSP emulator uses extensive compile-time metaprogramming


## Source Tree Layout

`source/` is grouped by ownership, then by manufacturer. Manufacturer folders use
codenames to stay clear of trademarks (`ronaldo` = Roland, and so on).

```
source/
├── cmake/        build glue: juce.cmake, skins.cmake, base.cmake, xcodeversion.cmake, …
├── 3rdparty/     everything not written by us — see source/3rdparty/README.md
├── cpu/          processor cores: dsp56300, mc68k, h8300h (+ common/ bus, scheduler)
├── framework/    synth-agnostic code
│   ├── baseLib/ synthLib/ hardwareLib/ networkLib/
│   ├── juce/     jucePluginLib, jucePluginEditorLib, juceRmlUi, juceRmlPlugin,
│   │             juceUiLib, jucePluginData, mcpServerLib
│   └── tools/    bridge, pluginTester, midiLearnTest, changelogGenerator
├── axel/         Access
├── waldi/        Waldorf — common/, microq/, xt/
├── claudia/      Clavia
└── ronaldo/      Roland — common/, custom_chips/, je8086/, 88emu/
```

The rule for placing something new: `<maker>/<family>/<target-dir>`, plus
`<maker>/common/` for code shared across that maker's families and `<maker>/<chip>/`
for that maker's own custom silicon. A processor core used by more than one
manufacturer goes in `cpu/`. A maker with only one family skips the family level.

**Include paths do not encode this layout.** Every library exports its own parent as
its include root (`target_include_directories(<lib> PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..`),
so you still write `#include "synthLib/device.h"` regardless of how deeply nested
either side is. Keep that line when adding a library, and never write depth-relative
includes like `../../foo/bar.h` — they break the next time anything moves.

MD/MM live in `source/elektron/md/` (`mdLib`, `mdJucePlugin`, `mdLibTest`).

## Architecture

**Emulation stack:** DSP56300 emulator (`source/cpu/dsp56300/`, JIT via asmjit; peripherals ESAI and HDI08) + MC68K emulator (`source/cpu/mc68k/`, Musashi) + H8/300H core (`source/cpu/h8300h/`, on the `source/cpu/common/` bus and scheduler). Each synth has a device library that loads firmware ROMs, initializes processor memory, and handles MIDI/audio via HDI08.

**Per-synth pattern:**
| Emulator | Hardware | Device Lib | Plugin Dir |
|---|---|---|---|
| Osirus | Virus A/B/C | `axel/virusLib/` | `axel/osirusJucePlugin/` |
| OsTIrus | Virus TI/TI2/Snow | `axel/virusLib/` | `axel/osTIrusJucePlugin/` |
| Vavra | Waldorf microQ | `waldi/common/mqLib/` | `waldi/microq/mqJucePlugin/` |
| Xenia | Waldorf MW II/XT | `waldi/xt/xtLib/` | `waldi/xt/xtJucePlugin/` |
| Nodal Red 2x | Nord Lead/Rack 2x | `claudia/n2x/n2xLib/` | `claudia/n2x/n2xJucePlugin/` |
| JE-8086 | Roland JP-8000 / JP-8080 | `ronaldo/je8086/jeLib/` | `ronaldo/je8086/jeJucePlugin/` |
| DSPBridge | Network bridge | `framework/tools/bridge/` | — |

Shared per-manufacturer code: `waldi/common/wLib/` (microQ + XT), `ronaldo/common/` and `ronaldo/custom_chips/` (Roland).

**Shared libraries** (all under `source/framework/`):
- `synthLib/` — Device base class, DAC, resampling, MIDI routing
- `juce/jucePluginLib/` — Parameter system, MIDI Learn, Patch Manager, program change routing
- `juce/jucePluginEditorLib/` — Plugin editor UI, parameter overlays, settings pages
- `juce/juceRmlUi/` — RmlUi integration (HTML/CSS-like UI framework)
- `juce/juceUiLib/` — Common UI components
- `juce/jucePluginData/` — Shared plugin assets and RML/RCSS templates
- `baseLib/` — Filesystem, logging, events, binary streams
- `hardwareLib/` — LCD, buttons, encoders abstractions
- `networkLib/` + `tools/bridge/` — optional network bridge for remote hardware

**Plugin build flow:** `createJucePluginWithFX()` macro in `source/cmake/juce.cmake` → links device lib → Processor inherits `synthLib::Plugin` wrapping `synthLib::Device` → skins via RML/RCSS compiled into binary data, multiple skins per synth via `addSkin()` / `buildSkinHeader()`.

**Device model pattern** — every synth follows it:

```cpp
namespace <synthName>Lib {
    class Device : public synthLib::Device {
        // DSP/MC68K instance, microcontroller for MIDI/UI,
        // ROM loading and memory setup, audio I/O via HDI08 or similar
    };
}
```

Responsibilities: load firmware ROM, initialize DSP/CPU memory, handle MIDI via microcontroller or direct DSP communication, process audio blocks, keep front panel state in sync.

## Code Conventions

- **Tabs for indentation** (tab size 4, UseTab: Always), 120 char column limit
- **Braces on new lines** for all constructs; namespace content indented
- **Naming:** PascalCase classes, camelCase functions/vars, `m_` member prefix, `_` parameter prefix (`void func(int _param)`)
- **Namespaces:** camelCase (`virusLib`, `synthLib`, `dsp56k`)
- **Early returns** preferred over deep nesting
- **Match the file you are in** — respect existing local patterns over these defaults
- `.clang-format` in `source/` directory
- C++17 required

## Key Build Files

- `source/cmake/base.cmake` — Compiler flags, platform-specific optimization settings
- `source/cmake/juce.cmake` — JUCE plugin configuration and multi-format support
- `source/cmake/skins.cmake` — Skin asset compilation
- `source/cmake/exporttarget.cmake` / `changelog.cmake` — target export, changelog generation
- `scripts/pack.cmake` / `generate.cmake` / `deployAll.cmake` / `deployGitHub.cmake` — CI packaging and deploy
- `source/3rdparty/README.md` — which third-party code is forked, why, and how to update it
- `.github/workflows/cmake.yml` — Public CI (GitHub Actions)

## Critical Implementation Details

- **State save/restore:** Device `getState()` MUST append to `_state` with `insert()`, never `assign()` — see the State Save/Restore section below
- **RmlUi threading:** DOM modifications MUST happen on the JUCE message thread. Use `juce::MessageManager::callAsync` from audio/MIDI callbacks
- **callAsync safety:** use a static instance set (lock, check the pointer is still registered) to guard lambdas against use-after-free
- **Includes arrive by declaration, not by luck:** there is no blanket `source/` include root any more. If a header stops resolving after a move, decide whether the dependency is real — repath it — or was never real, and delete the include. Do not add a link edge to make it compile.

## State Save/Restore (DAW Persistence)

**Save:** `Processor::getStateInformation()` → `saveChunkData()` → `Plugin::getState()` → `Device::getState()`. The device produces `std::vector<SMidiEvent>` of sysex dumps, serialized into a flat byte vector and wrapped with a version header by `Plugin::getState()`.

**Restore:** `Processor::setStateInformation()` → `Plugin::setState()` → `Device::setState()`. `splitMultipleSysex()` splits the flat stream back into messages by `0xF0`…`0xF7` pairs and feeds them into the device.

**Per-synth:** Virus uses Single/Multi dumps via the `virusLib` sysex protocol. JE8086 uses `ronaldo/common/Storage` for system + temp performance dumps (`createHeader()` → `Storage::read()` appends body → `createFooter()`). Vavra/Xenia/NodalRed2x use different mechanisms and do not call `Storage::read()`.

**Append semantics — this has caused real regressions.** By the time `Device::getState()` runs, `Plugin::getState()` has already pushed header bytes into `_state`. A device building sysex into a local buffer MUST use `_state.insert(_state.end(), …)`, never `_state.assign(…)`, which overwrites the header. Both JE8086 and Vavra/Xenia shipped this bug in 2.1.2.


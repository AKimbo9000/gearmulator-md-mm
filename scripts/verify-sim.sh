#!/usr/bin/env bash
# iOS Simulator build plus headless self-test (PLAN.md Phase 4).
#
#   scripts/verify-sim.sh [--device "iPad Pro 13-inch (M5)"]
#
# Stages, in order. The script stops at the first stage that fails and names it:
#   preflight  Xcode, an iOS Simulator runtime, the simulator device, firmware
#   configure  the tree has iOS support, then cmake -G Xcode for iphonesimulator
#   build      MD and MM Standalone apps (with their AUv3 extensions)
#   install    boot the simulator, install both apps, copy firmware into each app's
#              data container (Documents/ROMs) -- never into the bundle
#   selftest   launch each app with --selftest --run-id <id>, wait for its JSON,
#              compare the hash with the macOS interpreter render
#
# Until PLAN.md Phase 4 lands, configure fails because the iOS build glue is missing.
set -uo pipefail
source "$(dirname "$0")/lib/verify-common.sh"
cd "$VERIFY_ROOT"

SIM_NAME="iPad Pro 13-inch (M5)"
while [[ $# -gt 0 ]]; do
	case "$1" in
		--device) SIM_NAME="$2"; shift 2 ;;
		-h|--help) sed -n '2,16p' "$0"; exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done

# ---------------------------------------------------------------- preflight
xcode="$(xcodebuild -version 2>/dev/null | tr '\n' ' ')"
[[ -n "$xcode" ]] && verify preflight.xcode PASS "$xcode" || verify_stop_at preflight "xcodebuild not available"
runtime="$(xcrun simctl list runtimes 2>/dev/null | grep -m1 '^iOS ' | sed 's/ - com.*//')"
[[ -n "$runtime" ]] && verify preflight.runtime PASS "$runtime" || verify_stop_at preflight "no iOS Simulator runtime installed"
SIM_UDID="$(xcrun simctl list devices available 2>/dev/null | grep -F "    $SIM_NAME (" | head -1 | sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/')"
[[ -n "$SIM_UDID" ]] && verify preflight.simulator PASS "$SIM_NAME $SIM_UDID" || verify_stop_at preflight "simulator '$SIM_NAME' not found"
verify_find_roms
[[ $VERIFY_FAILED -ne 0 ]] && verify_stop_at preflight "firmware missing"

# ---------------------------------------------------------------- configure
if reason="$(verify_ios_support_missing)"; then
	verify_stop_at configure "$reason"
fi

BUILD_DIR="build/ios-sim"
if ! run_timeout 1800 "$VERIFY_OUT/configure.log" cmake -G Xcode -S . -B "$BUILD_DIR" \
	-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 \
	-DCMAKE_OSX_DEPLOYMENT_TARGET=17.4 -DDSP56K_FORCE_INTERPRETER=ON \
	-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
	-Dgearmulator_SYNTH_OSIRUS=OFF -Dgearmulator_SYNTH_OSTIRUS=OFF -Dgearmulator_SYNTH_VAVRA=OFF \
	-Dgearmulator_SYNTH_XENIA=OFF -Dgearmulator_SYNTH_NODALRED2X=OFF -Dgearmulator_SYNTH_JE8086=OFF \
	-Dgearmulator_SYNTH_88EMU=OFF -Dgearmulator_SYNTH_ELEKTRON=ON \
	-Dgearmulator_COMPONENT_DSPBRIDGE=OFF -Dgearmulator_BUILD_INSTALLER=OFF; then
	verify_stop_at configure "cmake failed, see configure.log"
fi
verify configure PASS "$BUILD_DIR"

# ---------------------------------------------------------------- build / install / selftest
# Filled in by PLAN.md Phase 4 (app targets, data-container ROM copy, --selftest).
verify_stop_at build "app targets and --selftest are added in PLAN.md Phase 4"

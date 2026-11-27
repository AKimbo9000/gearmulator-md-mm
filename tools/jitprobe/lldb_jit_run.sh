#!/bin/bash
# Starts Gearmulator MM on a connected device with the JIT enabled from this Mac (no StikDebug needed): installs the
# app, launches it stopped, services its JIT request through LLDB (lldb_jit_service.py), then pulls realtime.log and
# engine.log after the given number of seconds.
#
# Use: tools/jitprobe/lldb_jit_run.sh <hardware udid> <coredevice uuid> <out dir> [seconds] [app path]
# The two device ids are in CLAUDE.local.md. GEARMULATOR_MM_AUTOPLAY_SECONDS and GEARMULATOR_IOS_* are passed on to the
# app if set.
set -u
udid=$1; coredevice=$2; out=$3; seconds=${4:-60}
app=${5:-"bin/plugins/Release/Standalone/Gearmulator MM.app"}
bundle=com.aki.gearmulator.mm
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"

xcrun devicectl device install app --device "$udid" "$app" > "$out/install.txt" 2>&1 || { echo "RESULT FAIL install"; exit 1; }
pid=$(xcrun devicectl device info processes --device "$udid" 2>/dev/null | grep "Gearmulator MM.app" | awk '{print $1}' | head -1)
[ -n "$pid" ] && xcrun devicectl device process terminate --device "$udid" --pid "$pid" > /dev/null 2>&1 && sleep 2

# the app's test hooks: GEARMULATOR_MM_AUTOPLAY_SECONDS and every GEARMULATOR_IOS_* and GEARMULATOR_PGO_* variable set here
for v in GEARMULATOR_MM_AUTOPLAY_SECONDS $(compgen -e | grep -E '^GEARMULATOR_(IOS|PGO)_'); do
	[ -n "${!v:-}" ] && export "DEVICECTL_CHILD_$v=${!v}"
done
xcrun devicectl device process launch --start-stopped --device "$udid" --json-output "$out/launch.json" $bundle > /dev/null 2>&1
pid=$(python3 -c "import json;print(json.load(open('$out/launch.json'))['result']['process']['processIdentifier'])" 2>/dev/null)
[ -z "$pid" ] && { echo "RESULT FAIL launch (device locked?)"; exit 1; }
echo "launched stopped, pid $pid"

GEARMULATOR_JIT_UDID=$coredevice GEARMULATOR_JIT_PID=$pid GEARMULATOR_JIT_TOUCH=1 GEARMULATOR_JIT_TIMEOUT=240 \
	xcrun lldb -b -o "command script import $here/lldb_jit_service.py" > "$out/lldb.txt" 2>&1
grep "jit-service" "$out/lldb.txt" | grep -v SIG | tail -4

sleep "$seconds"
for f in realtime.log engine.log; do
	xcrun devicectl device copy from --device "$udid" --domain-type appDataContainer --domain-identifier $bundle \
		--source "Documents/$f" --destination "$out/$f" > /dev/null 2>&1
done
alive=$(xcrun devicectl device info processes --device "$udid" 2>/dev/null | grep -c "Gearmulator MM.app")
echo "app running: $alive"

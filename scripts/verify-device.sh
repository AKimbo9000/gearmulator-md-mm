#!/usr/bin/env bash
# Device build, install and headless self-test on the iPad (PLAN.md Phases 1.5, 5, 6).
#
#   scripts/verify-device.sh            MD and MM apps, --selftest      (Phase 5)
#   scripts/verify-device.sh --bench    headless bench app, serial RTF  (Phase 1.5)
#   scripts/verify-device.sh --auv3     AUv3 + ROM import self-tests    (Phase 6)
#
# The device and the signing team come from CLAUDE.local.md (gitignored); override
# with DEVICE_ID / DEVELOPMENT_TEAM. Neither is printed.
#
# Stages, in order. The script stops at the first stage that fails and names it:
#   preflight  device connected and unlocked, Developer Mode on, an Apple Development
#              identity for the team, firmware present
#   configure  the tree has iOS support, then cmake -G Xcode for iphoneos
#   build      signed app(s)
#   install    xcrun devicectl device install app; firmware copied into the app's
#              data container (Documents/ROMs) with devicectl copy to -- never into the IPA
#   selftest   devicectl device process launch <bundle> --selftest --run-id <id>; poll
#              devicectl copy from until that run's JSON exists (a fresh run-id defeats
#              the stale-file trap); compare its hash with the macOS interpreter render
set -uo pipefail
source "$(dirname "$0")/lib/verify-common.sh"
cd "$VERIFY_ROOT"

MODE=selftest
for arg in "$@"; do
	case "$arg" in
		--bench) MODE=bench ;;
		--auv3) MODE=auv3 ;;
		-h|--help) sed -n '2,22p' "$0"; exit 0 ;;
		*) echo "unknown argument: $arg" >&2; exit 2 ;;
	esac
done
verify mode INFO "$MODE"

DEVICE_ID="${DEVICE_ID:-$(verify_local_value 'CoreDevice UUID')}"
DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM:-$(verify_local_value 'Team ID')}"

# ---------------------------------------------------------------- preflight
[[ -n "$DEVICE_ID" ]] || verify_stop_at preflight "no device id (CLAUDE.local.md 'CoreDevice UUID' or DEVICE_ID)"
[[ -n "$DEVELOPMENT_TEAM" ]] || verify_stop_at preflight "no team id (CLAUDE.local.md 'Team ID' or DEVELOPMENT_TEAM)"

devices_json="$VERIFY_OUT/devices.json"
if ! run_timeout 60 "$VERIFY_OUT/devicectl-list.log" xcrun devicectl list devices --json-output "$devices_json"; then
	verify_stop_at preflight "xcrun devicectl list devices failed"
fi
device_state="$(python3 -I - "$devices_json" "$DEVICE_ID" <<'EOF'
import json, sys
data = json.load(open(sys.argv[1]))
for d in data.get("result", {}).get("devices", []):
    hw = d.get("hardwareProperties", {})
    ids = {d.get("identifier"), hw.get("udid")}
    if sys.argv[2] in ids:
        name = d.get("deviceProperties", {}).get("name", "?")
        model = hw.get("marketingName", hw.get("productType", "?"))
        tunnel = d.get("connectionProperties", {}).get("tunnelState", "?")
        devmode = d.get("deviceProperties", {}).get("developerModeStatus", "?")
        print(f"{name}|{model}|{tunnel}|{devmode}")
        break
EOF
)"
[[ -n "$device_state" ]] || verify_stop_at preflight "device not found by devicectl"
IFS='|' read -r dev_name dev_model dev_tunnel dev_devmode <<< "$device_state"
verify preflight.device "$([[ $dev_tunnel == connected ]] && echo PASS || echo FAIL)" "$dev_name ($dev_model) tunnel=$dev_tunnel"
verify preflight.devmode "$([[ $dev_devmode == enabled ]] && echo PASS || echo FAIL)" "developerMode=$dev_devmode"

lock_json="$VERIFY_OUT/lockstate.json"
if run_timeout 60 "$VERIFY_OUT/devicectl-lock.log" xcrun devicectl device info lockState --device "$DEVICE_ID" --json-output "$lock_json"; then
	locked="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("result",{}).get("passcodeRequired","?"))' "$lock_json" 2>/dev/null)"
	verify preflight.unlocked "$([[ $locked == False ]] && echo PASS || echo FAIL)" "passcodeRequired=$locked"
else
	verify preflight.unlocked FAIL "devicectl lockState query failed"
fi

# An Apple Development certificate whose subject OU is the team, with a private key
# (find-identity -v lists only certificates that have one).
identity=no
if security find-identity -v -p codesigning 2>/dev/null | grep -q '"Apple Development'; then
	pems="$VERIFY_OUT/apple-development.pem"
	security find-certificate -a -c "Apple Development" -p > "$pems" 2>/dev/null
	while IFS= read -r subject; do
		[[ "$subject" == *"OU=$DEVELOPMENT_TEAM"* || "$subject" == *"OU = $DEVELOPMENT_TEAM"* ]] && identity=yes
	done < <(awk '/BEGIN CERT/{f="'"$VERIFY_OUT"'/cert" ++n ".pem"} f{print > f} /END CERT/{close(f); f=""}' "$pems";
		for c in "$VERIFY_OUT"/cert*.pem; do [[ -f "$c" ]] && openssl x509 -in "$c" -noout -subject 2>/dev/null; done)
	rm -f "$pems" "$VERIFY_OUT"/cert*.pem
fi
verify preflight.identity "$([[ $identity == yes ]] && echo PASS || echo FAIL)" "Apple Development identity for the CLAUDE.local.md team: $([[ $identity == yes ]] && echo present || echo MISSING)"

verify_find_roms
[[ $VERIFY_FAILED -ne 0 ]] && verify_stop_at preflight "see the failed preflight checks above"

# ---------------------------------------------------------------- configure
if reason="$(verify_ios_support_missing)"; then
	verify_stop_at configure "$reason"
fi

# ---------------------------------------------------------------- build / install / selftest
# Filled in by PLAN.md Phase 1.5 (--bench), Phase 5 (apps) and Phase 6 (--auv3).
verify_stop_at build "device targets are added in PLAN.md Phase 1.5 (--bench) and Phase 5"

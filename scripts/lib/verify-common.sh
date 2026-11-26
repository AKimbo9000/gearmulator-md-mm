# Shared helpers for scripts/verify-*.sh. Source it; do not run it.
#
# Contract (see PLAN.md): every check prints one line
#   VERIFY <key> <details...> PASS|FAIL|INFO
# and the script ends with "VERIFY RESULT PASS|FAIL", exiting 0 only on PASS.
# Logs and a JSON summary go to build/verify/<script>/<timestamp>/.
# Firmware is never printed and never copied into a bundle.

VERIFY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERIFY_NAME="$(basename "$0" .sh)"
VERIFY_STAMP="$(date +%Y%m%d-%H%M%S)"
VERIFY_OUT="$VERIFY_ROOT/build/verify/$VERIFY_NAME/$VERIFY_STAMP"
VERIFY_FAILED=0
VERIFY_JSON_LINES=()
mkdir -p "$VERIFY_OUT"

readonly VERIFY_MD_SHA256="68542e30917b9918ccaee2b2237df62c8a00479938680b85aca93ce4fbca44c8"
readonly VERIFY_MM_SHA256="369849175602e20a9dd2b6e0ad8ac404b76f82718b14afbf1cbc01b7acabec7e"

# verify <key> <status> <details...>
verify()
{
	local key="$1" status="$2"
	shift 2
	local details="$*"
	printf 'VERIFY %s %s %s\n' "$key" "$details" "$status" | tee -a "$VERIFY_OUT/verify.log"
	[[ "$status" == FAIL ]] && VERIFY_FAILED=1
	VERIFY_JSON_LINES+=("{\"key\":\"$key\",\"status\":\"$status\",\"details\":\"${details//\"/\\\"}\"}")
	return 0
}

# Print the final line, write the JSON summary and exit.
verify_finish()
{
	local result=PASS
	[[ $VERIFY_FAILED -ne 0 ]] && result=FAIL
	{
		printf '{"script":"%s","timestamp":"%s","git":"%s","result":"%s","checks":[' \
			"$VERIFY_NAME" "$VERIFY_STAMP" "$(git -C "$VERIFY_ROOT" rev-parse --short HEAD 2>/dev/null)" "$result"
		local IFS=,
		printf '%s' "${VERIFY_JSON_LINES[*]-}"
		printf ']}\n'
	} > "$VERIFY_OUT/summary.json"
	printf 'VERIFY RESULT %s   (logs: %s)\n' "$result" "${VERIFY_OUT#"$VERIFY_ROOT"/}" | tee -a "$VERIFY_OUT/verify.log"
	[[ $result == PASS ]] && exit 0
	exit 1
}

# Stop at a named stage: used by scripts whose later stages are not ported yet.
verify_stop_at()
{
	local stage="$1"
	shift
	verify "stage=$stage" FAIL "$*"
	verify_finish
}

# run_timeout <seconds> <logfile> <cmd...>: run cmd with stdout+stderr to logfile.
# Returns the command's exit code, or 124 on timeout.
run_timeout()
{
	local secs="$1" log="$2"
	shift 2
	"$@" > "$log" 2>&1 &
	local pid=$!
	local waited=0 rc
	while kill -0 "$pid" 2>/dev/null; do
		if (( waited >= secs )); then
			kill -TERM "$pid" 2>/dev/null
			sleep 2
			kill -KILL "$pid" 2>/dev/null
			wait "$pid" 2>/dev/null
			return 124
		fi
		sleep 1
		waited=$((waited + 1))
	done
	wait "$pid"
	rc=$?
	return $rc
}

# Locate MD/MM firmware by SHA-256 (file names in roms/ contain spaces) unless the
# GEARMULATOR_*_FIRMWARE_BIN variables already point at valid images. Exports both.
verify_find_roms()
{
	local f sum
	if [[ -n "${GEARMULATOR_MD_FIRMWARE_BIN:-}" ]] && [[ "$(shasum -a 256 "$GEARMULATOR_MD_FIRMWARE_BIN" 2>/dev/null | awk '{print $1}')" != "$VERIFY_MD_SHA256" ]]; then
		unset GEARMULATOR_MD_FIRMWARE_BIN
	fi
	if [[ -n "${GEARMULATOR_MM_FIRMWARE_BIN:-}" ]] && [[ "$(shasum -a 256 "$GEARMULATOR_MM_FIRMWARE_BIN" 2>/dev/null | awk '{print $1}')" != "$VERIFY_MM_SHA256" ]]; then
		unset GEARMULATOR_MM_FIRMWARE_BIN
	fi
	shopt -s nullglob
	for f in "$VERIFY_ROOT"/roms/*; do
		[[ -f "$f" ]] || continue
		[[ "$(stat -f %z "$f")" == 8388608 ]] || continue
		sum="$(shasum -a 256 "$f" | awk '{print $1}')"
		[[ "$sum" == "$VERIFY_MD_SHA256" && -z "${GEARMULATOR_MD_FIRMWARE_BIN:-}" ]] && export GEARMULATOR_MD_FIRMWARE_BIN="$f"
		[[ "$sum" == "$VERIFY_MM_SHA256" && -z "${GEARMULATOR_MM_FIRMWARE_BIN:-}" ]] && export GEARMULATOR_MM_FIRMWARE_BIN="$f"
	done
	shopt -u nullglob
	[[ -n "${GEARMULATOR_MD_FIRMWARE_BIN:-}" ]] && verify roms.md PASS "found by sha256" || verify roms.md FAIL "MD OS 1.63 image not found in roms/"
	[[ -n "${GEARMULATOR_MM_FIRMWARE_BIN:-}" ]] && verify roms.mm PASS "found by sha256" || verify roms.mm FAIL "MM OS 1.32b image not found in roms/"
}

# Read "<label>: <value>" from CLAUDE.local.md (gitignored), e.g. "Apple Developer Team ID".
verify_local_value()
{
	local label="$1" file="$VERIFY_ROOT/CLAUDE.local.md"
	[[ -f "$file" ]] || return 1
	grep -m1 -F "$label" "$file" | sed -E 's/^[^:]*:[[:space:]]*//; s/[[:space:]]+$//'
}

# The iOS phases need build glue that PLAN.md Phase 4 (and 1.5) adds. Until then
# this is the honest reason the iOS scripts stop at their configure stage.
verify_ios_support_missing()
{
	if ! grep -q 'BUILD_JUCEPLUGIN_AUV3' "$VERIFY_ROOT/source/cmake/juce.cmake"; then
		echo "source/cmake/juce.cmake has no AUv3/iOS support yet (PLAN.md Phase 4)"
		return 0
	fi
	if ! grep -q 'DSP56K_COW_MACH' "$VERIFY_ROOT/source/cpu/dsp56300/source/dsp56kBase/cowmemory.cpp"; then
		echo "dsp56300 cowmemory.cpp still uses Mach VM on iOS (PLAN.md Phase 1a)"
		return 0
	fi
	return 1
}

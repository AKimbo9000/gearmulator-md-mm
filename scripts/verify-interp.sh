#!/usr/bin/env bash
# macOS arm64 check of the DSP56300 interpreter against the JIT (PLAN.md).
#
#   scripts/verify-interp.sh              compare interpreter vs JIT, and JIT vs baseline
#   scripts/verify-interp.sh --baseline   record the JIT reference (doc/ios-baselines/)
#   scripts/verify-interp.sh --debug      same checks on Debug builds (asserts on)
#   scripts/verify-interp.sh --perf       real-time factor (PLAN.md Phase 2, not yet)
#   scripts/verify-interp.sh --timed=...  timed threaded mode (PLAN.md Phase 3, not yet)
#
# Builds build/mac-jit[-debug] and build/mac-interp[-debug] (-DDSP56K_FORCE_INTERPRETER=ON):
# Ninja, arm64 only, plug-ins and other synths off. Then runs dsp56kTestRunner in both
# and the fixed trigPressFirmwareTest scenario (boot, Trigger 1 press/hold/release) for
# MD and MM, comparing the hash of the raw DSP output words and per-DSP counters
# every 0.25 s of emulated time.
set -uo pipefail
source "$(dirname "$0")/lib/verify-common.sh"
cd "$VERIFY_ROOT"

MODE=compare
CONFIG=Release
for arg in "$@"; do
	case "$arg" in
		--baseline) MODE=baseline ;;
		--debug) CONFIG=Debug ;;
		--perf) MODE=perf ;;
		--timed=*) MODE=timed ;;
		-h|--help) sed -n '2,15p' "$0"; exit 0 ;;
		*) echo "unknown argument: $arg" >&2; exit 2 ;;
	esac
done

if [[ $MODE == perf ]]; then verify_stop_at perf "not implemented until PLAN.md Phase 2"; fi
if [[ $MODE == timed ]]; then verify_stop_at timed "not implemented until PLAN.md Phase 3"; fi

BASELINE_DIR="$VERIFY_ROOT/doc/ios-baselines"
SUFFIX=""
[[ $CONFIG == Debug ]] && SUFFIX="-debug"
ENGINES=(jit interp)
MODELS=(md mm)

verify_find_roms
[[ $VERIFY_FAILED -ne 0 ]] && verify_finish

# ---------------------------------------------------------------- build
build_engine()
{
	local engine="$1" dir="build/mac-$1$SUFFIX"
	local extra=()
	[[ $engine == interp ]] && extra+=(-DDSP56K_FORCE_INTERPRETER=ON)
	if ! run_timeout 900 "$VERIFY_OUT/configure-$engine.log" cmake -G Ninja -S . -B "$dir" \
		-DCMAKE_BUILD_TYPE="$CONFIG" -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_TESTING=ON \
		-Dgearmulator_BUILD_JUCEPLUGIN=OFF -Dgearmulator_BUILD_INSTALLER=OFF \
		-Dgearmulator_COMPONENT_DSPBRIDGE=OFF \
		-Dgearmulator_SYNTH_OSIRUS=OFF -Dgearmulator_SYNTH_OSTIRUS=OFF -Dgearmulator_SYNTH_VAVRA=OFF \
		-Dgearmulator_SYNTH_XENIA=OFF -Dgearmulator_SYNTH_NODALRED2X=OFF -Dgearmulator_SYNTH_JE8086=OFF \
		-Dgearmulator_SYNTH_88EMU=OFF -Dgearmulator_SYNTH_ELEKTRON=ON \
		${extra[@]+"${extra[@]}"}; then
		verify "build.$engine" FAIL "configure failed, see configure-$engine.log"
		return 1
	fi
	if ! run_timeout 3600 "$VERIFY_OUT/build-$engine.log" cmake --build "$dir" \
		--target dsp56kTestRunner trigPressFirmwareTest; then
		verify "build.$engine" FAIL "build failed, see build-$engine.log"
		return 1
	fi
	verify "build.$engine" PASS "$dir ($CONFIG, arm64)"
}

tool_path()
{
	find "build/mac-$1$SUFFIX" -type f -name "$2" -perm -u+x 2>/dev/null | head -1
}

for engine in "${ENGINES[@]}"; do build_engine "$engine"; done
[[ $VERIFY_FAILED -ne 0 ]] && verify_finish

# ---------------------------------------------------------------- DSP unit tests
# bash 3.2 (macOS /bin/bash) has no associative arrays: one variable per engine.
for engine in "${ENGINES[@]}"; do
	runner="$(tool_path "$engine" dsp56kTestRunner)"
	run_timeout 1800 "$VERIFY_OUT/dsp56kTestRunner-$engine.log" "$runner"
	printf -v "runner_rc_$engine" '%s' "$?"
done
verify dsp56kTestRunner.jit "$([[ $runner_rc_jit == 0 ]] && echo PASS || echo FAIL)" "exit=$runner_rc_jit"
if [[ $MODE == baseline ]]; then
	verify dsp56kTestRunner.interp INFO "exit=$runner_rc_interp (recorded as-is in baseline mode)"
else
	verify dsp56kTestRunner.interp "$([[ $runner_rc_interp == 0 ]] && echo PASS || echo FAIL)" "exit=$runner_rc_interp"
fi

# ---------------------------------------------------------------- renders
# Runs are independent processes and run in parallel; wall-clock rtf printed here is
# therefore informational only (Phase 2 measures speed on an idle machine).
start_render()
{
	local engine="$1" model="$2" tag="$3" timeout="$4"
	local bin
	bin="$(tool_path "$engine" trigPressFirmwareTest)"
	( run_timeout "$timeout" "$VERIFY_OUT/render-$engine-$model-$tag.log" \
		"$bin" "$model" --counters "$VERIFY_OUT/counters-$engine-$model-$tag.csv"
	  echo $? > "$VERIFY_OUT/render-$engine-$model-$tag.rc" ) &
}

render_field()  # <engine> <model> <tag> <field>
{
	grep -m1 '^RENDER ' "$VERIFY_OUT/render-$1-$2-$3.log" 2>/dev/null | tr ' ' '\n' | sed -n "s/^$4=//p"
}

render_rc() { cat "$VERIFY_OUT/render-$1-$2-$3.rc" 2>/dev/null || echo 255; }

first_divergence()  # <csvA> <csvB>: prints "line N: A | B" for the first differing line
{
	paste -d'|' "$1" "$2" | awk -F'|' 'NR>1 && $1!=$2 { print "checkpoint " NR-1 ": " $1 " vs " $2; exit }'
}

for model in "${MODELS[@]}"; do
	start_render jit "$model" run1 1800
	start_render interp "$model" run1 3600
	[[ $MODE == baseline ]] && start_render jit "$model" run2 1800
done
wait

for model in "${MODELS[@]}"; do
	for engine in "${ENGINES[@]}"; do
		rc="$(render_rc "$engine" "$model" run1)"
		status=PASS
		[[ $rc != 0 ]] && status=FAIL
		[[ $engine == interp && $MODE == baseline && $rc != 0 ]] && status=INFO
		[[ $rc == 124 ]] && rc="timeout"
		verify "$model.$engine.run" "$status" "exit=$rc hash=$(render_field "$engine" "$model" run1 hash) boot_ready=$(render_field "$engine" "$model" run1 boot_ready) hold_peak=$(render_field "$engine" "$model" run1 hold_peak) rtf=$(render_field "$engine" "$model" run1 rtf)"
	done

	jit_hash="$(render_field jit "$model" run1 hash)"
	int_hash="$(render_field interp "$model" run1 hash)"
	jit_csv="$VERIFY_OUT/counters-jit-$model-run1.csv"
	int_csv="$VERIFY_OUT/counters-interp-$model-run1.csv"

	if [[ $MODE == baseline ]]; then
		h2="$(render_field jit "$model" run2 hash)"
		if [[ -n "$jit_hash" && "$jit_hash" == "$h2" ]] && cmp -s "$jit_csv" "$VERIFY_OUT/counters-jit-$model-run2.csv"; then
			verify "$model.jit.determinism" PASS "run1==run2 hash=$jit_hash counters identical"
		else
			verify "$model.jit.determinism" FAIL "run1=$jit_hash run2=$h2 $(first_divergence "$jit_csv" "$VERIFY_OUT/counters-jit-$model-run2.csv")"
		fi
	elif [[ -f "$BASELINE_DIR/jit-$model-counters.csv" ]]; then
		base_hash="$(sed -n "s/.*\"$model\":{\"hash\":\"\([0-9a-f]*\)\".*/\1/p" "$BASELINE_DIR/jit-render.json")"
		if [[ "$jit_hash" == "$base_hash" ]] && cmp -s "$jit_csv" "$BASELINE_DIR/jit-$model-counters.csv"; then
			verify "jit.unchanged.$model" PASS "== doc/ios-baselines ($base_hash)"
		else
			verify "jit.unchanged.$model" FAIL "hash=$jit_hash baseline=$base_hash $(first_divergence "$BASELINE_DIR/jit-$model-counters.csv" "$jit_csv")"
		fi
	else
		verify "jit.unchanged.$model" FAIL "no baseline; run with --baseline first"
	fi

	if [[ -n "$jit_hash" && "$jit_hash" == "$int_hash" ]] && cmp -s "$jit_csv" "$int_csv"; then
		verify "$model.render" PASS "jit=$jit_hash interp=$int_hash MATCH, counters $(($(wc -l < "$jit_csv") - 1)) checkpoints MATCH"
	else
		div="$(first_divergence "$jit_csv" "$int_csv")"
		[[ -z "$div" ]] && div="counters identical over the shorter run"
		verify "$model.render" "$([[ $MODE == baseline ]] && echo INFO || echo FAIL)" "jit=$jit_hash interp=${int_hash:-none} MISMATCH first-divergence: $div"
	fi
done

# ---------------------------------------------------------------- baseline record
if [[ $MODE == baseline && $VERIFY_FAILED -eq 0 ]]; then
	mkdir -p "$BASELINE_DIR"
	{
		printf '{"note":"JIT reference renders of trigPressFirmwareTest on the untouched tree; later phases must reproduce them",'
		printf '"git":"%s","dsp56300":"%s","config":"%s","date":"%s"' \
			"$(git rev-parse --short HEAD)" "$(git -C source/cpu/dsp56300 rev-parse --short HEAD)" "$CONFIG" "$(date +%Y-%m-%d)"
		for model in "${MODELS[@]}"; do
			printf ',"%s":{"hash":"%s","frames":%s,"uc_cycles":%s,"dsp0_instr":%s,"dsp0_cycles":%s,"dsp1_instr":%s,"dsp1_cycles":%s}' "$model" \
				"$(render_field jit "$model" run1 hash)" "$(render_field jit "$model" run1 frames)" \
				"$(render_field jit "$model" run1 uc_cycles)" \
				"$(render_field jit "$model" run1 dsp0_instr)" "$(render_field jit "$model" run1 dsp0_cycles)" \
				"$(render_field jit "$model" run1 dsp1_instr)" "$(render_field jit "$model" run1 dsp1_cycles)"
			cp "$VERIFY_OUT/counters-jit-$model-run1.csv" "$BASELINE_DIR/jit-$model-counters.csv"
		done
		printf '}\n'
	} > "$BASELINE_DIR/jit-render.json"
	verify baseline PASS "written doc/ios-baselines/jit-render.json and jit-{md,mm}-counters.csv"
fi

verify_finish

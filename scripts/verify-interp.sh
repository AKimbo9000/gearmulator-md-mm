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

if [[ $MODE == timed ]]; then verify_stop_at timed "not implemented until PLAN.md Phase 3"; fi

BASELINE_DIR="$VERIFY_ROOT/doc/ios-baselines"
SUFFIX=""
[[ $CONFIG == Debug ]] && SUFFIX="-debug"
ENGINES=(jit interp)
MODELS=(md mm)

verify_find_roms
[[ $VERIFY_FAILED -ne 0 ]] && verify_finish

# ---------------------------------------------------------------- --perf (PLAN.md Phase 2)
# Interpreter real-time factor in three configurations, measured one run at a time on an idle machine:
#   plain      Release (-Ofast), as build/mac-interp
#   opt        + GEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP + GEARMULATOR_MDMM_APPLE_THINLTO
#   pgo        + a profile trained on this tree's MD and MM interpreter scenario
# plus the JIT for reference, and an MD_TRANSPORT_DIAGNOSTICS build for synchronisation counts and the share of
# host time per component, from which perf.bound is the speed-up three ideal threads could give (total/largest).
# Each run: the trigPressFirmwareTest boot and Trig 1 press, then 3 windows of 20 s; the best window counts.
perf_configure_build()   # <dir> <cmake args...>
{
	local dir="$1"; shift
	run_timeout 900 "$VERIFY_OUT/configure-$(basename "$dir").log" cmake -G Ninja -S . -B "$dir" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_TESTING=ON \
		-Dgearmulator_BUILD_JUCEPLUGIN=OFF -Dgearmulator_BUILD_INSTALLER=OFF -Dgearmulator_COMPONENT_DSPBRIDGE=OFF \
		-Dgearmulator_SYNTH_OSIRUS=OFF -Dgearmulator_SYNTH_OSTIRUS=OFF -Dgearmulator_SYNTH_VAVRA=OFF \
		-Dgearmulator_SYNTH_XENIA=OFF -Dgearmulator_SYNTH_NODALRED2X=OFF -Dgearmulator_SYNTH_JE8086=OFF \
		-Dgearmulator_SYNTH_88EMU=OFF -Dgearmulator_SYNTH_ELEKTRON=ON "$@" || return 1
	run_timeout 3600 "$VERIFY_OUT/build-$(basename "$dir").log" cmake --build "$dir" --target trigPressFirmwareTest
}
perf_idle()   # wait until the 1-minute load average is at most VERIFY_PERF_MAX_LOAD (default 3); FAIL after
{             # VERIFY_PERF_IDLE_WAIT seconds (default 600). Our own builds and runs decay out of it first.
	local load max="${VERIFY_PERF_MAX_LOAD:-3}" waited=0 limit="${VERIFY_PERF_IDLE_WAIT:-600}"
	while :; do
		load="$(sysctl -n vm.loadavg | awk '{print $2}')"
		if awk -v l="$load" -v m="$max" 'BEGIN{exit !(l <= m)}'; then
			verify "perf.idle$1" PASS "loadavg=$load (max $max, waited ${waited}s)"; return 0
		fi
		(( waited >= limit )) && break
		sleep 15; waited=$((waited + 15))
	done
	verify "perf.idle$1" FAIL "loadavg=$load still above $max after ${waited}s: machine busy, numbers would not be valid"; return 1
}
perf_run()   # <label> <dir> <model>
{
	local label="$1" dir="$2" model="$3" log="$VERIFY_OUT/perf-$1-$3.log" rc
	perf_idle ".$label.$model" || return 1
	run_timeout 1800 "$log" "$dir/source/elektron/md/mdLibTest/trigPressFirmwareTest" "$model" \
		--boot-seconds 25 --hold-seconds 2 --tail-seconds 0 --windows 3 --window-seconds 20
	rc=$?
	local best windows mips0 mips1 ready
	best="$(grep -m1 '^RENDER' "$log" | tr ' ' '\n' | sed -n 's/^best_window_rtf=//p')"
	ready="$(grep -m1 '^RENDER' "$log" | tr ' ' '\n' | sed -n 's/^boot_ready=//p')"
	windows="$(grep '^PERF' "$log" | sed -nE 's/.* rtf=([0-9.]+).*/\1/p' | paste -sd, -)"
	mips0="$(grep '^PERF' "$log" | sed -nE 's/.*dsp0_mips=([0-9.]+).*/\1/p' | sort -n | tail -1)"
	mips1="$(grep '^PERF' "$log" | sed -nE 's/.*dsp1_mips=([0-9.]+).*/\1/p' | sort -n | tail -1)"
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$label" "$model" "$best" "$windows" "$mips0" "$mips1" >> "$VERIFY_OUT/perf.tsv"
	if [[ $rc == 0 && $ready == 1 && -n "$best" ]]; then
		verify "perf.$model.$label" PASS "rtf=${best}x (best of 3x20 s: $windows) mixer=${mips0} producer=${mips1} Minstr/s"
	else
		verify "perf.$model.$label" FAIL "exit=$rc boot_ready=$ready rtf=$best"
	fi
}
if [[ $MODE == perf ]]; then
	PGO_DIR="$VERIFY_ROOT/build/perf-pgo"
	OPT=(-DGEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP=ON -DGEARMULATOR_MDMM_APPLE_THINLTO=ON)
	for spec in "build/mac-jit|" "build/mac-interp|-DDSP56K_FORCE_INTERPRETER=ON" \
		"build/perf-interp-opt|-DDSP56K_FORCE_INTERPRETER=ON ${OPT[*]}" \
		"build/perf-interp-pgogen|-DDSP56K_FORCE_INTERPRETER=ON ${OPT[*]} -DGEARMULATOR_MDMM_APPLE_PGO_MODE=generate" \
		"build/perf-interp-diag|-DDSP56K_FORCE_INTERPRETER=ON -DMD_TRANSPORT_DIAGNOSTICS=ON"; do
		( dir="${spec%%|*}"; args="${spec#*|}"
		  # shellcheck disable=SC2086
		  perf_configure_build "$dir" $args; echo $? > "$VERIFY_OUT/built-$(basename "$dir").rc" ) &
	done
	wait
	for d in mac-jit mac-interp perf-interp-opt perf-interp-pgogen perf-interp-diag; do
		rc="$(cat "$VERIFY_OUT/built-$d.rc")"
		verify "perf.build.$d" "$([[ $rc == 0 ]] && echo PASS || echo FAIL)" "exit=$rc"
	done
	[[ $VERIFY_FAILED -ne 0 ]] && verify_finish

	# PGO: train the instrumented interpreter on both machines' scenario, merge, rebuild with the profile
	PGO_GEN_BIN=build/perf-interp-pgogen/source/elektron/md/mdLibTest/trigPressFirmwareTest
	if [[ -f "$PGO_DIR/mdmm-interp.profdata" && "$PGO_DIR/mdmm-interp.profdata" -nt "$PGO_GEN_BIN" ]]; then
		verify perf.pgo.profile INFO "reusing $PGO_DIR/mdmm-interp.profdata (newer than the instrumented binary)"
		echo 0 > "$VERIFY_OUT/pgo-train-md.rc"; echo 0 > "$VERIFY_OUT/pgo-train-mm.rc"
	else
	rm -rf "$PGO_DIR"; mkdir -p "$PGO_DIR"
	for model in "${MODELS[@]}"; do
		( LLVM_PROFILE_FILE="$PGO_DIR/$model-%p.profraw" run_timeout 3600 "$VERIFY_OUT/pgo-train-$model.log" \
			build/perf-interp-pgogen/source/elektron/md/mdLibTest/trigPressFirmwareTest "$model"
		  echo $? > "$VERIFY_OUT/pgo-train-$model.rc" ) &
	done
	wait
	xcrun llvm-profdata merge -output="$PGO_DIR/mdmm-interp.profdata" "$PGO_DIR"/*.profraw > "$VERIFY_OUT/pgo-merge.log" 2>&1
	fi
	if [[ -f "$PGO_DIR/mdmm-interp.profdata" ]] \
		&& perf_configure_build build/perf-interp-pgo -DDSP56K_FORCE_INTERPRETER=ON "${OPT[@]}" \
			-DGEARMULATOR_MDMM_APPLE_PGO_MODE=use -DGEARMULATOR_MDMM_APPLE_PGO_PROFILE="$PGO_DIR/mdmm-interp.profdata"; then
		verify perf.build.perf-interp-pgo PASS "trained on md+mm (train exit $(cat "$VERIFY_OUT"/pgo-train-md.rc)/$(cat "$VERIFY_OUT"/pgo-train-mm.rc))"
	else
		verify perf.build.perf-interp-pgo FAIL "profile merge or build failed, see pgo-merge.log / build-perf-interp-pgo.log"
	fi

	# measurements, one at a time
	: > "$VERIFY_OUT/perf.tsv"
	for model in "${MODELS[@]}"; do
		perf_run jit build/mac-jit "$model"
		perf_run plain build/mac-interp "$model"
		perf_run opt build/perf-interp-opt "$model"
		[[ -x build/perf-interp-pgo/source/elektron/md/mdLibTest/trigPressFirmwareTest ]] && perf_run pgo build/perf-interp-pgo "$model"
	done

	# synchronisation counts and host-time split (diagnostics build; its own timing overhead is excluded from rtf)
	for model in "${MODELS[@]}"; do
		log="$VERIFY_OUT/perf-diag-$model.log"
		run_timeout 1800 "$log" build/perf-interp-diag/source/elektron/md/mdLibTest/trigPressFirmwareTest "$model"
		dsp_hz=101606400
		seconds="$(grep -m1 '^RENDER' "$log" | tr ' ' '\n' | sed -n 's/^frames=//p' | awk '{print $1/44100}')"
		for p in coldfire_to_dsp0 coldfire_to_dsp1 dsp_to_dsp0 dsp_to_dsp1 inline_hdi08_dsp0 inline_hdi08_dsp1 background_dsp0 background_dsp1 background_uc; do
			line="$(grep "^SYNC $p " "$log")"
			per_s="$(sed -nE 's/.*per_s=([0-9]+).*/\1/p' <<< "$line")"
			gap="$(awk -v p="$per_s" -v h="$dsp_hz" 'BEGIN{ if (p > 0) printf "%.0f", h/p; else print "-" }')"
			verify "perf.sync.$model.$p" INFO "${per_s:-0}/s, mean gap ${gap} DSP cycles"
		done
		for d in 0 1; do verify "perf.sync.$model.link$d" INFO "$(grep "^SYNC link$d " "$log" | sed 's/^SYNC link[01] //')"; done
		split="$(grep -m1 '^SPLIT' "$log")"
		if [[ -n "$split" ]]; then
			verify "perf.split.$model" INFO "$(sed -E 's/^SPLIT //; s/ bound=.*//; s/[a-z]+_ns=[0-9]+ //g' <<< "$split")"
			verify "perf.bound.$model" PASS "threads <= $(sed -nE 's/.*bound=([0-9.]+).*/\1/p' <<< "$split")x (total host time / largest component)"
		else
			verify "perf.bound.$model" FAIL "no SPLIT line from the diagnostics build"
		fi
	done
	verify_finish
fi

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
	if ! run_timeout 3600 "$VERIFY_OUT/build-$engine.log" cmake --build "$dir"; then
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

# ---------------------------------------------------------------- ctest (compare mode)
# Every test in the build (mdLib unit + firmware tests and the dsp56300 tests), with
# firmware required so a missing ROM fails instead of skipping. Runs in the background
# alongside the renders.
start_ctest()
{
	local engine="$1"
	( cd "build/mac-$engine$SUFFIX" && GEARMULATOR_REQUIRE_FIRMWARE_TESTS=1 \
		ctest -C "$CONFIG" -j 4 --output-on-failure --timeout "${VERIFY_CTEST_TIMEOUT:-1800}" \
		> "$VERIFY_OUT/ctest-$engine.log" 2>&1; echo $? > "$VERIFY_OUT/ctest-$engine.rc" ) &
}
report_ctest()
{
	local engine="$1" rc summary
	rc="$(cat "$VERIFY_OUT/ctest-$engine.rc" 2>/dev/null || echo 255)"
	# "100% tests passed out of 89" when none failed, "97% tests passed, 3 tests failed out of 89" otherwise
	summary="$(grep -E 'tests passed(, [0-9]+ tests failed)? out of [0-9]+' "$VERIFY_OUT/ctest-$engine.log" | tail -1)"
	local total failed
	total="$(sed -nE 's/.*out of ([0-9]+).*/\1/p' <<< "$summary")"
	failed="$(sed -nE 's/.*passed, ([0-9]+) tests failed.*/\1/p' <<< "$summary")"
	[[ -n "$summary" && -z "$failed" ]] && failed=0
	if [[ $rc == 0 && -n "$total" && "$failed" == 0 ]]; then
		verify "ctest.mdLib.$engine" PASS "$total/$total (GEARMULATOR_REQUIRE_FIRMWARE_TESTS=1)"
	else
		verify "ctest.mdLib.$engine" FAIL "exit=$rc ${summary:-no summary}; failed: $(grep -E '^\s+[0-9]+ - ' "$VERIFY_OUT/ctest-$engine.log" | sed -E 's/^ +[0-9]+ - //' | tr '\n' ' ')"
	fi
}
if [[ $MODE != baseline ]]; then
	for engine in "${ENGINES[@]}"; do start_ctest "$engine"; done
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

# Wall-clock limit per render. The JIT renders take ~10 s here; the interpreter gets
# more room. Override with VERIFY_JIT_TIMEOUT / VERIFY_INTERP_TIMEOUT (seconds).
JIT_TIMEOUT="${VERIFY_JIT_TIMEOUT:-300}"
INTERP_TIMEOUT="${VERIFY_INTERP_TIMEOUT:-900}"
[[ $CONFIG == Debug ]] && JIT_TIMEOUT="${VERIFY_JIT_TIMEOUT:-1800}" && INTERP_TIMEOUT="${VERIFY_INTERP_TIMEOUT:-5400}"
for model in "${MODELS[@]}"; do
	start_render jit "$model" run1 "$JIT_TIMEOUT"
	start_render interp "$model" run1 "$INTERP_TIMEOUT"
	[[ $MODE == baseline ]] && start_render jit "$model" run2 "$JIT_TIMEOUT"
done
wait   # renders and, in compare mode, the ctest runs

for model in "${MODELS[@]}"; do
	for engine in "${ENGINES[@]}"; do
		rc="$(render_rc "$engine" "$model" run1)"
		status=PASS
		[[ $rc != 0 ]] && status=FAIL
		[[ $engine == interp && $MODE == baseline && $rc != 0 ]] && status=INFO
		if [[ $rc == 124 ]]; then
			limit="$JIT_TIMEOUT"; [[ $engine == interp ]] && limit="$INTERP_TIMEOUT"
			last="$(tail -1 "$VERIFY_OUT/counters-$engine-$model-run1.csv" 2>/dev/null)"
			[[ -z "$last" || "$last" == frame,* ]] && last="none"
			verify "$model.$engine.run" "$status" "TIMEOUT after ${limit}s wall, last checkpoint (frame,uc,dsp0 instr/cycles/pc,dsp1 instr/cycles/pc): $last"
			continue
		fi
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

if [[ $MODE != baseline ]]; then
	for engine in "${ENGINES[@]}"; do report_ctest "$engine"; done
fi

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

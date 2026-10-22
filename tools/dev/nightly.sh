#!/usr/bin/env bash
# The nightly tier (ADR 5): slow, thorough checks, one command, local only.
#
#   nightly.sh [--steps S,...] [--endurance SECONDS]
#   nightly.sh --leg native|container [--steps S,...] [--endurance SECONDS]
#
# Without --leg, both legs run at once, as in ci.sh: the native leg here
# (the Mac) and the container leg in the Linux reference container
# (container.sh run --clean: the commit under test, offline).  Each leg's
# output is printed whole when both are done, native first, and goes to
# build/nightly/nightly.log, replaced by each run.  --leg runs one leg's
# steps on this machine (the container leg inside the container).
#
# Steps, each printing what it runs, a SKIP naming what is missing, or
# its failure:
#   native leg:
#   analyzer        the Clang Static Analyzer (clang-tidy clang-analyzer-*,
#                   pinned LLVM, deeper limits than the check tier's) on
#                   every fork C++ file in the debug-harness build's
#                   compilation database
#   thread_safety   clang's -Wthread-safety on the same files
#   tsan            ThreadSanitizer on the audio-thread tests (none yet)
#   hardened_demo   D2 demo playback in the hardened debug-harness build,
#                   windowless through run.sh; a hardening trap, a
#                   sanitizer report, a crash report or an unclean end
#                   (a non-zero status or a hang after TERM) fails it
#   endurance       demo playback for an hour (--endurance), sampling the
#                   game's memory: it must stay flat (rss_trend.py)
#   cppcheck,       SKIPs until toolchain.pins pins them (the owner's
#   codeql          decision)
#   container leg:
#   glibcxx_debug   the unit tests built with libstdc++'s debug mode
#                   (_GLIBCXX_DEBUG: checked iterators) and run
# --steps runs only the steps named (of the leg's); --endurance sets the
# endurance run's length.  DXX_NIGHTLY_DATABASE names a compilation
# database to analyse instead of building the harness (the tests).
# Exit status 1 names the failed steps (or legs), 2 is a usage error.
# Build the image first with `container.sh build`; the container leg tests
# the commit (HEAD), not uncommitted changes.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
source "$dev_dir/env.sh"

usage()
{
	echo "usage: $0 [--leg native|container] [--steps S,...] [--endurance SECONDS]" >&2
	exit 2
}

native_steps=(analyzer thread_safety tsan hardened_demo endurance cppcheck codeql)
container_steps=(glibcxx_debug)

harness_dir=build/nightly/harness
clang_tidy=$(dev_llvm_tool clang-tidy)
# The LLVM release finds the macOS SDK through SDKROOT (as in format.sh).
if [[ $dev_os == darwin && -z ${SDKROOT-} ]]; then
	SDKROOT=$(xcrun --show-sdk-path 2>/dev/null) && export SDKROOT
fi

# Build the game with the dev profile (hardened) and the debug harness
# into $harness_dir, with its compilation database.
build_harness()
{
	"$dev_dir/build.sh" dev dbg_harness=1 "builddir=$harness_dir" \
		"compilation_database=$harness_dir/compile_commands.json"
}

# Print the compilation database to analyse: DXX_NIGHTLY_DATABASE, else
# the harness build's, built first.
database()
{
	if [[ -n ${DXX_NIGHTLY_DATABASE-} ]]; then
		printf '%s\n' "$DXX_NIGHTLY_DATABASE"
		return
	fi
	build_harness >&2 || return
	printf '%s\n' "$root/$harness_dir/compile_commands.json"
}

# Print the fork directories (tools/dev/fork-dirs), one per line.
fork_dirs()
{
	grep -v -e '^#' -e '^$' "$dev_dir/fork-dirs"
}

# Print, NUL-separated, every C++ source file of database $1 that lies in
# a fork directory, as an absolute path, each once.
fork_sources()
{
	python3 - "$1" "$root" <<'PY'
import json, os, sys
db, root = sys.argv[1], sys.argv[2]
dirs = [d.strip() for d in open(os.path.join(root, 'tools/dev/fork-dirs'))
	if d.strip() and not d.startswith('#')]
seen = set()
for e in json.load(open(db)):
	f = os.path.normpath(os.path.join(e['directory'], e['file']))
	rel = os.path.relpath(f, root)
	if not f.endswith(('.cpp', '.cc', '.cxx')) or f in seen:
		continue
	if any(rel == d or rel.startswith(d + '/') for d in dirs):
		seen.add(f)
		sys.stdout.write(f + '\0')
PY
}

# Run the pinned clang-tidy with config $1 on every fork C++ file of the
# database, reporting findings in fork files only; print how many files
# step $2 checked.  The remaining arguments go to clang-tidy.
tidy_fork()
{
	local config=$1 step=$2 db files=() filter
	shift 2
	if [[ ! -x $clang_tidy ]]; then
		echo "SKIP $step: $clang_tidy not found; run tools/dev/deps.sh build"
		return 0
	fi
	db=$(database) || { echo "$step: building the harness failed"; return 1; }
	mapfile -d '' -t files < <(fork_sources "$db")
	echo "$step: ${#files[@]} fork files in $db"
	[[ ${#files[@]} -gt 0 ]] || { echo "$step: no fork file to check"; return 1; }
	filter="^$root/($(fork_dirs | paste -sd '|' -))/"
	"$clang_tidy" -p "$(dirname "$db")" --quiet --config="$config" \
		--header-filter="$filter" "$@" "${files[@]}" 2>&1 |
		grep -v -e '^[0-9]* warnings\? generated\.$' -e '^Suppressed [0-9]* warnings'
	return "${PIPESTATUS[0]}"
}

# The Clang Static Analyzer on every fork file, with deeper limits than
# the check tier's clang-tidy run: four times the exploded-graph nodes and
# twice the loop unrolling.
step_analyzer()
{
	tidy_fork "{Checks: '-*,clang-analyzer-*', WarningsAsErrors: '*'}" analyzer \
		--extra-arg=-Xclang --extra-arg=-analyzer-config \
		--extra-arg=-Xclang --extra-arg=max-nodes=900000 \
		--extra-arg=-Xclang --extra-arg=-analyzer-max-loop --extra-arg=-Xclang --extra-arg=8
}

# clang's thread-safety analysis on every fork file (libc++'s std::mutex
# carries the annotations); says so when no fork file takes a lock.
step_thread_safety()
{
	local d n=0
	tidy_fork "{Checks: '-*,clang-diagnostic-thread-safety*', WarningsAsErrors: '*'}" \
		thread_safety --allow-no-checks --extra-arg=-Wthread-safety || return
	while IFS= read -r d; do
		n=$((n + $(grep -rlE --include='*.[ch]' --include='*.[ch]pp' --include='*.mm' \
			'std::(mutex|shared_mutex|lock_guard|unique_lock|scoped_lock)|SDL_LockMutex' \
			"$root/$d" 2>/dev/null | wc -l)))
	done < <(fork_dirs)
	[[ $n -gt 0 ]] || echo "thread_safety: no fork file takes a lock yet; all compile cleanly with -Wthread-safety"
}

# libstdc++'s debug mode: build the unit tests with _GLIBCXX_DEBUG, beside
# the hardened flags, into build/nightly/glibcxx-debug and run them all
# (scons -k).  Debug mode changes the layout of the standard containers,
# so a test that reads a class of the prebuilt Boost.Test library holding
# one gets the library's layout wrong and crashes: a test failing for that
# is listed in tools/dev/glibcxx-debug-known with the reason, and printed
# rather than failing the step.  Any other failure fails it.
step_glibcxx_debug()
{
	local flags out rc n name reason unexpected=()
	flags="$(python3 "$dev_dir/hardening.py") -D_GLIBCXX_DEBUG"
	out=$("$dev_dir/build.sh" dev builddir=build/nightly/glibcxx-debug \
		compilation_database=build/nightly/glibcxx-debug/compile_commands.json \
		"CPPFLAGS=$flags" -k check 2>&1) && rc=0 || rc=$?
	printf '%s\n' "$out"
	[[ $rc -eq 0 ]] && return 0
	while IFS= read -r n; do
		name=${n##*/}
		name=${name%.check}
		reason=$(sed -n "s/^$name -- //p" "$dev_dir/glibcxx-debug-known")
		if [[ -n $reason ]]; then
			echo "glibcxx_debug: $name fails for a known reason: $reason"
		else
			unexpected+=("$n")
		fi
	done < <(awk '/^Failed node list:/ { f = 1; next } f && /^\t/ { print $1; next } { f = 0 }' \
		<<<"$out")
	[[ $(grep -c '^Failed node list:' <<<"$out") -gt 0 ]] || return 1
	[[ ${#unexpected[@]} -eq 0 ]] || { echo "glibcxx_debug: failed: ${unexpected[*]}"; return 1; }
}

# ThreadSanitizer on the audio-thread tests, on the Mac (TSan fails under
# Rosetta in the container): none exists yet, so a SKIP naming #45, the
# first fork code with an audio thread, which adds them and this step's
# build.  tools/dev/tests/gates_test.sh proves TSan reports a planted race.
step_tsan()
{
	echo "SKIP tsan: no audio-thread tests yet; #45 (sound effects on SDL3_mixer) adds them"
}

# Analyzers the owner has not decided to install: a SKIP until
# tools/dev/toolchain.pins pins them.  $1 names the step, $2 the tool.
not_pinned()
{
	if ! grep -qE "^[a-z]+ $2 " "$dev_dir/toolchain.pins"; then
		echo "SKIP $1: $2 is not pinned in tools/dev/toolchain.pins; installing it is the owner's decision (#59)"
		return 0
	fi
	echo "$1: $2 is pinned but nightly.sh has no runner for it yet"
	return 1
}
step_cppcheck() { not_pinned cppcheck cppcheck; }
step_codeql() { not_pinned codeql codeql; }

# The demo runs: the debug-harness game (DXX_NIGHTLY_GAME, else built),
# the D2 data (DXX_D2_DATA), and a work directory for the frame hash logs,
# homes and outputs, derived from the licensed data (DXX_NIGHTLY_WORK,
# default traces/local/nightly, which git ignores).
work=${DXX_NIGHTLY_WORK:-$root/traces/local/nightly}

# Succeed if DXX_D2_DATA holds the D2 data; else print step $1's SKIP
# and fail.
need_data()
{
	[[ -n ${DXX_D2_DATA-} && -f $DXX_D2_DATA/DESCENT2.HOG ]] && return 0
	echo "SKIP $1: DXX_D2_DATA names no directory holding DESCENT2.HOG (docs/agents/game-data.md)"
	return 1
}

# Print the game to run, building it unless DXX_NIGHTLY_GAME names one.
game_binary()
{
	if [[ -n ${DXX_NIGHTLY_GAME-} ]]; then
		printf '%s\n' "$DXX_NIGHTLY_GAME"
		return
	fi
	build_harness >&2 || return
	printf '%s\n' "$root/$harness_dir/d2x-rebirth/d2x-rebirth"
}

# Start demo run $1 of game $2 in the background, windowless as
# docs/agents/game-data.md says, from a fresh home; set run_pid (run.sh's),
# game (the binary), frames (the frame hash log) and out (its output).
demo_start()
{
	game=$2
	local home=$work/$1-home
	rm -rf "$home" && mkdir -p "$home" || return
	frames=$work/$1.frames out=$work/$1.out
	rm -f "$frames"
	ASAN_OPTIONS=abort_on_error=0 UBSAN_OPTIONS=halt_on_error=1:abort_on_error=0 \
		DXX_DBG_HIDDEN=1 DXX_DBG_FIXED_FRAMETIME=30 DXX_DBG_FRAMEHASH=$frames \
		D2X_REBIRTH_HOME=$home "$dev_dir/run.sh" "$2" -hogdir "$DXX_D2_DATA" \
		-window -pilot harness -nomovies -autodemo > "$out" 2>&1 &
	run_pid=$!
}

# Print how many demo playbacks the frame hash log holds whole: each
# section but the last, which the run's end may cut.
demos_played()
{
	local n
	n=$(grep -c '^# demo playback' "$frames" 2>/dev/null)
	echo $(( ${n:-0} > 0 ? n - 1 : 0 ))
}

# End demo run $1 and fail on what makes it red: the game ending by itself
# ($2 set), a crash report, focus taken, a hardening, sanitizer or
# assertion message in its output, or an unclean end.  The run ends by TERM
# to the game itself, not to run.sh, so run.sh returns the game's own exit
# status, which must be 0: a hidden harness run quits on TERM without
# asking (the "Abort game?" box it would show is not on screen).  TERM is
# sent again every second: the game drops a quit that arrives while a demo
# loads (event_flush when the game window opens).  So a
# crash at shutdown is a non-zero status and a game still running
# RUN_KILL_AFTER seconds (run.sh's grace, default 10) later hangs; it then
# gets TERM through run.sh, which kills it.  The output is named on failure.
demo_finish()
{
	local rc=0 status hits pid grace=${RUN_KILL_AFTER:-10} i
	if [[ -n $2 ]]; then
		wait "$run_pid"
		echo "$1: the game ended by itself, status $?, before the run was over"
		rc=1
	else
		pid=$(game_pid "$run_pid" "$game")
		for ((i = 0; i < grace * 10; i++)); do
			[[ -z $pid || $((i % 10)) -ne 0 ]] || kill -TERM "$pid" 2>/dev/null
			kill -0 "$run_pid" 2>/dev/null || break
			sleep 0.1
		done
		if kill -0 "$run_pid" 2>/dev/null; then
			echo "$1: the game still runs $grace s after TERM; ending it through run.sh"
			kill -TERM "$run_pid" 2>/dev/null
			rc=1
		fi
		wait "$run_pid"
		status=$?
		[[ $status -eq 0 ]] || { echo "$1: the game ended with status $status after TERM, not 0"; rc=1; }
	fi
	hits=$(grep -E 'crash report|no-window\.sh:|still runs .*; killing it|Hardening assertion|_GLIBCXX|Assertion .* failed|runtime error:|Sanitizer' "$out")
	[[ -z $hits ]] || { printf '%s\n' "$hits"; rc=1; }
	[[ $rc -eq 0 ]] || echo "$1: output in $out"
	return "$rc"
}

# D2 demo playback in the hardened build: at least one whole demo within
# DXX_NIGHTLY_DEMO_TIMEOUT seconds (default 600).
step_hardened_demo()
{
	local bin early='' played=0 rc=0 timeout=${DXX_NIGHTLY_DEMO_TIMEOUT:-600} deadline
	need_data hardened_demo || return 0
	bin=$(game_binary) || { echo "hardened_demo: building the harness failed"; return 1; }
	demo_start hardened_demo "$bin" || return 1
	deadline=$((EPOCHSECONDS + timeout))
	while [[ $EPOCHSECONDS -lt $deadline ]]; do
		played=$(demos_played)
		[[ $played -ge 1 ]] && break
		kill -0 "$run_pid" 2>/dev/null || { early=1; break; }
		sleep 0.5
	done
	demo_finish hardened_demo "$early" || rc=1
	played=$(demos_played)
	echo "hardened_demo: $played complete demo playbacks"
	[[ $played -ge 1 ]] || { echo "hardened_demo: no complete demo playback in $timeout s"; rc=1; }
	return "$rc"
}

# Print the pid of game $2 among the descendants of process $1: the one
# whose command is the game, or a script interpreter running it.
game_pid()
{
	ps -A -o pid=,ppid=,args= | awk -v top="$1" -v bin="$2" '
		{ pid[NR] = $1; ppid[NR] = $2; a1[NR] = $3; a2[NR] = $4 }
		END {
			tree[top] = 1
			for (more = 1; more; ) {
				more = 0
				for (i = 1; i <= NR; i++)
					if (!(pid[i] in tree) && (ppid[i] in tree)) { tree[pid[i]] = 1; more = 1 }
			}
			for (i = 1; i <= NR; i++)
				if ((pid[i] in tree) && (a1[i] == bin || a2[i] == bin)) { print pid[i]; exit }
		}'
}

# Endurance: demo playback for DXX_NIGHTLY_ENDURANCE seconds (--endurance,
# default 3600), sampling the game's RSS every DXX_NIGHTLY_SAMPLE_INTERVAL
# seconds (default 10) into endurance.rss; rss_trend.py then fails growth
# over DXX_NIGHTLY_MAX_SLOPE MiB/h (default 8) after DXX_NIGHTLY_WARMUP
# seconds (default 120), fitted to the minima of DXX_NIGHTLY_WINDOW-second
# windows (default 60), from at least DXX_NIGHTLY_MIN_SAMPLES samples
# (default 10).
step_endurance()
{
	local bin early='' rc=0 start now el pid rss samples=$work/endurance.rss
	local interval=${DXX_NIGHTLY_SAMPLE_INTERVAL:-10}
	need_data endurance || return 0
	bin=$(game_binary) || { echo "endurance: building the harness failed"; return 1; }
	demo_start endurance "$bin" || return 1
	: > "$samples"
	echo "endurance: $DXX_NIGHTLY_ENDURANCE s, RSS every $interval s into $samples"
	# The clock starts with the game, which run.sh starts after its checks.
	for ((el = 0; el < ${DXX_NIGHTLY_DEMO_TIMEOUT:-600} * 10; el++)); do
		pid=$(game_pid "$run_pid" "$bin")
		if [[ -n $pid ]] || ! kill -0 "$run_pid" 2>/dev/null; then
			break
		fi
		sleep 0.1
	done
	start=${EPOCHREALTIME/[.,]/}
	while :; do
		now=${EPOCHREALTIME/[.,]/}
		el=$(( now - start ))
		kill -0 "$run_pid" 2>/dev/null || { early=1; break; }
		[[ -n $pid ]] && kill -0 "$pid" 2>/dev/null || pid=$(game_pid "$run_pid" "$bin")
		rss=${pid:+$(ps -o rss= -p "$pid" | tr -d ' ')}
		[[ -z $rss ]] || printf '%d.%06d %s\n' $((el / 1000000)) $((el % 1000000)) "$rss" >> "$samples"
		[[ $el -ge $((DXX_NIGHTLY_ENDURANCE * 1000000)) ]] && break
		sleep "$interval"
	done
	demo_finish endurance "$early" || rc=1
	echo "endurance: $(demos_played) complete demo playbacks"
	[[ $(demos_played) -ge 1 ]] || { echo "endurance: no complete demo playback"; rc=1; }
	python3 "$dev_dir/rss_trend.py" --warmup "${DXX_NIGHTLY_WARMUP:-120}" \
		--window "${DXX_NIGHTLY_WINDOW:-60}" --min-samples "${DXX_NIGHTLY_MIN_SAMPLES:-10}" \
		--max-slope "${DXX_NIGHTLY_MAX_SLOPE:-8}" "$samples" || rc=1
	return "$rc"
}

leg='' only='' endurance=''
while [[ $# -gt 0 ]]; do
	case $1 in
	--leg) [[ $# -ge 2 && $2 =~ ^(native|container)$ ]] || usage; leg=$2; shift ;;
	--steps) [[ $# -ge 2 ]] || usage; only=$2; shift ;;
	--endurance) [[ $# -ge 2 && $2 =~ ^[1-9][0-9]*$ ]] || usage; endurance=$2; shift ;;
	*) usage ;;
	esac
	shift
done
export DXX_NIGHTLY_ENDURANCE=${endurance:-${DXX_NIGHTLY_ENDURANCE:-3600}}

# Run leg $1's steps (those named by --steps, if given); fail if any did.
run_leg()
{
	local -n all=${1}_steps
	local s steps=() failed=() start
	if [[ -n $only ]]; then
		for s in ${only//,/ }; do
			[[ " ${native_steps[*]} ${container_steps[*]} " == *" $s "* ]] || usage
			[[ " ${all[*]} " == *" $s "* ]] && steps+=("$s")
		done
	else
		steps=("${all[@]}")
	fi
	for s in "${steps[@]+"${steps[@]}"}"; do
		echo "== $s"
		start=$EPOCHSECONDS
		"step_$s" || failed+=("$s")
		echo "-- $s: $((EPOCHSECONDS - start)) s"
	done
	if [[ ${#failed[@]} -ne 0 ]]; then
		echo "nightly.sh: failed: ${failed[*]}"
		return 1
	fi
	echo "nightly.sh: $1 leg passed"
}

if [[ -n $leg ]]; then
	run_leg "$leg"
	exit
fi

# Both legs at once; each one's output is printed whole, native first, and
# into the log.
log=$root/build/nightly/nightly.log
dev_tm_exclude "$root/build" >/dev/null
if ! mkdir -p "${log%/*}" || ! : > "$log"; then
	echo "nightly.sh: cannot write $log" >&2
	exit 1
fi
legs=$(mktemp -d "${TMPDIR:-/tmp}/nightly.XXXXXX") || exit 1
trap 'rm -rf "$legs"' EXIT
args=(--endurance "$DXX_NIGHTLY_ENDURANCE")
[[ -z $only ]] || args+=(--steps "$only")
echo "nightly.sh: log: $log" | tee -a "$log"
"$0" --leg native "${args[@]}" > "$legs/native" 2>&1 &
native=$!
"$dev_dir/container.sh" run --clean tools/dev/nightly.sh --leg container "${args[@]}" \
	> "$legs/container" 2>&1 &
container=$!
failed=()
wait "$native" || failed+=(native)
{ echo "== nightly: native leg"; cat "$legs/native"; } | tee -a "$log"
wait "$container" || failed+=(container)
{ echo "== nightly: container leg"; cat "$legs/container"; } | tee -a "$log"
if [[ ${#failed[@]} -ne 0 ]]; then
	echo "nightly.sh: failed legs: ${failed[*]}; log: $log" | tee -a "$log" >&2
	exit 1
fi
echo "nightly.sh: passed; log: $log" | tee -a "$log"

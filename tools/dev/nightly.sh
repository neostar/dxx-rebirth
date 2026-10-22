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
#   thread_safety   clang's -Wthread-safety on the same files, with SDL's
#                   lock annotations on, proven on each kind of lock taken
#   tsan            ThreadSanitizer on the audio-thread tests (none yet)
#   hardened_demo   D2 demo playback in the hardened debug-harness build,
#                   windowless through run.sh; a hardening trap, a
#                   sanitizer report, a crash report or an unclean end
#                   (a non-zero status or a hang after TERM) fails it
#   endurance       demo playback for an hour (--endurance), sampling the
#                   game's memory: it must stay flat (rss_trend.py); a run
#                   under 30 min after the warm-up is too short to judge
#                   and fails; the 8 MiB/h limit is provisional until
#                   calibrated from the numbers each run logs
#   full_run        before both legs (no --leg): the full run, ci.sh --clean
#                   (ADR 6); a red one holds merges (orchestrating.md)
#   container leg:
#   glibcxx_debug   the unit tests built with libstdc++'s debug mode
#                   (_GLIBCXX_DEBUG: checked iterators) and run
#   fanalyzer       GCC's -fanalyzer on every fork C file
# --steps runs only the steps named (of the leg's); --endurance sets the
# endurance run's length.  A missing input (the pinned clang-tidy, the D2
# data in DXX_D2_DATA, a build) fails its step: a scheduled run must not
# stay green on a step that checked nothing; only tsan, which waits on
# #45, is a SKIP.
# For the tests only: DXX_NIGHTLY_DATABASE names a compilation database to
# analyse and DXX_NIGHTLY_GAME a game to run instead of building the
# harness; DXX_NIGHTLY_WORK the demo runs' work directory;
# DXX_NIGHTLY_DEMO_TIMEOUT how long a demo may take (600 s);
# DXX_NIGHTLY_SAMPLE_INTERVAL the endurance run's sampling interval (10 s)
# and DXX_NIGHTLY_TREND_ARGS rss_trend.py's options.
# Exit status 1 names the failed steps (or legs), 2 is a usage error.
# TERM, INT or HUP ends the run (the demo run through run.sh, both legs)
# leaving none of it running, with status 128 + the signal's number.
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

native_steps=(analyzer thread_safety tsan hardened_demo endurance)
container_steps=(glibcxx_debug fanalyzer)

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

# Set the caller's array dirs to the fork directories, as the build's
# strict warnings read tools/dev/fork-dirs (fork_warnings.py: one parser);
# fail, saying why, when it lists none.
fork_dirs()
{
	local out
	out=$(python3 "$dev_dir/fork_warnings.py" --dirs) || return
	mapfile -t dirs <<<"$out"
}

# Print, NUL-separated, every C++ source file of database $1 that lies in
# a fork directory (the caller's dirs, from fork_dirs), as an absolute
# path, each once.
fork_sources()
{
	python3 - "$1" "$root" "${dirs[@]}" <<'PY'
import json, os, sys
db, root, dirs = sys.argv[1], sys.argv[2], sys.argv[3:]
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

# Set dirs to the fork directories, db to the compilation database and
# files to its fork C++ files, for step $1; fail, saying why, without the
# pinned clang-tidy, a fork directory, the database or a fork file in it.
# A missing tool fails: a scheduled run must not stay green on a step that
# checked nothing.
fork_files()
{
	if [[ ! -x $clang_tidy ]]; then
		echo "$1: $clang_tidy not found; run tools/dev/deps.sh build"
		return 1
	fi
	fork_dirs || { echo "$1: no fork directory"; return 1; }
	db=$(database) || { echo "$1: building the harness failed"; return 1; }
	mapfile -d '' -t files < <(fork_sources "$db")
	echo "$1: ${#files[@]} fork files in $db"
	[[ ${#files[@]} -gt 0 ]] || { echo "$1: no fork file to check"; return 1; }
}

# Run the pinned clang-tidy with config $1 on files $3... of database $2,
# reporting findings in fork files only (the caller's dirs, from
# fork_dirs).  Arguments after -- go to clang-tidy.
tidy()
{
	local config=$1 dbfile=$2 filter args=() srcs=()
	shift 2
	while [[ $# -gt 0 && $1 != -- ]]; do srcs+=("$1"); shift; done
	[[ $# -gt 0 ]] && shift
	args=("$@")
	filter="^$root/($(printf '%s\n' "${dirs[@]}" | paste -sd '|' -))/"
	"$clang_tidy" -p "$(dirname "$dbfile")" --quiet --config="$config" \
		--header-filter="$filter" "${args[@]+"${args[@]}"}" "${srcs[@]}" 2>&1 |
		grep -v -e '^[0-9]* warnings\? generated\.$' -e '^Suppressed [0-9]* warnings'
	return "${PIPESTATUS[0]}"
}

# The Clang Static Analyzer on every fork file, with deeper limits than
# the check tier's clang-tidy run: four times the exploded-graph nodes and
# twice the loop unrolling.
step_analyzer()
{
	local dirs db files=()
	fork_files analyzer || return
	tidy "{Checks: '-*,clang-analyzer-*', WarningsAsErrors: '*'}" "$db" "${files[@]}" -- \
		--extra-arg=-Xclang --extra-arg=-analyzer-config \
		--extra-arg=-Xclang --extra-arg=max-nodes=900000 \
		--extra-arg=-Xclang --extra-arg=-analyzer-max-loop --extra-arg=-Xclang --extra-arg=8
}

# clang's thread-safety analysis on every fork file, with the annotations
# on: libc++'s std::mutex carries them, SDL's mutex functions only with
# SDL_THREAD_SAFETY_ANALYSIS.  For each kind of lock a fork file takes, a
# probe compiled with that file's flags writes a guarded member unlocked,
# and the step fails when the analysis misses it: a lock whose annotations
# are off would pass unchecked.  Says so when no fork file takes a lock.
thread_safety_config="{Checks: '-*,clang-diagnostic-thread-safety*', WarningsAsErrors: '*'}"
thread_safety_args=(--allow-no-checks --extra-arg=-Wthread-safety
	--extra-arg=-DSDL_THREAD_SAFETY_ANALYSIS)
step_thread_safety()
{
	local dirs db files=() kind pattern user probes rc=0 locks=()
	fork_files thread_safety || return
	tidy "$thread_safety_config" "$db" "${files[@]}" -- "${thread_safety_args[@]}" || rc=1
	probes=$(mktemp -d "${TMPDIR:-/tmp}/thread_safety.XXXXXX") || return 1
	for kind in std SDL; do
		case $kind in
		std) pattern='std::(mutex|shared_mutex|recursive_mutex|timed_mutex)' ;;
		SDL) pattern='SDL_(LockMutex|LockRWLockFor|CreateMutex|CreateRWLock)' ;;
		esac
		user=$(grep -lE "$pattern" "${files[@]}" | head -n 1)
		[[ -n $user ]] || continue
		locks+=("$kind (${user#"$root"/})")
		lock_probe "$kind" "$user" "$probes" || rc=1
	done
	rm -rf "$probes"
	if [[ ${#locks[@]} -eq 0 ]]; then
		echo "thread_safety: no fork file takes a lock yet; all compile cleanly with -Wthread-safety"
	else
		echo "thread_safety: locks checked, the analysis proven on each: ${locks[*]}"
	fi
	return "$rc"
}

# Succeed if the thread-safety analysis reports an unlocked write to a
# member guarded by a lock of kind $1 (std or SDL), compiled in directory
# $3 with the flags and the SDL includes of fork file $2.
lock_probe()
{
	local kind=$1 user=$2 dir=$3 out
	{
		[[ $kind == SDL ]] && grep -E '^#[[:space:]]*include[[:space:]]*[<"]SDL' "$user"
		[[ $kind == std ]] && echo '#include <mutex>'
		echo 'struct thread_safety_probe'
		echo '{'
		case $kind in
		std) printf '\tstd::mutex lock;\n\tint value __attribute__((guarded_by(lock))){0};\n' ;;
		SDL) printf '\tdecltype(SDL_CreateMutex()) lock{nullptr};\n\tint value SDL_GUARDED_BY(lock){0};\n' ;;
		esac
		printf '\tvoid bump() { ++value; }\n};\n'
	} > "$dir/$kind.cpp"
	python3 - "$db" "$user" "$dir/$kind.cpp" "$dir/compile_commands.json" <<'PY' || return 1
import json, os, shlex, sys
db, user, probe, out = sys.argv[1:]
for e in json.load(open(db)):
	if os.path.normpath(os.path.join(e['directory'], e['file'])) == user:
		args = e.get('arguments') or shlex.split(e['command'])
		args = [probe if a == e['file'] or a == user else a for a in args]
		json.dump([dict(directory=e['directory'], file=probe, arguments=args)], open(out, 'w'))
		break
PY
	out=$(tidy "$thread_safety_config" "$dir/compile_commands.json" "$dir/$kind.cpp" -- \
		"${thread_safety_args[@]}")
	if [[ $out == *"requires holding mutex 'lock'"* ]]; then
		return 0
	fi
	echo "thread_safety: the annotations of $kind locks are off: an unlocked write to a guarded member passed with the flags of $user"
	return 1
}

# libstdc++'s debug mode: build the unit tests with _GLIBCXX_DEBUG, beside
# the hardened flags, into build/nightly/glibcxx-debug and run them all
# (scons -k).  Debug mode changes the layout of the standard containers,
# so a test that reads a class of the prebuilt Boost.Test library holding
# one gets the library's layout wrong and crashes: such a test is listed in
# tools/dev/glibcxx-debug-known as "NAME -- FAILURE -- reason", and passes
# only when its run (NAME.check, not its build) fails with FAILURE, a fixed
# string, in the output.  Any other failure fails the step, as does a
# listed test that no longer fails (the entry is stale) or a failed-node
# list that disagrees with SCons's failed target count.
step_glibcxx_debug()
{
	local flags out rc n name entry failure count known=() unexpected=() matched=() stale=()
	flags="$(python3 "$dev_dir/hardening.py") -D_GLIBCXX_DEBUG"
	out=$("$dev_dir/build.sh" dev builddir=build/nightly/glibcxx-debug \
		compilation_database=build/nightly/glibcxx-debug/compile_commands.json \
		"CPPFLAGS=$flags" -k check 2>&1) && rc=0 || rc=$?
	printf '%s\n' "$out"
	# shellcheck disable=SC2001  # a regex, which ${out//...} cannot express
	out=$(sed $'s/\x1b\\[[0-9;]*m//g' <<<"$out")
	mapfile -t known < <(grep -v -e '^#' -e '^$' "$dev_dir/glibcxx-debug-known")
	while IFS= read -r n; do
		name=${n##*/}
		entry=$(printf '%s\n' "${known[@]+"${known[@]}"}" | awk -F ' -- ' -v t="${name%.check}" '$1 == t')
		failure=$(awk -F ' -- ' '{ print $2 }' <<<"$entry")
		if [[ $n == *.check && -n $failure ]] && grep -qF -- "$failure" <<<"$out"; then
			echo "glibcxx_debug: ${name%.check} fails as listed: $(awk -F ' -- ' '{ print $3 }' <<<"$entry")"
			matched+=("${name%.check}")
		else
			unexpected+=("$n")
		fi
	done < <(awk '/^Failed node list:/ { f = 1; next } f && /^\t/ { print $1; next } { f = 0 }' \
		<<<"$out")
	if [[ $rc -ne 0 ]]; then
		count=$(sed -n 's/^Failed target count: total=\([0-9]*\).*/\1/p' <<<"$out")
		if [[ -z $count || $count -ne $((${#matched[@]} + ${#unexpected[@]})) || $count -eq 0 ]]; then
			echo "glibcxx_debug: the build failed with ${count:-no} failed targets counted and $((${#matched[@]} + ${#unexpected[@]})) in the failed node list"
			return 1
		fi
	fi
	for entry in "${known[@]+"${known[@]}"}"; do
		name=${entry%% -- *}
		[[ " ${matched[*]-} " == *" $name "* ]] || stale+=("$name")
	done
	[[ ${#unexpected[@]} -eq 0 ]] || echo "glibcxx_debug: failed: ${unexpected[*]}"
	[[ ${#stale[@]} -eq 0 ]] ||
		echo "glibcxx_debug: listed in glibcxx-debug-known but did not fail as listed: ${stale[*]}"
	[[ ${#unexpected[@]} -eq 0 && ${#stale[@]} -eq 0 ]]
}

# GCC's -fanalyzer on every C file in the fork directories, with the
# pinned SDL3 headers (GCC documents it as suitable for C only; the Clang
# Static Analyzer covers the C++).  Without GCC (the Mac's gcc is clang)
# the step fails: it belongs to the container leg.
step_fanalyzer()
{
	local files=() f rc=0 dirs
	if ! gcc --version 2>/dev/null | grep -q 'Free Software Foundation'; then
		echo "fanalyzer: GCC not found (gcc is $(command -v gcc || echo missing)); it runs in the container leg"
		return 1
	fi
	fork_dirs || { echo "fanalyzer: no fork directory"; return 1; }
	mapfile -t files < <(cd "$root" && find "${dirs[@]}" -name '*.c' -type f | sort)
	echo "fanalyzer: ${#files[@]} fork C files"
	[[ ${#files[@]} -gt 0 ]] || { echo "fanalyzer: no fork C file"; return 1; }
	for f in "${files[@]}"; do
		gcc -std=c11 -fanalyzer -Werror -c -o /dev/null -isystem "$dev_deps_prefix/include" \
			"$root/$f" || rc=1
	done
	return "$rc"
}

# ThreadSanitizer on the audio-thread tests, on the Mac (TSan fails under
# Rosetta in the container): none exists yet, so a SKIP naming #45, the
# first fork code with an audio thread, which adds them and this step's
# build.  tools/dev/tests/gates_test.sh proves TSan reports a planted race.
step_tsan()
{
	echo "SKIP tsan: no audio-thread tests yet; #45 (sound effects on SDL3_mixer) adds them"
}

# The demo runs: the debug-harness game (DXX_NIGHTLY_GAME, else built),
# the D2 data (DXX_D2_DATA), and a work directory for the frame hash logs,
# homes and outputs, derived from the licensed data (DXX_NIGHTLY_WORK,
# default traces/local/nightly, which git ignores).
work=${DXX_NIGHTLY_WORK:-$root/traces/local/nightly}

# Succeed if DXX_D2_DATA holds the D2 data; else fail step $1, saying so.
need_data()
{
	[[ -n ${DXX_D2_DATA-} && -f $DXX_D2_DATA/DESCENT2.HOG ]] && return 0
	echo "$1: DXX_D2_DATA names no directory holding DESCENT2.HOG (docs/agents/game-data.md)"
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
# loads (event_flush when the game window opens).  So a crash at shutdown
# is a non-zero status, and a game still running RUN_KILL_AFTER seconds
# (run.sh's grace, default 10) later hangs; it then gets TERM through
# run.sh, which kills it.  The grace watches the game, not run.sh: after
# the game no-window.sh still takes a sample (slow under emulation) and
# run.sh looks for crash reports, both bounded by run.sh.  A game not
# found among run.sh's processes fails at once.  The output is named on
# failure.
demo_finish()
{
	local rc=0 status hits pid grace=${RUN_KILL_AFTER:-10} i
	if [[ -n $2 ]]; then
		wait "$run_pid"
		echo "$1: the game ended by itself, status $?, before the run was over"
		rc=1
	else
		pid=$(game_pid "$run_pid" "$game")
		if [[ -z $pid ]]; then
			echo "$1: no process of $game found under run.sh; ending the run through run.sh"
			end_run_sh "$1"
			rc=1
		else
			for ((i = 0; i < grace * 10; i++)); do
				[[ $((i % 10)) -ne 0 ]] || kill -TERM "$pid" 2>/dev/null
				alive "$pid" || break
				sleep 0.1
			done
			if alive "$pid"; then
				echo "$1: the game still runs $grace s after TERM; ending it through run.sh"
				end_run_sh "$1"
				rc=1
			fi
		fi
		wait "$run_pid"
		status=$?
		[[ $status -eq 0 ]] || { echo "$1: the game ended with status $status after TERM, not 0"; rc=1; }
	fi
	run_pid=''
	hits=$(grep -E 'crash report|no-window\.sh:|still runs .*; killing it|Hardening assertion|_GLIBCXX|Assertion .* failed|runtime error:|Sanitizer' "$out")
	[[ -z $hits ]] || { printf '%s\n' "$hits"; rc=1; }
	[[ $rc -eq 0 ]] || echo "$1: output in $out"
	return "$rc"
}

# End demo run $1's run.sh: TERM to it every second until it ends, and
# what of the run still runs once its teardown could be over (run.sh's
# grace and 15 s) killed, saying so.  A TERM can be dropped: one that lands
# in the child run.sh is starting, before its exec, runs run.sh's trap in
# that child, so the run would go on.
end_run_sh()
{
	local i pids
	pids=$(tree_pids "$run_pid")
	for ((i = 0; i < (${RUN_KILL_AFTER:-10} + 15) * 10; i++)); do
		kill -0 "$run_pid" 2>/dev/null || return 0
		[[ $((i % 10)) -ne 0 ]] || kill -TERM "$run_pid" 2>/dev/null
		sleep 0.1
	done
	echo "$1: run.sh still runs after TERM; killing the run"
	# shellcheck disable=SC2046,SC2086  # one pid per word
	kill -KILL $(tree_pids "$run_pid") $pids 2>/dev/null
	return 0
}

# D2 demo playback in the hardened build: at least one whole demo within
# DXX_NIGHTLY_DEMO_TIMEOUT seconds (default 600).
step_hardened_demo()
{
	local bin early='' played=0 rc=0 timeout=${DXX_NIGHTLY_DEMO_TIMEOUT:-600} deadline
	need_data hardened_demo || return
	bin=$(game_binary) || { echo "hardened_demo: building the harness failed"; return 1; }
	demo_start hardened_demo "$bin" || return 1
	deadline=$((EPOCHSECONDS + timeout))
	while [[ $EPOCHSECONDS -lt $deadline ]]; do
		played=$(demos_played)
		[[ $played -ge 1 ]] && break
		kill -0 "$run_pid" 2>/dev/null || { early=1; break; }
		sleep 0.5 & wait $!
	done
	demo_finish hardened_demo "$early" || rc=1
	played=$(demos_played)
	echo "hardened_demo: $played complete demo playbacks"
	[[ $played -ge 1 ]] || { echo "hardened_demo: no complete demo playback in $timeout s"; rc=1; }
	return "$rc"
}

# Succeed if process $1 runs: it exists and is not a zombie, which still
# answers kill -0 until its parent reaps it.
alive()
{
	local stat
	stat=$(ps -o stat= -p "$1" 2>/dev/null) || return 1
	[[ -n $stat && $stat != Z* ]]
}

# Print the pids of process $1 and its descendants.
tree_pids()
{
	ps -A -o pid=,ppid= | awk -v top="$1" '
		{ parent[$1] = $2 }
		END { for (p in parent) for (q = p; q > 1; q = parent[q]) if (q == top) { print p; break } }'
}

# On signal $1 (exit status $2), end the demo run, if one runs, through
# run.sh's own TERM handling (TERM to the game, KILL after its grace, its
# crash report check), and exit.  What still runs once that teardown
# could be over (the grace and 15 s) is killed.
end_demo_run()
{
	local pids i
	trap '' TERM INT HUP
	echo "nightly.sh: $1; ending the run"
	if [[ -n $run_pid ]] && kill -0 "$run_pid" 2>/dev/null; then
		pids=$(tree_pids "$run_pid")
		kill -TERM "$run_pid" 2>/dev/null
		for ((i = 0; i < (${RUN_KILL_AFTER:-10} + 15) * 10; i++)); do
			kill -0 "$run_pid" 2>/dev/null || break
			sleep 0.1
		done
		if kill -0 "$run_pid" 2>/dev/null; then
			echo "nightly.sh: run.sh still runs; killing the run"
			# shellcheck disable=SC2046,SC2086  # one pid per word
			kill -KILL $(tree_pids "$run_pid") $pids 2>/dev/null
		fi
		wait "$run_pid"
	fi
	exit "$2"
}

# Print the pid of game $2 among the descendants of process $1: the one
# whose command is the game, or a script interpreter running it, and whose
# parent's is not.  A child the game forks carries its command line until
# it execs (slow under emulation), and ps may list it first: pids wrap.
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
				if ((pid[i] in tree) && (a1[i] == bin || a2[i] == bin)) game[pid[i]] = 1
			for (i = 1; i <= NR; i++)
				if ((pid[i] in game) && !(ppid[i] in game)) { print pid[i]; exit }
		}'
}

# Endurance: demo playback for --endurance seconds (default 3600), sampling
# the game's memory (footprint.py: its physical footprint on macOS) every
# 10 s into endurance.mem; rss_trend.py with its defaults then fails
# growth over 8 MiB/h after a 300 s warm-up, or a run too short to judge,
# printing the windows, slope and noise into the log.
step_endurance()
{
	local bin early='' rc=0 start now el pid kib samples=$work/endurance.mem
	local interval=${DXX_NIGHTLY_SAMPLE_INTERVAL:-10} trend_args=()
	read -ra trend_args <<<"${DXX_NIGHTLY_TREND_ARGS-}"
	need_data endurance || return
	bin=$(game_binary) || { echo "endurance: building the harness failed"; return 1; }
	demo_start endurance "$bin" || return 1
	: > "$samples"
	echo "endurance: $endurance s, memory every $interval s into $samples"
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
		kib=${pid:+$(python3 "$dev_dir/footprint.py" "$pid" 2>/dev/null)}
		[[ -z $kib ]] || printf '%d.%06d %s\n' $((el / 1000000)) $((el % 1000000)) "$kib" >> "$samples"
		[[ $el -ge $((endurance * 1000000)) ]] && break
		sleep "$interval" & wait $!
	done
	demo_finish endurance "$early" || rc=1
	echo "endurance: $(demos_played) complete demo playbacks"
	[[ $(demos_played) -ge 1 ]] || { echo "endurance: no complete demo playback"; rc=1; }
	python3 "$dev_dir/rss_trend.py" "${trend_args[@]+"${trend_args[@]}"}" "$samples" || rc=1
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
endurance=${endurance:-3600}

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

# A signal to nightly.sh ends the run with it: nothing of it is left
# running.  The demo runs' sleeps are waited for, so the trap runs at once.
run_pid=''
if [[ -n $leg ]]; then
	trap 'end_demo_run TERM 143' TERM
	trap 'end_demo_run INT 130' INT
	trap 'end_demo_run HUP 129' HUP
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
native='' container='' full=''
# On signal $1 (exit status $2), pass TERM to both legs, wait for them to
# end (each bounds its own end), print their output so far, and exit.
end_legs()
{
	local p
	trap '' TERM INT HUP
	echo "nightly.sh: $1; ending the run" | tee -a "$log"
	for p in $full $native $container; do
		kill -TERM "$p" 2>/dev/null
	done
	for p in $full $native $container; do
		wait "$p"
	done
	[[ ! -s $legs/full ]] || { echo "== nightly: full run"; cat "$legs/full"; } | tee -a "$log"
	{ echo "== nightly: native leg"; cat "$legs/native"; } 2>/dev/null | tee -a "$log"
	{ echo "== nightly: container leg"; cat "$legs/container"; } 2>/dev/null | tee -a "$log"
	exit "$2"
}
trap 'end_legs TERM 143' TERM
trap 'end_legs INT 130' INT
trap 'end_legs HUP 129' HUP
failed=()
full_run=1 legs_too=1
if [[ -n $only ]]; then
	[[ ",$only," == *,full_run,* ]] || full_run=0
	only=$(tr ',' '\n' <<<"$only" | grep -vx full_run | paste -sd , -)
	[[ -n $only ]] || legs_too=0
fi
args=(--endurance "$endurance")
[[ -z $only ]] || args+=(--steps "$only")
echo "nightly.sh: log: $log" | tee -a "$log"
if [[ $full_run -eq 1 ]]; then
	echo "== nightly: full run (ci.sh --clean)" | tee -a "$log"
	# ci.sh writes its own log; its output also goes into this one.
	"$dev_dir/ci.sh" --clean > "$legs/full" 2>&1 &
	full=$!
	wait "$full" || failed+=(full_run)
	full=''
	tee -a "$log" < "$legs/full"
fi
if [[ $legs_too -eq 0 ]]; then
	if [[ ${#failed[@]} -ne 0 ]]; then
		echo "nightly.sh: failed legs: ${failed[*]}; log: $log" | tee -a "$log" >&2
		exit 1
	fi
	echo "nightly.sh: passed; log: $log" | tee -a "$log"
	exit 0
fi
"$0" --leg native "${args[@]}" > "$legs/native" 2>&1 &
native=$!
"$dev_dir/container.sh" run --clean tools/dev/nightly.sh --leg container "${args[@]}" \
	> "$legs/container" 2>&1 &
container=$!
wait "$native" || failed+=(native)
{ echo "== nightly: native leg"; cat "$legs/native"; } | tee -a "$log"
wait "$container" || failed+=(container)
{ echo "== nightly: container leg"; cat "$legs/container"; } | tee -a "$log"
if [[ ${#failed[@]} -ne 0 ]]; then
	echo "nightly.sh: failed legs: ${failed[*]}; log: $log" | tee -a "$log" >&2
	exit 1
fi
echo "nightly.sh: passed; log: $log" | tee -a "$log"

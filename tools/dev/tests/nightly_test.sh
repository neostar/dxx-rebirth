#!/usr/bin/env bash
# Command-line behaviour of tools/dev/nightly.sh, the nightly tier (ADR 5):
# each step goes red on a planted defect, with the real tools where they
# are cheap (clang-tidy) and fakes for the builds and the game.
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"
GATES="$DEV_DIR/tests/gates"

# Print a fork repository (lib.sh) whose fork directory src/ holds a clean
# src/anchor.cpp and file $1 as src/plant.cpp, both in its compilation
# database.
plant_repo()
{
	local r
	r=$(fork_repo) || die "fork_repo failed"
	printf 'int anchor() { return 0; }\n' > "$r/src/anchor.cpp"
	cp "$1" "$r/src/plant.cpp"
	add_database_entry "$r" src/plant.cpp
	printf '%s\n' "$r"
}

# Run the native leg's step $2 of fork repository $1 on its compilation
# database, with the remaining arguments.
native_step()
{
	DXX_NIGHTLY_DATABASE=$1/build/dev/compile_commands.json \
		"$1/tools/dev/nightly.sh" --leg native --steps "$2" "${@:3}" 2>&1
}

# Skip unless the pinned clang-tidy runs, on the Mac, whose database
# names Apple clang and libc++ (the native leg).
need_native_tidy()
{
	[[ $dev_os == darwin ]] || skip "the native leg's analyzers run on the Mac"
	command -v "$(dev_llvm_tool clang-tidy)" >/dev/null ||
		skip "clang-tidy not found; tools/dev/deps.sh installs the LLVM tools"
}

# Test that an unknown option or step is a usage error.
test_usage_errors()
{
	local rc
	"$DEV_DIR/nightly.sh" --bogus >/dev/null 2>&1 && rc=0 || rc=$?
	assert_eq "$rc" 2 "--bogus"
	"$DEV_DIR/nightly.sh" --leg native --steps bogus >/dev/null 2>&1 && rc=0 || rc=$?
	assert_eq "$rc" 2 "--steps bogus"
}

# Test that the analyzer step fails on a leak in a fork file, naming the
# Clang Static Analyzer's checker.
test_analyzer_fails_on_leak()
{
	need_native_tidy
	local r out rc
	r=$(plant_repo "$GATES/analyzer_leak.cpp.in") || die "plant_repo failed"
	out=$(native_step "$r" analyzer) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "[clang-analyzer-unix.Malloc"
	assert_contains "$out" "nightly.sh: failed: analyzer"
}

# Test that the analyzer and thread-safety steps fail, not skip, without
# the pinned clang-tidy.
test_analyzers_fail_without_clang_tidy()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	mkdir -p "$r/third_party/none"
	out=$(DXX_DEPS_PREFIX=$r/third_party/none native_step "$r" analyzer,thread_safety) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "analyzer: $r/third_party/none/opt/llvm/bin/clang-tidy not found"
	assert_contains "$out" "nightly.sh: failed: analyzer thread_safety"
	assert_not_contains "$out" "SKIP"
}

# Test that the analyzer step passes the representative fork file, naming
# how many fork files it analysed.
test_analyzer_passes_clean_file()
{
	need_native_tidy
	local r out
	r=$(plant_repo "$GATES/clean.cpp.in") || die "plant_repo failed"
	out=$(native_step "$r" analyzer)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "analyzer: 2 fork files"
}

# Test that the analyzer step reads tools/dev/fork-dirs as the build's
# strict warnings do (fork_warnings.py): an indented comment and a
# directory followed by blanks.
test_analyzer_reads_fork_dirs_as_the_build_does()
{
	need_native_tidy
	local r out
	r=$(plant_repo "$GATES/clean.cpp.in") || die "plant_repo failed"
	printf '# fork directories\n  # indented comment\n\nsrc \t\n' > "$r/tools/dev/fork-dirs"
	out=$(native_step "$r" analyzer)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "analyzer: 2 fork files"
}

# Test that the analyzer step fails, naming the file, when fork-dirs lists
# no directory.
test_analyzer_fails_without_fork_dirs()
{
	need_native_tidy
	local r out rc
	r=$(plant_repo "$GATES/clean.cpp.in") || die "plant_repo failed"
	printf '# only a comment\n\n' > "$r/tools/dev/fork-dirs"
	out=$(native_step "$r" analyzer) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "fork-dirs lists no directory"
	assert_contains "$out" "nightly.sh: failed: analyzer"
}

# Test that the thread-safety step fails on a member written without its
# mutex held.
test_thread_safety_fails_on_unguarded_write()
{
	need_native_tidy
	local r d out rc
	d=$(make_scratch) || die "make_scratch failed"
	cat > "$d/race.cpp" <<'CPP'
#include <mutex>

class counter
{
	std::mutex lock;
	int value __attribute__((guarded_by(lock))){0};

public:
	void bump() { ++value; }
};
CPP
	r=$(plant_repo "$d/race.cpp") || die "plant_repo failed"
	out=$(native_step "$r" thread_safety) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "requires holding mutex 'lock'"
}

# Test that the thread-safety step checks SDL's mutexes: their annotations
# are on, so a member written without its SDL mutex held fails.
test_thread_safety_fails_on_unguarded_sdl_write()
{
	need_native_tidy
	local r d out rc
	d=$(make_scratch) || die "make_scratch failed"
	cat > "$d/race.cpp" <<'CPP'
#include <SDL3/SDL_mutex.h>

class counter
{
	SDL_Mutex *lock{SDL_CreateMutex()};
	int value SDL_GUARDED_BY(lock){0};

public:
	void bump() { ++value; }
};
CPP
	r=$(plant_repo "$d/race.cpp") || die "plant_repo failed"
	out=$(native_step "$r" thread_safety) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "requires holding mutex 'lock'"
}

# Test that the thread-safety step names the kinds of lock fork code takes
# once it has proven the analysis on each, and fails when a kind's
# annotations are off (an SDL whose mutex.h has none, here).
test_thread_safety_proves_each_lock_kind()
{
	need_native_tidy
	local r d out rc
	d=$(make_scratch) || die "make_scratch failed"
	cat > "$d/locked.cpp" <<'CPP'
#include <SDL3/SDL_mutex.h>

class counter
{
	SDL_Mutex *lock{SDL_CreateMutex()};
	int value SDL_GUARDED_BY(lock){0};

public:
	void bump()
	{
		SDL_LockMutex(lock);
		++value;
		SDL_UnlockMutex(lock);
	}
};
CPP
	r=$(plant_repo "$d/locked.cpp") || die "plant_repo failed"
	out=$(native_step "$r" thread_safety)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "locks checked, the analysis proven on each: SDL (src/plant.cpp)"
	mkdir -p "$r/fake/SDL3"
	printf '#pragma once\ntypedef struct SDL_Mutex SDL_Mutex;\n#define SDL_GUARDED_BY(x)\n%s\n' \
		'SDL_Mutex *SDL_CreateMutex(); void SDL_LockMutex(SDL_Mutex *); void SDL_UnlockMutex(SDL_Mutex *);' \
		> "$r/fake/SDL3/SDL_mutex.h"
	sed -i.bak "s|\"-isystem\", |\"-isystem\", \"$r/fake\", \"-isystem\", |g" \
		"$r/build/dev/compile_commands.json"
	out=$(native_step "$r" thread_safety) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "the annotations of SDL locks are off"
}

# Test that the thread-safety step passes fork code that takes no lock,
# and says that none does.
test_thread_safety_says_when_no_fork_code_locks()
{
	need_native_tidy
	local r out
	r=$(plant_repo "$GATES/clean.cpp.in") || die "plant_repo failed"
	out=$(native_step "$r" thread_safety)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "no fork file takes a lock yet"
}

# Replace build.sh in fork repository $1 with a fake that records its
# arguments, one per line, in $1/build.args, prints $1/build.out (if any)
# and exits with status $2.
fake_build()
{
	printf '#!/bin/sh\nprintf "%%s\\n" "$@" > "%s/build.args"\ncat "%s/build.out" 2>/dev/null\nexit %s\n' \
		"$1" "$1" "$2" > "$1/tools/dev/build.sh"
}

# Write to $1/build.out what scons -k check prints for failed test runs
# (NAME.check), builds (NAME) and a target count ($2, else their number),
# each run printing its Boost.Test failure in colour.  The remaining
# arguments are NAME[.check]:FAILURE.
fake_failures()
{
	local r=$1 count=$2 a n=0
	shift 2
	{
		for a in "$@"; do
			printf '\e[4;31;49m%s\e[0;39;49m\n' "${a#*:}"
		done
		echo "Failed target count: total=${count:-$#}; targets with enable_build_failure_summary=1: $#"
		echo "Failed node list:"
		for a in "$@"; do
			printf '\tbuild/nightly/glibcxx-debug/common/%s\n' "${a%%:*}"
			n=$((n + 1))
		done
	} > "$r/build.out"
}

# The failure tools/dev/glibcxx-debug-known lists for test-physfsrwops.
PHYSFS_FAILURE='fatal error: in "read_returns_complete_object_count": memory access violation at address: 0x10000'

# Test that the container leg builds the unit tests with libstdc++'s debug
# mode beside the hardened flags, in a build directory of its own, and
# runs them.
test_glibcxx_debug_builds_and_runs_unit_tests()
{
	local r out args
	r=$(fork_repo) || die "fork_repo failed"
	fake_build "$r" 2
	fake_failures "$r" '' "test-physfsrwops.check:$PHYSFS_FAILURE"
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1)
	assert_eq "$?" 0 "$out"
	args=$(cat "$r/build.args")
	assert_contains "$args" "-D_GLIBCXX_DEBUG"
	assert_contains "$args" "-D_GLIBCXX_ASSERTIONS"
	assert_contains "$args" "builddir=build/nightly/glibcxx-debug"
	assert_contains "$args" $'\ncheck'
}

# Test that a failing build with no failed-node list fails the step.
test_glibcxx_debug_fails_with_the_tests()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	fake_build "$r" 1
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "nightly.sh: failed: glibcxx_debug"
}

# Test that a test listed in tools/dev/glibcxx-debug-known passes only when
# its run fails with the listed failure: another failure of it, its build
# failing, or another test failing fails the step.
test_glibcxx_debug_known_failures()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	fake_build "$r" 2
	fake_failures "$r" '' "test-physfsrwops.check:$PHYSFS_FAILURE"
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "test-physfsrwops fails as listed: reads Boost.Test's"
	fake_failures "$r" '' "test-physfsrwops.check:$PHYSFS_FAILURE" "test-homing.check:x"
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "glibcxx_debug: failed: build/nightly/glibcxx-debug/common/test-homing.check"
	fake_failures "$r" '' 'test-physfsrwops.check:Error: attempt to dereference a past-the-end iterator.'
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "glibcxx_debug: failed: build/nightly/glibcxx-debug/common/test-physfsrwops.check"
	fake_failures "$r" '' "test-physfsrwops:$PHYSFS_FAILURE"
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "glibcxx_debug: failed: build/nightly/glibcxx-debug/common/test-physfsrwops"
}

# Test that a listed test which no longer fails makes the entry stale and
# fails the step, and that a test name is compared whole, not as a pattern.
test_glibcxx_debug_stale_entry_fails()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	fake_build "$r" 0
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "did not fail as listed: test-physfsrwops"
	fake_build "$r" 2
	fake_failures "$r" '' "test-physfsrwopsX.check:$PHYSFS_FAILURE"
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "glibcxx_debug: failed: build/nightly/glibcxx-debug/common/test-physfsrwopsX.check"
}

# Test that a failed-node list shorter than SCons's failed target count
# fails the step: a failure the list leaves out is not let through.
test_glibcxx_debug_counts_failed_targets()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	fake_build "$r" 2
	fake_failures "$r" 2 "test-physfsrwops.check:$PHYSFS_FAILURE"
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "2 failed targets counted and 1 in the failed node list"
}

# Test that the fanalyzer step runs GCC's -fanalyzer on the fork C files,
# failing on a double free and passing a clean file (GCC only: the
# container leg).
test_fanalyzer_fails_on_double_free()
{
	gcc --version 2>/dev/null | grep -q 'Free Software Foundation' ||
		skip "gcc is not GCC; -fanalyzer runs in the container leg"
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf '#include <stdlib.h>\nint main(void)\n{\n\tchar *p = malloc(4);\n\tfree(p);\n\treturn 0;\n}\n' \
		> "$r/src/clean.c"
	out=$("$r/tools/dev/nightly.sh" --leg container --steps fanalyzer 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "fanalyzer: 1 fork C files"
	printf '#include <stdlib.h>\nint main(void)\n{\n\tchar *p = malloc(4);\n\tfree(p);\n\tfree(p);\n\treturn 0;\n}\n' \
		> "$r/src/plant.c"
	out=$("$r/tools/dev/nightly.sh" --leg container --steps fanalyzer 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "double-free"
	assert_contains "$out" "nightly.sh: failed: fanalyzer"
}

# Write into directory $1 (DEMO_TIMEOUT: the demo timeout, default 60 s)
# a fake game for the demo steps, a fake D2 data
# directory, an empty crash report directory and a fake frontmost-app
# sampler, and print the environment assignments that make nightly.sh use
# them, one per line.  The game records its arguments and the harness
# variables in $1/game.args, then every 0.1 s writes a demo section to the
# frame hash log until TERM ends it with status 0, and per FAKE_GAME: `trap`
# prints a libc++ hardening message and exits 1 after one section, `report`
# writes a crash report of itself, `idle` writes no section, `grow` takes
# 0.5 MB more memory each time (the endurance run must sample it rise),
# `shutdown_crash` answers TERM with a heap
# corruption message and status 134, `hang` ignores TERM, `drop_term`
# ignores the first TERM (the game drops a quit while a demo loads),
# `vanish` becomes a process that is not the game, which then writes two
# sections, `fork` runs a child that carries the game's command line.
# GAME_START_DELAY and EXEC_DELAY (default 0) slow the game's start and
# vanish's exec: raising their defaults and SAMPLER_DELAY's here
# simulates a loaded machine under emulation.
# The endurance verdicts come from endurance_repo's fixed series, judged
# with TREND_ARGS (default: no warm-up, windows of 2 s, a 1000 MiB/h limit,
# at least 4 samples and 3 windows).
fake_game()
{
	local d=$1
	mkdir -p "$d/data" "$d/reports" "$d/work"
	touch "$d/data/DESCENT2.HOG"
	cat > "$d/fake-game" <<GAME
#!/usr/bin/env bash
sleep ${GAME_START_DELAY:-0}
case \${FAKE_GAME-} in
shutdown_crash) trap 'echo "malloc: Heap corruption detected" >&2; exit 134' TERM ;;
hang) trap '' TERM ;;
drop_term) trap 'trap "exit 0" TERM' TERM ;;
fork) ( while :; do sleep 0.1; done ) &
	trap "kill \$!; exit 0" TERM ;;
*) trap 'exit 0' TERM ;;
esac
echo "\$* hidden=\$DXX_DBG_HIDDEN frametime=\$DXX_DBG_FIXED_FRAMETIME home=\$D2X_REBIRTH_HOME" \
	"asan=\$ASAN_OPTIONS ubsan=\$UBSAN_OPTIONS" > "$d/game.args"
ls -A "\$D2X_REBIRTH_HOME" >> "$d/game.args"
s=
while :; do
	case \${FAKE_GAME-} in
	idle|vanish) ;;
	*) printf '# demo playback demos/fake.dem\n0 0\n' >> "\$DXX_DBG_FRAMEHASH" ;;
	esac
	case \${FAKE_GAME-} in
	trap) echo 'x.h:1: libc++ Hardening assertion n < size() failed: out of range' >&2
		exit 1 ;;
	report) printf '{"pid" : %d, "procName" : "fake-game"}\n' \$\$ > "$d/reports/fake.ips" ;;
	grow) s+=\$(head -c 500000 /dev/zero | tr '\\0' x) ;;
	# The sections come after the exec, which is slow under emulation.
	vanish) sleep ${EXEC_DELAY:-0}
		exec bash -c 'trap "exit 0" TERM
			printf "# demo playback demos/fake.dem\n0 0\n%.0s" 1 2 >> "\$DXX_DBG_FRAMEHASH"
			while :; do sleep 0.1 & wait \$!; done' "$d/vanished" ;;
	esac
	# A trap runs only once a foreground command ends, but at once in wait.
	sleep 0.1 & wait \$!
done
GAME
	chmod +x "$d/fake-game"
	printf '%s\n' "DXX_NIGHTLY_GAME=$d/fake-game" "DXX_D2_DATA=$d/data" \
		"DXX_NIGHTLY_WORK=$d/work" "NO_WINDOW_REPORTS_DIR=$d/reports" \
		"NO_WINDOW_FRONTMOST_CMD=$(slow_sampler "$d")" \
		"DXX_NIGHTLY_DEMO_TIMEOUT=${DEMO_TIMEOUT:-60}" DXX_NIGHTLY_SAMPLE_INTERVAL=0.2 \
		"DXX_NIGHTLY_TREND_ARGS=${TREND_ARGS:---warmup 0 --max-slope 1000 --window 2 --min-samples 4 --min-windows 3}" \
		RUN_KILL_AFTER=2
}

# Write into directory $1 a fake frontmost-app sampler that answers
# "1 Terminal" after SAMPLER_DELAY seconds (default none), as osascript does
# slowly under emulation on a loaded machine, and print its path.
slow_sampler()
{
	local sampler
	sampler=$(fake_sampler "$1" "1 Terminal") || return
	printf '#!/bin/sh\nsleep %s\nexec "%s"\n' "${SAMPLER_DELAY:-0}" "$sampler" > "$1/slow-sampler"
	chmod +x "$1/slow-sampler"
	printf '%s\n' "$1/slow-sampler"
}

# Wait for process $1, a child, and pass on its status; after $2 seconds
# (default 120) send it TERM (nightly.sh then ends its run), and KILL 30 s
# later, say so and fail with 124, so a hang fails its test instead of
# stalling the tests.
bounded()
{
	local i limit=${2:-120}
	for ((i = 0; i < limit * 10; i++)); do
		kill -0 "$1" 2>/dev/null || { wait "$1"; return; }
		sleep 0.1
	done
	kill -TERM "$1"
	for ((i = 0; i < 300; i++)); do
		kill -0 "$1" 2>/dev/null || break
		sleep 0.1
	done
	kill -KILL "$1" 2>/dev/null
	wait "$1"
	echo "nightly.sh still ran after $limit s"
	return 124
}

# Run the native leg's step $2 with the fake game in directory $1, with
# the remaining arguments, for at most 120 s (bounded), from tools/dev or
# from NIGHTLY_DEV_DIR.
game_step()
{
	local vars
	mapfile -t vars < <(fake_game "$1")
	env "${vars[@]}" "${NIGHTLY_DEV_DIR:-$DEV_DIR}/nightly.sh" --leg native --steps "$2" "${@:3}" 2>&1 &
	bounded "$!"
}

# Test that the hardened demo run passes once a whole demo has played, and
# runs the game windowless as docs/agents/game-data.md says: hidden, fixed
# frame time, a fresh home, the harness pilot, no movies, the sanitizer
# options that never abort.
test_hardened_demo_passes_a_played_demo()
{
	local d out args
	d=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$d/work/hardened_demo-home" && touch "$d/work/hardened_demo-home/stale"
	out=$(game_step "$d" hardened_demo)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "complete demo playback"
	args=$(cat "$d/game.args")
	assert_contains "$args" "-hogdir $d/data -window -pilot harness -nomovies -autodemo"
	assert_contains "$args" "hidden=1 frametime=30 home=$d/work/hardened_demo-home"
	assert_contains "$args" "asan=abort_on_error=0 ubsan=halt_on_error=1:abort_on_error=0"
	assert_not_contains "$args" "stale"
}

# Test that a hardening trap fails the run, quoting the trap.
test_hardened_demo_fails_on_trap()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$(FAKE_GAME='trap' game_step "$d" hardened_demo) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "libc++ Hardening assertion"
	assert_contains "$out" "ended by itself"
}

# Test that a crash report of the game fails the run.
test_hardened_demo_fails_on_crash_report()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$(FAKE_GAME=report game_step "$d" hardened_demo) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "crash report"
}

# Test that a crash at shutdown, after TERM, fails the run, naming the
# status: the hardened demo and the endurance run share the ending.
test_demo_steps_fail_on_crash_at_shutdown()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$(FAKE_GAME=shutdown_crash game_step "$d" hardened_demo) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "hardened_demo: the game ended with status 134 after TERM, not 0"
	d=$(make_scratch) || die "make_scratch failed"
	out=$(FAKE_GAME=shutdown_crash game_step "$d" endurance --endurance 3) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "endurance: the game ended with status 134 after TERM, not 0"
}

# Test that a game still running after TERM (a hang, or a box nobody can
# answer) fails the run and is ended.
test_hardened_demo_fails_when_the_game_ignores_term()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$(FAKE_GAME=hang game_step "$d" hardened_demo) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "hardened_demo: the game still runs 2 s after TERM"
	assert_contains "$out" "killing it"
	pgrep -f "$d/fake-game" >/dev/null && fail "the fake game still runs"
	return 0
}

# Test that TERM is sent again while the game runs: a game that drops the
# first one still ends cleanly.
test_hardened_demo_repeats_term()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	out=$(FAKE_GAME=drop_term game_step "$d" hardened_demo)
	assert_eq "$?" 0 "$out"
}

# Test that a game which quits on TERM passes when no-window.sh is slow to
# see it gone: a frontmost-app sampler taking 1.5 s, as under emulation,
# stretches run.sh's end past the 2 s grace, which is the game's alone.
test_hardened_demo_passes_with_a_slow_sampler()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	out=$(SAMPLER_DELAY=1.5 game_step "$d" hardened_demo)
	assert_eq "$?" 0 "$out"
}

# Test that a game not found among run.sh's processes fails the run at
# once, which then ends through run.sh.
test_demo_steps_fail_when_the_game_is_not_found()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$(FAKE_GAME=vanish game_step "$d" hardened_demo) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "hardened_demo: no process of $d/fake-game found under run.sh"
	pgrep -f "$d/vanished" >/dev/null && fail "the vanished game still runs"
	return 0
}

# Test that the demo steps end the game, not a child of it that still
# carries its command line (a fork before its exec, slow under emulation),
# whichever ps lists first: pids wrap, so a child can be listed before
# its parent.  Ending such a child left the game running and the step
# waiting for it.
test_demo_steps_find_the_game_not_its_fork()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	mkdir "$d/bin"
	printf '#!/bin/sh\n"%s" "$@" | sort -rn\n' "$(command -v ps)" > "$d/bin/ps"
	chmod +x "$d/bin/ps"
	out=$(PATH=$d/bin:$PATH FAKE_GAME=fork game_step "$d" hardened_demo)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "complete demo playback"
}

# Test that a run with no whole demo before the timeout fails.
test_hardened_demo_fails_without_a_demo()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$(FAKE_GAME=idle DEMO_TIMEOUT=3 game_step "$d" hardened_demo) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "no complete demo playback in 3 s"
}

# Write into fork repository $1 a run.sh that starts the game 5 s late (a
# loaded machine) and drops the first $2 TERMs, as run.sh does with one
# that lands in the child it is starting (before its exec the child runs
# run.sh's trap, keeping the signal); the TERM after them ends the game
# and run.sh.
dropping_run_sh()
{
	cat > "$1/tools/dev/run.sh" <<RUN
#!/usr/bin/env bash
n=0 game=''
trap 'n=\$((n + 1)); [[ \$n -le $2 ]] || { [[ -z \$game ]] || kill -TERM "\$game"; exit 143; }' TERM
sleep 5 & wait \$!
"\$@" &
game=\$!
while kill -0 "\$game" 2>/dev/null; do wait "\$game"; done
RUN
}

# Test that the demo steps send TERM to run.sh again until it ends, so a
# dropped TERM neither leaves the late game running nor hangs the step;
# and that a run.sh still running once its teardown could be over is
# killed with the game, the step failing in time either way.
test_demo_steps_repeat_term_to_run_sh()
{
	local r d out rc
	r=$(fork_repo) || die "fork_repo failed"
	d=$(make_scratch) || die "make_scratch failed"
	dropping_run_sh "$r" 1
	out=$(NIGHTLY_DEV_DIR=$r/tools/dev FAKE_GAME=idle DEMO_TIMEOUT=3 game_step "$d" hardened_demo) \
		&& rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "hardened_demo: no process of $d/fake-game found under run.sh"
	assert_contains "$out" "no complete demo playback in 3 s"
	assert_not_contains "$out" "killing the run"
	pgrep -f "$d/fake-game" >/dev/null && fail "the fake game still runs"
	d=$(make_scratch) || die "make_scratch failed"
	dropping_run_sh "$r" 1000
	out=$(NIGHTLY_DEV_DIR=$r/tools/dev FAKE_GAME=idle DEMO_TIMEOUT=3 game_step "$d" hardened_demo) \
		&& rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "hardened_demo: run.sh still runs after TERM; killing the run"
	assert_contains "$out" "no complete demo playback in 3 s"
	pgrep -f "$d/fake-game" >/dev/null && fail "the fake game still runs"
	return 0
}

# Test that the demo steps fail, naming DXX_D2_DATA, without the data: a
# scheduled run must not stay green on them.
test_demo_steps_fail_without_data()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$(env DXX_D2_DATA="$d/none" "$DEV_DIR/nightly.sh" --leg native \
		--steps hardened_demo,endurance 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "hardened_demo: DXX_D2_DATA names no directory"
	assert_contains "$out" "nightly.sh: failed: hardened_demo endurance"
	assert_not_contains "$out" "SKIP"
}

# Print a fork repository (lib.sh) for the endurance run, whose memory
# sampler takes 6 s a sample, as under emulation on a loaded machine.  How
# many samples a run takes depends on that load (2 in 10 s under emulation
# while others built, too few to judge), so the real rss_trend.py, given
# the run's options, judges a fixed series instead: 20 samples over 10 s
# (4 whole windows of 2 s) from 1000 KiB, growing by $1 KiB (default 0)
# each.  It does so once the run has taken a sample and, with $2, once the
# run's own samples (the first and the last, taken at least 6 s apart)
# show the game's memory up by $2 KiB: proof the run samples the game.
endurance_repo()
{
	local r i
	r=$(fork_repo) || die "fork_repo failed"
	mv "$r/tools/dev/footprint.py" "$r/tools/dev/real-footprint.py"
	printf 'import runpy, time\ntime.sleep(6)\nrunpy.run_path("%s", run_name="__main__")\n' \
		"$r/tools/dev/real-footprint.py" > "$r/tools/dev/footprint.py"
	for ((i = 0; i < 20; i++)); do
		printf '%d.%d %d\n' $((i / 2)) $((i % 2 * 5)) $((1000 + i * ${1:-0}))
	done > "$r/series.mem"
	mv "$r/tools/dev/rss_trend.py" "$r/tools/dev/real-rss_trend.py"
	cat > "$r/tools/dev/rss_trend.py" <<STANDIN
import runpy, sys
with open(sys.argv[-1]) as f:
	kib = [float(line.split()[1]) for line in f if line.strip()]
if not kib:
	sys.exit('rss_trend stand-in: the run took no memory sample')
if '${2-}' and kib[-1] - kib[0] < float('${2:-0}'):
	sys.exit(f'rss_trend stand-in: the run sampled {kib[0]:g} then {kib[-1]:g} KiB, not up by ${2-} KiB')
sys.argv[-1] = '$r/series.mem'
runpy.run_path('$r/tools/dev/real-rss_trend.py', run_name='__main__')
STANDIN
	printf '%s\n' "$r"
}

# Test that the endurance run passes a game whose memory stays flat,
# naming the sampled slope.
test_endurance_passes_flat_memory()
{
	local r d out
	r=$(endurance_repo) || die "endurance_repo failed"
	d=$(make_scratch) || die "make_scratch failed"
	out=$(NIGHTLY_DEV_DIR=$r/tools/dev game_step "$d" endurance --endurance 10)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "MiB/h"
	assert_contains "$out" ": flat"
}

# Test that the endurance run fails a game whose memory grows.
test_endurance_fails_growing_memory()
{
	local r d out rc
	# 500 KiB every 0.5 s: about 3500 MiB/h, over the test's 1000 MiB/h.
	# The game takes 0.5 MB every 0.1 s: over 6 s 1 MiB is a few of those.
	r=$(endurance_repo 500 1024) || die "endurance_repo failed"
	d=$(make_scratch) || die "make_scratch failed"
	out=$(NIGHTLY_DEV_DIR=$r/tools/dev FAKE_GAME=grow game_step "$d" endurance --endurance 10) \
		&& rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" ": growing"
	assert_contains "$out" "nightly.sh: failed: endurance"
}

# Test that an endurance run too short for the trend (fewer windows than
# rss_trend.py's default 30) fails as too short to judge, with no slope.
test_endurance_too_short_fails()
{
	local r d out rc
	r=$(endurance_repo) || die "endurance_repo failed"
	d=$(make_scratch) || die "make_scratch failed"
	out=$(NIGHTLY_DEV_DIR=$r/tools/dev TREND_ARGS="--warmup 0 --window 2 --min-samples 4" \
		game_step "$d" endurance --endurance 10) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rss_trend: too short to judge: "
	assert_contains "$out" "windows of 2 s after the 0 s warm-up, need 30"
	assert_not_contains "$out" "slope"
	assert_contains "$out" "nightly.sh: failed: endurance"
}

# Test that the endurance verdict's numbers (windows, slope with its
# range, the minima's spread) reach the nightly log, where the scheduled
# runs calibrate the limit from them.
test_endurance_numbers_reach_log()
{
	local r d vars out
	r=$(endurance_repo) || die "endurance_repo failed"
	d=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\necho "container.sh $*"\n' > "$r/tools/dev/container.sh"
	mapfile -t vars < <(fake_game "$d")
	out=$(env "${vars[@]}" "$r/tools/dev/nightly.sh" --steps endurance --endurance 10 2>&1 &
		bounded "$!")
	assert_eq "$?" 0 "$out"
	out=$(cat "$r/build/nightly/nightly.log")
	assert_contains "$out" "rss_trend: flat: slope "
	assert_contains "$out" "MiB/h (95% range "
	assert_contains "$out" "over 4 windows of 2 s"
	assert_contains "$out" "MiB (spread "
}

# Start nightly.sh in fork repository $1 (with a container.sh that runs
# until TERM) in the background, with the fake game in directory $2, the
# game idle (no demo ends the run), and the remaining arguments; set
# nightly_pid and wait for the game to start (at most 30 s).
start_idle_nightly()
{
	local r=$1 d=$2 vars i
	printf '#!/usr/bin/env bash\ntrap "exit 0" TERM\nwhile :; do sleep 0.1 & wait $!; done\n' \
		> "$r/tools/dev/container.sh"
	mapfile -t vars < <(DEMO_TIMEOUT=600 fake_game "$d")
	env "${vars[@]}" FAKE_GAME=idle "$r/tools/dev/nightly.sh" "${@:3}" > "$d/nightly.out" 2>&1 &
	nightly_pid=$!
	for ((i = 0; i < 300; i++)); do
		[[ -s $d/game.args ]] && return
		sleep 0.1
	done
	kill -TERM "$nightly_pid"
	die "the fake game did not start: $(cat "$d/nightly.out")"
}

# Test that TERM to nightly.sh, one leg or both, ends the run's every
# process (run.sh, no-window.sh, the hidden game, the container leg) and
# fails the run with 143.
test_term_leaves_no_process_of_the_run()
{
	local r d args rc
	for args in "--leg native --steps hardened_demo" "--steps hardened_demo"; do
		r=$(fork_repo) || die "fork_repo failed"
		d=$(make_scratch) || die "make_scratch failed"
		# shellcheck disable=SC2086  # the options are words
		start_idle_nightly "$r" "$d" $args
		kill -TERM "$nightly_pid"
		bounded "$nightly_pid" 60 && rc=0 || rc=$?
		assert_eq "$rc" 143 "$args: $(cat "$d/nightly.out")"
		assert_contains "$(cat "$d/nightly.out")" "nightly.sh: TERM; ending the run"
		pgrep -f "$d/" >/dev/null && fail "$args: a process of the run still runs: $(pgrep -lf "$d/")"
		pgrep -f "$r/tools/dev/container.sh" >/dev/null && fail "$args: the container leg still runs"
	done
	return 0
}

# Test that TERM to nightly.sh kills the run once run.sh's teardown could
# be over (its 2 s grace and 15 s) when run.sh does not end: none of it is
# left running.
test_term_kills_a_run_that_does_not_end()
{
	local r d rc
	r=$(fork_repo) || die "fork_repo failed"
	d=$(make_scratch) || die "make_scratch failed"
	printf '#!/usr/bin/env bash\ntrap "" TERM\n"$@" &\nwait\n' > "$r/tools/dev/run.sh"
	start_idle_nightly "$r" "$d" --leg native --steps hardened_demo
	kill -TERM "$nightly_pid"
	bounded "$nightly_pid" 60 && rc=0 || rc=$?
	assert_eq "$rc" 143 "$(cat "$d/nightly.out")"
	assert_contains "$(cat "$d/nightly.out")" "nightly.sh: run.sh still runs; killing the run"
	pgrep -f "$d/" >/dev/null && fail "a process of the run still runs: $(pgrep -lf "$d/")"
	return 0
}

# Test that the TSan step, which has no tests to run until #45, is a SKIP
# naming that ticket.
test_tsan_skips_visibly()
{
	local out
	out=$("$DEV_DIR/nightly.sh" --leg native --steps tsan 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP tsan: no audio-thread tests yet; #45"
}

# Test that without --leg both legs run, the container leg through
# container.sh run --clean with the same steps, each leg printed whole
# and logged, and a failed leg fails the run, naming it.
test_both_legs_run_and_log()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf '#!/bin/sh\necho "container.sh $*"\nexit "${FAKE_CONTAINER_RC:-0}"\n' \
		> "$r/tools/dev/container.sh"
	out=$("$r/tools/dev/nightly.sh" --steps tsan --endurance 60 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP tsan: no audio-thread tests yet"
	assert_contains "$out" \
		"container.sh run --clean tools/dev/nightly.sh --leg container --endurance 60 --steps tsan"
	assert_contains "$(cat "$r/build/nightly/nightly.log")" "== nightly: container leg"
	out=$(FAKE_CONTAINER_RC=1 "$r/tools/dev/nightly.sh" --steps tsan 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "nightly.sh: failed legs: container"
}

# Test that the full run (ci.sh --clean, ADR 6) runs before both legs when
# no --steps is given or --steps names full_run, a failed one failing the
# run, naming it; and that --leg takes no full_run.
test_full_run_runs_first()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	printf '#!/bin/sh\necho "container.sh $*"\n' > "$r/tools/dev/container.sh"
	printf '#!/bin/sh\necho "ci.sh $*"\nexit "${FAKE_CI_RC:-0}"\n' > "$r/tools/dev/ci.sh"
	out=$("$r/tools/dev/nightly.sh" --steps full_run,tsan 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" $'== nightly: full run (ci.sh --clean)\nci.sh --clean'
	assert_contains "$out" "SKIP tsan"
	assert_contains "$out" "container.sh run --clean tools/dev/nightly.sh --leg container --endurance 3600 --steps tsan"
	assert_contains "$(cat "$r/build/nightly/nightly.log")" "ci.sh --clean"
	out=$("$r/tools/dev/nightly.sh" --steps full_run 2>&1)
	assert_eq "$?" 0 "$out"
	assert_not_contains "$out" "container.sh"
	out=$(FAKE_CI_RC=1 "$r/tools/dev/nightly.sh" --steps full_run,tsan 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "nightly.sh: failed legs: full_run"
	out=$("$r/tools/dev/nightly.sh" --steps tsan 2>&1)
	assert_not_contains "$out" "ci.sh"
	out=$("$r/tools/dev/nightly.sh" --leg native --steps full_run 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
}

run_tests --parallel

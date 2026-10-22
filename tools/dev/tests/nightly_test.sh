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
# arguments, one per line, in $1/build.args, prints scons' list of failed
# nodes for the test programs named by the remaining arguments, and exits
# with status $2.
fake_build()
{
	local r=$1 rc=$2
	shift 2
	{
		printf '#!/bin/sh\nprintf "%%s\\n" "$@" > "%s/build.args"\n' "$r"
		if [[ $# -gt 0 ]]; then
			printf 'echo "Failed node list:"\n'
			printf 'printf "\\tbuild/nightly/glibcxx-debug/common/%s.check\\n"\n' "$@"
		fi
		printf 'exit %s\n' "$rc"
	} > "$r/tools/dev/build.sh"
}

# Test that the container leg builds the unit tests with libstdc++'s debug
# mode beside the hardened flags, in a build directory of its own, and
# runs them.
test_glibcxx_debug_builds_and_runs_unit_tests()
{
	local r out args
	r=$(fork_repo) || die "fork_repo failed"
	fake_build "$r" 0
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1)
	assert_eq "$?" 0 "$out"
	args=$(cat "$r/build.args")
	assert_contains "$args" "-D_GLIBCXX_DEBUG"
	assert_contains "$args" "-D_GLIBCXX_ASSERTIONS"
	assert_contains "$args" "builddir=build/nightly/glibcxx-debug"
	assert_contains "$args" $'\ncheck'
}

# Test that a failing unit test in debug mode fails the step.
test_glibcxx_debug_fails_with_the_tests()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	fake_build "$r" 1
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "nightly.sh: failed: glibcxx_debug"
}

# Test that a unit test failing for the reason tools/dev/glibcxx-debug-known
# gives is printed with it and passes, and any other fails the step.
test_glibcxx_debug_known_failures()
{
	local r out rc
	r=$(fork_repo) || die "fork_repo failed"
	fake_build "$r" 2 test-physfsrwops
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "test-physfsrwops fails for a known reason: reads Boost.Test's"
	fake_build "$r" 2 test-physfsrwops test-homing
	out=$("$r/tools/dev/nightly.sh" --leg container --steps glibcxx_debug 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "glibcxx_debug: failed: build/nightly/glibcxx-debug/common/test-homing.check"
}

# Test that the steps without a runner yet are SKIPs naming why: TSan the
# ticket that adds audio-thread tests, cppcheck and CodeQL the owner's
# install decision.
test_unbuilt_steps_skip_visibly()
{
	local out
	out=$("$DEV_DIR/nightly.sh" --leg native --steps tsan,cppcheck,codeql 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP tsan: no audio-thread tests yet; #45"
	assert_contains "$out" "SKIP cppcheck: cppcheck is not pinned"
	assert_contains "$out" "SKIP codeql: codeql is not pinned"
}

run_tests --parallel

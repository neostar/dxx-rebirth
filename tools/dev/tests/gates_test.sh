#!/usr/bin/env bash
# Gate self-test (ADR 5): every gate goes red on a planted violation.
#
# Each file in gates/ is a planted violation (or, clean.cpp.in, the
# representative fork file) whose header names the gates it trips:
#
#   // gate NAME: EXPECT          gate NAME fails with output containing
#                                 EXPECT, or passes if EXPECT is `pass`
#   // gate NAME (#N): EXPECT     the same, for a gate ticket #N builds
#   // staged as: PATH            gate tidy stages the file at PATH, not
#                                 src/plant.cpp (a check scoped by file name)
#
# test_GATE runs gate_GATE on each planted file that names it, copied into
# a throwaway place under a neutral name, so that an expectation can match
# only the diagnostic, never the file's own name.  A gate without a runner
# is a SKIP naming its ticket.  A later ticket adds a gate by writing
# gate_NAME and test_NAME here; the planted files stay out of the fork
# gates because they end in .in.
source "$(dirname "$0")/lib.sh"
GATES="$DEV_DIR/tests/gates"

# Skip the current test, naming tool $1, unless $1 runs.
need()
{
	local hint='tools/dev/deps.sh installs the LLVM tools'
	[[ $1 == */* ]] || hint='install a C++ compiler'
	command -v "$1" >/dev/null || skip "$1 not found; $hint"
}

# The compiler build.sh's builds use: SCons' default CXX, g++, which no
# site file overrides (Apple clang on the Mac, GCC 14 in the container).
cxx=g++

# The tool each gate runs, which a test needs before running the gate.
declare -A gate_tool=([tidy]=$(dev_llvm_tool clang-tidy) [strict]=$cxx [hardening]=$cxx
	[asan]=$cxx [lsan]=$cxx [ubsan]=$cxx [ubsanclang]=$cxx [leaks]=$cxx [gpu]=$cxx [gpumetal]=$cxx)

# The pre-commit gate: stage file $1 as src/plant.cpp, or as the path its
# `// staged as: PATH` line names (a check scoped by file name), in a fork
# repository and run format.sh --staged.
gate_tidy()
{
	local r path
	r=$(fork_repo) || die "fork_repo failed"
	path=$(sed -n 's|^// staged as: ||p' "$1")
	path=${path:-src/plant.cpp}
	cp "$1" "$r/$path"
	tgit -C "$r" add "$path"
	"$r/tools/dev/format.sh" --staged 2>&1
}

# The strict warnings (fork_warnings.py) as the build adds them: compile
# file $1 with them and the pinned SDL3 headers.  GCC's [-Werror=NAME] is
# printed as clang's [-Werror,-WNAME], so one expectation fits both.
gate_strict()
{
	local d rc
	d=$(make_scratch) || die "make_scratch failed"
	cp "$1" "$d/plant.cpp"
	# shellcheck disable=SC2046  # the flags are words
	"${gate_tool[strict]}" -std=c++23 $(python3 "$DEV_DIR/fork_warnings.py") \
		-isystem "$dev_deps_prefix/include" -fsyntax-only "$d/plant.cpp" > "$d/out" 2>&1
	rc=$?
	sed -E 's/\[-Werror=([a-z0-9-]+)\]/[-Werror,-W\1]/' "$d/out"
	return "$rc"
}

# Build file $1 as a program with the compiler flags "${@:2}" and the
# hardened standard library, and print its path.  hardening.py's flags
# force hardening-exit.h in, so a libc++ trap exits 1, as in the build.
build_plant()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	cp "$1" "$d/plant.cpp"
	# shellcheck disable=SC2046  # the flags are words
	# The flags follow the sources, so that GNU ld sees libraries last.
	"$cxx" -std=c++23 -g $(python3 "$DEV_DIR/hardening.py") \
		"$d/plant.cpp" "${@:2}" -o "$d/plant" >&2 || return
	printf '%s\n' "$d/plant"
}

# The hardened standard library (hardening.py) as the dev profile builds
# it: run file $1, built with it.  On macOS a trap must exit 1, never
# abort() into a crash report (hardening-exit.h); such a run prints only
# its status, which no expectation matches.  It runs under no-window.sh,
# which reports a crash report of the run.
gate_hardening()
{
	local p rc out
	p=$(build_plant "$1") || return
	out=$("$DEV_DIR/no-window.sh" "$p" 2>&1) && rc=0 || rc=$?
	if [[ $dev_os == darwin && $rc -gt 1 ]]; then
		echo "gate hardening: the trap ended with status $rc, not 1 (abort?)"
	else
		printf '%s\n' "$out"
	fi
	return "$rc"
}

# Print site-asan.py's values of the variables "$@", space-separated, as
# SCons reads them (with tools/dev on sys.path).
asan_site()
{
	python3 -I -c 'import sys, os
sys.path.insert(0, os.path.dirname(sys.argv[1]))
v = {}
exec(open(sys.argv[1]).read(), {}, v)
print(" ".join(" ".join(x) if isinstance(x, list) else x for x in (v[n] for n in sys.argv[2:])))' \
		"$DEV_DIR/site-asan.py" "$@"
}

# The asan profile (build.sh asan): run file $1 built with site-asan.py's
# compiler and linker flags, fork code's among them, under env.sh's
# sanitizer options (LeakSanitizer on, except on macOS; reporting), under
# no-window.sh, so that a crash report fails it.  abort_on_error=0 is
# appended to both option sets, where the last setting wins: an inherited
# abort_on_error=1 would leave a crash report on macOS.
# AddressSanitizer, LeakSanitizer and UBSan are one build, so gates asan,
# lsan, ubsan and ubsanclang are one runner.
gate_sanitizers()
{
	local p flags
	flags=$(asan_site CXXFLAGS FORK_ONLY_CXXFLAGS LINKFLAGS) || return
	# shellcheck disable=SC2086  # the flags are words
	p=$(build_plant "$1" $flags) || return
	ASAN_OPTIONS=${ASAN_OPTIONS:+$ASAN_OPTIONS:}abort_on_error=0 \
		UBSAN_OPTIONS=${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}abort_on_error=0 \
		"$DEV_DIR/no-window.sh" "$p" 2>&1
}
gate_asan() { gate_sanitizers "$@"; }
gate_lsan() { gate_sanitizers "$@"; }
gate_ubsan() { gate_sanitizers "$@"; }
# UBSan's Clang-only checks, which site-asan.py adds where g++ has them.
gate_ubsanclang() { gate_sanitizers "$@"; }

# macOS's leak check (leaks.sh, the check tier's leaks step): run file $1,
# built without sanitizers, under leaks --atExit and no-window.sh.
gate_leaks()
{
	local p
	p=$(build_plant "$1") || return
	"$DEV_DIR/no-window.sh" "$DEV_DIR/leaks.sh" "$p" 2>&1
}

# The GPU validation gate (gpu-validate.sh): run file $1, built against the
# pinned SDL3, under it and no-window.sh, headless (no window; offscreen
# video in the container, as its image sets).
gate_gpu()
{
	local p video=()
	# -I: the plants include their shared probe, gates/gpu_probe.h.in.
	p=$(build_plant "$1" -I"$GATES" -isystem "$dev_deps_prefix/include" \
		-L"$dev_deps_prefix/lib" -Wl,-rpath,"$dev_deps_prefix/lib" -lSDL3) || return
	[[ $dev_os == darwin ]] || video=(SDL_VIDEO_DRIVER=offscreen)
	env "${video[@]}" "$DEV_DIR/no-window.sh" "$DEV_DIR/gpu-validate.sh" "$p" 2>&1
}

# The GPU validation gate's Metal-only half (shader validation).
gate_gpumetal() { gate_gpu "$@"; }

# Succeed inside a container: Docker's or Podman's marker file.
in_container()
{
	[[ -e /.dockerenv || -e /run/.containerenv ]]
}

# Run gate $1 on every planted file that names it, and fail unless one does.
# A run that no-window.sh reports (a crash report, focus taken) fails
# whatever its expectation: a red plant must go red by its diagnostic.
run_planted()
{
	local gate=$1 f expect out rc n=0
	need "${gate_tool[$gate]}"
	while IFS= read -r -d '' f && IFS= read -r expect; do
		expect=${expect#"// gate $gate: "}
		n=$((n + 1))
		out=$("gate_$gate" "$f" </dev/null) && rc=0 || rc=$?
		if [[ $out == *"no-window.sh:"* ]]; then
			fail "gate $gate on ${f##*/} crashed or took focus: $out"
		elif [[ $expect == pass ]]; then
			[[ $rc -eq 0 ]] || fail "gate $gate failed ${f##*/}: $out"
		elif [[ $rc -eq 0 ]]; then
			fail "gate $gate passed ${f##*/}; expected: $expect"
		else
			[[ $out == *"$expect"* ]] ||
				fail "gate $gate on ${f##*/}: expected '$expect', got: $out"
		fi
	done < <(grep -H --null "^// gate $gate: " "$GATES"/*.in)
	[[ $n -gt 0 ]] || fail "no planted file names gate $gate"
}

test_tidy() { run_planted tidy; }
test_strict() { run_planted strict; }
test_hardening() { run_planted hardening; }
test_asan() { run_planted asan; }
test_ubsan() { run_planted ubsan; }

test_lsan()
{
	[[ $dev_os == darwin ]] &&
		skip "LeakSanitizer is unsupported on Apple Silicon macOS; gate leaks covers the Mac"
	run_planted lsan
}

# Skip the current test, adding $1 to the reason, unless $cxx has every
# UBSan check site-asan.py adds where the compiler has it.
need_clang_ubsan()
{
	local missing
	need "$cxx"
	missing=$(asan_site CLANG_UBSAN_MISSING 2>/dev/null) || die "site-asan.py failed"
	[[ -z $missing ]] || skip "$cxx lacks -fsanitize=${missing// /,}; $1"
}

test_ubsanclang()
{
	need_clang_ubsan "the Mac's Apple clang covers them"
	run_planted ubsanclang
}

# Test that the local-bounds plant reads past AddressSanitizer's redzone:
# built with ASan alone it runs unreported, so only local-bounds sees it.
test_local_bounds_plant_is_past_asan_redzone()
{
	local p out
	need_clang_ubsan "gate ubsanclang skips too"
	p=$(build_plant "$GATES/local_bounds.cpp.in" -O1 -fsanitize=address) ||
		{ fail "building the plant failed"; return; }
	out=$(ASAN_OPTIONS=abort_on_error=0:halt_on_error=1:detect_leaks=0 \
		"$DEV_DIR/no-window.sh" "$p" 2>&1)
	assert_not_contains "$out" "AddressSanitizer"
	assert_not_contains "$out" "no-window.sh:"
}

test_leaks()
{
	[[ $dev_os == darwin ]] || skip "leaks is macOS only; gate lsan covers Linux"
	run_planted leaks
}

# Skip the current test unless the pinned SDL3 is built.  The container's
# image holds it: missing there, the gate is broken, not unavailable.
need_pinned_sdl3()
{
	[[ -e $dev_deps_prefix/include/SDL3/SDL_gpu.h ]] && return
	in_container && die "no pinned SDL3 in $dev_deps_prefix, which the image builds"
	skip "no pinned SDL3 in $dev_deps_prefix; run tools/dev/deps.sh build"
}

test_gpu()
{
	need_pinned_sdl3
	run_planted gpu
}

test_gpumetal()
{
	[[ $dev_os == darwin ]] || skip "Metal is macOS only; gate gpu covers Vulkan here"
	need_pinned_sdl3
	run_planted gpumetal
}

# Test that a planted run that crashed into a report or took focus fails
# its gate even when its output matches the expectation: no-window.sh's
# verdict, not the plant's text, decides.
# Run gate fake, whose run matches its expectation but which no-window.sh
# reports, on a plant in directory $1; print whether run_planted failed.
run_crashed_plant()
{
	local GATES=$1
	printf '// gate fake: boom\n' > "$GATES/crash.cpp.in"
	gate_tool[fake]=true
	# shellcheck disable=SC2329  # run by run_planted
	gate_fake() { printf 'boom\nno-window.sh: new crash report of the run: x.ips\n'; return 1; }
	_test_failed=0
	run_planted fake
	echo "failed=$_test_failed"
}

test_planted_crash_or_focus_fails_the_gate()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	out=$(run_crashed_plant "$d" 2>&1)
	assert_contains "$out" "failed=1"
	assert_contains "$out" "crashed or took focus"
}

# Test that the sanitizer gates report and never abort (an abort leaves a
# crash report on macOS), whatever ASAN_OPTIONS and UBSAN_OPTIONS the
# environment sets: the plant sees abort_on_error=0 last in both.
test_sanitizer_gates_never_abort_under_inherited_options()
{
	local d out
	need "${gate_tool[asan]}"
	d=$(make_scratch) || die "make_scratch failed"
	cat > "$d/env.cpp.in" <<-'EOF'
	#include <cstdio>
	#include <cstdlib>
	int main()
	{
		const char *const asan{std::getenv("ASAN_OPTIONS")};
		const char *const ubsan{std::getenv("UBSAN_OPTIONS")};
		std::printf("asan=%s\nubsan=%s\n", asan ? asan : "", ubsan ? ubsan : "");
		return 0;
	}
	EOF
	out=$(ASAN_OPTIONS=abort_on_error=1 UBSAN_OPTIONS=abort_on_error=1 gate_asan "$d/env.cpp.in") ||
		{ fail "gate asan failed: $out"; return; }
	[[ $out =~ asan=[^$'\n']*abort_on_error=0($'\n'|$) ]] || fail "ASAN_OPTIONS may abort: $out"
	[[ $out =~ ubsan=[^$'\n']*abort_on_error=0($'\n'|$) ]] || fail "UBSAN_OPTIONS may abort: $out"
}

# Test that the GPU gates fail without the pinned SDL3 in the container,
# whose image builds it, and skip without it elsewhere.
# shellcheck disable=SC2329  # in_container is run by need_pinned_sdl3
test_gpu_without_pinned_sdl3_fails_only_in_container()
{
	local out rc empty
	empty=$(make_scratch) || die "make_scratch failed"
	out=$(exec 2>&1; dev_deps_prefix=$empty; in_container() { :; }; test_gpu; echo ran) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "no pinned SDL3 in $empty, which the image builds"
	out=$(exec 2>&1; dev_deps_prefix=$empty; in_container() { false; }; test_gpu; echo ran) &&
		rc=0 || rc=$?
	assert_eq "$rc" 77 "$out"
	out=$(exec 2>&1; dev_deps_prefix=$empty; in_container() { false; }; test_gpumetal; echo ran) &&
		rc=0 || rc=$?
	assert_eq "$rc" 77 "$out"
}

# Test that each gate a planted file names without a runner has an owning
# ticket, and skip, naming those gates and tickets.
test_later_gates()
{
	local f line later=() orphans=()
	while IFS= read -r -d '' f && IFS= read -r line; do
		[[ $line =~ ^//\ gate\ ([a-z]+)(\ \((#[0-9]+)\))?: ]] || continue
		declare -F "gate_${BASH_REMATCH[1]}" >/dev/null && continue
		if [[ -z ${BASH_REMATCH[3]} ]]; then
			orphans+=("${BASH_REMATCH[1]} in ${f##*/}")
		else
			later+=("${BASH_REMATCH[1]} (${BASH_REMATCH[3]})")
		fi
	done < <(grep -H --null '^// gate ' "$GATES"/*.in)
	[[ ${#orphans[@]} -eq 0 ]] || { fail "no runner and no ticket: ${orphans[*]}"; return; }
	[[ ${#later[@]} -eq 0 ]] || skip "built later: ${later[*]}"
}

# Test that every pattern the fork clang-tidy config enables is proven by a
# planted file: some tidy expectation ends with a [check,...] list naming a
# check the pattern matches.  A glob is proven by one check under it, not
# each (ADR 5, Gate self-test).
test_every_enabled_check_is_planted()
{
	local line pat name found n=0 names=() listed
	while IFS= read -r line; do
		IFS=, read -ra listed <<<"${line##*[}"
		names+=("${listed[@]}")
	done < <(grep -h '^// gate tidy: .*\[' "$GATES"/*.in)
	while IFS= read -r pat; do
		n=$((n + 1))
		found=0
		for name in "${names[@]}"; do
			# shellcheck disable=SC2053  # $pat is a glob, as in the config
			[[ $name == $pat ]] && found=1
		done
		[[ $found == 1 ]] || fail "no planted file proves $pat"
	done < <(sed -nE 's/^  ([a-z][a-z0-9.*-]*),?$/\1/p' "$DEV_DIR/style/clang-tidy")
	[[ $n -gt 0 ]] || fail "no enabled check found in tools/dev/style/clang-tidy"
}

# Test that the checks the fork clang-tidy config enables on the pinned LLVM
# are exactly those tools/dev/style/clang-tidy-checks lists, so that an LLVM
# upgrade adding, moving or removing a check under a glob (LLVM 23 moved the
# hicpp checks into bugprone) changes the list in a commit of its own.
test_enabled_checks_match_committed_list()
{
	local tidy out
	tidy=$(dev_llvm_tool clang-tidy)
	need "$tidy"
	out=$(diff <("$tidy" --experimental-custom-checks --list-checks --config-file="$DEV_DIR/style/clang-tidy" 2>/dev/null |
		sed -n 's/^    //p') "$DEV_DIR/style/clang-tidy-checks" 2>&1) ||
		fail "enabled checks (<) differ from style/clang-tidy-checks (>); review each, then" \
			"update the list: $out"
}

# Run "$@" up to three times, printing each run's wall clock as $2, until
# one takes under $1 s; fail if none does.  The shell tests run in
# parallel, so one slow run can be the load of the others; three are not.
# The budgets are the Mac's (ADR 5): elsewhere, in the container under
# amd64 emulation among them, all three runs are timed and printed only.
within_budget()
{
	local budget=$1 label=$2 start secs i
	shift 2
	for i in 1 2 3; do
		start=$EPOCHREALTIME
		"$@" || { fail "$label: $* failed"; return; }
		secs=$(awk -v a="$start" -v b="$EPOCHREALTIME" 'BEGIN {printf "%.2f", b - a}')
		echo "    $label, run $i: $secs s"
		[[ $dev_os == darwin ]] || continue
		awk -v s="$secs" -v b="$budget" 'BEGIN {exit !(s < b)}' && return 0
	done
	if [[ $dev_os != darwin ]]; then
		echo "    $label: $budget s budget enforced on macOS only"
		return 0
	fi
	fail "$label took over its $budget s budget in three runs"
}

# Test the editor tier: clangd --check on a fork file reports the fork
# clang-tidy checks from the fork directory's .clang-tidy, the clangd
# process taking under 1 s of wall clock; the same file in an upstream
# directory gets none of them.
test_editor_tier_clangd_uses_fork_config_within_budget()
{
	local r clangd
	clangd=$(dev_llvm_tool clangd)
	need "$clangd"
	r=$(fork_repo) || die "fork_repo failed"
	cp "$GATES/owning_raw_pointer.cpp.in" "$r/src/owner.cpp"
	cp "$GATES/owning_raw_pointer.cpp.in" "$r/common/owner.cpp"
	cp "$REPO_ROOT/.clang-tidy" "$r/.clang-tidy"
	# clangd --check exits non-zero on a file with diagnostics.
	clangd_check()
	{
		(cd "$r" && "$clangd" --check="$1" > "$r/out" 2>&1) || true
	}
	within_budget 1 "editor tier (clangd)" clangd_check src/owner.cpp
	assert_contains "$(cat "$r/out")" "[cppcoreguidelines-owning-memory]"
	clangd_check common/owner.cpp
	assert_contains "$(cat "$r/out")" "All checks completed"
	assert_not_contains "$(cat "$r/out")" "cppcoreguidelines-"
}

# Test the pre-commit tier: the real pre-commit hook, installed by setup.sh
# in a fork repository, commits a typical fork diff (the representative
# file and two small ones) in under 10 s.
test_precommit_tier_within_budget()
{
	local r
	need "${gate_tool[tidy]}"
	r=$(fork_repo) || die "fork_repo failed"
	(cd "$r" && tools/dev/setup.sh >/dev/null)
	cp "$GATES/clean.cpp.in" "$r/src/clean.cpp"
	printf 'int one()\n{\n\treturn 1;\n}\n' > "$r/src/one.cpp"
	printf 'int two()\n{\n\treturn 2;\n}\n' > "$r/src/two.cpp"
	tgit -C "$r" add src
	# Commit the diff, then undo the commit, keeping the diff staged.
	# shellcheck disable=SC2329  # run by within_budget
	commit_diff()
	{
		tgit -C "$r" commit -q -m 'fork diff' > "$r/out" 2>&1 &&
			tgit -C "$r" reset -q --soft HEAD~1
	}
	within_budget 10 "pre-commit hook" commit_diff
	assert_contains "$(cat "$r/out")" "== staged"
}

run_tests

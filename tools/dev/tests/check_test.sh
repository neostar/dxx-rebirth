#!/usr/bin/env bash
# Command-line behaviour of tools/dev/check.sh and the pre-commit hook,
# run against a throwaway repository holding a copy of tools/dev.
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"
# check.sh's doctor step reads free disk in Docker's VM from a running
# daemon; these tests leave the real one alone.
export DOCKER_HOST=unix://$_scratch_root/no-docker.sock
# Running shellcheck over the copy of tools/dev is the slowest fast step (9 s in
# the container, per run) and only the shellcheck test needs it: the
# others run check.sh with it a SKIP.  check.sh's own shellcheck step
# still covers these scripts.
export CHECK_SHELLCHECK=no-such-shellcheck

# Make a throwaway repo with tools/dev copied in (no tests, so check.sh
# does not recurse into this file) and the fork's hooks installed.  Its
# fork-dirs lists tools/ alone, whatever the real one lists: the other fork
# directories are not copied in.
make_dev_repo()
{
	local r
	r=$(make_repo) || die "make_repo failed"
	mkdir -p "$r/tools/dev/tests"
	cp "$DEV_DIR"/*.sh "$r/tools/dev/"
	cp "$DEV_DIR/repo-config" "$DEV_DIR/toolchain.pins" "$DEV_DIR/known-failures" \
		"$DEV_DIR/requirements.lock" "$DEV_DIR/commit-size-exclude" "$DEV_DIR/commit-size-allow" "$DEV_DIR/gpu-programs" \
		"$DEV_DIR/gpu_debug_mode.py" "$DEV_DIR/gpu_wrapper.py" "$DEV_DIR/check_sdl_arms.py" "$r/tools/dev/"
	# The real allowlist names upstream files the throwaway repository lacks.
	: > "$r/tools/dev/sdl-arms-allow"
	# It builds no GPU programs: the tests register their own.
	grep '^#' "$DEV_DIR/gpu-programs" > "$r/tools/dev/gpu-programs"
	# It holds no rtrace programs either: a stub SKIPs their tests.
	printf '#!/bin/sh\necho "SKIP rtrace tests ($1): a stub in the throwaway repository"\n' \
		> "$r/tools/dev/rtrace-tests.sh"
	cp -R "$DEV_DIR/hooks" "$DEV_DIR/style" "$r/tools/dev/"
	printf 'tools\n' > "$r/tools/dev/fork-dirs"
	cp "$DEV_DIR/style/clang-tidy" "$r/tools/.clang-tidy"
	cp "$DEV_DIR/tests/lib.sh" "$r/tools/dev/tests/"
	cp "$REPO_ROOT/.gitleaks.toml" "$r/"
	(cd "$r" && tools/dev/setup.sh >/dev/null)
	printf '%s\n' "$r"
}

# Test that the throwaway repository does not depend on the real fork-dirs:
# with a fork directory listed outside tools/, which the throwaway
# repository does not hold, the fast subset still passes there.
test_dev_repo_ignores_real_fork_dirs()
{
	local d r out
	d=$(make_scratch) || die "make_scratch failed"
	cp -R "$DEV_DIR" "$d/dev"
	printf 'tools\ncommon/debug\n' > "$d/dev/fork-dirs"
	r=$(DEV_DIR=$d/dev make_dev_repo) || die "make_dev_repo failed"
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that the fast subset passes on a clean index and names its steps.
test_fast_passes_clean_index()
{
	local r out
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf 'int x;\n' > "$r/a.cpp"
	tgit -C "$r" add a.cpp
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "guard"
}

# Replace dev repository $1's doctor.sh with one that prints the remaining
# arguments as lines and exits with the status in the first.
fake_doctor()
{
	local r=$1 rc=$2; shift 2
	{ echo '#!/bin/sh'; printf "echo '%s'\n" "$@"; echo "exit $rc"; } > "$r/tools/dev/doctor.sh"
}

# Test that a doctor.sh FAIL line that tools/dev/known-failures lists for
# this platform prints as KNOWN FAIL with the decision it waits for and
# does not fail the doctor step; another FAIL beside it, a listed one on
# another platform, or doctor.sh failing with no FAIL line does.
test_known_doctor_failure_reported_not_failed()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '# comment\n%s FAIL libs lib/a.dylib: * -- owner: rebuild a\nother FAIL x -- y\n' \
		"$dev_os" > "$r/tools/dev/known-failures"
	fake_doctor "$r" 1 'ok   cmake 4.4.4' 'FAIL libs lib/a.dylib: /gone/b.dylib lies outside'
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "KNOWN FAIL libs lib/a.dylib: /gone/b.dylib lies outside (owner: rebuild a)"
	fake_doctor "$r" 1 'FAIL libs lib/a.dylib: /gone/b.dylib lies outside' 'FAIL x'
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "failed: doctor"
	fake_doctor "$r" 2 'usage'
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "failed: doctor"
}

# Test that the fast subset fails when licensed data is staged.
test_fast_fails_on_staged_game_data()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf 'x' > "$r/descent2.hog"
	tgit -C "$r" add descent2.hog
	out=$(cd "$r" && tools/dev/check.sh --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "descent2.hog"
}

# Test that the fast subset runs every *_test.sh and fails if one fails.
test_fast_runs_shell_tests()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/usr/bin/env bash\necho ran-passing\n' > "$r/tools/dev/tests/pass_test.sh"
	printf '#!/usr/bin/env bash\necho ran-failing; exit 3\n' > "$r/tools/dev/tests/zfail_test.sh"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "ran-passing"
	assert_contains "$out" "ran-failing"
	assert_contains "$out" "zfail_test.sh"
}

# Test that shell tests run concurrently, DXX_JOBS at a time, and that
# each one's output is printed whole and in file order: a_test waits for a
# file b_test writes, so it passes only alongside b_test.
test_fast_runs_shell_tests_in_parallel()
{
	local r d out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	d=$(make_scratch) || die "make_scratch failed"
	printf '#!/usr/bin/env bash
echo a-start
for _ in $(seq 50); do [[ -e %s/b ]] && { echo a-done; exit 0; }; sleep 0.1; done
exit 1
' \
		"$d" > "$r/tools/dev/tests/a_test.sh"
	printf '#!/usr/bin/env bash
echo b-start
touch %s/b
echo b-done
' "$d" > "$r/tools/dev/tests/b_test.sh"
	out=$(DXX_JOBS=2 "$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" $'-- tests/a_test.sh\na-start\na-done\n-- tests/b_test.sh\nb-start\nb-done'
	rm -f "$d/b"
	out=$(DXX_JOBS=1 "$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAILED: tests/a_test.sh"
}

# Test that each shell test runs under no-window.sh: a test taking focus
# fails the check.
test_fast_runs_shell_tests_windowless()
{
	local r d out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	printf '#!/usr/bin/env bash\necho $$ > "%s/pid"; sleep 1\n' "$d" > "$r/tools/dev/tests/focus_test.sh"
	out=$(NO_WINDOW_FRONTMOST_CMD=$(run_sampler "$d") NO_WINDOW_REPORTS_DIR="$d/reports" \
		"$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "intruder"
	assert_contains "$out" "FAILED: tests/focus_test.sh"
}

# Test that the fast subset checks the format of every fork file, staged or
# not, with nothing staged.
test_fast_fails_on_misformatted_fork_code()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf 'static int twice(const int& x) {\n    return x*2;\n}\n' > "$r/tools/bad.cpp"
	out=$(cd "$r" && tools/dev/check.sh --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "bad.cpp"
	assert_eq "$(grep '^check.sh: failed' <<<"$out")" 'check.sh: failed: format' "$out"
}

# Test that the fast subset runs the pre-commit gate on the staged content
# of fork files: a misformatted file staged, then formatted on disk, passes
# the format step and fails the staged one.
test_fast_runs_gate_on_staged_fork_code()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf 'static int twice(const int& x) {\n    return x*2;\n}\n' > "$r/tools/bad.cpp"
	tgit -C "$r" add tools/bad.cpp
	"$r/tools/dev/format.sh"
	out=$(cd "$r" && tools/dev/check.sh --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "tools/bad.cpp: not formatted"
	assert_eq "$(grep '^check.sh: failed' <<<"$out")" 'check.sh: failed: staged' "$out"
}

# Test that the full checks run the shell tests with DXX_BUILD_TESTS=1 and
# the tidy step on every fork file, with a fake build.sh: a NOLINT without a
# reason in a C file, which clang-tidy itself skips, fails the tidy step.
test_full_runs_build_tests_and_tidy()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/bin/sh\nexit 0\n' > "$r/tools/dev/build.sh"
	printf '#!/usr/bin/env bash\necho "build_tests=${DXX_BUILD_TESTS-}"\n' \
		> "$r/tools/dev/tests/env_test.sh"
	printf 'int probe(void); // NOLINT\n' > "$r/tools/probe.c"
	"$r/tools/dev/format.sh"
	out=$("$r/tools/dev/check.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "build_tests=1"
	assert_contains "$out" "tools/probe.c:1: NOLINT without a check and a reason"
	assert_eq "$(grep '^check.sh: failed' <<<"$out")" 'check.sh: failed: tidy' "$out"
}

# Test that the full checks generate kconfig.udlr.h from clean through the
# dev profile: a fake build.sh records its arguments and whether the stale
# outputs were gone when that call ran.
test_full_generates_kconfig_header_from_clean()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	mkdir -p "$r/build/dev/d2x-rebirth"
	touch "$r/build/dev/d2x-rebirth/kconfig.ui-table.i" "$r/build/dev/d2x-rebirth/kconfig.udlr.h"
	printf '#!/bin/sh\ncd "$(dirname "$0")/../.."\necho "build.sh $*" >> calls
case $* in *kconfig*) for f in build/dev/d2x-rebirth/kconfig.*; do
	[ -e "$f" ] && echo "stale $f" >> calls; done ;; esac
exit 0\n' > "$r/tools/dev/build.sh"
	out=$("$r/tools/dev/check.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "-- kconfig: "
	assert_contains "$(cat "$r/calls")" "build.sh dev build/dev/d2x-rebirth/kconfig.udlr.h"
	assert_not_contains "$(cat "$r/calls")" "stale"
}

# Test that the full checks time each step and fail, naming the budget,
# when they take longer than the check tier's budget (ADR 5), here
# DXX_CHECK_BUDGET=-1 s; the fast subset keeps no budget, and the
# default budget is 900 s.
test_full_fails_over_its_budget()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/bin/sh\nexit 0\n' > "$r/tools/dev/build.sh"
	out=$(DXX_CHECK_BUDGET=-1 "$r/tools/dev/check.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "-- unit_tests: "
	[[ $out =~ check.sh:\ full\ checks\ took\ [0-9]+\ s,\ budget\ -1\ s ]] || fail "no timing: $out"
	assert_contains "$out" "check.sh: failed: budget"
	out=$(DXX_CHECK_BUDGET=-1 "$r/tools/dev/check.sh" --fast 2>&1)
	assert_not_contains "$out" "budget"
	out=$(env -u DXX_CHECK_BUDGET "$r/tools/dev/check.sh" 2>&1)
	assert_contains "$out" ", budget 900 s"
}

# Test that the fast subset runs shellcheck on scripts in the fork
# directories, found by extension or by shebang.
test_fast_fails_on_shellcheck_finding()
{
	local r out rc
	command -v shellcheck >/dev/null || skip 'shellcheck not installed'
	export CHECK_SHELLCHECK=shellcheck
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/usr/bin/env bash\ncd "$1"\n' > "$r/tools/bad.sh"
	printf '#!/bin/sh\ncd "$1"\n' > "$r/tools/noext"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "tools/bad.sh"
	assert_contains "$out" "tools/noext"
	assert_contains "$out" "SC2164"
	assert_contains "$out" "failed: shellcheck"
}

# Test that a missing shellcheck is a visible SKIP, not a failure.
test_fast_skips_shellcheck_when_absent()
{
	local r out
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/usr/bin/env bash\ncd "$1"\n' > "$r/tools/bad.sh"
	out=$(CHECK_SHELLCHECK=no-such-shellcheck "$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP shellcheck"
}

# Make a dev repo whose upstream/master is its tools commit, plus one
# fork-only commit on top; print the repo, the upstream commit and the
# fork commit.
make_forked_dev_repo()
{
	make_forked_dev_repo_in "$(make_dev_repo)"
}

# make_forked_dev_repo on dev repo $1.
make_forked_dev_repo_in()
{
	local r=$1
	tgit -C "$r" add -A && tgit -C "$r" commit -q --no-verify -m tools
	tgit -C "$r" update-ref refs/remotes/upstream/master HEAD
	tgit -C "$r" commit -q --no-verify --allow-empty -m 'fork change'
	printf '%s %s %s\n' "$r" "$(tgit -C "$r" rev-parse HEAD~1)" "$(tgit -C "$r" rev-parse HEAD)"
}

# Test that the fast subset fails on a fork-only commit hash cited in
# docs/, CLAUDE.md or GLOSSARY.md, naming file and hash, and accepts
# upstream hashes and hex that is no commit.
test_fast_fails_on_fork_commit_hash_in_docs()
{
	local r up fork out rc
	read -r r up fork < <(make_forked_dev_repo)
	[[ -n $fork ]] || die "make_forked_dev_repo failed"
	mkdir -p "$r/docs/agents"
	printf 'Like %s and (%s).\nsha256 %s, pin %s.\n' "${up:0:9}" "$up" \
		"$(printf x | shasum -a 256 | cut -c1-64)" 0123456789abcdef0123456789abcdef01234567 \
		> "$r/docs/agents/conventions.md"
	printf 'See %s.\n' "${up:0:7}" > "$r/CLAUDE.md"
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	printf 'Done in %s.\n' "${fork:0:9}" > "$r/GLOSSARY.md"
	printf 'And %s.\n' "$fork" >> "$r/docs/agents/conventions.md"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "GLOSSARY.md: ${fork:0:9}"
	assert_contains "$out" "docs/agents/conventions.md: $fork"
	assert_contains "$out" "failed: stale_hashes"
}

# Test that the hash check is a visible SKIP without upstream/master.
test_fast_skips_hash_check_without_upstream()
{
	local r up fork out
	read -r r up fork < <(make_forked_dev_repo)
	[[ -n $fork ]] || die "make_forked_dev_repo failed"
	printf 'Done in %s.\n' "${fork:0:9}" > "$r/GLOSSARY.md"
	tgit -C "$r" update-ref -d refs/remotes/upstream/master
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP stale_hashes"
}

# Test that the fast subset fails, naming file and line, on a test that
# runs git outside tgit and real_git, however it is spelt, also with a
# trailing marker; and lets a line through below a whole-line marker.
test_fast_fails_on_raw_git_in_tests()
{
	local r out rc body i
	# raw git: what test_git must find
	body=('never() { git -C "$1" status; }' 'x() { "$git" status; }' 'x() { ${git} status; }')
	# raw git: what test_git must find
	body+=('x() { \git status; }' 'x() { "git" status; }' "x() { 'git' status; }" 'g=git')
	# raw git: what test_git must find
	body+=('cmd=(git status)' 'x() { git -C "$1" status; } # raw git: trailing')
	# raw git: what test_git must find
	body+=('x() { /usr/bin/git status; }' 'x() { "${GIT:-git}" status; }' 'G=/usr/bin/git')
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '%s\n' '#!/usr/bin/env bash' "${body[@]}" > "$r/tools/dev/tests/raw_test.sh"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	for i in "${!body[@]}"; do
		assert_contains "$out" "tools/dev/tests/raw_test.sh:$((i + 2)):${body[i]}"
	done
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "test_git"
	printf '%s\n' '#!/usr/bin/env bash' '# raw git: a test' "${body[0]}" \
		'y() { tgit -C "$1" status; }' 'echo .git real_git git-dir' \
		> "$r/tools/dev/tests/raw_test.sh"
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_not_contains "$out" "tools/dev/tests/raw_test.sh:"
	assert_not_contains "$(grep '^check.sh: failed' <<<"$out")" "test_git"
}

# Test that the fast subset fails, naming file and line, on a scratch
# directory or repository assigned without a check of its result.
test_fast_fails_on_unchecked_scratch()
{
	local r out rc plant
	r=$(make_dev_repo) || die "make_dev_repo failed"
	# unchecked: planted
	plant=('x() { d=$(make_scratch); }')
	# unchecked: planted
	plant+=('y() { r=$(make_repo) || die "make_repo failed"; b=$(make_ci_repo "$d"); }')
	plant+=('z() { d=$(make_scratch) && [[ -d $d ]] || die; }' '# unchecked: planted' "${plant[0]}")
	printf '%s\n' '#!/usr/bin/env bash' "${plant[@]}" > "$r/tools/dev/tests/raw_test.sh"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "tools/dev/tests/raw_test.sh:2:${plant[0]}"
	assert_contains "$out" "tools/dev/tests/raw_test.sh:3:${plant[1]}"
	assert_not_contains "$out" 'tools/dev/tests/raw_test.sh:4:'
	assert_not_contains "$out" 'tools/dev/tests/raw_test.sh:6:'
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "test_git"
}

# Test that the fast subset fails on fork code creating an SDL_GPU device
# without debug_mode true, naming file and line, and that the check lets
# true through, however the call is spread over lines; a gpu_debug_mode
# only as a constexpr, and a configuration's debug_mode field.
test_fast_fails_on_gpu_device_without_debug_mode()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '%s\n' 'auto *a = SDL_CreateGPUDevice(f(1, 2),' '	true, nullptr);' \
		'auto *b = SDL_CreateGPUDevice(f(1, 2), false, nullptr);' > "$r/tools/gpu.cpp"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "tools/gpu.cpp:3: SDL_CreateGPUDevice without debug_mode true"
	assert_not_contains "$out" "tools/gpu.cpp:1: SDL_CreateGPUDevice"
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "gpu_gate"
	sed -i.bak 's/false, nullptr/debug, nullptr/' "$r/tools/gpu.cpp"
	out=$(python3 "$r/tools/dev/gpu_debug_mode.py" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	sed -i.bak '3d' "$r/tools/gpu.cpp"
	out=$(python3 "$r/tools/dev/gpu_debug_mode.py" "$r" 2>&1)
	assert_eq "$?" 0 "$out"
	# The constant passes only as a constexpr; a local copied from the
	# configuration fails, the configuration's own field passes (#24).
	printf '%s\n' 'void f(const config &c) {' 'const bool gpu_debug_mode{c.debug_mode};' \
		'auto *d = SDL_CreateGPUDevice(f, gpu_debug_mode, nullptr);' '}' > "$r/tools/gpu2.cpp"
	out=$(python3 "$r/tools/dev/gpu_debug_mode.py" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "tools/gpu2.cpp:3: SDL_CreateGPUDevice without debug_mode true"
	sed -i.bak 's/const bool gpu_debug_mode/constexpr bool gpu_debug_mode/' "$r/tools/gpu2.cpp"
	out=$(python3 "$r/tools/dev/gpu_debug_mode.py" "$r" 2>&1)
	assert_eq "$?" 0 "$out"
	# Any declaration other than a constexpr bool fails, however spelled.
	for decl in 'const auto gpu_debug_mode{c.debug_mode};' 'bool const gpu_debug_mode = c.debug_mode;' \
		'constexpr auto gpu_debug_mode = true;' 'static const bool gpu_debug_mode(c.debug_mode);'; do
		printf '%s\n' 'void f(const config &c) {' "$decl" \
			'auto *d = SDL_CreateGPUDevice(f, gpu_debug_mode, nullptr);' '}' > "$r/tools/gpu2.cpp"
		out=$(python3 "$r/tools/dev/gpu_debug_mode.py" "$r" 2>&1) && rc=0 || rc=$?
		assert_eq "$rc" 1 "$decl: $out"
		assert_contains "$out" "tools/gpu2.cpp:3: SDL_CreateGPUDevice without debug_mode true"
	done
	printf '%s\n' 'static constexpr bool gpu_debug_mode = true;' \
		'auto *d = SDL_CreateGPUDevice(f, gpu_debug_mode, nullptr);' > "$r/tools/gpu2.cpp"
	out=$(python3 "$r/tools/dev/gpu_debug_mode.py" "$r" 2>&1)
	assert_eq "$?" 0 "$out"
	printf '%s\n' 'void g(const config &c) {' 'SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, c.debug_mode);' \
		'auto *d = SDL_CreateGPUDeviceWithProperties(p);' '}' > "$r/tools/gpu3.cpp"
	out=$(python3 "$r/tools/dev/gpu_debug_mode.py" "$r" 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that the fast subset fails on an SDL_MAJOR_VERSION chain with an
# SDL2 branch and no SDL3 branch, naming it and the sdl_arms step, and
# passes once the file is on the allowlist.
test_fast_fails_on_sdl2_chain_without_sdl3()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	mkdir -p "$r/common/arch"
	printf '%s\n' '#if SDL_MAJOR_VERSION == 1' '#elif SDL_MAJOR_VERSION == 2' '#endif' > "$r/common/arch/x.cpp"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "common/arch/x.cpp:2: SDL2 branch without an SDL3 or >= 2 branch"
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "sdl_arms"
	printf '%s\n' 'common/arch/x.cpp 1 -- ported later' > "$r/tools/dev/sdl-arms-allow"
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that the GPU debug-mode gate scans game code and Objective-C++
# outside the fork directories but not third_party/, ignores comments,
# accepts the gpu_debug_mode constant, wants the debug-mode property set in
# the function that creates a device from properties, and reports any other
# device creation.
test_gpu_gate_scans_whole_repository()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	mkdir -p "$r/similar/main" "$r/common/arch" "$r/third_party/sdl"
	printf '%s\n' '/* SDL_CreateGPUDevice(f, false, nullptr); */' \
		'// SDL_CreateGPUDevice(f, false, nullptr);' \
		'auto *a = SDL_CreateGPUDevice(f, dcx::gpu_debug_mode, nullptr);' \
		'auto *b = SDL_CreateGPUDevice(f, false, nullptr);' > "$r/similar/main/gpu.mm"
	printf '%s\n' 'namespace dcx {' 'SDL_GPUDevice *good(SDL_PropertiesID p)' '{' \
		'	SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true);' \
		'	if (p) { return SDL_CreateGPUDeviceWithProperties(p); }' '	return nullptr;' '}' \
		'SDL_GPUDevice *bad(SDL_PropertiesID p)' '{' \
		'	return SDL_CreateGPUDeviceWithProperties(p);' '}' \
		'auto *c = SDL_CreateGPUDeviceForTesting(p);' '}' > "$r/common/arch/gpu.cpp"
	echo 'auto *d = SDL_CreateGPUDevice(f, false, nullptr);' > "$r/third_party/sdl/gpu.c"
	out=$(python3 "$r/tools/dev/gpu_debug_mode.py" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "similar/main/gpu.mm:4: SDL_CreateGPUDevice without debug_mode true"
	assert_contains "$out" "common/arch/gpu.cpp:10: SDL_CreateGPUDeviceWithProperties without"
	assert_contains "$out" \
		"common/arch/gpu.cpp:12: SDL_CreateGPUDeviceForTesting: a device creation"
	assert_eq "$(wc -l <<<"$out" | tr -d ' ')" 3 "$out"
}

# Test that a device made from properties needs the last debug-mode
# property set before it, on the props object it is given, to be true or
# gpu_debug_mode, and that a creation function named but not called (its
# address taken) fails; a name in a string literal passes.
test_gpu_gate_follows_props_and_flags_taken_address()
{
	local r out rc set=SDL_SetBooleanProperty p=SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN
	r=$(make_repo) || die "make_repo failed"
	printf '%s\n' \
		"void a() { $set(other, $p, true); $set(props, $p, false);" \
		'  SDL_CreateGPUDeviceWithProperties(props); }' \
		"void b() { $set(props, $p, true); $set(props, $p, false);" \
		'  SDL_CreateGPUDeviceWithProperties(props); }' \
		"void c() { SDL_CreateGPUDeviceWithProperties(props); $set(props, $p, true); }" \
		'auto *d = &SDL_CreateGPUDevice;' 'auto e = SDL_CreateGPUDeviceWithProperties;' \
		"void f() { $set(props, $p, false); $set(props, $p, gpu_debug_mode);" \
		'  SDL_CreateGPUDeviceWithProperties(props); SDL_Log("SDL_CreateGPUDevice failed"); }' \
		> "$r/gpu.cpp"
	out=$(python3 "$DEV_DIR/gpu_debug_mode.py" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "gpu.cpp:2: SDL_CreateGPUDeviceWithProperties without"
	assert_contains "$out" "gpu.cpp:4: SDL_CreateGPUDeviceWithProperties without"
	assert_contains "$out" "gpu.cpp:5: SDL_CreateGPUDeviceWithProperties without"
	assert_contains "$out" "gpu.cpp:6: SDL_CreateGPUDevice named but not called"
	assert_contains "$out" "gpu.cpp:7: SDL_CreateGPUDeviceWithProperties named but not called"
	assert_not_contains "$out" "gpu.cpp:9:"
	assert_eq "$(grep -c . <<<"$out")" 5 "$out"
}

# Test that the full run fails when tools/dev/rtrace-tests.sh is missing or
# not executable, naming it, in both steps that run it.
test_full_fails_without_rtrace_tests()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/bin/sh\nexit 0\n' > "$r/tools/dev/build.sh"
	chmod -x "$r/tools/dev/rtrace-tests.sh"
	out=$("$r/tools/dev/check.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace tests (asan): tools/dev/rtrace-tests.sh is missing or not executable"
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "sanitizer_tests"
	rm "$r/tools/dev/rtrace-tests.sh"
	out=$("$r/tools/dev/check.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace tests (asan): tools/dev/rtrace-tests.sh is missing or not executable"
}

# Test that the fast subset fails on game code that sets SDL's
# background-app hint, by its macro or its string, and lets a test's or a
# tool's main() set it (#24: the game is a foreground app).
test_fast_fails_on_background_app_hint_in_game_code()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	mkdir -p "$r/common/render/unittest" "$r/similar/main"
	echo 'int main() { SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1"); }' \
		> "$r/common/render/unittest/gpu.cpp"
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	echo 'void f() { SDL_SetHint("SDL_MAC_BACKGROUND_APP", "1"); }' > "$r/similar/main/game.cpp"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" 'similar/main/game.cpp:1:void f()'
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "gpu_gate"
}

# Test that the fast subset fails on a command in check.sh or
# rtrace-tests.sh that runs a registered GPU program other than as an
# argument of gpu-validate.sh, in the shape step_gpu_tests runs it, also
# with gpu-validate.sh only mentioned on the line (a trailing comment, an
# echo, another command), over a continuation, by a registered path; and
# lets a comment naming one through.
test_fast_fails_on_gpu_program_run_outside_the_wrapper()
{
	local r out rc b bad ok
	r=$(make_dev_repo) || die "make_dev_repo failed"
	echo build/dev/gpu-demo >> "$r/tools/dev/gpu-programs"
	bad=('unwrapped() { "$dev_dir/no-window.sh" "$root/$gpu_program" || rc=1; }'
		'braced() { ${gpu_program}; }' 'c() { "$root/$gpu_program"; } # gpu-validate.sh'
		'e() { echo gpu-validate.sh "$gpu_program"; }'
		'f() { "$dev_dir/gpu-validate.sh" true; "$root/$gpu_program"; }'
		'g() { "$root/$gpu_program" "$dev_dir/gpu-validate.sh"; }' 'h() { build/dev/gpu-demo; }'
		'i() { bounded "$gpu_program"; }')
	# shellcheck disable=SC1003  # a planted line continuation
	ok=('# runs $gpu_program' 'k() { "$dev_dir/no-window.sh" "$dev_dir/gpu-validate.sh" \'
		'	"$root/$gpu_program" || rc=1; }' 'l() { local gpu_program; }'
		'm() { bounded "$dev_dir/no-window.sh" "$dev_dir/gpu-validate.sh" "$gpu_program"; }')
	printf '%s\n' "${bad[@]}" "${ok[@]}" >> "$r/tools/dev/check.sh"
	printf '#!/usr/bin/env bash\n%s\n' 'x() { "$1" "$gpu_program"; }' \
		> "$r/tools/dev/rtrace-tests.sh"
	out=$(python3 "$r/tools/dev/gpu_wrapper.py" "$r/tools/dev/gpu-programs" \
		"$r/tools/dev/check.sh" "$r/tools/dev/rtrace-tests.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	for b in "${bad[@]}"; do
		assert_contains "$out" "$b"
	done
	assert_contains "$out" 'rtrace-tests.sh:2:x() { "$1" "$gpu_program"; }'
	assert_eq "$(grep -c . <<<"$out")" "$((${#bad[@]} + 1))" "$out"
	rm "$r/tools/dev/rtrace-tests.sh"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "gpu_gate"
}

# Test that the full checks build every registered GPU program with
# sdl3=1 and run it under gpu-validate.sh, failing on a validation message;
# SKIP with none registered; fail on a program outside build/dev-sdl3, also
# on a last line without a newline (check.sh --gpu, the gate's gpu part),
# and on a missing gpu-programs.
test_full_runs_gpu_programs_under_gpu_validate()
{
	local r out rc
	[[ $dev_os == darwin ]] || skip "macOS only: gpu_tests is a SKIP elsewhere (#24)"
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/bin/sh\ncd "$(dirname "$0")/../.."\necho "build.sh $*" >> calls\nexit 0\n' \
		> "$r/tools/dev/build.sh"
	echo '# none' > "$r/tools/dev/gpu-programs"
	out=$("$r/tools/dev/check.sh" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP gpu_tests: no GPU program in tools/dev/gpu-programs"
	mkdir -p "$r/build/dev-sdl3"
	printf '%s\n' '#!/bin/sh' "echo 'Validation layers enabled'" "echo 'SDL_GPU Driver: Vulkan'" \
		"echo 'Validation Error: [ VUID-x ] bad'" > "$r/build/dev-sdl3/gpu-probe"
	chmod +x "$r/build/dev-sdl3/gpu-probe"
	echo build/dev-sdl3/gpu-probe >> "$r/tools/dev/gpu-programs"
	out=$("$r/tools/dev/check.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$(cat "$r/calls")" "build.sh dev sdl3=1 build/dev-sdl3/gpu-probe"
	assert_contains "$out" "gpu-validate: gpu-probe: 1 from the driver's validation layer"
	assert_eq "$(grep '^check.sh: failed' <<<"$out")" 'check.sh: failed: gpu_tests' "$out"
	printf 'build/dev/gpu-other' >> "$r/tools/dev/gpu-programs"
	out=$("$r/tools/dev/check.sh" --gpu 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "gpu_tests: build/dev/gpu-other is not a build/dev-sdl3 program"
	rm "$r/tools/dev/gpu-programs"
	out=$("$r/tools/dev/check.sh" --gpu 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "gpu_tests: no tools/dev/gpu-programs"
}

# Test that the full checks build the SDL3 unit tests (macOS) and fail
# when a test program links an SDL3 library from outside the pinned prefix
# (otool -L): a fake build.sh writes the program, a fake otool prints the
# libraries it links.
test_full_fails_on_sdl3_test_linking_outside_pinned_prefix()
{
	local r bin out rc pinned
	[[ $dev_os == darwin ]] || skip "macOS only: otool, and Homebrew's SDL3"
	r=$(make_dev_repo) || die "make_dev_repo failed"
	bin=$(make_scratch) || die "make_scratch failed"
	pinned=$dev_deps_prefix
	printf '#!/bin/sh\ncd "$(dirname "$0")/../.."\necho "build.sh $*" >> calls
case $* in *sdl3=1*) mkdir -p build/dev-sdl3/common
	printf "#!/bin/sh\\n" > build/dev-sdl3/common/test-physfsrwops
	chmod +x build/dev-sdl3/common/test-physfsrwops ;; esac
exit 0\n' > "$r/tools/dev/build.sh"
	printf '#!/bin/sh\necho "$2:"\ncat "%s/otool.out"\n' "$r" > "$bin/otool"
	chmod +x "$bin/otool"
	printf '\t%s\n' '@rpath/libSDL3_image.0.dylib (compatibility version 409.0.0)' \
		"$pinned/lib/libSDL3_mixer.0.dylib (compatibility version 205.0.0)" \
		'/opt/homebrew/opt/physfs/lib/libphysfs.1.dylib (compatibility version 1.0.0)' \
		> "$r/otool.out"
	out=$(PATH="$bin:$PATH" DXX_DEPS_PREFIX=$pinned "$r/tools/dev/check.sh" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$(cat "$r/calls")" "build.sh dev sdl3=1 build/dev-sdl3/common/test-physfsrwops"
	assert_contains "$out" "-- sdl3_link: "
	printf '\t%s\n' '/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib (compatibility version 401.0.0)' \
		>> "$r/otool.out"
	out=$(PATH="$bin:$PATH" DXX_DEPS_PREFIX=$pinned "$r/tools/dev/check.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "sdl3_link: build/dev-sdl3/common/test-physfsrwops links an SDL3 library outside $pinned: /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib"
	assert_eq "$(grep '^check.sh: failed' <<<"$out")" 'check.sh: failed: sdl3_link' "$out"
}

# Test that an unknown option is a usage error with exit status 2.
test_unknown_option_is_usage_error()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	out=$("$r/tools/dev/check.sh" --bogus 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2
	assert_contains "$out" "usage"
}

# Test that the pre-commit hook refuses a commit with game data and
# allows one without it.
test_pre_commit_hook_blocks_game_data()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf 'DHF\0\0\0\0' > "$r/renamed.bin"
	tgit -C "$r" add renamed.bin
	out=$(tgit -C "$r" commit -q -m data 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "renamed.bin"
	tgit -C "$r" rm -q --cached renamed.bin
	printf 'int y;\n' > "$r/b.cpp"
	tgit -C "$r" add b.cpp
	out=$(tgit -C "$r" commit -q -m code 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that the pre-commit hook runs the pre-commit tier only: a failing
# shell test, which fails check.sh --fast, does not block a commit, and
# misformatted staged fork code does.
test_pre_commit_hook_runs_pre_commit_tier_only()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/usr/bin/env bash\nexit 3\n' > "$r/tools/dev/tests/zfail_test.sh"
	printf 'int y;\n' > "$r/b.cpp"
	tgit -C "$r" add b.cpp
	out=$(tgit -C "$r" commit -q -m code 2>&1)
	assert_eq "$?" 0 "$out"
	assert_not_contains "$out" "shell_tests"
	printf 'static int twice(const int& x) {\n    return x*2;\n}\n' > "$r/tools/bad.cpp"
	tgit -C "$r" add tools/bad.cpp
	out=$(tgit -C "$r" commit -q -m bad 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "tools/bad.cpp: not formatted"
}

# Print a fake GitHub token, assembled here so that this file itself holds
# none for a scanner to find.
fake_token()
{
	printf 'gh%s_%s' p aB3dE5fG7hJ9kL1mN2pQ4rS6tU8vW0xY2zA4
}

# Skip the current test unless the pinned gitleaks is in the prefix.
need_gitleaks()
{
	[[ -x $dev_deps_prefix/opt/gitleaks/bin/gitleaks ]] ||
		skip "gitleaks not in $dev_deps_prefix; run tools/dev/deps.sh build"
}

# Test that the pre-commit hook refuses a staged secret, naming the file
# and the rule without printing the secret, and allows a clean commit.
test_pre_commit_hook_blocks_staged_secret()
{
	local r out rc
	need_gitleaks
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf 'token = "%s"\n' "$(fake_token)" > "$r/settings.txt"
	tgit -C "$r" add settings.txt
	out=$(tgit -C "$r" commit -q -m secret 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "settings.txt"
	assert_contains "$out" "github-pat"
	assert_contains "$out" "failed: secrets"
	assert_not_contains "$out" "$(fake_token)"
	tgit -C "$r" rm -q --cached settings.txt
	printf 'int y;\n' > "$r/b.cpp"
	tgit -C "$r" add b.cpp
	out=$(tgit -C "$r" commit -q -m code 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "== secrets"
}

# Test that the pre-push hook scans exactly the commits being pushed: a
# token committed past the pre-commit hook (--no-verify) is refused on
# push, to an existing branch and to a new one, without being printed; a
# clean push and a clean new branch go through.
test_pre_push_hook_blocks_pushed_secret()
{
	local r d out rc
	need_gitleaks
	r=$(make_dev_repo) || die "make_dev_repo failed"
	d=$(make_scratch) || die "make_scratch failed"
	tgit -C "$d" init -q --bare "$d/remote.git" || die "init failed"
	tgit -C "$r" remote add origin "$d/remote.git"
	tgit -C "$r" add -A && tgit -C "$r" commit -q --no-verify -m tools
	out=$(tgit -C "$r" push -q origin main 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "push_secrets: refs/heads/main -> refs/heads/main"
	printf 'token = "%s"\n' "$(fake_token)" > "$r/settings.txt"
	tgit -C "$r" add settings.txt
	tgit -C "$r" commit -q --no-verify -m secret
	out=$(tgit -C "$r" push -q origin main 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "github-pat"
	assert_contains "$out" "failed: push_secrets"
	assert_not_contains "$out" "$(fake_token)"
	out=$(tgit -C "$r" push -q origin HEAD:refs/heads/new 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "github-pat"
	assert_not_contains "$(tgit -C "$d/remote.git" for-each-ref)" "refs/heads/new"
	tgit -C "$r" reset -q --hard HEAD~1
	out=$(tgit -C "$r" push -q origin HEAD:refs/heads/clean 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that the full checks scan the whole history of HEAD: a secret
# committed past the hook fails them, a clean history passes the scan.
test_full_fails_on_secret_in_history()
{
	local r out rc
	need_gitleaks
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/bin/sh\nexit 0\n' > "$r/tools/dev/build.sh"
	out=$("$r/tools/dev/check.sh" 2>&1)
	assert_contains "$out" "== secrets_history"
	assert_not_contains "$(grep '^check.sh: failed' <<<"$out")" "secrets_history"
	printf 'token = "%s"\n' "$(fake_token)" > "$r/settings.txt"
	tgit -C "$r" add settings.txt
	tgit -C "$r" commit -q --no-verify -m secret
	tgit -C "$r" rm -q settings.txt
	tgit -C "$r" commit -q --no-verify -m removed
	out=$("$r/tools/dev/check.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "github-pat"
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "secrets_history"
	assert_not_contains "$out" "$(fake_token)"
}

# Test that without the pinned gitleaks or one at its version on PATH
# the pre-commit tier fails, naming how to install it (no commit goes in
# unscanned), the fast checks SKIP, and the reference image
# (DXX_REFERENCE_IMAGE=1), which must have it, fails.
test_secrets_without_gitleaks_fail_pre_commit_skip_elsewhere()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	export CHECK_GITLEAKS=no-such-gitleaks CHECK_GITLEAKS_FALLBACK=no-such-gitleaks
	out=$("$r/tools/dev/check.sh" --pre-commit 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAIL secrets: gitleaks not found (no-such-gitleaks)"
	assert_contains "$out" "tools/dev/deps.sh all"
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_contains "$out" "SKIP secrets: gitleaks not found (no-such-gitleaks)"
	out=$(DXX_REFERENCE_IMAGE=1 "$r/tools/dev/check.sh" --pre-commit 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "the reference image must have it"
}

# Test that the pre-commit tier falls back to a gitleaks on PATH at the
# pinned version, and fails on one at another version.
test_secrets_fall_back_to_pinned_gitleaks_on_path()
{
	local r d out rc pin
	r=$(make_dev_repo) || die "make_dev_repo failed"
	d=$(make_scratch) || die "make_scratch failed"
	pin=$(awk '$2 == "gitleaks" {print $4; exit}' "$DEV_DIR/toolchain.pins" |
		sed -nE 's|.*/download/v([0-9.]+)/.*|\1|p')
	printf '#!/bin/sh\n%s\necho fake-gitleaks-ran\n' \
		'[ "$1" = version ] && { echo "$FAKE_VERSION"; exit 0; }' > "$d/fake-gitleaks"
	chmod +x "$d/fake-gitleaks"
	export CHECK_GITLEAKS=no-such-gitleaks CHECK_GITLEAKS_FALLBACK=$d/fake-gitleaks
	out=$(FAKE_VERSION=$pin "$r/tools/dev/check.sh" --pre-commit 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "fake-gitleaks-ran"
	out=$(FAKE_VERSION=1.0.0 "$r/tools/dev/check.sh" --pre-commit 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "is '1.0.0', not the pinned $pin"
}

# Test that a .gitleaksignore file or an inline allow comment fails both
# tiers unless .gitleaks.toml lists its file with a reason.
test_gitleaks_allow_needs_a_listed_reason()
{
	local r out rc marker='gitleaks'':allow'
	r=$(make_dev_repo) || die "make_dev_repo failed"
	export CHECK_GITLEAKS=no-such-gitleaks CHECK_GITLEAKS_FALLBACK=no-such-gitleaks
	printf 'x = 1 # %s\n' "$marker" > "$r/a.txt"
	printf 'abc:rule:1\n' > "$r/.gitleaksignore"
	tgit -C "$r" add a.txt .gitleaksignore
	out=$(DXX_REFERENCE_IMAGE="" "$r/tools/dev/check.sh" --pre-commit 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "a.txt: a .gitleaksignore or $marker comment"
	assert_contains "$out" ".gitleaksignore: a .gitleaksignore"
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "gitleaks_allow"
	printf '# allowed: a.txt -- a fixture\n# allowed: .gitleaksignore --\n' >> "$r/.gitleaks.toml"
	out=$("$r/tools/dev/check.sh" --pre-commit 2>&1)
	assert_not_contains "$out" "a.txt: a .gitleaksignore"
	assert_contains "$out" ".gitleaksignore: a .gitleaksignore"
}

# Test that setup.sh installs the hooks and turns rerere off in a fresh
# clone: it once replayed a bad resolution.
test_setup_installs_hooks()
{
	local r
	r=$(make_dev_repo) || die "make_dev_repo failed"
	tgit -C "$r" config --unset core.hooksPath
	(cd "$r" && tools/dev/setup.sh >/dev/null)
	assert_eq "$(tgit -C "$r" config core.hooksPath)" "tools/dev/hooks"
	assert_eq "$(tgit -C "$r" config rerere.enabled)" "false"
}

# Write fakes of xcrun and xcodebuild into directory $1: `xcrun metal
# --version` succeeds when $1/metal exists, xcodebuild logs its arguments.
fake_xcode()
{
	printf '#!/bin/sh\n[ "$*" = "metal --version" ] && [ -e "%s/metal" ]\n' "$1" > "$1/xcrun"
	printf '#!/bin/sh\necho "$*" >> "%s/xcodebuild.log"\n' "$1" > "$1/xcodebuild"
	chmod +x "$1/xcrun" "$1/xcodebuild"
}

# Test that setup.sh on macOS downloads Xcode's Metal toolchain, which
# tools/shaders/check.sh compiles MSL with, only when xcrun cannot run
# metal.  This is setup.sh's default, outside the tests (lib.sh).
test_setup_installs_metal_toolchain()
{
	local r bin
	[[ $dev_os == darwin ]] || skip "macOS only: the Metal toolchain is Xcode's"
	r=$(make_dev_repo) || die "make_dev_repo failed"
	bin=$(make_scratch) || die "make_scratch failed"
	fake_xcode "$bin"
	(cd "$r" && PATH="$bin:$PATH" env -u DXX_SETUP_NO_DOWNLOAD tools/dev/setup.sh >/dev/null)
	assert_eq "$(cat "$bin/xcodebuild.log" 2>/dev/null)" "-downloadComponent MetalToolchain"
	rm -f "$bin/xcodebuild.log"
	: > "$bin/metal"
	(cd "$r" && PATH="$bin:$PATH" env -u DXX_SETUP_NO_DOWNLOAD tools/dev/setup.sh >/dev/null)
	assert_eq "$(cat "$bin/xcodebuild.log" 2>/dev/null)" ""
}

# Test that no test reaches the real xcodebuild: setup.sh, which
# make_dev_repo and other tests run with the real PATH, downloads nothing
# under lib.sh, even where xcrun cannot run metal.
test_setup_in_tests_downloads_nothing()
{
	local r bin
	[[ $dev_os == darwin ]] || skip "macOS only: the Metal toolchain is Xcode's"
	bin=$(make_scratch) || die "make_scratch failed"
	fake_xcode "$bin"
	r=$(PATH="$bin:$PATH" make_dev_repo) || die "make_dev_repo failed"
	(cd "$r" && PATH="$bin:$PATH" tools/dev/setup.sh >/dev/null)
	assert_eq "$(cat "$bin/xcodebuild.log" 2>/dev/null)" ""
}

# Test that the fast subset fails, naming the key, when the repository's
# local config deviates from what setup.sh records.
test_fast_fails_on_repo_config_deviation()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	tgit -C "$r" config user.name intruder
	tgit -C "$r" config commit.gpgsign false
	tgit -C "$r" config --unset rerere.enabled
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "user.name"
	assert_contains "$out" "commit.gpgsign"
	assert_contains "$out" "rerere.enabled"
	assert_contains "$out" "failed: repo_config"
}

# Test that a worktree's check reads the shared config of its main clone.
test_fast_checks_repo_config_from_worktree()
{
	local r wt out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	tgit -C "$r" add -A && tgit -C "$r" commit -q --no-verify -m tools
	wt=$(make_scratch) || die "make_scratch failed"; wt=$wt/wt
	tgit -C "$r" worktree add -q -b side "$wt"
	out=$("$wt/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	tgit -C "$r" config user.email intruder@example.invalid
	out=$(cd "$wt" && tools/dev/check.sh --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "user.email"
}

# Test that the shell tests and check.sh, run with GIT_DIR and
# GIT_INDEX_FILE exported to a decoy repository as inside a git hook, leave
# the decoy's config and HEAD unchanged.
test_hook_environment_does_not_leak_into_decoy_repo()
{
	local decoy r config_before head_before
	decoy=$(make_repo) || die "make_repo failed"
	r=$(make_dev_repo) || die "make_dev_repo failed"
	config_before=$(cat "$decoy/.git/config")
	head_before=$(tgit -C "$decoy" rev-parse HEAD)
	GIT_DIR="$decoy/.git" GIT_INDEX_FILE="$decoy/.git/index" \
		bash "$DEV_DIR/tests/guard_test.sh" >/dev/null 2>&1
	GIT_DIR="$decoy/.git" GIT_INDEX_FILE="$decoy/.git/index" \
		bash -c 'source "$1"; make_repo >/dev/null' _ "$DEV_DIR/tests/lib.sh"
	(cd "$r" && GIT_DIR="$decoy/.git" GIT_INDEX_FILE="$decoy/.git/index" \
		tools/dev/check.sh --fast >/dev/null 2>&1)
	assert_eq "$(cat "$decoy/.git/config")" "$config_before"
	assert_eq "$(tgit -C "$decoy" rev-parse HEAD)" "$head_before"
}

# Test that the shell tests run without any of git's repository variables,
# whether or not the test sources lib.sh.
test_shell_tests_see_no_git_repository_variables()
{
	local r out
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/usr/bin/env bash\necho "ns=${GIT_NAMESPACE-unset} alt=${GIT_ALTERNATE_OBJECT_DIRECTORIES-unset}"\n' \
		> "$r/tools/dev/tests/env_test.sh"
	out=$(cd "$r" && GIT_NAMESPACE=decoy GIT_ALTERNATE_OBJECT_DIRECTORIES=/nonexistent \
		tools/dev/check.sh --fast 2>&1)
	assert_contains "$out" "ns=unset alt=unset"
}

# Print the ASAN_OPTIONS a shell test sees under check.sh when uname -s
# reports $1.
asan_options_on()
{
	local r bin
	r=$(make_dev_repo) || die "make_dev_repo failed"
	bin=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\necho %s\n' "$1" > "$bin/uname"; chmod +x "$bin/uname"
	printf '#!/usr/bin/env bash\necho "asan=$ASAN_OPTIONS"\n' > "$r/tools/dev/tests/env_test.sh"
	# A faked Darwin on Linux sends no-window.sh to its macOS sampler:
	# give it a fake one, so the test runs the same on both.
	(cd "$r" && env -u ASAN_OPTIONS PATH="$bin:$PATH" \
		NO_WINDOW_FRONTMOST_CMD="$(fake_sampler "$bin" '1 Terminal')" \
		NO_WINDOW_REPORTS_DIR="$bin" tools/dev/check.sh --fast 2>&1) | grep '^asan='
}

# Test that LeakSanitizer stays on where it works and is off on macOS.
test_leak_detection_on_except_macos()
{
	assert_contains "$(asan_options_on Linux)" "detect_leaks=1"
	assert_contains "$(asan_options_on Darwin)" "detect_leaks=0"
	assert_contains "$(asan_options_on Linux)" "abort_on_error=0"
}

# Test that shell tests run in a clean environment: a variable outside the
# allowlist, an unknown DXX_* among them, is invisible to them; the named
# DXX_* settings and no-window.sh's SDL_MAC_BACKGROUND_APP get through.
test_shell_tests_see_only_allowlisted_environment()
{
	local r out
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/usr/bin/env bash\necho "token=%s dxx=%s jobs=%s sdl=%s"\n' \
		'${SECRET_TOKEN-unset}' '${DXX_DECOY-unset}' '${DXX_JOBS-unset}' \
		'${SDL_MAC_BACKGROUND_APP-unset}' > "$r/tools/dev/tests/env_test.sh"
	out=$(SECRET_TOKEN=decoy DXX_DECOY=leaked DXX_JOBS=3 "$r/tools/dev/check.sh" --fast 2>&1)
	assert_contains "$out" "token=unset dxx=unset jobs=3 sdl=1"
}

# Test that the fast subset fails on a workflow upstream/master does not
# have, naming it, and accepts upstream's own workflows.
test_fast_fails_on_new_github_workflow()
{
	local r up fork out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	mkdir -p "$r/.github/workflows"
	echo 'on: push' > "$r/.github/workflows/ci.yml"
	read -r r up fork < <(make_forked_dev_repo_in "$r")
	[[ -n $fork ]] || die "make_forked_dev_repo failed"
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	echo 'on: push' > "$r/.github/workflows/fork.yml"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" ".github/workflows/fork.yml: a GitHub workflow upstream does not have"
	assert_not_contains "$out" "ci.yml:"
	assert_contains "$out" "failed: workflows"
}

# Test that the workflow check is a visible SKIP without upstream/master.
test_fast_skips_workflow_check_without_upstream()
{
	local r up fork out
	read -r r up fork < <(make_forked_dev_repo)
	[[ -n $fork ]] || die "make_forked_dev_repo failed"
	mkdir -p "$r/.github/workflows"
	echo 'on: push' > "$r/.github/workflows/fork.yml"
	tgit -C "$r" update-ref -d refs/remotes/upstream/master
	out=$("$r/tools/dev/check.sh" --fast 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP workflows"
}

# Test that the pre-commit hook rejects a change over 300 lines, not
# counting generated lockfiles, unless DXX_ALLOW_LARGE_COMMIT gives a
# reason, which it prints.
test_pre_commit_hook_limits_commit_size()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	# A generated file of this repository's own, beside requirements.lock,
	# which doctor.sh checks against the venv.
	echo gen.lock >> "$r/tools/dev/commit-size-exclude"
	tgit -C "$r" add -A && tgit -C "$r" commit -q --no-verify -m tools
	seq 301 > "$r/big.txt"
	tgit -C "$r" add big.txt
	out=$(tgit -C "$r" commit -q -m big 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "301 changed lines, over 300"
	out=$(DXX_ALLOW_LARGE_COMMIT='one generated table' tgit -C "$r" commit -q -m big 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "301 changed lines allowed: one generated table"
	seq 400 > "$r/gen.lock"
	seq 300 > "$r/small.txt"
	tgit -C "$r" add gen.lock small.txt
	out=$(tgit -C "$r" commit -q -m lock 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a rebase -i squash and fixup joining small commits into one
# over 300 lines, which git makes without the pre-commit hook, fails the
# fast checks' commit_sizes step, naming it; a Large-commit: trailer or an
# allow-list entry with a reason lets it through.
test_fast_fails_on_large_commit_made_by_squash()
{
	local r up fork out rc i
	read -r r up fork < <(make_forked_dev_repo)
	[[ -n $fork ]] || die "make_forked_dev_repo failed"
	for i in 1 2 3; do
		seq 120 | sed "s/^/$i /" > "$r/part$i.txt"
		tgit -C "$r" add "part$i.txt"
		tgit -C "$r" commit -q -m "part $i" || die "commit $i failed"
	done
	GIT_SEQUENCE_EDITOR="sed -i.bak -e '/part 2/s/^pick/squash/' -e '/part 3/s/^pick/fixup/'" \
		GIT_EDITOR=true tgit -C "$r" rebase -q -i HEAD~3 >/dev/null || die "rebase failed"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "commit-size: 360 changed lines, over 300: part 1"
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "commit_sizes"
	printf 'part 1 -- three parts of one table\n' >> "$r/tools/dev/commit-size-allow"
	out=$(cd "$r" && tools/dev/commit-size.sh --history upstream/master 2>&1)
	assert_eq "$?" 0 "$out"
	sed -i.bak '$d' "$r/tools/dev/commit-size-allow"
	tgit -C "$r" commit -q --amend -m 'part 1' -m 'Large-commit: three parts of one table' --no-verify
	out=$(cd "$r" && tools/dev/commit-size.sh --history upstream/master 2>&1)
	assert_eq "$?" 0 "$out"
	out=$(cd "$r" && tools/dev/commit-size.sh --history upstrem/master 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "needs a base commit, got 'upstrem/master'"
}

# Test that an amend is measured as the whole commit: a 250-line commit
# amended by 250 more lines is over 300, though each step is not.
test_pre_commit_hook_measures_amend_as_whole_commit()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	tgit -C "$r" add -A && tgit -C "$r" commit -q --no-verify -m tools
	seq 250 > "$r/a.txt"
	tgit -C "$r" add a.txt
	out=$(tgit -C "$r" commit -q -m a 2>&1)
	assert_eq "$?" 0 "$out"
	seq 251 500 > "$r/b.txt"
	tgit -C "$r" add b.txt
	out=$(tgit -C "$r" commit -q --amend -m a 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "500 changed lines, over 300"
	out=$(tgit -C "$r" commit -q -m b 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a binary file counts as a fixed 100 lines.
test_pre_commit_hook_counts_binary_files()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	tgit -C "$r" add -A && tgit -C "$r" commit -q --no-verify -m tools
	printf 'bin\0%s\n' 1 2 3 > "$r/one.bin"
	printf 'bin\0%s\n' 4 5 6 > "$r/two.bin"
	seq 150 > "$r/a.txt"
	tgit -C "$r" add one.bin two.bin a.txt
	out=$(tgit -C "$r" commit -q -m bin 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "350 changed lines, over 300"
}

# The slowest test file: each test runs check.sh in a copy of tools/dev.
# They share nothing, so they run DXX_JOBS at a time.
run_tests --parallel

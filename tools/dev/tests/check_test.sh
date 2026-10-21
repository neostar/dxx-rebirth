#!/usr/bin/env bash
# Command-line behaviour of tools/dev/check.sh and the pre-commit hook,
# run against a throwaway repository holding a copy of tools/dev.
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"

# Make a throwaway repo with tools/dev copied in (no tests, so check.sh
# does not recurse into this file) and the fork's hooks installed.
make_dev_repo()
{
	local r
	r=$(make_repo) || die "make_repo failed"
	mkdir -p "$r/tools/dev/tests"
	cp "$DEV_DIR"/*.sh "$r/tools/dev/"
	cp "$DEV_DIR/fork-dirs" "$DEV_DIR/repo-config" "$DEV_DIR/toolchain.pins" \
		"$DEV_DIR/requirements.lock" "$DEV_DIR/commit-size-exclude" "$DEV_DIR/gpu-programs" \
		"$DEV_DIR/gpu_debug_mode.py" "$r/tools/dev/"
	cp -R "$DEV_DIR/hooks" "$DEV_DIR/style" "$r/tools/dev/"
	cp "$DEV_DIR/style/clang-tidy" "$r/tools/.clang-tidy"
	cp "$DEV_DIR/tests/lib.sh" "$r/tools/dev/tests/"
	(cd "$r" && tools/dev/setup.sh >/dev/null)
	printf '%s\n' "$r"
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

# Test that the full checks time each step and fail, naming the budget,
# when they take longer than the check tier's budget (ADR 5), here
# DXX_CHECK_BUDGET=-1 s; the fast subset keeps no budget.
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
}

# Test that the fast subset runs shellcheck on scripts in the fork
# directories, found by extension or by shebang.
test_fast_fails_on_shellcheck_finding()
{
	local r out rc
	command -v shellcheck >/dev/null || skip 'shellcheck not installed'
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
# true through, however the call is spread over lines.
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

# Test that the fast subset fails on a line of check.sh that runs a
# registered GPU program other than through gpu-validate.sh, in the shape
# step_gpu_tests runs it, and lets a comment naming one through.
test_fast_fails_on_gpu_program_run_outside_the_wrapper()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '%s\n' 'unwrapped() { "$dev_dir/no-window.sh" "$root/$gpu_program" || rc=1; }' \
		'braced() { ${gpu_program}; }' '# runs $gpu_program' >> "$r/tools/dev/check.sh"
	out=$("$r/tools/dev/check.sh" --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" 'unwrapped() { "$dev_dir/no-window.sh" "$root/$gpu_program"'
	assert_contains "$out" 'braced() { ${gpu_program}; }'
	assert_not_contains "$out" '# runs $gpu_program'
	assert_contains "$(grep '^check.sh: failed' <<<"$out")" "gpu_gate"
}

# Test that the full checks run every registered GPU program under
# gpu-validate.sh, failing on a validation message, and SKIP naming #24
# with none registered.
test_full_runs_gpu_programs_under_gpu_validate()
{
	local r out rc
	r=$(make_dev_repo) || die "make_dev_repo failed"
	printf '#!/bin/sh\nexit 0\n' > "$r/tools/dev/build.sh"
	out=$("$r/tools/dev/check.sh" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP gpu_tests: no GPU program in tools/dev/gpu-programs; #24"
	mkdir -p "$r/build/dev"
	printf '%s\n' '#!/bin/sh' "echo 'Validation layers enabled'" "echo 'SDL_GPU Driver: Vulkan'" \
		"echo 'Validation Error: [ VUID-x ] bad'" > "$r/build/dev/gpu-probe"
	chmod +x "$r/build/dev/gpu-probe"
	echo build/dev/gpu-probe >> "$r/tools/dev/gpu-programs"
	out=$("$r/tools/dev/check.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "gpu-validate: gpu-probe: 1 from the driver's validation layer"
	assert_eq "$(grep '^check.sh: failed' <<<"$out")" 'check.sh: failed: gpu_tests' "$out"
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

# Test that setup.sh installs the hooks and enables rerere in a fresh clone.
test_setup_installs_hooks()
{
	local r
	r=$(make_dev_repo) || die "make_dev_repo failed"
	tgit -C "$r" config --unset core.hooksPath
	(cd "$r" && tools/dev/setup.sh >/dev/null)
	assert_eq "$(tgit -C "$r" config core.hooksPath)" "tools/dev/hooks"
	assert_eq "$(tgit -C "$r" config rerere.enabled)" "true"
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

run_tests

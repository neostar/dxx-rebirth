#!/usr/bin/env bash
# The merge gate (ADR 6): ci.sh --gate's choice of steps by changed path,
# with a fake check.sh and a fake docker, and check.sh --gate's steps, R1
# baseline reuse and budget, with a fake build.sh, doctor.sh and r1.sh.
# The real R1 needs the licensed data and is not part of the tests
# (docs/agents/game-data.md).
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"
# check.sh's doctor step is faked here; leave the real Docker alone.
export DOCKER_HOST=unix://$_scratch_root/no-docker.sock
export CHECK_SHELLCHECK=no-such-shellcheck

# Make a throwaway repository holding tools/dev with a fake check.sh that
# prints its arguments (its --doctor prints $FAKE_DOCTOR_OUT and exits
# $FAKE_DOCTOR_RC, default 0; else exit $FAKE_CHECK_RC, default 0; writing the file
# $FAKE_CHECK_TOUCH when set), a tests/ directory
# with a_test.sh (naming run.sh) and lib.sh, committed, and the base
# branch `base` at that commit; put a fake docker into $1 (logging its
# arguments to $1/docker.args).  Print the repository's path.
make_gate_repo()
{
	local bin=$1 r
	r=$(make_repo) || die "make_repo failed"
	mkdir -p "$r/tools/dev/tests"
	cp "$DEV_DIR"/{ci,container,deps,env,run}.sh "$DEV_DIR"/{toolchain.pins,requirements.lock} \
		"$r/tools/dev/"
	cp -R "$DEV_DIR/container" "$r/tools/dev/"
	printf '#!/usr/bin/env bash\n%s\necho "native check $*"\n%s\nexit "${FAKE_CHECK_RC:-0}"\n' \
		'[[ ${1-} != --doctor ]] || { echo "${FAKE_DOCTOR_OUT-doctor ok}"; exit "${FAKE_DOCTOR_RC:-0}"; }' \
		'[[ -z ${FAKE_CHECK_TOUCH-} ]] || echo x > "$FAKE_CHECK_TOUCH"' > "$r/tools/dev/check.sh"
	chmod +x "$r/tools/dev/check.sh"
	printf '# runs tools/dev/run.sh\n' > "$r/tools/dev/tests/a_test.sh"
	printf '# helpers\n' > "$r/tools/dev/tests/lib.sh"
	printf '# none\n' > "$r/tools/dev/tests/b_test.sh"
	printf 'build/\ntraces/local/\n' > "$r/.gitignore"
	tgit -C "$r" add -A
	tgit -C "$r" commit -q --no-verify -m tools
	tgit -C "$r" branch base
	cat > "$bin/docker" <<DOCKER
#!/usr/bin/env bash
case "\$1 \${2-}" in
'image inspect') exit 0 ;;
esac
printf '%s\n' "\$@" > "$bin/docker.args"
echo "container ran"
DOCKER
	chmod +x "$bin/docker"
	printf '%s\n' "$r"
}

# Run ci.sh of repo $1 with the fake docker in $2, a fake DXX_D2_DATA
# and the remaining arguments.
run_gate()
{
	local r=$1 bin=$2; shift 2
	mkdir -p "$bin/tm" "$bin/d2"
	: > "$bin/d2/DESCENT2.HOG"
	(cd "$r" && PATH="$bin:$PATH" DXX_D2_DATA=${DATA-$bin/d2} DXX_TMUTIL="$(fake_tmutil "$bin/tm")" \
		DXX_CI_MACHINE_LOCK="$bin/machine.lock" tools/dev/ci.sh "$@" 2>&1)
}

# Write file $2 in repo $1 (a line added to an existing one) and commit it.
change()
{
	mkdir -p "$1/$(dirname "$2")"
	echo changed >> "$1/$2"
	tgit -C "$1" add -A || die "staging $2 failed"
	tgit -C "$1" commit -q --no-verify -m "change $2" || die "committing $2 failed"
}

# Test that --plan names, for each changed path, what it adds to the gate
# (ADR 6's table), and the steps of the whole change; the gate itself
# always runs.
test_plan_maps_paths_to_steps()
{
	local bin r out p want
	bin=$(make_scratch) || die "make_scratch failed"
	declare -A want=(
		[common/main/game.cpp]='game'
		[common/render/device.cpp]='game'
		[d2x-rebirth/main/x.cpp]='game'
		[similar/main/y.cpp]='game'
		[tools/dev/run.sh]='tools'
		[docs/agents/issue-tracker.md]='tools'
		[tools/dev/toolchain.pins]='tools container gpu'
		[tools/dev/deps.sh]='tools container gpu'
		[tools/dev/vk_layer_settings.txt]='tools gpu'
		[common/render/device.h]='game gpu'
		[common/render/readback.h]='game gpu'
		[common/render/caps.h]='game gpu'
		[common/render/render_device.h]='game gpu'
		[common/sdl/dxx-sdl.h]='game gpu'
		[common/sdl/sdl_deleter.h]='game gpu'
		[shaders/generated/tex_modulate_frag.h]='container gpu'
		[tools/dev/container/Dockerfile]='tools container'
		[tools/dev/site-dev.py]='tools container'
		[tools/dev/fork_warnings.py]='tools container'
		[SConstruct]='container gpu'
		[site_scons/x.py]='container'
		[shaders/hlsl/x.hlsl]='container gpu'
		[common/render/sdlgpu_device.cpp]='game gpu'
		[common/render/unittest/sdlgpu_device.cpp]='game gpu'
		[common/sdl/sdl_wrap_gpu.h]='game gpu'
		[tools/dev/gpu-programs]='tools gpu'
		[tools/dev/gpu-validate.sh]='tools gpu'
		[README.md]='full'
		[docs/adr/0009-x.md]='full'
	)
	# One repository, one commit per path, each planned against its parent.
	r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
	for p in "${!want[@]}"; do
		change "$r" "$p"
		out=$(run_gate "$r" "$bin" --gate --plan --base HEAD~1)
		assert_eq "$?" 0 "$p: $out"
		assert_contains "$out" "gate: $p: ${want[$p]}"
		assert_eq "$(grep -vc -e '^gate: head ' -e '^gate: base ' -e '^gate: parts: ' -e '^gate: shell tests: ' <<<"$out")" \
			1 "$p: $out"
	done
}

# Test that a changed tools/dev script runs the shell tests naming it, a
# file no test names all of them, a changed test itself, and a docs/agents
# file none (shellcheck alone).
test_plan_picks_shell_tests()
{
	local bin r out p want
	bin=$(make_scratch) || die "make_scratch failed"
	declare -A want=(
		[tools/dev/run.sh]='a_test.sh'
		[tools/dev/unnamed.txt]='a_test.sh b_test.sh'
		[tools/dev/tests/b_test.sh]='b_test.sh'
		[docs/agents/x.md]='none'
	)
	r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
	for p in "${!want[@]}"; do
		change "$r" "$p"
		out=$(run_gate "$r" "$bin" --gate --plan --base HEAD~1)
		assert_eq "$?" 0 "$p: $out"
		assert_contains "$out" "gate: parts: gate tools"$'\n'"gate: shell tests: ${want[$p]}"
	done
}

# Test that a planted unmapped path runs the gate and then the full run,
# both legs, the container leg on a clean clone, in place of the tools and
# container parts, which it holds; and that a failed gate fails ci.sh,
# naming it.
test_unmapped_path_runs_the_full_run()
{
	local bin r out rc sha
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
	sha=$(tgit -C "$r" rev-parse base)
	change "$r" contrib/planted.txt
	change "$r" tools/dev/run.sh
	change "$r" SConstruct
	out=$(run_gate "$r" "$bin" --gate --base base) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "gate: contrib/planted.txt: full"
	assert_contains "$out" "gate: parts: gate full"
	assert_contains "$out" "native check --gate $sha"
	assert_contains "$out" $'== ci: native check.sh \nnative check \n'
	assert_contains "$out" "container ran"
	assert_contains "$(cat "$bin/docker.args")" "CI_COMMIT=$(tgit -C "$r" rev-parse HEAD)"
	assert_not_contains "$out" "native check --tools"
	out=$(FAKE_CHECK_RC=1 run_gate "$r" "$bin" --gate --base base) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "ci.sh: failed: gate full"
}

# Test that the gate checks the toolchain first, before its parts, and that
# a missing dependency prefix (doctor.sh: a SKIP, and the tools it holds
# failing) stops it before any part, naming deps.sh all.
test_gate_stops_on_a_stale_prefix()
{
	local bin r out rc
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
	change "$r" contrib/planted.txt
	out=$(run_gate "$r" "$bin" --gate --base base) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" $'== ci: doctor (check.sh --doctor)\ndoctor ok\n'
	out=$(FAKE_DOCTOR_RC=1 FAKE_DOCTOR_OUT=$'SKIP deps: /p not built; run tools/dev/deps.sh\nFAIL llvm: found (clangd missing), manifest pins 23.1.3' \
		run_gate "$r" "$bin" --gate --base base) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "ci.sh: the dependency prefix is stale or missing; run tools/dev/deps.sh all"
	assert_not_contains "$out" "gate part"
	assert_not_contains "$out" "native check"
}

# Test that a game change runs the gate alone, a tools change adds the
# shell tests, and an image input adds the container leg, not the full run.
test_parts_follow_the_changed_paths()
{
	local bin r out
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
	change "$r" common/main/game.cpp
	out=$(run_gate "$r" "$bin" --gate --base base)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "native check --gate"
	assert_not_contains "$out" "native check --tools"
	assert_not_contains "$out" "container ran"
	assert_contains "$out" "ci.sh: passed"
	change "$r" tools/dev/run.sh
	out=$(run_gate "$r" "$bin" --gate --base base)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "native check --tools a_test.sh"
	assert_not_contains "$out" "container ran"
	change "$r" tools/dev/toolchain.pins
	out=$(run_gate "$r" "$bin" --gate --base base)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "container ran"
	assert_contains "$(cat "$bin/docker.args")" "CI_COMMIT="
	assert_not_contains "$out" $'native check \n'
}

# Test that a change to the SDL_GPU device's sources adds the gpu part,
# check.sh --gpu, after the gate (#24), and that other game code does not.
test_gpu_sources_run_the_gpu_programs()
{
	local bin r out
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
	change "$r" common/main/game.cpp
	out=$(run_gate "$r" "$bin" --gate --base base)
	assert_eq "$?" 0 "$out"
	assert_not_contains "$out" "native check --gpu"
	change "$r" common/render/sdlgpu_device.cpp
	out=$(run_gate "$r" "$bin" --gate --base base)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "gate: parts: gate gpu"
	assert_contains "$out" "native check --gpu"
	assert_contains "$(grep -n 'native check' <<<"$out" | head -n 1)" "native check --gate"
	out=$(FAKE_CHECK_RC=1 run_gate "$r" "$bin" --gate --base base)
	assert_eq "$?" 1 "$out"
	assert_contains "$out" "ci.sh: failed: gate gpu"
}

# Test that the gate fails before running anything when DXX_D2_DATA is
# unset or names no game data, or the tree has uncommitted or untracked
# files, and that a bad --base or option mix is a usage error.
test_gate_refuses_before_running()
{
	local bin r out rc d
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
	change "$r" common/main/game.cpp
	for d in '' "$bin"; do
		out=$(DATA=$d run_gate "$r" "$bin" --gate --base base) && rc=0 || rc=$?
		assert_eq "$rc" 1 "data '$d': $out"
		assert_contains "$out" "the merge gate needs DXX_D2_DATA"
		assert_not_contains "$out" "native check"
	done
	echo draft > "$r/untracked.txt"
	out=$(run_gate "$r" "$bin" --gate --base base) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "commit or remove"
	assert_not_contains "$out" "native check"
	rm "$r/untracked.txt"
	for a in '--gate --base no-such-rev' '--gate --fast' '--plan' '--base base' '--gate --base' \
		'--tested' '--tested HEAD --gate' '--gate --tested HEAD' '--tested no-such-rev'; do
		# shellcheck disable=SC2086  # split the arguments on purpose
		out=$(run_gate "$r" "$bin" $a) && rc=0 || rc=$?
		assert_eq "$rc" 2 "$a: $out"
	done
}

# Test that the gate refuses an empty base..HEAD (HEAD is the base, or
# already merged into it), which would check nothing, with --plan too.
test_gate_refuses_an_empty_range()
{
	local bin r out rc a
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
	tgit -C "$r" branch sdl-gpu-port
	for a in '--gate --base HEAD' '--gate --base HEAD --plan' '--gate'; do
		# shellcheck disable=SC2086  # split the arguments on purpose
		out=$(run_gate "$r" "$bin" $a) && rc=0 || rc=$?
		assert_eq "$rc" 1 "$a: $out"
		assert_contains "$out" "no commits in"
		assert_not_contains "$out" "native check"
	done
	change "$r" common/main/game.cpp
	tgit -C "$r" branch -f sdl-gpu-port HEAD
	out=$(run_gate "$r" "$bin" --gate) && rc=0 || rc=$?
	assert_eq "$rc" 1 "a merged tip: $out"
	assert_contains "$out" "no commits in"
}

# Test that the gate logs HEAD and its tree first, and that --tested REV
# passes only on a tree a passing gate checked, in any worktree, whatever
# the commit (signing at merge rewrites the commit, not the tree).
test_tested_names_the_trees_the_gate_passed()
{
	local bin r out rc head tree wt
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
	change "$r" common/main/game.cpp
	head=$(tgit -C "$r" rev-parse HEAD) tree=$(tgit -C "$r" rev-parse 'HEAD^{tree}')
	out=$(run_gate "$r" "$bin" --gate --base base --plan)
	assert_contains "$(sed -n 1p <<<"$out")" "gate: head $head tree $tree"
	out=$(run_gate "$r" "$bin" --tested HEAD) && rc=0 || rc=$?
	assert_eq "$rc" 1 "untested: $out"
	assert_contains "$out" "the merge gate has not passed on tree $tree"
	out=$(FAKE_CHECK_RC=1 run_gate "$r" "$bin" --gate --base base)
	out=$(run_gate "$r" "$bin" --tested HEAD) && rc=0 || rc=$?
	assert_eq "$rc" 1 "a failed gate: $out"
	out=$(FAKE_CHECK_TOUCH=$r/new.txt run_gate "$r" "$bin" --gate --base base) && rc=0 || rc=$?
	assert_eq "$rc" 1 "a tree changed while the gate ran: $out"
	assert_contains "$out" "ci.sh: failed: head"
	out=$(run_gate "$r" "$bin" --tested HEAD) && rc=0 || rc=$?
	assert_eq "$rc" 1 "a gate on a changing tree: $out"
	rm "$r/new.txt"
	out=$(run_gate "$r" "$bin" --gate --base base) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$(grep -m1 '^gate: ' "$r/build/ci/ci.log")" "gate: head $head tree $tree"
	out=$(run_gate "$r" "$bin" --tested HEAD) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "ci.sh: the merge gate passed on tree $tree"
	# A rewritten commit with the same tree, checked from another worktree.
	tgit -C "$r" commit -q --amend --no-verify -m signed
	wt=$bin/wt
	tgit -C "$r" worktree add -q "$wt" HEAD
	out=$(run_gate "$wt" "$bin" --tested HEAD) && rc=0 || rc=$?
	assert_eq "$rc" 0 "same tree: $out"
	change "$r" common/main/game.cpp
	out=$(run_gate "$r" "$bin" --tested HEAD) && rc=0 || rc=$?
	assert_eq "$rc" 1 "a changed tree: $out"
}

# Make a throwaway repository holding a copy of tools/dev (no tests, so
# check.sh does not recurse into this file; lib.sh and a_test.sh, which
# passes, and b_test.sh, which fails), a scene in common/debug/scenes,
# with fakes that log each call to
# calls: doctor.sh, build.sh, format.sh and r1.sh.  The fake r1.sh's
# capture writes R1_DIR/baseline/commit and a build tree in R1_DIR/src;
# it exits $FAKE_CAPTURE_RC (default 0); its check writes R1_DIR/run/s1.log
# and a game binary holding $FAKE_GAME_STRING, and exits $FAKE_R1_RC
# (default 0).
# Commit it, with branch `base` there, and print its path.
make_check_repo()
{
	local r f
	r=$(make_repo) || die "make_repo failed"
	mkdir -p "$r/tools/dev/tests"
	for f in "$DEV_DIR"/*; do
		[[ -f $f ]] && cp "$f" "$r/tools/dev/"
	done
	cp -R "$DEV_DIR/hooks" "$DEV_DIR/style" "$r/tools/dev/"
	: > "$r/tools/dev/sdl-arms-allow"
	# The repository holds no rtrace programs: a stub SKIPs their tests.
	printf '#!/bin/sh\necho "SKIP rtrace tests ($1): a stub in the throwaway repository"\n' \
		> "$r/tools/dev/rtrace-tests.sh"
	printf 'tools\n' > "$r/tools/dev/fork-dirs"
	cp "$DEV_DIR/style/clang-tidy" "$r/tools/.clang-tidy"
	cp "$DEV_DIR/tests/lib.sh" "$r/tools/dev/tests/"
	mkdir -p "$r/common/debug/scenes"
	echo scene > "$r/common/debug/scenes/s1-title-menu.txt"
	printf 'exit 0\n' > "$r/tools/dev/tests/a_test.sh"
	printf 'echo b failed; exit 1\n' > "$r/tools/dev/tests/b_test.sh"
	cp "$REPO_ROOT/.gitleaks.toml" "$r/"
	# The real ignore rules: the gate's outputs must be among them.
	{ cat "$REPO_ROOT/.gitignore"; echo calls; } > "$r/.gitignore"
	for f in doctor build format; do
		printf '#!/bin/sh\necho "%s.sh $*" >> "%s/calls"\nexit 0\n' "$f" "$r" > "$r/tools/dev/$f.sh"
	done
	cat > "$r/tools/dev/r1.sh" <<R1
#!/usr/bin/env bash
echo "r1.sh \$* R1_DIR=\$R1_DIR" >> "$r/calls"
echo "fake r1 \$1"
if [[ \$1 == capture ]]; then
	mkdir -p "\$R1_DIR/baseline" "\$R1_DIR/src/build"
	echo "\$3 base" > "\$R1_DIR/baseline/commit"
	exit "\${FAKE_CAPTURE_RC:-0}"
else
	[[ -f \$R1_DIR/baseline/commit ]] || { echo "no baseline"; exit 2; }
	mkdir -p "\$R1_DIR/run" && echo frames > "\$R1_DIR/run/s1.log"
	mkdir -p "$r/build/r1/d2x-rebirth"
	printf 'game %s\n' "\${FAKE_GAME_STRING-}" > "$r/build/r1/d2x-rebirth/d2x-rebirth"
fi
exit "\${FAKE_R1_RC:-0}"
R1
	chmod +x "$r/tools/dev/"*.sh
	(cd "$r" && tools/dev/setup.sh >/dev/null)
	tgit -C "$r" add -A
	tgit -C "$r" commit -q --no-verify -m tools
	tgit -C "$r" branch base
	printf '%s\n' "$r"
}

# Run check.sh of repo $1 with a fake DXX_D2_DATA and the remaining
# arguments.
run_check()
{
	local r=$1 d; shift
	d=$r/../d2-$RANDOM
	mkdir -p "$d" && : > "$d/DESCENT2.HOG"
	(cd "$r" && DXX_D2_DATA=${DATA-$d} tools/dev/check.sh "$@" 2>&1)
}

# Test that the gate runs its steps (doctor once, the dev and asan builds
# with their tests, gitleaks on the new commits, R1 against the base),
# timing each, and passes; that R1's baseline is captured once per base
# commit, into the clone's git directory without its build tree, and
# reused; and that nothing it writes shows in git.
test_gate_runs_its_steps_and_reuses_the_baseline()
{
	local r out rc sha common
	r=$(make_check_repo) || die "make_check_repo failed"
	sha=$(tgit -C "$r" rev-parse base)
	common=$(tgit -C "$r" rev-parse --path-format=absolute --git-common-dir)
	change "$r" src/game.cpp
	out=$(run_check "$r" --gate "$sha") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	for s in doctor new_secrets unit_tests sanitizer_tests leaks changed_format r1; do
		assert_contains "$out" "-- $s: "
	done
	assert_contains "$out" "new_secrets: $sha..HEAD"
	assert_contains "$out" "check.sh: gate checks passed"
	assert_eq "$(grep -c '^doctor.sh' "$r/calls")" 1
	assert_contains "$(cat "$r/calls")" "build.sh dev check build/dev/compile_commands.json"
	assert_contains "$(cat "$r/calls")" "build.sh asan check"
	[[ $(cat "$r/calls") =~ r1.sh\ capture\ --base\ $sha\ R1_DIR=$common/dxx-r1/$sha-[0-9a-f]{12}$'\n' ]] ||
		fail "no capture keyed by the base and its inputs: $(cat "$r/calls")"
	assert_contains "$(cat "$r/calls")" "r1.sh check --base $sha R1_DIR=$(cd "$r" && pwd)/traces/local/r1-gate"
	[[ -z $(find "$common/dxx-r1" -name src) ]] || fail "the baseline's build tree was kept"
	assert_eq "$(tgit -C "$r" status --porcelain --ignored=no)" ""
	: > "$r/calls"
	out=$(run_check "$r" --gate "$sha") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_not_contains "$(cat "$r/calls")" "r1.sh capture"
	assert_contains "$(cat "$r/calls")" "r1.sh check --base $sha"
}

# Test that a changed scene, r1.sh or toolchain.pins captures the baseline
# again, as the base commit alone would reuse one of other inputs.
test_gate_recaptures_on_changed_r1_inputs()
{
	local r out rc p
	r=$(make_check_repo) || die "make_check_repo failed"
	change "$r" src/game.cpp
	out=$(run_check "$r" --gate base) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	for p in common/debug/scenes/s1-title-menu.txt common/debug/scenes/s9-new.txt \
		tools/dev/r1.sh tools/dev/toolchain.pins; do
		change "$r" "$p"
		: > "$r/calls"
		out=$(run_check "$r" --gate base) && rc=0 || rc=$?
		assert_eq "$rc" 0 "$p: $out"
		assert_contains "$(cat "$r/calls")" "r1.sh capture --base"
		: > "$r/calls"
		out=$(run_check "$r" --gate base) && rc=0 || rc=$?
		assert_not_contains "$(cat "$r/calls")" "r1.sh capture"
	done
	rm -r "$r/common/debug/scenes"
	out=$(run_check "$r" --gate base) && rc=0 || rc=$?
	assert_eq "$rc" 1 "no scenes: $out"
	assert_contains "$out" "check.sh: failed: r1_baseline r1"
}

# Test that a failed R1 check fails the gate, naming it, and that a failed
# capture of the baseline fails it before R1 runs.
test_gate_fails_on_r1()
{
	local r out rc
	r=$(make_check_repo) || die "make_check_repo failed"
	change "$r" src/game.cpp
	out=$(FAKE_R1_RC=1 run_check "$r" --gate base) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "fake r1 check"
	assert_eq "$(tail -1 <<<"$out")" "check.sh: failed: r1"
	r=$(make_check_repo) || die "make_check_repo failed"
	out=$(FAKE_CAPTURE_RC=1 run_check "$r" --gate base) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "check.sh: failed: r1_baseline r1"
	assert_not_contains "$out" "fake r1 check"
}

# Test that the gate fails R1 when the game binary it built holds SDL's
# background-app hint (#24): the game is a foreground app.
test_gate_fails_on_background_hint_in_game_binary()
{
	local r out rc
	r=$(make_check_repo) || die "make_check_repo failed"
	change "$r" src/game.cpp
	out=$(run_check "$r" --gate base)
	assert_eq "$?" 0 "$out"
	out=$(FAKE_GAME_STRING=SDL_MAC_BACKGROUND_APP run_check "$r" --gate base) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "holds SDL_MAC_BACKGROUND_APP; the game is a foreground app"
	assert_eq "$(tail -1 <<<"$out")" "check.sh: failed: r1"
}

# Print a fake GitHub token, assembled here so that this file itself holds
# none for a scanner to find.
fake_token()
{
	printf 'gh%s_%s' p aB3dE5fG7hJ9kL1mN2pQ4rS6tU8vW0xY2zA4
}

# Test that the gate scans the new commits for secrets: one committed
# after the base fails it without printing the secret; one before the base
# is left to the full run's history scan.
test_gate_scans_the_new_commits()
{
	local r out rc
	[[ -x $dev_deps_prefix/opt/gitleaks/bin/gitleaks ]] ||
		skip "gitleaks not in $dev_deps_prefix; run tools/dev/deps.sh build"
	r=$(make_check_repo) || die "make_check_repo failed"
	printf 'token = "%s"\n' "$(fake_token)" > "$r/old.txt"
	tgit -C "$r" add old.txt || die "staging old.txt failed"
	tgit -C "$r" commit -q --no-verify -m old || die "committing old.txt failed"
	tgit -C "$r" branch -f base
	out=$(run_check "$r" --gate base) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	printf 'token = "%s"\n' "$(fake_token)" > "$r/new.txt"
	tgit -C "$r" add new.txt || die "staging new.txt failed"
	tgit -C "$r" commit -q --no-verify -m new || die "committing new.txt failed"
	out=$(run_check "$r" --gate base) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "check.sh: failed: new_secrets"
	assert_not_contains "$out" "$(fake_token)"
}

# Test that the gate formats and tidies the changed fork files only.
test_gate_tidies_the_changed_fork_files()
{
	local r out
	r=$(make_check_repo) || die "make_check_repo failed"
	change "$r" tools/x.cpp
	change "$r" common/upstream.cpp
	out=$(run_check "$r" --gate base)
	assert_eq "$?" 0 "$out"
	assert_contains "$(cat "$r/calls")" "format.sh --check $(cd "$r" && pwd)/tools/x.cpp"
	assert_contains "$(cat "$r/calls")" "format.sh --tidy $(cd "$r" && pwd)/tools/x.cpp"
	assert_not_contains "$(cat "$r/calls")" "upstream.cpp"
}

# Test that the gate times itself against its 5-minute budget (ADR 6) and
# fails, naming the budget, when it takes longer, here
# DXX_GATE_BUDGET=-1 s.
test_gate_fails_over_its_budget()
{
	local r out rc
	r=$(make_check_repo) || die "make_check_repo failed"
	out=$(DXX_GATE_BUDGET=-1 run_check "$r" --gate base) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	[[ $out =~ check.sh:\ merge\ gate\ took\ [0-9]+\ s,\ budget\ -1\ s ]] || fail "no timing: $out"
	assert_contains "$out" "check.sh: failed: budget"
	out=$(env -u DXX_GATE_BUDGET "$r/tools/dev/check.sh" --gate base 2>&1)
	assert_contains "$out" "needs DXX_D2_DATA"
	out=$(run_check "$r" --gate base)
	assert_contains "$out" ", budget 300 s"
}

# Test that the gate fails before any step without the game data, and
# that it needs a base commit.
test_gate_needs_data_and_base()
{
	local r out rc d
	r=$(make_check_repo) || die "make_check_repo failed"
	for d in '' "$r"; do
		out=$(DATA=$d run_check "$r" --gate base) && rc=0 || rc=$?
		assert_eq "$rc" 1 "data '$d': $out"
		assert_contains "$out" "the merge gate needs DXX_D2_DATA"
		assert_not_contains "$out" "== "
	done
	for a in '--gate' '--gate no-such-rev' '--gate base extra'; do
		# shellcheck disable=SC2086  # split the arguments on purpose
		out=$(run_check "$r" $a) && rc=0 || rc=$?
		assert_eq "$rc" 2 "$a: $out"
	done
}

# Test that --tools runs the named shell tests only, failing on a failed
# one or a missing name.
test_tools_runs_the_named_shell_tests()
{
	local r out rc
	r=$(make_check_repo) || die "make_check_repo failed"
	out=$(run_check "$r" --tools a_test.sh) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "-- tests/a_test.sh"
	assert_not_contains "$out" "b_test.sh"
	assert_contains "$out" "== shellcheck"
	out=$(run_check "$r" --tools a_test.sh b_test.sh) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAILED: tests/b_test.sh"
	out=$(run_check "$r" --tools no_test.sh) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "no_test.sh: no such shell test"
	out=$(run_check "$r" --tools) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_not_contains "$out" "-- tests/"
}

run_tests --parallel

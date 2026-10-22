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
# prints its arguments (exit $FAKE_CHECK_RC, default 0), a tests/ directory
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
	printf '#!/usr/bin/env bash\necho "native check $*"\nexit "${FAKE_CHECK_RC:-0}"\n' \
		> "$r/tools/dev/check.sh"
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
		[tools/dev/toolchain.pins]='tools container'
		[tools/dev/container/Dockerfile]='tools container'
		[tools/dev/site-dev.py]='tools container'
		[tools/dev/fork_warnings.py]='tools container'
		[SConstruct]='container'
		[site_scons/x.py]='container'
		[shaders/hlsl/x.hlsl]='container'
		[README.md]='full'
		[docs/adr/0009-x.md]='full'
	)
	for p in "${!want[@]}"; do
		r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
		change "$r" "$p"
		out=$(run_gate "$r" "$bin" --gate --plan --base base)
		assert_eq "$?" 0 "$p: $out"
		assert_contains "$out" "gate: $p: ${want[$p]}"
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
	for p in "${!want[@]}"; do
		r=$(make_gate_repo "$bin") || die "make_gate_repo failed"
		change "$r" "$p"
		out=$(run_gate "$r" "$bin" --gate --plan --base base)
		assert_eq "$?" 0 "$p: $out"
		assert_contains "$out" "gate: parts: gate tools"$'\n'"gate: shell tests: ${want[$p]}"
	done
}

# Make a throwaway repository holding a copy of tools/dev (no tests, so
# check.sh does not recurse into this file; lib.sh and a_test.sh, which
# passes, and b_test.sh, which fails), with fakes that log each call to
# calls: doctor.sh, build.sh, format.sh and r1.sh.  The fake r1.sh's
# capture writes R1_DIR/baseline/commit and a build tree in R1_DIR/src;
# it exits $FAKE_CAPTURE_RC (default 0); its check writes R1_DIR/run/s1.log
# and exits $FAKE_R1_RC (default 0).
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
	printf 'tools\n' > "$r/tools/dev/fork-dirs"
	cp "$DEV_DIR/style/clang-tidy" "$r/tools/.clang-tidy"
	cp "$DEV_DIR/tests/lib.sh" "$r/tools/dev/tests/"
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
	assert_contains "$(cat "$r/calls")" "r1.sh capture --base $sha R1_DIR=$common/dxx-r1/$sha"
	assert_contains "$(cat "$r/calls")" "r1.sh check --base $sha R1_DIR=$(cd "$r" && pwd)/traces/local/r1-gate"
	[[ ! -e $common/dxx-r1/$sha/src ]] || fail "the baseline's build tree was kept"
	assert_eq "$(tgit -C "$r" status --porcelain --ignored=no)" ""
	: > "$r/calls"
	out=$(run_check "$r" --gate "$sha") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_not_contains "$(cat "$r/calls")" "r1.sh capture"
	assert_contains "$(cat "$r/calls")" "r1.sh check --base $sha"
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

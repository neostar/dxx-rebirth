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

# Write file $2 in repo $1 (a line added to an existing one) and commit it.
change()
{
	mkdir -p "$1/$(dirname "$2")"
	echo changed >> "$1/$2"
	tgit -C "$1" add -A || die "staging $2 failed"
	tgit -C "$1" commit -q --no-verify -m "change $2" || die "committing $2 failed"
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

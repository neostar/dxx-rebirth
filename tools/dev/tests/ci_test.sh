#!/usr/bin/env bash
# Command-line behaviour of tools/dev/ci.sh and container.sh's run modes,
# in a throwaway repository with a fake check.sh and a fake docker that
# records how it was called.
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"

# Make a throwaway repo holding tools/dev with a fake check.sh, commit it,
# and put a fake docker into $1 that logs its arguments to $1/docker.args
# and its standard input to $1/docker.stdin.
make_ci_repo()
{
	local bin=$1 r
	r=$(make_repo)
	mkdir -p "$r/tools/dev"
	cp "$DEV_DIR"/{ci,container,deps,env}.sh "$DEV_DIR"/{toolchain.pins,requirements.lock} \
		"$r/tools/dev/"
	cp -R "$DEV_DIR/container" "$r/tools/dev/"
	printf '#!/usr/bin/env bash\necho "native check $*"\n%s\nexit "${FAKE_CHECK_RC:-0}"\n' \
		"${FAKE_CHECK_WAIT-}" > "$r/tools/dev/check.sh"
	printf '#!/usr/bin/env bash\necho "deps $*"\n' > "$r/tools/dev/deps.sh"
	chmod +x "$r/tools/dev/check.sh" "$r/tools/dev/deps.sh"
	tgit -C "$r" add -A
	tgit -C "$r" commit -q -m tools
	cat > "$bin/docker" <<DOCKER
#!/usr/bin/env bash
echo "\$*" >> "$bin/docker.log"
case "\$1 \${2-}" in
'image inspect') exit 0 ;;
'image ls') printf '%s\n' \${FAKE_IMAGES-}; exit 0 ;;
'run '*) touch "$bin/container-started" ;;
'save '*) echo image-bytes; exit 0 ;;
'build '*|'rmi '*|'image prune'|'builder prune') exit 0 ;;
esac
printf '%s\n' "\$@" > "$bin/docker.args"
cat > "$bin/docker.stdin"
echo "container ran"
DOCKER
	chmod +x "$bin/docker"
	printf '%s\n' "$r"
}

# Run ci.sh of repo $1 with the fake docker in $2 and the remaining arguments.
run_ci()
{
	local r=$1 bin=$2; shift 2
	(cd "$r" && PATH="$bin:$PATH" DXX_DEPS_CACHE="$bin/cache" tools/dev/ci.sh "$@" 2>&1)
}

# Test that ci.sh runs the native and the container leg at once, and
# prints each leg's output whole, native first: the fake native check
# passes only once the container leg has started.
test_runs_legs_concurrently()
{
	local bin r out rc
	bin=$(make_scratch)
	FAKE_CHECK_WAIT="for _ in \$(seq 50); do [[ -e $bin/container-started ]] && exit 0; sleep 0.1; done; exit 1" \
		r=$(make_ci_repo "$bin")
	out=$(run_ci "$r" "$bin" --fast) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" $'== ci: native check.sh --fast\nnative check --fast'
	assert_contains "$out" $'container ran\nci.sh: passed'
}

# Test that ci.sh runs check.sh natively and in the container, offline.
test_runs_native_then_container()
{
	local bin r out rc
	bin=$(make_scratch)
	r=$(make_ci_repo "$bin")
	out=$(run_ci "$r" "$bin" --fast) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "native check --fast"
	assert_contains "$out" "container ran"
	assert_contains "$(cat "$bin/docker.args")" $'--network\nnone'
	assert_contains "$(cat "$bin/docker.args")" $'tools/dev/check.sh\n--fast'
}

# Test that a failing native leg fails ci.sh, after the container leg ran.
test_native_failure_fails()
{
	local bin r out rc
	bin=$(make_scratch)
	r=$(make_ci_repo "$bin")
	out=$(FAKE_CHECK_RC=1 run_ci "$r" "$bin") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "container ran"
	assert_contains "$out" "ci.sh: failed: native"
}

# Test that the default container leg gets the working tree, uncommitted
# files included.
test_container_leg_gets_working_tree()
{
	local bin r out
	bin=$(make_scratch)
	r=$(make_ci_repo "$bin")
	echo draft > "$r/uncommitted.txt"
	out=$(run_ci "$r" "$bin" --container)
	assert_not_contains "$out" "native check"
	assert_contains "$(tar -tf "$bin/docker.stdin" 2>&1)" "uncommitted.txt"
}

# Test that --clean clones the commit under test from the read-only git
# directory, with the source cache read-only, offline, and gets nothing
# from the working tree.
test_clean_clones_commit_under_test()
{
	local bin r args out
	bin=$(make_scratch)
	r=$(make_ci_repo "$bin")
	echo draft > "$r/uncommitted.txt"
	out=$(run_ci "$r" "$bin" --container --clean)
	assert_contains "$out" "uncommitted changes are excluded"
	args=$(cat "$bin/docker.args")
	assert_contains "$args" $'--network\nnone'
	assert_contains "$args" "$(git -C "$r" rev-parse --path-format=absolute --git-common-dir):/src.git:ro"
	assert_not_contains "$args" "/cache"
	assert_contains "$args" "CI_COMMIT=$(git -C "$r" rev-parse HEAD)"
	assert_contains "$args" "git clone"
	[[ ! -s $bin/docker.stdin ]] || fail "--clean sent the working tree"
}

# Test that an unknown option is a usage error.
test_unknown_option_is_usage_error()
{
	local bin r rc
	bin=$(make_scratch)
	r=$(make_ci_repo "$bin")
	run_ci "$r" "$bin" --bogus >/dev/null && rc=0 || rc=$?
	assert_eq "$rc" 2
}

run_tests

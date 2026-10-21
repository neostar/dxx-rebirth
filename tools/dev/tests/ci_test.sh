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
	r=$(make_repo) || die "make_repo failed"
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
'run '*) sleep "\${FAKE_DOCKER_DELAY:-0}"; touch "$bin/container-started" ;;
'save '*) echo image-bytes; exit 0 ;;
'build '*|'rmi '*|'image prune'|'builder prune'|'system prune') exit 0 ;;
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
# passes only once the container leg has started.  Legs run one after the
# other would never meet, so the wait's bound only ends that case; 120 s
# keeps a loaded machine (ci.sh's own shell tests in parallel) from failing
# a correct ci.sh: here the container leg starts 6 s late.
test_runs_legs_concurrently()
{
	local bin r out rc
	bin=$(make_scratch) || die "make_scratch failed"
	FAKE_CHECK_WAIT="for _ in \$(seq 1200); do [[ -e $bin/container-started ]] && exit 0; sleep 0.1; done; exit 1" \
		r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	# The container leg starting 6 s late, as on a loaded machine.
	out=$(FAKE_DOCKER_DELAY=6 run_ci "$r" "$bin" --fast) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" $'== ci: native check.sh --fast\nnative check --fast'
	assert_contains "$out" $'container ran\nci.sh: passed'
}

# Test that ci.sh runs check.sh natively and in the container, offline.
test_runs_native_then_container()
{
	local bin r out rc
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
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
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
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
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
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
	local bin r args out common
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	echo draft > "$r/uncommitted.txt"
	out=$(run_ci "$r" "$bin" --container --clean)
	assert_contains "$out" "uncommitted changes are excluded"
	args=$(cat "$bin/docker.args")
	assert_contains "$args" $'--network\nnone'
	common=$(tgit -C "$r" rev-parse --path-format=absolute --git-common-dir)
	assert_contains "$args" "$common:/src.git:ro"
	assert_not_contains "$args" "/cache"
	assert_contains "$args" "CI_COMMIT=$(tgit -C "$r" rev-parse HEAD)"
	# raw git: the fake docker's arguments
	assert_contains "$args" "git clone"
	[[ ! -s $bin/docker.stdin ]] || fail "--clean sent the working tree"
}

# Put a fake docker into $1 whose `run` runs the container's script here,
# with /src.git and /work replaced by the mounted git directory and $1/work.
exec_docker()
{
	cat > "$1/docker" <<DOCKER
#!/usr/bin/env bash
[[ \$1 == run ]] || exit 0
shift
src= envs=()
while [[ \$1 == -* ]]; do
	case \$1 in
	-v) src=\${2%%:/src.git:ro} ;;
	-e) envs+=("\$2") ;;
	--network|--platform) ;;
	*) shift; continue ;;
	esac
	shift 2
done
shift 3
script=\${1//\/src.git/\$src}
script=\${script//\/work/$1/work}
shift
exec env "\${envs[@]}" bash -c "\$script" "\$@"
DOCKER
	chmod +x "$1/docker"
}

# Test that both container runs fail, naming it, when the clone has no
# upstream/master to fetch, rather than letting the upstream checks SKIP,
# and run the command once it has.
test_container_run_needs_upstream()
{
	local bin r out rc mode
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	printf '#!/usr/bin/env bash\n' > "$r/tools/dev/setup.sh"
	chmod +x "$r/tools/dev/setup.sh"
	tgit -C "$r" add -A && tgit -C "$r" commit -q -m setup
	exec_docker "$bin"
	for mode in --clean ''; do
		rm -rf "$bin/work"
		out=$(PATH="$bin:$PATH" "$r/tools/dev/container.sh" run $mode echo ran 2>&1) &&
			rc=0 || rc=$?
		assert_eq "$rc" 1 "$mode: $out"
		assert_contains "$out" "no upstream/master in the"
		assert_not_contains "$out" "ran"$'\n'
	done
	tgit -C "$r" update-ref refs/remotes/upstream/master HEAD
	for mode in --clean ''; do
		rm -rf "$bin/work"
		out=$(PATH="$bin:$PATH" "$r/tools/dev/container.sh" run $mode echo ran 2>&1)
		assert_eq "$?" 0 "$mode: $out"
		assert_contains "$out" "ran"
	done
}

# Test that a successful build removes the older dxx-rebirth-ref images
# and keeps the new one.
test_build_removes_older_images()
{
	local bin r tag log
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	tag=$("$r/tools/dev/container.sh" tag)
	FAKE_IMAGES="dxx-rebirth-ref:0123456789ab $tag" PATH="$bin:$PATH" \
		DXX_DEPS_CACHE="$bin/cache" "$r/tools/dev/container.sh" build >/dev/null 2>&1 ||
		fail "build failed"
	log=$(cat "$bin/docker.log")
	assert_contains "$log" "build "
	assert_contains "$log" "rmi dxx-rebirth-ref:0123456789ab"
	assert_not_contains "$log" "rmi $tag"
}

# Test that prune removes only this project's images: the older
# dxx-rebirth-ref ones and the dangling ones by the image's label; never an
# unfiltered image prune, nor the build cache, which is shared and cannot
# be filtered by project.
test_prune_touches_only_reference_images()
{
	local bin r tag log
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	tag=$("$r/tools/dev/container.sh" tag)
	FAKE_IMAGES="dxx-rebirth-ref:0123456789ab $tag" PATH="$bin:$PATH" \
		"$r/tools/dev/container.sh" prune >/dev/null 2>&1 || fail "prune failed"
	log=$(cat "$bin/docker.log")
	assert_contains "$log" "rmi dxx-rebirth-ref:0123456789ab"
	assert_not_contains "$log" "rmi $tag"
	assert_contains "$log" "image prune -f --filter label=dxx-rebirth.reference-image"
	if grep -E 'prune' "$bin/docker.log" | grep -vqE '^image prune -f --filter label=dxx-rebirth'; then
		fail "unfiltered prune: $(grep prune "$bin/docker.log")"
	fi
	assert_contains "$(cat "$r/tools/dev/container/Dockerfile")" "LABEL dxx-rebirth.reference-image=1"
}

# Test that save keeps exactly one exported image, the current one, under
# third_party/images, and that load reads it back.
test_save_keeps_one_image_and_load_reads_it()
{
	local bin r tag out
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	tag=$("$r/tools/dev/container.sh" tag)
	mkdir -p "$r/third_party/images"
	echo old > "$r/third_party/images/dxx-rebirth-ref-0123456789ab.tar.zst"
	PATH="$bin:$PATH" "$r/tools/dev/container.sh" save >/dev/null 2>&1 || fail "save failed"
	assert_eq "$(ls "$r/third_party/images")" "dxx-rebirth-ref-${tag#*:}.tar.zst"
	out=$(PATH="$bin:$PATH" "$r/tools/dev/container.sh" load 2>&1) || fail "load failed: $out"
	assert_contains "$(cat "$bin/docker.log")" "load"
	assert_eq "$(cat "$bin/docker.stdin")" image-bytes
}

# Test that an unknown option is a usage error.
test_unknown_option_is_usage_error()
{
	local bin r rc
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	run_ci "$r" "$bin" --bogus >/dev/null && rc=0 || rc=$?
	assert_eq "$rc" 2
}

run_tests

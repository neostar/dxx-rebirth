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

# Run ci.sh of repo $1 with the fake docker in $2 and the remaining
# arguments.  Its machine-wide lock is $2/machine.lock, or $MACHINE_LOCK:
# never the real one, which a real ci.sh running these tests holds.
run_ci()
{
	local r=$1 bin=$2; shift 2
	mkdir -p "$bin/tm"
	(cd "$r" && PATH="$bin:$PATH" DXX_DEPS_CACHE="$bin/cache" DXX_TMUTIL="$(fake_tmutil "$bin/tm")" \
		DXX_CI_MACHINE_LOCK="${MACHINE_LOCK:-$bin/machine.lock}" tools/dev/ci.sh "$@" 2>&1)
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

# Test that ci.sh writes its whole output to build/ci/ci.log, kept out of
# Time Machine and replaced by each run, and prints the path at the start
# and the end, also when a leg fails.
test_writes_its_own_log()
{
	local bin r out rc log
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	log=$(cd "$r" && pwd)/build/ci/ci.log
	out=$(run_ci "$r" "$bin" --fast) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$(head -1 <<<"$out")" "ci.sh: log: $log"
	assert_eq "$(tail -1 <<<"$out")" "ci.sh: log: $log"
	assert_eq "$(cat "$log")" "$out"
	assert_contains "$(cat "$bin/tm/log")" "addexclusion $(cd "$r" && pwd)/build"
	out=$(FAKE_CHECK_RC=1 run_ci "$r" "$bin" --fast) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_eq "$(tail -1 <<<"$out")" "ci.sh: log: $log"
	assert_eq "$(cat "$log")" "$out"
}

# Test that a second ci.sh, while one runs, fails at once, naming the
# running one, and leaves its log alone; the lock is gone after a run.
test_second_run_fails_fast()
{
	local bin r out rc log first
	bin=$(make_scratch) || die "make_scratch failed"
	FAKE_CHECK_WAIT="for _ in \$(seq 1200); do [[ -e $bin/release ]] && break; sleep 0.1; done" \
		r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	log=$(cd "$r" && pwd)/build/ci/ci.log
	run_ci "$r" "$bin" --fast > "$bin/first.out" &
	first=$!
	# A bound that only ends a broken run.
	for _ in $(seq 1200); do [[ -e $bin/container-started ]] && break; sleep 0.1; done
	out=$(run_ci "$r" "$bin" --fast) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "ci.sh: another ci.sh (pid "
	assert_contains "$(cat "$log")" "ci.sh: log: $log"
	assert_not_contains "$(cat "$log")" "another ci.sh"
	touch "$bin/release"
	wait "$first" || fail "the first run failed: $(cat "$bin/first.out")"
	[[ ! -e $r/build/ci/lock ]] || fail "the lock outlived the run"
}

# Test that a lock left by a ci.sh that no longer runs does not stop the
# next run.
test_stale_lock_is_taken_over()
{
	local bin r out dead
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	sh -c 'exit 0' &
	dead=$!
	wait "$dead"
	mkdir -p "$r/build/ci/lock"
	echo "$dead" > "$r/build/ci/lock/pid"
	out=$(run_ci "$r" "$bin" --fast)
	assert_eq "$?" 0 "$out"
	[[ ! -e $r/build/ci/lock ]] || fail "the lock outlived the run"
}

# Make a repo for start_blocked_ci in scratch directory $1: its native
# check waits for $1/release.
blocked_ci_repo()
{
	FAKE_CHECK_WAIT="for _ in \$(seq 1200); do [[ -e $1/release ]] && break; sleep 0.1; done" \
		make_ci_repo "$1"
}

# Start ci.sh of repo $1 with the fake docker in $2, with job control, so
# INT is not ignored as in a background command; return once the run holds
# the lock and its container leg started.  The job's pid is $2/ci.job.
start_blocked_ci()
{
	local r=$1 bin=$2
	set -m
	run_ci "$r" "$bin" --fast > "$bin/ci.out" 2>&1 &
	echo "$!" > "$bin/ci.job"
	set +m
	# A bound that only ends a broken run.
	for _ in $(seq 1200); do
		[[ -e $bin/container-started && -s $r/build/ci/lock/pid ]] && break
		sleep 0.1
	done
}

# Stop ci.sh by signal $1 while it runs, and fail if it still runs or its
# lock outlived it.
signal_removes_the_lock()
{
	local sig=$1 bin r ci
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(blocked_ci_repo "$bin") || die "make_ci_repo failed"
	start_blocked_ci "$r" "$bin"
	ci=$(cat "$r/build/ci/lock/pid" 2>/dev/null)
	[[ -n $ci ]] || { fail "ci.sh never took the lock"; return; }
	kill -"$sig" "$ci"
	for _ in $(seq 600); do kill -0 "$ci" 2>/dev/null || break; sleep 0.1; done
	kill -0 "$ci" 2>/dev/null && fail "ci.sh still runs after $sig"
	[[ ! -e $r/build/ci/lock ]] || fail "the lock outlived ci.sh after $sig"
	touch "$bin/release"
	wait "$(cat "$bin/ci.job")" 2>/dev/null
}

# Test that the lock is gone when ci.sh is stopped by TERM.
test_term_removes_the_lock()
{
	signal_removes_the_lock TERM
}

# Test that the lock is gone when ci.sh is stopped by HUP.
test_hup_removes_the_lock()
{
	signal_removes_the_lock HUP
}

# Test that the lock is gone when ci.sh is stopped by INT (Ctrl-C).  A
# test file run as a background command (check.sh) ignores INT, and so
# does every ci.sh it starts: there INT cannot stop ci.sh, and the test
# is skipped.
test_int_removes_the_lock()
{
	[[ $(trap -p INT) != "trap -- '' SIGINT" ]] || skip 'INT is ignored here (background command)'
	signal_removes_the_lock INT
}

# Test that INT stops ci.sh, removing the lock, also while ci.sh waits for
# a command (here the tee of its first log line, held until released):
# without a trap, bash defers an INT sent to it alone until the command
# ends and then, the command not having died of it, carries on.
test_int_while_running_a_command_removes_the_lock()
{
	[[ $(trap -p INT) != "trap -- '' SIGINT" ]] || skip 'INT is ignored here (background command)'
	local bin r ci job
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	cat > "$bin/tee" <<FAKE
#!/bin/sh
touch "$bin/tee-started"
for _ in \$(seq 1200); do [ -e "$bin/release" ] && break; sleep 0.1; done
exec /usr/bin/tee "\$@"
FAKE
	chmod +x "$bin/tee"
	set -m
	run_ci "$r" "$bin" --fast > "$bin/ci.out" 2>&1 &
	job=$!
	set +m
	for _ in $(seq 1200); do [[ -e $bin/tee-started ]] && break; sleep 0.1; done
	ci=$(cat "$r/build/ci/lock/pid" 2>/dev/null)
	[[ -n $ci ]] || { fail "ci.sh never took the lock"; return; }
	kill -INT "$ci"
	touch "$bin/release"
	for _ in $(seq 600); do kill -0 "$ci" 2>/dev/null || break; sleep 0.1; done
	kill -0 "$ci" 2>/dev/null && fail "ci.sh still runs after INT"
	wait "$job" 2>/dev/null
	[[ ! -e $r/build/ci/lock ]] || fail "the lock outlived ci.sh after INT"
	assert_not_contains "$(cat "$bin/ci.out")" "native check"
}

# Test that ci.sh leaves a lock it no longer owns: one taken over while it
# ran (here its pid file names another process) is not removed at its exit.
test_exit_leaves_a_lock_it_no_longer_owns()
{
	local bin r
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(blocked_ci_repo "$bin") || die "make_ci_repo failed"
	start_blocked_ci "$r" "$bin"
	[[ -s $r/build/ci/lock/pid ]] || { fail "ci.sh never took the lock"; return; }
	echo "$$" > "$r/build/ci/lock/pid"
	touch "$bin/release"
	wait "$(cat "$bin/ci.job")"
	assert_eq "$(cat "$r/build/ci/lock/pid" 2>/dev/null)" "$$" "the other owner's lock was removed"
}

# Test that a stale lock another ci.sh is taking over (it holds
# lock.takeover) is left to it: the run fails at once, saying so, without
# touching the lock or running a leg.
test_takeover_in_progress_fails()
{
	local bin r out rc dead
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	sh -c 'exit 0' &
	dead=$!
	wait "$dead"
	mkdir -p "$r/build/ci/lock" "$r/build/ci/lock.takeover"
	echo "$dead" > "$r/build/ci/lock/pid"
	out=$(run_ci "$r" "$bin" --fast) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "ci.sh: another ci.sh is taking over the lock"
	assert_not_contains "$out" "native check"
	assert_eq "$(cat "$r/build/ci/lock/pid")" "$dead"
	[[ -d $r/build/ci/lock.takeover ]] || fail "the other run's lock.takeover was removed"
}

# Test that a lock without a pid file (its ci.sh was killed between taking
# it and writing its pid) is taken over once it stays so for a few seconds.
test_lock_without_pid_is_taken_over()
{
	local bin r out
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	mkdir -p "$r/build/ci/lock"
	out=$(run_ci "$r" "$bin" --fast)
	assert_eq "$?" 0 "$out"
	[[ ! -e $r/build/ci/lock ]] || fail "the lock outlived the run"
}

# Test that a ci.sh in another worktree waits for the running one, the
# machine-wide lock's owner, printing whom it waits for, and runs once that
# one ended, removing the lock after it.
test_second_worktree_waits_for_the_machine_lock()
{
	local bin1 bin2 r1 r2 out job first
	bin1=$(make_scratch) || die "make_scratch failed"
	bin2=$(make_scratch) || die "make_scratch failed"
	export MACHINE_LOCK=$bin1/machine.lock
	r1=$(blocked_ci_repo "$bin1") || die "make_ci_repo failed"
	r2=$(make_ci_repo "$bin2") || die "make_ci_repo failed"
	start_blocked_ci "$r1" "$bin1"
	first=$(cat "$MACHINE_LOCK/pid" 2>/dev/null)
	[[ -n $first ]] || { fail "the first ci.sh never took the machine lock"; touch "$bin1/release"; return; }
	DXX_CI_POLL=0.2 run_ci "$r2" "$bin2" --fast > "$bin2/ci.out" &
	job=$!
	# A bound that only ends a broken run.
	for _ in $(seq 1200); do grep -q 'waiting for' "$bin2/ci.out" && break; sleep 0.1; done
	assert_contains "$(cat "$bin2/ci.out")" "ci.sh: waiting for ci.sh pid $first in $(cd "$r1" && pwd)"
	assert_not_contains "$(cat "$bin2/ci.out")" "native check"
	touch "$bin1/release"
	wait "$(cat "$bin1/ci.job")"
	wait "$job" || fail "the second run failed: $(cat "$bin2/ci.out")"
	assert_contains "$(cat "$bin2/ci.out")" "ci.sh: passed"
	[[ ! -e $MACHINE_LOCK ]] || fail "the machine lock outlived the runs"
}

# Test that a ci.sh waiting for the machine lock longer than
# DXX_CI_MAX_WAIT seconds fails, naming the owner, without running a leg
# or touching the owner's lock.
test_machine_lock_wait_times_out()
{
	local bin1 bin2 r1 r2 out rc first
	bin1=$(make_scratch) || die "make_scratch failed"
	bin2=$(make_scratch) || die "make_scratch failed"
	export MACHINE_LOCK=$bin1/machine.lock
	r1=$(blocked_ci_repo "$bin1") || die "make_ci_repo failed"
	r2=$(make_ci_repo "$bin2") || die "make_ci_repo failed"
	start_blocked_ci "$r1" "$bin1"
	first=$(cat "$MACHINE_LOCK/pid" 2>/dev/null)
	out=$(DXX_CI_MAX_WAIT=1 DXX_CI_POLL=0.2 run_ci "$r2" "$bin2" --fast) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "ci.sh: gave up after 1 s waiting for ci.sh pid $first"
	assert_not_contains "$out" "native check"
	assert_eq "$(cat "$MACHINE_LOCK/pid" 2>/dev/null)" "$first" "the owner's lock was touched"
	touch "$bin1/release"
	wait "$(cat "$bin1/ci.job")"
	out=$(DXX_CI_MAX_WAIT=x run_ci "$r2" "$bin2" --fast) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
}

# Test that a DXX_CI_POLL that is not a positive number of seconds is a
# usage error, also while the machine lock is free: sleep would fail at
# once and the wait would spin.
test_bad_poll_interval_is_usage_error()
{
	local bin r out rc p
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	for p in x 0 -1 1.2.3 ''; do
		out=$(DXX_CI_POLL=$p run_ci "$r" "$bin" --fast) && rc=0 || rc=$?
		assert_eq "$rc" 2 "DXX_CI_POLL='$p': $out"
		assert_contains "$out" "ci.sh: DXX_CI_POLL is '$p', not a positive number of seconds"
		assert_not_contains "$out" "native check"
	done
}

# Test that a machine lock left by a ci.sh that no longer runs is taken
# over without waiting, and removed after the run.
test_stale_machine_lock_is_taken_over()
{
	local bin r out dead
	bin=$(make_scratch) || die "make_scratch failed"
	r=$(make_ci_repo "$bin") || die "make_ci_repo failed"
	sh -c 'exit 0' &
	dead=$!
	wait "$dead"
	mkdir -p "$bin/machine.lock"
	echo "$dead" > "$bin/machine.lock/pid"
	out=$(DXX_CI_MAX_WAIT=0 run_ci "$r" "$bin" --fast)
	assert_eq "$?" 0 "$out"
	assert_not_contains "$out" "waiting for"
	[[ ! -e $bin/machine.lock ]] || fail "the machine lock outlived the run"
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

#!/usr/bin/env bash
# The fork's CI (ADR 4): local only, one command.
#
#   ci.sh [--fast] [--clean] [--container]
#   ci.sh --gate [--base REV] [--plan]
#   ci.sh --tested REV
#
# ci.sh --clean is the full run (ADR 6): the nightly runs it, and it is the
# gate before an upstream offer or a release tag.  ci.sh --gate is the
# merge gate, run before every merge (below).
#
# Two legs, run at once:
# 1. native: tools/dev/check.sh on this machine (skipped with --container);
# 2. container: tools/dev/check.sh in the Linux reference container,
#    offline (container.sh check): by default on a copy of the working
#    tree; with --clean on a fresh clone of the commit under test, with no
#    build caches.
# Each leg's output is printed whole when both are done, native first.
# First, before the machine lock and either leg or the merge gate's parts,
# ci.sh checks this machine's toolchain (check.sh --doctor, its known
# failures passing) and stops at once when it fails: a stale or missing
# dependency prefix names `tools/dev/deps.sh all`.  --container alone
# skips it.
# The whole output also goes to build/ci/ci.log (build/ is kept out of
# Time Machine), replaced by each run; its path is printed at the start and
# the end.  Read that file rather than redirecting ci.sh's output.  One
# ci.sh runs at a time per clone: a second one fails at once, naming the
# first (lock: build/ci/lock, taken over when its ci.sh no longer runs).
# One runs at a time on the machine: a ci.sh in another worktree waits,
# naming the running one, up to DXX_CI_MAX_WAIT seconds (default 3600),
# then fails (lock: ~/Library/Caches/dxx-rebirth/ci.lock, on Linux under
# ~/.cache; DXX_CI_MACHINE_LOCK overrides it).
# --fast passes --fast to check.sh in both legs.  Both legs always run;
# exit status 1 names the failed ones, 2 is a usage error.  Build the image
# first with `container.sh build`.
#
# --gate: the merge gate of ADR 6, on the commits between the base REV
# (default: the merge-base of HEAD and sdl-gpu-port) and HEAD, which must
# be clean (no uncommitted or untracked file).  It runs `check.sh --gate
# REV` natively (builds, tests, leaks, tidy and format of the changed fork
# files, doctor, gitleaks on the new commits, R1 against REV's baseline;
# its own 5-minute budget), and adds by changed path (path_classes):
# tools/dev and docs/agents files run shellcheck and the shell tests of the
# changed scripts (`check.sh --tools`); the SDL_GPU device's sources,
# gpu-programs and gpu-validate.sh the GPU programs (`check.sh --gpu`,
# #24); image inputs, SConstruct,
# site_scons, fork build flags and shaders the container leg on a clean
# clone; any other path the full run, both legs, --clean, which holds the
# tools and container parts.  Each part runs,
# in that order, after the one before; exit 1 names the failed ones.  It
# fails before running anything when DXX_D2_DATA names no directory
# holding DESCENT2.HOG (R1 needs the game data, game-data.md), and on an
# empty base..HEAD (HEAD is the base, or merged into it), which would
# check nothing.  It logs HEAD and its tree first; HEAD must not move
# while it runs.  --plan prints that, the changed paths, what each adds
# and the parts that would run, and runs nothing.  A passing gate records
# the tree it checked in the clone's git directory (dxx-gate/passed/,
# shared by its worktrees).
#
# --tested REV: succeed when the merge gate passed on REV's tree, in any
# worktree of this clone, else fail.  The orchestrator runs it on the
# merged sdl-gpu-port before pushing (orchestrating.md): signing rewrites
# the commits, not their tree.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
source "$dev_dir/env.sh"

usage()
{
	echo "usage: $0 [--fast] [--clean] [--container]" >&2
	echo "       $0 --gate [--base REV] [--plan]" >&2
	echo "       $0 --tested REV" >&2
	exit 2
}

fast=() clean=() native=1 gate=0 plan=0 base='' tested=''
while [[ $# -gt 0 ]]; do
	case $1 in
	--fast) fast=(--fast) ;;
	--clean) clean=(--clean) ;;
	--container) native=0 ;;
	--gate) gate=1 ;;
	--plan) plan=1 ;;
	--base) [[ $# -ge 2 && -n $2 ]] || usage; base=$2; shift ;;
	--tested) [[ $# -eq 2 && -n $2 ]] || usage; tested=$2; shift ;;
	*) usage ;;
	esac
	shift
done
if [[ -n $tested ]]; then
	[[ ${#fast[@]} -eq 0 && ${#clean[@]} -eq 0 && $native -eq 1 && $gate -eq 0 && $plan -eq 0 &&
		-z $base ]] || usage
elif [[ $gate -eq 1 ]]; then
	[[ ${#fast[@]} -eq 0 && ${#clean[@]} -eq 0 && $native -eq 1 ]] || usage
elif [[ $plan -eq 1 || -n $base ]]; then
	usage
fi

# Print what changed path $1 adds to the merge gate (ADR 6's table): game
# (the gate alone), tools (shellcheck and the shell tests of the changed
# scripts), gpu (check.sh --gpu: the GPU programs under gpu-validate.sh,
# on Metal; for the SDL_GPU device's sources, sdl_wrap_gpu.h, gpu-programs
# and gpu-validate.sh), container (the container leg), or full (the full
# run, which holds gpu).  An
# image input or fork build flag in tools/dev adds both tools and
# container; a path no line maps runs the full run.
path_classes()
{
	case $1 in
	common/render/sdlgpu*|common/render/unittest/sdlgpu*|common/sdl/sdl_wrap_gpu.h)
		echo game gpu ;;
	tools/dev/gpu-programs|tools/dev/gpu-validate.sh) echo tools gpu ;;
	common/*|similar/*|d1x-rebirth/*|d2x-rebirth/*) echo game ;;
	tools/dev/deps.sh|tools/dev/env.sh|tools/dev/toolchain.pins|tools/dev/requirements.*|\
	tools/dev/container/*|tools/dev/build.sh|tools/dev/site-*.py|tools/dev/fork_warnings.py|\
	tools/dev/hardening.py|tools/dev/hardening-exit.h|tools/dev/upstream_begin.h|\
	tools/dev/upstream_end.h|tools/dev/fork-dirs)
		echo tools container ;;
	tools/dev/*|docs/agents/*) echo tools ;;
	SConstruct|site_scons/*|shaders/*) echo container ;;
	*) echo full ;;
	esac
}

# Print the shell tests (names in tools/dev/tests) changed path $1 runs: a
# test runs itself (none once deleted); any other tools/dev file runs every
# test naming it, or every test when none does; a docs/agents file none.
path_tests()
{
	local name=${1##*/} hits rc
	case $1 in
	docs/agents/*) return 0 ;;
	tools/dev/tests/*_test.sh) [[ ! -f $root/$1 ]] || echo "$name"; return 0 ;;
	esac
	hits=$(cd "$dev_dir/tests" && grep -lF -- "$name" ./*_test.sh) && rc=0 || rc=$?
	[[ $rc -le 1 ]] || { echo "ci.sh: cannot search tools/dev/tests for $name" >&2; return 1; }
	[[ $rc -eq 0 ]] || hits=$(cd "$dev_dir/tests" && ls ./*_test.sh) || return 1
	printf '%s\n' "${hits//.\//}"
}

# Print the directory of the trees passing gates checked: in the clone's
# git directory, shared by its worktrees, never committed.
passed_dir()
{
	local common
	common=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir) ||
		{ echo "ci.sh: cannot find the clone's git directory" >&2; return 1; }
	printf '%s\n' "$common/dxx-gate/passed"
}

if [[ -n $tested ]]; then
	tree=$(git -C "$root" rev-parse --verify -q "$tested^{tree}") ||
		{ echo "ci.sh: --tested $tested is no commit" >&2; exit 2; }
	dir=$(passed_dir) || exit 1
	if [[ ! -f $dir/$tree ]]; then
		echo "ci.sh: the merge gate has not passed on tree $tree ($tested); run ci.sh --gate on it" >&2
		exit 1
	fi
	echo "ci.sh: the merge gate passed on tree $tree ($tested): $(cat "$dir/$tree")"
	exit 0
fi

# --gate: set gate_rev, gate_parts (the parts to run, in order),
# gate_tests (the shell tests of the tools part) and plan (lines naming
# each changed path with what it adds) from the paths changed between the
# base and HEAD.
gate_plan()
{
	local p c paths cs classes=() tests=()
	if [[ -z $base ]]; then
		base=$(git -C "$root" merge-base HEAD sdl-gpu-port) ||
			{ echo "ci.sh: no merge-base of HEAD and sdl-gpu-port; pass --base" >&2; return 2; }
	fi
	gate_rev=$(git -C "$root" rev-parse --verify -q "$base^{commit}") ||
		{ echo "ci.sh: --base $base is no commit" >&2; return 2; }
	if ! gate_head=$(git -C "$root" rev-parse --verify HEAD) ||
		! gate_tree=$(git -C "$root" rev-parse --verify "HEAD^{tree}") ||
		! c=$(git -C "$root" rev-list --count "$gate_rev..HEAD"); then
		echo "ci.sh: cannot read HEAD or the commits since $gate_rev" >&2
		return 1
	fi
	if [[ $c -eq 0 ]]; then
		echo "ci.sh: no commits in ${gate_rev:0:12}..HEAD (${gate_head:0:12}): HEAD is the base" \
			"or merged into it, and the gate would check nothing" >&2
		return 1
	fi
	paths=$(git -C "$root" diff --no-renames --name-only "$gate_rev" HEAD) ||
		{ echo "ci.sh: cannot list the paths changed since $gate_rev" >&2; return 1; }
	plan_lines=("gate: head $gate_head tree $gate_tree" "gate: base $gate_rev")
	while IFS= read -r p; do
		[[ -n $p ]] || continue
		c=$(path_classes "$p")
		plan_lines+=("gate: $p: $c")
		read -r -a cs <<<"$c"
		classes+=("${cs[@]}")
		if [[ " $c " == *" tools "* ]]; then
			c=$(path_tests "$p") || return 1
			[[ -z $c ]] || mapfile -t -O "${#tests[@]}" tests <<<"$c"
		fi
	done <<<"$paths"
	gate_parts=(gate)
	for c in tools gpu container full; do
		[[ " ${classes[*]-} " != *" $c "* ]] && continue
		# The full run holds the container leg and every shell test.
		[[ $c != full && " ${classes[*]} " == *" full "* ]] && continue
		gate_parts+=("$c")
	done
	mapfile -t gate_tests < <(printf '%s\n' "${tests[@]+"${tests[@]}"}" | sed '/^$/d' | sort -u)
	plan_lines+=("gate: parts: ${gate_parts[*]}")
	[[ " ${gate_parts[*]} " != *" tools "* ]] || plan_lines+=("gate: shell tests: ${gate_tests[*]-none}")
}

if [[ $gate -eq 1 ]]; then
	gate_plan || exit
	if [[ $plan -eq 1 ]]; then
		printf '%s\n' "${plan_lines[@]}"
		exit 0
	fi
	if [[ -z ${DXX_D2_DATA-} || ! -f ${DXX_D2_DATA-}/DESCENT2.HOG ]]; then
		echo "ci.sh: the merge gate needs DXX_D2_DATA, the directory holding DESCENT2.HOG," \
			"for R1 (docs/agents/game-data.md); it is '${DXX_D2_DATA-}'" >&2
		exit 1
	fi
	dirty=$(git -C "$root" status --porcelain --untracked-files=normal) ||
		{ echo "ci.sh: cannot read the working tree's status" >&2; exit 1; }
	if [[ -n $dirty ]]; then
		echo "ci.sh: the merge gate checks commits; commit or remove these first:" >&2
		printf '%s\n' "$dirty" >&2
		exit 1
	fi
fi

log=$root/build/ci/ci.log
lock=$root/build/ci/lock
if [[ $dev_os == darwin ]]; then
	machine_lock=${DXX_CI_MACHINE_LOCK:-$HOME/Library/Caches/dxx-rebirth/ci.lock}
else
	machine_lock=${DXX_CI_MACHINE_LOCK:-${XDG_CACHE_HOME:-$HOME/.cache}/dxx-rebirth/ci.lock}
fi
dev_tm_exclude "$root/build" >/dev/null
if ! mkdir -p "${log%/*}" "${machine_lock%/*}"; then
	echo "ci.sh: cannot write $log" >&2
	exit 1
fi
# A lock is a directory holding its owner's pid and worktree (mkdir is
# atomic; macOS has no flock).  try_lock takes one, or takes it over once
# from a ci.sh that no longer runs: its pid no longer runs, or there is no
# pid file for 3 s (killed between mkdir and writing it).  A takeover holds
# a second lock, <lock>.takeover (mkdir again), checks again under it, and
# writes the new pid before letting it go.  Only a takeover or the owner's
# exit removes a lock, and only one takeover runs at a time, so of runs
# taking over at once one gets the lock, and none removes a lock taken
# since its check.  The exit removes a lock only while its pid file is ours.
owned=() takeover='' logs=
trap '[[ -z $takeover ]] || rmdir "$takeover"
for l in "${owned[@]+"${owned[@]}"}"; do
	[[ $(cat "$l/pid" 2>/dev/null) != "$$" ]] || rm -rf "$l"
done
rm -rf ${logs:+"$logs"}' EXIT
# Without an INT trap, bash ignores an INT that reaches it alone (kill, not
# Ctrl-C) while it waits for a command, and carries on once the command
# ends.  The trap exits then, through the EXIT trap, and dies of INT, so a
# calling shell stops too.  An INT ignored at entry (a background command)
# stays ignored.
trap 'trap - INT; kill -INT $$' INT
# Print the pid in lock $1's pid file, waiting up to 3 s for one to appear.
lock_owner()
{
	local owner
	for _ in $(seq 30); do
		owner=$(cat "$1/pid" 2>/dev/null)
		[[ -z $owner && -d $1 ]] || break
		sleep 0.1
	done
	printf '%s\n' "$owner"
}
# Succeed if lock $1 exists and its owner $2 (empty: no pid file) no longer
# runs.
lock_stale()
{
	[[ -d $1 ]] && ! { [[ -n $2 ]] && kill -0 "$2" 2>/dev/null; }
}
# Make lock $1, which mkdir just made, ours.
own_lock()
{
	owned+=("$1")
	printf '%s\n' "$root" > "$1/worktree"
	echo $$ > "$1/pid"
}
# Try once to take lock $1.  Status 0: taken; 1: a running ci.sh holds it,
# its pid in held_by; 2: another ci.sh is taking over a stale one.
try_lock()
{
	local l=$1 try got=
	held_by=
	for try in 1 2; do
		mkdir "$l" 2>/dev/null && { own_lock "$l"; return 0; }
		held_by=$(cat "$l/pid" 2>/dev/null)
		if [[ $try -eq 2 ]] || ! lock_stale "$l" "$held_by"; then
			[[ -n $held_by ]] || held_by=$(lock_owner "$l")
			return 1
		fi
		mkdir "$l.takeover" 2>/dev/null || return 2
		takeover=$l.takeover
		lock_stale "$l" "$(lock_owner "$l")" && rm -rf "$l"
		mkdir "$l" 2>/dev/null && { own_lock "$l"; got=1; }
		rmdir "$l.takeover"
		takeover=
		[[ -z $got ]] || return 0
	done
	return 1
}
try_lock "$lock"
case $? in
1)
	echo "ci.sh: another ci.sh (pid ${held_by:-unknown}) is running; its log: $log" >&2
	echo "ci.sh: remove $lock if it no longer runs" >&2
	exit 1 ;;
2)
	echo "ci.sh: another ci.sh is taking over the lock of one that no longer runs" >&2
	echo "ci.sh: remove $lock.takeover if none runs" >&2
	exit 1 ;;
esac
if ! : > "$log"; then
	echo "ci.sh: cannot write $log" >&2
	exit 1
fi
# Print the arguments as a line, also into the log.
say()
{
	printf '%s\n' "$*" | tee -a "$log"
}
say "ci.sh: log: $log"

# Check the toolchain first, before the machine lock and any leg or part;
# stop on a failure.
if [[ $native -eq 1 || $gate -eq 1 ]]; then
	say "== ci: doctor (check.sh --doctor)"
	doctor_out=$("$dev_dir/check.sh" --doctor 2>&1) && rc=0 || rc=$?
	printf '%s\n' "$doctor_out" | tee -a "$log"
	if [[ $rc -ne 0 ]]; then
		if grep -qE '^(FAIL|SKIP) (deps|venv|libs)[ :]' <<<"$doctor_out"; then
			say "ci.sh: the dependency prefix is stale or missing; run tools/dev/deps.sh all" >&2
		else
			say "ci.sh: the doctor failed (its FAIL lines above); fix them first" >&2
		fi
		say "ci.sh: failed: doctor" >&2
		say "ci.sh: log: $log" >&2
		exit 1
	fi
fi

# One ci.sh at a time on this machine, whatever its worktree: parallel runs
# overload it (budget overruns, timing flakes).  Wait for the machine lock,
# polling every DXX_CI_POLL seconds (default 5), up to DXX_CI_MAX_WAIT
# seconds (default 3600), then fail; a value that is no such number is a
# usage error.  It is taken after the worktree's own lock, always in that
# order.
max_wait=${DXX_CI_MAX_WAIT:-3600}
[[ $max_wait =~ ^[0-9]+$ ]] || { echo "ci.sh: DXX_CI_MAX_WAIT is '$max_wait', not seconds" >&2; exit 2; }
poll=${DXX_CI_POLL-5}
if ! [[ $poll =~ ^([0-9]+\.?[0-9]*|\.[0-9]+)$ && $poll =~ [1-9] ]]; then
	echo "ci.sh: DXX_CI_POLL is '$poll', not a positive number of seconds" >&2
	exit 2
fi
waited_for='' wait_start=$EPOCHSECONDS
while :; do
	try_lock "$machine_lock"
	locked=$?
	[[ $locked -ne 0 ]] || break
	if [[ -n $held_by && $held_by != "$waited_for" ]]; then
		waited_for=$held_by
		say "ci.sh: waiting for ci.sh pid $held_by in $(cat "$machine_lock/worktree" 2>/dev/null ||
			echo 'an unknown worktree') (machine lock $machine_lock, up to $max_wait s)"
	fi
	if ((EPOCHSECONDS - wait_start >= max_wait)); then
		if [[ $locked -eq 2 ]]; then
			say "ci.sh: gave up after $max_wait s waiting for another ci.sh to take over" \
				"the stale machine lock $machine_lock; remove $machine_lock.takeover if none runs" >&2
		else
			say "ci.sh: gave up after $max_wait s waiting for ci.sh pid ${held_by:-unknown};" \
				"remove $machine_lock if it no longer runs" >&2
		fi
		exit 1
	fi
	sleep "$poll"
done

logs=$(mktemp -d "${TMPDIR:-/tmp}/ci.XXXXXX")
failed=()
# Run the legs: native (unless --container) and container, at once, each
# one's output printed whole when both are done, native first; add each
# failed one to failed.
run_legs()
{
	local leg legs=()
	declare -A pid
	if [[ $native -eq 1 ]]; then
		legs+=(native)
		{
			echo "== ci: native check.sh ${fast[*]-}"
			"$dev_dir/check.sh" "${fast[@]+"${fast[@]}"}"
		} > "$logs/native" 2>&1 &
		pid[native]=$!
	fi
	legs+=(container)
	{
		echo "== ci: container check.sh ${clean[*]-} ${fast[*]-}"
		"$dev_dir/container.sh" check "${clean[@]+"${clean[@]}"}" "${fast[@]+"${fast[@]}"}"
	} > "$logs/container" 2>&1 &
	pid[container]=$!
	for leg in "${legs[@]}"; do
		wait "${pid[$leg]}" || failed+=("$leg")
		tee -a "$log" < "$logs/$leg"
	done
}

# Run part $1 of the merge gate, the command "${@:2}", its output also
# into the log; add $1 to failed when it fails.
gate_part()
{
	local part=$1 start=$EPOCHSECONDS rc
	shift
	say "== ci: gate part $part: $*"
	"$@" 2>&1 | tee -a "$log"
	rc=${PIPESTATUS[0]}
	say "-- ci: gate part $part: $((EPOCHSECONDS - start)) s"
	[[ $rc -eq 0 ]] || failed+=("$part")
}

if [[ $gate -eq 0 ]]; then
	run_legs
else
	for line in "${plan_lines[@]}"; do
		say "$line"
	done
	for part in "${gate_parts[@]}"; do
		case $part in
		gate) gate_part gate "$dev_dir/check.sh" --gate "$gate_rev" ;;
		tools) gate_part tools "$dev_dir/check.sh" --tools "${gate_tests[@]+"${gate_tests[@]}"}" ;;
		gpu) gate_part gpu "$dev_dir/check.sh" --gpu ;;
		container) gate_part container "$dev_dir/container.sh" check --clean ;;
		full)
			clean=(--clean)
			say "== ci: gate part full: ci.sh --clean"
			run_legs
			[[ " ${failed[*]-} " != *" native "* && " ${failed[*]-} " != *" container "* ]] ||
				failed=("${failed[@]/native}") failed=("${failed[@]/container}") failed+=(full)
			read -r -a failed <<<"${failed[*]}" ;;
		esac
	done
fi

# The gate checked the tree it named only if HEAD stayed there, clean.
if [[ $gate -eq 1 && ${#failed[@]} -eq 0 ]]; then
	if ! now=$(git -C "$root" rev-parse HEAD) ||
		! dirty=$(git -C "$root" status --porcelain --untracked-files=normal) ||
		[[ $now != "$gate_head" || -n $dirty ]]; then
		say "ci.sh: HEAD moved from ${gate_head:0:12} or the tree changed while the gate ran" >&2
		failed+=(head)
	elif ! dir=$(passed_dir) || ! mkdir -p "$dir" ||
		! echo "head $gate_head base $gate_rev $(date -u +%FT%TZ)" > "$dir/$gate_tree"; then
		say "ci.sh: cannot record the tested tree $gate_tree" >&2
		failed+=(record)
	fi
fi
if [[ ${#failed[@]} -ne 0 ]]; then
	say "ci.sh: failed: ${failed[*]}" >&2
	say "ci.sh: log: $log" >&2
	exit 1
fi
say "ci.sh: passed"
say "ci.sh: log: $log"

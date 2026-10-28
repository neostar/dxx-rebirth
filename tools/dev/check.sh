#!/usr/bin/env bash
# Single entry point for the fork's checks.
#
#   check.sh --pre-commit   the pre-commit tier (ADR 5, under 10 s), run
#                           by the pre-commit hook after the size limit
#   check.sh --pre-push     gitleaks on the commits being pushed, read from
#                           the pre-push hook's standard input
#   check.sh --fast         quick subset: the pre-commit steps and more,
#                           the shell tests among them
#   check.sh                everything: the fast subset plus the full steps
#   check.sh --gate REV     the merge gate's native steps (ADR 6) on the
#                           commits REV..HEAD, within its 5-minute budget;
#                           ci.sh --gate runs it
#   check.sh --tools [TEST...]  shellcheck, the docs and test checks, and
#                           the named tools/dev/tests files (ci.sh --gate's
#                           tools part)
#   check.sh --gpu          the GPU programs under gpu-validate.sh (ci.sh
#                           --gate's gpu part, #24)
#   check.sh --doctor       the doctor step alone (ci.sh runs it first)
#
# Later tickets add a step by writing a step_<name> function and listing it
# in fast_steps or full_steps.  Each step prints what it runs; check.sh runs
# all steps, then exits 1 if any failed.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
source "$dev_dir/env.sh"

usage()
{
	echo "usage: $0 [--pre-commit|--pre-push|--fast|--gate REV|--tools [TEST...]|--gpu|--doctor]" >&2
	exit 2
}

# Reject licensed game data staged for commit.
step_guard()
{
	"$dev_dir/guard-licensed-data.sh" --staged
}

# Check the clone's local config against tools/dev/repo-config.  Worktrees
# share the main clone's config file, read directly so that -c options and
# global config cannot hide a deviation.  The repository is the one holding
# this script, wherever check.sh is run from.
step_repo_config()
{
	local config rule key want got rc=0
	config=$(git -C "$dev_dir" rev-parse --path-format=absolute --git-common-dir)/config
	while IFS= read -r rule; do
		case $rule in
		'#'*|'') ;;
		'!'*)
			got=$(git config --file "$config" --name-only --get-regexp "^${rule#!}\\.")
			for key in $got; do
				echo "repo config: $key is set; remove it (git config --unset $key)"
				rc=1
			done ;;
		*=*)
			key=${rule%%=*} want=${rule#*=}
			got=$(git config --file "$config" --get "$key")
			if [[ $got != "$want" ]]; then
				echo "repo config: $key is '${got}', expected '$want' (run tools/dev/setup.sh)"
				rc=1
			fi ;;
		esac
	done < "$dev_dir/repo-config"
	return "$rc"
}

# Run shellcheck on the shell scripts in the fork directories
# (tools/dev/fork-dirs): *.sh files and files with a sh or bash shebang.
# CHECK_SHELLCHECK names the binary (default shellcheck); a missing one is
# a SKIP.
step_shellcheck()
{
	local shellcheck=${CHECK_SHELLCHECK:-shellcheck} d f dirs=() files=()
	if ! command -v "$shellcheck" >/dev/null; then
		echo "SKIP shellcheck: $shellcheck not found; install shellcheck to run this check"
		return 0
	fi
	while IFS= read -r d; do
		[[ -z $d || $d == \#* ]] || dirs+=("$d")
	done < "$dev_dir/fork-dirs"
	while IFS= read -r -d '' f; do
		[[ $f == *.sh ]] || head -n 1 "$root/$f" | grep -qE '^#!.*[/ ](ba)?sh$' && files+=("$f")
	done < <(cd "$root" && find "${dirs[@]}" -type f -print0)
	[[ ${#files[@]} -eq 0 ]] && return 0
	(cd "$root" && "$shellcheck" -x --source-path=SCRIPTDIR "${files[@]}")
}

# Succeed if upstream/master exists; else print step $1's SKIP and fail.
need_upstream()
{
	git -C "$root" rev-parse -q --verify upstream/master >/dev/null && return 0
	echo "SKIP $1: no upstream/master; fetch upstream to run this check"
	return 1
}

# Fail on commit hashes in docs/, CLAUDE.md and GLOSSARY.md that are not in
# upstream/master: fork-only hashes go stale on every rebase or re-sign, so
# fork commits are cited by subject.  Only 7-40 digit hex tokens that name
# a commit here count; other hex (checksums, other repositories' commits)
# passes.  Without upstream/master the check is a SKIP.
step_stale_hashes()
{
	local f tok rc=0 files=()
	need_upstream stale_hashes || return 0
	while IFS= read -r -d '' f; do
		files+=("$f")
	done < <(cd "$root" && find docs CLAUDE.md GLOSSARY.md -type f -name '*.md' -print0 2>/dev/null)
	for f in "${files[@]+"${files[@]}"}"; do
		for tok in $(tr -cs '0-9a-zA-Z' '\n' < "$root/$f" | grep -xE '[0-9a-f]{7,40}' | sort -u); do
			git -C "$root" rev-parse -q --verify "$tok^{commit}" >/dev/null || continue
			git -C "$root" merge-base --is-ancestor "$tok" upstream/master && continue
			echo "$f: $tok is a fork commit; cite it by subject"
			rc=1
		done
	done
	return "$rc"
}

# The one fork workflow ADR 4 allows: the manual D3D12 check (#49).
manual_workflow=.github/workflows/d3d12-check.yml

# Whether workflow file $1's triggers are workflow_dispatch alone.  Every
# top-level line but a comment must be one of a workflow's keys, bare and
# followed by `:` and a space or the line's end (an allowlist: a quoted,
# escaped, tagged or complex key, or `on :`, fails, so no other spelling of
# `on` slips past).  There is one `on:`, whose value is `workflow_dispatch`
# on its line or a block whose keys, at its first indent, are
# workflow_dispatch only.  Any other form, a flow list or map among them,
# fails: the check reads YAML only this far.
only_workflow_dispatch()
{
	awk '
		function strip(s) { sub(/[ \t]+#.*$/, "", s); sub(/[ \t]+$/, "", s); return s }
		/^[ \t]*(#|$)/ { next }
		/^[^ \t]/ {
			inon = 0
			if ($0 !~ /^(name|run-name|on|permissions|env|defaults|concurrency|jobs):( |$)/) {
				bad++
				next
			}
			key = $0
			sub(/:.*/, "", key)
			if (key == "on") {
				ons++
				value = strip(substr($0, index($0, ":") + 1))
				sub(/^[ \t]+/, "", value)
				if (value == "workflow_dispatch") ok++
				else if (value == "") inon = 1
				else bad++
			}
			next
		}
		inon {
			line = strip($0)
			match(line, /^[ \t]*/)
			if (!indent) indent = RLENGTH
			if (RLENGTH < indent) { bad++; next }
			if (RLENGTH > indent) next
			if (line ~ /^[ \t]*workflow_dispatch:$/) ok++
			else bad++
		}
		END { exit !(ons == 1 && ok == 1 && !bad) }' "$1"
}

# Fail on any file under .github/workflows/ that upstream/master does not
# have, but the manual D3D12 check: the fork's CI is local (ADR 4), and
# GitHub must not run a workflow of its own, but by hand.  That one fails
# when it has any trigger but workflow_dispatch.  Without upstream/master
# the check is a SKIP.
step_workflows()
{
	local f rc=0
	need_upstream workflows || return 0
	while IFS= read -r f; do
		if [[ $f == "$manual_workflow" ]]; then
			only_workflow_dispatch "$root/$f" && continue
			echo "$f: its only trigger must be workflow_dispatch (ADR 4)"
		else
			echo "$f: a GitHub workflow upstream does not have; the fork's CI is tools/dev/ci.sh"
		fi
		rc=1
	done < <(comm -23 \
		<(git -C "$root" ls-files -co --exclude-standard -- .github/workflows | sort -u) \
		<(git -C "$root" ls-tree -r --name-only upstream/master -- .github/workflows | sort -u))
	return "$rc"
}

# Print the gitleaks version toolchain.pins pins.
gitleaks_pin()
{
	awk '$1 == "prebuilt" && $2 == "gitleaks" {print $4; exit}' "$dev_dir/toolchain.pins" |
		sed -nE 's|.*/download/v([0-9.]+)/.*|\1|p'
}

# Run gitleaks with the repository's .gitleaks.toml (without one,
# gitleaks' default rules) and the remaining arguments, redacted, from the
# repository root: the pinned one in the prefix (CHECK_GITLEAKS overrides
# it), else a gitleaks on PATH (CHECK_GITLEAKS_FALLBACK names it) at the
# pinned version.  Without either, the pre-commit tier and the reference
# image (DXX_REFERENCE_IMAGE=1, which has every pinned tool) fail, naming
# how to install it: no commit goes in unscanned.  Elsewhere it is a SKIP.
# $1 names the step.
run_gitleaks()
{
	local step=$1 gitleaks=${CHECK_GITLEAKS:-$dev_deps_prefix/opt/gitleaks/bin/gitleaks}
	local fallback=${CHECK_GITLEAKS_FALLBACK:-gitleaks} pin got why config=()
	shift
	[[ -f $root/.gitleaks.toml ]] && config=(--config .gitleaks.toml)
	if ! command -v "$gitleaks" >/dev/null; then
		pin=$(gitleaks_pin)
		why="gitleaks not found ($gitleaks)"
		if command -v "$fallback" >/dev/null; then
			got=$("$fallback" version 2>/dev/null)
			got=${got#v}
			if [[ -n $pin && $got == "$pin" ]]; then
				gitleaks=$fallback
				why=
			else
				why="$why, and $fallback on PATH is '$got', not the pinned $pin"
			fi
		fi
	fi
	if [[ -n ${why-} ]]; then
		if [[ ${DXX_REFERENCE_IMAGE-} == 1 ]]; then
			echo "FAIL $step: $why; the reference image must have it"
			return 1
		elif [[ $mode == pre-commit || $mode == pre-push || $mode == gate ]]; then
			echo "FAIL $step: $why; nothing goes in or out unscanned: run" \
				"tools/dev/deps.sh all, or put gitleaks $pin on PATH"
			return 1
		fi
		echo "SKIP $step: $why; run tools/dev/deps.sh build"
		return 0
	fi
	(cd "$root" && env -u GITLEAKS_CONFIG -u GITLEAKS_CONFIG_TOML "$gitleaks" git "$@" \
		"${config[@]+"${config[@]}"}" --redact --verbose --no-banner --no-color --log-level warn .)
}

# Fail on a way past the secret scan in the index: a .gitleaksignore file
# or an inline allow comment (gitleaks plus ":allow"), unless .gitleaks.toml lists the file
# with a reason, as a line "# allowed: <path> -- <reason>".  .gitleaks.toml
# itself is the list.
step_gitleaks_allow()
{
	local f hits allowed rc=0 marker='gitleaks'':allow'
	allowed=$(sed -nE 's/^# allowed: ([^[:space:]]+) -- [^[:space:]].*/\1/p' \
		"$root/.gitleaks.toml" 2>/dev/null)
	hits=$(cd "$root" && { git ls-files --cached -- .gitleaksignore '*/.gitleaksignore'
		git grep --cached -l -I -F "$marker" -- . ':!.gitleaks.toml'; } | sort -u)
	while IFS= read -r f; do
		[[ -z $f ]] && continue
		grep -qxF "$f" <<<"$allowed" && continue
		echo "$f: a .gitleaksignore or $marker comment; list it in .gitleaks.toml" \
			"(# allowed: $f -- <reason>) or remove it"
		rc=1
	done <<<"$hits"
	return "$rc"
}

# Fail on a secret in the staged changes (the pre-commit tier).
step_secrets()
{
	run_gitleaks secrets --staged
}

# The pre-push tier: fail on a secret in exactly the commits being pushed,
# read from the pre-push hook's standard input ("<local ref> <local sha>
# <remote ref> <remote sha>" per ref): <remote sha>..<local sha>, or, for a
# ref new on the remote or a remote sha this clone lacks, the commits on no
# remote-tracking branch.  A deletion has nothing to scan.
step_push_secrets()
{
	local lref lsha rref rsha range rc=0 zero=0000000000000000000000000000000000000000
	while read -r lref lsha rref rsha; do
		[[ -z $lsha || $lsha == "$zero" ]] && continue
		if [[ $rsha != "$zero" ]] && git -C "$root" cat-file -e "$rsha^{commit}" 2>/dev/null; then
			range="$rsha..$lsha"
		else
			range="$lsha --not --remotes"
		fi
		echo "push_secrets: $lref -> $rref: $range"
		run_gitleaks push_secrets --log-opts="$range" || rc=1
	done
	return "$rc"
}

# The merge gate: fail on a secret in the new commits, base..HEAD.
step_new_secrets()
{
	echo "new_secrets: $gate_rev..HEAD"
	run_gitleaks new_secrets --log-opts="$gate_rev..HEAD"
}

# The merge gate: clang-format, then clang-tidy, on the fork C, C++ and
# Objective-C(++) files changed in base..HEAD (format.sh, as --staged
# picks them); none is a pass.
step_changed_format()
{
	local d f e changed files=() tidy_files=() dirs=()
	while IFS= read -r d; do
		[[ -z $d || $d == \#* ]] || dirs+=("$d")
	done < "$dev_dir/fork-dirs"
	changed=$(git -C "$root" diff --no-renames --diff-filter=d --name-only "$gate_rev" HEAD) ||
		{ echo "changed_format: cannot list the files changed since $gate_rev"; return 1; }
	while IFS= read -r f; do
		[[ -n $f ]] || continue
		e=${f##*.}
		[[ " c cc cpp cxx h hh hpp hxx m mm " == *" $e "* ]] || continue
		for d in "${dirs[@]}"; do
			[[ $f == "$d"/* ]] && { files+=("$root/$f"); break; }
		done
	done <<<"$changed"
	if [[ ${#files[@]} -eq 0 ]]; then
		echo "changed_format: no changed fork C or C++ file"
		return 0
	fi
	# The SDL3-only files (tools/dev/sdl3-only) are tidied by step_sdl3_games,
	# which tidies all of them, so not twice here; formatted here still.
	# Where it only SKIPs (not macOS), format.sh SKIPs them here, naming each.
	tidy_files=()
	for f in "${files[@]}"; do
		if [[ $dev_os == darwin ]] && grep -qxF -e "${f#"$root"/}" "$dev_dir/sdl3-only" 2>/dev/null; then
			continue
		fi
		tidy_files+=("$f")
	done
	"$dev_dir/format.sh" --check "${files[@]}" || return 1
	# No file left to tidy: format.sh --tidy without files tidies the fork directories.
	[[ ${#tidy_files[@]} -eq 0 ]] || "$dev_dir/format.sh" --tidy "${tidy_files[@]}"
}

# Write build/dev-sdl3's compilation database, with which format.sh tidies
# the SDL3-only sources (tools/dev/sdl3-only); macOS only, where the pinned
# SDL3 is built: elsewhere format.sh SKIPs them, naming each.
sdl3_database()
{
	[[ $dev_os == darwin ]] || return 0
	"$dev_dir/build.sh" dev sdl3=1 d1x=1 d2x=1 build/dev-sdl3/compile_commands.json >/dev/null ||
		{ echo "build.sh dev sdl3=1 d1x=1 d2x=1 build/dev-sdl3/compile_commands.json failed"; return 1; }
}

# The merge gate (#95): build both games with sdl3=1 (compile and link,
# build/dev-sdl3; the SDL3 unit tests alone leave an SDL3 arm in the games
# unbuilt), which also writes build/dev-sdl3's compilation database, then
# clang-tidy every file tools/dev/sdl3-only lists with it.  A SKIP line from
# the tidy fails the step: the gate must not skip the files only this
# database holds.  Elsewhere than macOS (no pinned SDL3 to build) a SKIP.
step_sdl3_games()
{
	local f out rc=0 files=()
	if [[ $dev_os != darwin ]]; then
		echo "SKIP sdl3_games: macOS only, where the pinned SDL3 is built"
		return 0
	fi
	"$dev_dir/build.sh" dev sdl3=1 d1x=1 d2x=1 build/dev-sdl3/compile_commands.json \
		build/dev-sdl3/d1x-rebirth/d1x-rebirth build/dev-sdl3/d2x-rebirth/d2x-rebirth || return 1
	while IFS= read -r f; do
		[[ -z $f || $f == \#* ]] || files+=("$root/$f")
	done < "$dev_dir/sdl3-only" || { echo "sdl3_games: cannot read tools/dev/sdl3-only"; return 1; }
	if [[ ${#files[@]} -eq 0 ]]; then
		echo "sdl3_games: tools/dev/sdl3-only lists no file"
		return 1
	fi
	for f in "${files[@]}"; do
		[[ -f $f ]] || { echo "sdl3_games: ${f#"$root"/} is in tools/dev/sdl3-only and missing"; rc=1; }
	done
	[[ $rc -eq 0 ]] || return 1
	out=$("$dev_dir/format.sh" --tidy "${files[@]}" 2>&1) || rc=1
	printf '%s\n' "$out"
	if grep -q '^SKIP' <<<"$out"; then
		echo "sdl3_games: the tidy of an SDL3-only file was skipped; the gate may not skip it"
		rc=1
	fi
	return "$rc"
}

# The merge gate's R1 (ADR 6): r1.sh check against the base commit's
# baseline.  r1_baseline captures it, once per base commit and R1 inputs
# (r1_inputs), into the clone's git directory (dxx-r1/<commit>-<inputs>:
# shared by its worktrees, never committed; the baseline's build tree is
# removed) and reuses it after; step_r1 plays the working tree's scenes
# into traces/local/r1-gate, its baseline a link to the captured one.
# Both derive from the licensed data and stay out of git (game-data.md).
# To clear the cache, remove the directory dxx-r1 in the clone's git
# directory: rm -rf "$(git rev-parse --git-common-dir)/dxx-r1".

# Print a 12-digit key of R1's inputs other than the base commit: the
# working tree's scenes (common/debug/scenes, which both sides play),
# r1.sh and toolchain.pins.  A change to any of them captures again.
r1_inputs()
{
	local f key hashes files=()
	while IFS= read -r -d '' f; do
		files+=("$f")
	done < <(cd "$root" && find common/debug/scenes -type f -print0 2>/dev/null | sort -z)
	if [[ ${#files[@]} -eq 0 ]]; then
		echo "r1_baseline: no scenes in $root/common/debug/scenes" >&2
		return 1
	fi
	files+=(tools/dev/r1.sh tools/dev/toolchain.pins)
	if ! hashes=$(cd "$root" && git hash-object -- "${files[@]}") ||
		! key=$(printf '%s\n' "${files[@]}" "$hashes" | git hash-object --stdin); then
		echo "r1_baseline: cannot hash R1's inputs" >&2
		return 1
	fi
	printf '%s\n' "${key:0:12}"
}
r1_baseline()
{
	local common inputs
	common=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir) || return 1
	inputs=$(r1_inputs) || return 1
	r1_cache=$common/dxx-r1/$gate_rev-$inputs
	if [[ -f $r1_cache/baseline/commit ]]; then
		echo "r1_baseline: reusing the baseline of $gate_rev and these R1 inputs in $r1_cache"
		return 0
	fi
	echo "r1_baseline: capturing the baseline of $gate_rev, once, outside the budget"
	R1_DIR=$r1_cache "$dev_dir/r1.sh" capture --base "$gate_rev" || return 1
	rm -rf "$r1_cache/src"
}
step_r1()
{
	local work=$root/traces/local/r1-gate
	rm -rf "$work" && mkdir -p "$work" && ln -s "$r1_cache/baseline" "$work/baseline" || return 1
	R1_DIR=$work "$dev_dir/r1.sh" check --base "$gate_rev" &&
		foreground_game "$root/build/r1/d2x-rebirth/d2x-rebirth"
}

# Fail when the game binary $1, the one R1 built and played, holds SDL's
# background-app hint (#24: the game is a foreground app; the source scan
# in gpu_gate misses a hint that reaches it through a macro or a library
# built in).  A missing binary fails too.
foreground_game()
{
	local rc
	[[ -f $1 ]] || { echo "r1: no game binary at $1 to scan for the background-app hint"; return 1; }
	grep -a -q -e SDL_MAC_BACKGROUND_APP -- "$1" && rc=0 || rc=$?
	case $rc in
		0) echo "r1: the game binary $1 holds SDL_MAC_BACKGROUND_APP; the game is a foreground app (#24)"
			return 1 ;;
		1) return 0 ;;
		*) echo "r1: cannot scan $1 for the background-app hint"; return 1 ;;
	esac
}

# Fail on a secret anywhere in the history of HEAD, upstream's included;
# other branches and remotes are left out.
step_secrets_history()
{
	run_gitleaks secrets_history --log-opts=HEAD
}

# Run every tools/dev/tests/*_test.sh, each under its own no-window.sh,
# env.sh's dev_jobs (DXX_JOBS) at a time; print each one's output whole,
# in file order, and fail if any failed.
step_shell_tests()
{
	local t i rc=0 out tests=() test_env
	for t in "$dev_dir"/tests/*_test.sh; do
		[[ -e $t ]] && tests+=("$t")
	done
	out=$(mktemp -d "${TMPDIR:-/tmp}/check-shell-tests.XXXXXX")
	dev_test_env test_env
	# no-window.sh exports SDL_MAC_BACKGROUND_APP=1 before env -i, which
	# would drop it: pass it on by name.
	test_env+=(SDL_MAC_BACKGROUND_APP=1)
	if [[ $mode == tools ]]; then
		tests=()
		for t in "${only_tests[@]+"${only_tests[@]}"}"; do
			if [[ ! -f $dev_dir/tests/$t ]]; then
				echo "shell_tests: $t: no such shell test in tools/dev/tests"
				rc=1
			fi
			tests+=("$dev_dir/tests/$t")
		done
		[[ $rc -eq 0 ]] || return 1
		[[ ${#tests[@]} -gt 0 ]] || { echo "shell_tests: none named"; return 0; }
	fi
	for i in "${!tests[@]}"; do
		while [[ $(jobs -rp | wc -l) -ge $dev_jobs ]]; do
			wait -n
		done
		# Each test sees only the allowlisted environment (env.sh):
		# no caller's secrets, and inside a git hook none of the GIT_DIR
		# and friends git exports for the repository being committed to.
		(unset "${dev_git_repo_vars[@]}"
			"$dev_dir/no-window.sh" env -i "${test_env[@]}" bash "${tests[i]}" > "$out/$i" 2>&1
			echo "$?" > "$out/$i.rc") &
	done
	wait
	for i in "${!tests[@]}"; do
		t=${tests[i]#"$dev_dir"/}
		echo "-- $t"
		cat "$out/$i"
		[[ $(cat "$out/$i.rc" 2>/dev/null) == 0 ]] || { echo "FAILED: $t"; rc=1; }
	done
	rm -rf "$out"
	return "$rc"
}

# Fail on a git command in tools/dev/tests outside lib.sh's tgit and
# real_git, which refuse any repository but a scratch one (tgit) and any
# change to this clone (real_git): any token ending in git, also quoted,
# escaped, as a path (/usr/bin/git), a variable ($git) or its default
# (${GIT:-git}), or assigned (g=git), outside a comment line; tgit,
# real_git and .git are other words.  A line
# directly below a whole-line comment "# raw git: <why>" is let through.
# Also fail on a scratch directory or repository assigned without a check
# (d=$(make_scratch) with no || or && after it): an empty path would
# make the test act on the current directory.  A line directly below a
# whole-line comment "# unchecked: <why>" is let through.
step_test_git()
{
	local hits lines unchecked
	unchecked=$(cd "$dev_dir/tests" && awk '
		FNR == 1 { prev = ""; f = substr(FILENAME, 3) }
		prev !~ /^[[:space:]]*# unchecked: / {
			s = $0
			while (match(s, /[[:alnum:]_]+=\$\((make_scratch|fork_repo|make_[a-z_]*repo)[^)]*\)/)) {
				s = substr(s, RSTART + RLENGTH)
				if (s !~ /^[[:space:]]*(\|\||&&)/) {
					printf "%s:%d:%s\n", f, FNR, $0
					break
				}
			}
		}
		{ prev = $0 }' ./*.sh) || { echo 'test_git: awk failed'; return 1; }
	if [[ -n $unchecked ]]; then
		mapfile -t lines <<<"$unchecked"
		printf 'tools/dev/tests/%s\n' "${lines[@]}"
		# shellcheck disable=SC2016  # the advice is code
		echo 'test_git: check the result, as in d=$(make_scratch) || die "make_scratch failed"'
	fi
	# shellcheck disable=SC2016  # $ and quotes are the regex's
	hits=$(cd "$dev_dir/tests" && awk -v q="'" '
		BEGIN { re = "(^|[^[:alnum:]_.])git([\"" q "})]|[[:space:];|&]|$)" }
		FNR == 1 { prev = ""; f = substr(FILENAME, 3) }
		$0 !~ /^[[:space:]]*#/ && $0 ~ re && prev !~ /^[[:space:]]*# raw git: / {
			printf "%s:%d:%s\n", f, FNR, $0
		}
		{ prev = $0 }' ./*.sh) || { echo 'test_git: awk failed'; return 1; }
	if [[ -n $hits ]]; then
		mapfile -t lines <<<"$hits"
		printf 'tools/dev/tests/%s\n' "${lines[@]}"
		echo "test_git: run git in a test through tgit or real_git (tools/dev/tests/lib.sh)"
	fi
	[[ -z $hits && -z $unchecked ]]
}

# The game is a foreground app (#24): fail on a C, C++ or Objective-C(++)
# source outside tools/, third_party/, the rtrace tool and the unittest/
# directories that names SDL's background-app hint, by its macro or its
# string.  Tests and tools set it in their own main(), never shared code.
background_app_hint()
{
	local hits rc
	hits=$(git -C "$root" grep -n -E --untracked -e 'SDL_HINT_MAC_BACKGROUND_APP|"SDL_MAC_BACKGROUND_APP"' \
		-- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' '*.m' '*.mm' \
		':(exclude)tools/' ':(exclude)third_party/' ':(exclude)common/render/rtrace/' \
		':(exclude,glob)**/unittest/**') && rc=0 || rc=$?
	case $rc in
		0)
			echo "$hits"
			echo "gpu_gate: only a test's or tool's main() sets SDL's background-app hint;" \
				"the game is a foreground app (#24)"
			return 1 ;;
		1) return 0 ;;
		*) echo "gpu_gate: git grep for the background-app hint failed"; return 1 ;;
	esac
}

# GPU validation is a gate (ADR 5): fail on an SDL_GPU device created
# anywhere outside third_party/ without debug mode (gpu_debug_mode.py), on
# game code setting SDL's background-app hint (background_app_hint), and
# on a command in this file or tools/dev/rtrace-tests.sh that runs a
# registered GPU program ($gpu_program or a gpu-programs path) other than
# as an argument of gpu-validate.sh (gpu_wrapper.py, which parses commands
# rather than lines).
step_gpu_gate()
{
	local rc=0
	python3 "$dev_dir/gpu_debug_mode.py" "$root" || rc=1
	background_app_hint || rc=1
	if ! (cd "$root" && python3 tools/dev/gpu_wrapper.py tools/dev/gpu-programs \
		tools/dev/check.sh tools/dev/rtrace-tests.sh); then
		echo "gpu_gate: run a GPU program through gpu-validate.sh"
		rc=1
	fi
	return "$rc"
}

# SDL3 is built beside SDL1.2 and SDL2 with `#elif SDL_MAJOR_VERSION == 3`
# branches (ADR 3): fail on a chain with an SDL2 branch and no SDL3 or >= 2
# branch that tools/dev/sdl-arms-allow does not allow (check_sdl_arms.py).
step_sdl_arms()
{
	python3 "$dev_dir/check_sdl_arms.py" "$root"
}

# GL stays behind the render device (ADR 2): the text pass (--fast) counts the
# GL calls in every C and C++ source outside the GL adapter and the platform
# code, and the clang pass (full) lists them with clang-query on every
# translation unit of build/dev's compilation database; both fail on a call
# tools/dev/gl-calls-pending does not name (check_gl_calls.py).
step_gl_text()
{
	python3 "$dev_dir/check_gl_calls.py" text "$root"
}

# CHECK_CLANG_QUERY names the binary (default: the pinned LLVM's, which
# deps.sh installs only if its prebuilt_members_llvm lists bin/clang-query);
# a missing one is a SKIP, printed by the script.
step_gl_calls()
{
	local cq=${CHECK_CLANG_QUERY:-$dev_deps_prefix/opt/llvm/bin/clang-query}
	python3 "$dev_dir/check_gl_calls.py" clang "$root" "$root/build/dev" "$cq"
}

# Run every GPU program tools/dev/gpu-programs registers under
# gpu-validate.sh (and no-window.sh), after building them: each is a
# build/dev-sdl3 program, built with build.sh's sdl3=1.  macOS only, on
# Metal: elsewhere it is a SKIP, until #25 runs them on lavapipe in the
# container; none registered is a SKIP, a missing gpu-programs a failure.
step_gpu_tests()
{
	local gpu_program line rc=0 programs=()
	[[ -f $dev_dir/gpu-programs ]] || { echo "gpu_tests: no tools/dev/gpu-programs"; return 1; }
	while IFS= read -r line || [[ -n $line ]]; do
		[[ -z $line || $line == \#* ]] || programs+=("$line")
	done < "$dev_dir/gpu-programs"
	if [[ ${#programs[@]} -eq 0 ]]; then
		echo "SKIP gpu_tests: no GPU program in tools/dev/gpu-programs"
		return 0
	fi
	if [[ $dev_os != darwin ]]; then
		echo "SKIP gpu_tests: macOS only; SDL_GPU on lavapipe in the container is #25"
		return 0
	fi
	for line in "${programs[@]}"; do
		[[ $line == build/dev-sdl3/* ]] ||
			{ echo "gpu_tests: $line is not a build/dev-sdl3 program"; return 1; }
	done
	"$dev_dir/build.sh" dev sdl3=1 "${programs[@]}" || return 1
	for gpu_program in "${programs[@]}"; do
		"$dev_dir/no-window.sh" "$dev_dir/gpu-validate.sh" "$root/$gpu_program" || rc=1
	done
	return "$rc"
}

# The commit size limit over the fork's history (commit-size.sh
# --history): every commit in upstream/master..HEAD against its first
# parent, which also catches a rebase -i squash or fixup, for which git
# runs no pre-commit hook.  Without upstream/master the check is a SKIP.
step_commit_sizes()
{
	need_upstream commit_sizes || return 0
	(cd "$root" && "$dev_dir/commit-size.sh" --history upstream/master)
}

# Check the toolchain against tools/dev/toolchain.pins.  A doctor.sh FAIL
# line that tools/dev/known-failures lists for this platform prints as
# KNOWN FAIL with the owner decision it waits for and does not fail the
# step; any other FAIL, or doctor.sh failing with no FAIL line, does.
step_doctor()
{
	local out rc line k plat rest unknown=0 known=()
	out=$("$dev_dir/doctor.sh" 2>&1) && rc=0 || rc=$?
	while read -r plat rest; do
		[[ -z $plat || $plat == \#* || $plat != "$dev_os" ]] || known+=("$rest")
	done < "$dev_dir/known-failures" || { echo "doctor: cannot read known-failures"; return 1; }
	while IFS= read -r line; do
		for k in "${known[@]+"${known[@]}"}"; do
			# shellcheck disable=SC2053  # the entry is a glob
			[[ $line == FAIL* && $line == ${k%% -- *} ]] && line="KNOWN $line (${k#* -- })"
		done
		[[ $line == FAIL* ]] && unknown=1
		printf '%s\n' "$line"
	done <<<"$out"
	[[ $rc -eq 0 ]] && return 0
	[[ $rc -eq 1 && $unknown -eq 0 ]] && grep -q '^FAIL' <<<"$out"
}

# Check every fork file against tools/dev/style/clang-format.
step_format()
{
	"$dev_dir/format.sh" --check
}

# The pre-commit gate (ADR 5): clang-format, NOLINT reasons and clang-tidy
# on the staged fork files; nothing to do with nothing staged.
step_staged()
{
	"$dev_dir/format.sh" --staged
}

# clang-tidy on every fork file, with the compilation database the dev
# build (unit_tests) writes; the format step has run clang-format.
step_tidy()
{
	sdl3_database && "$dev_dir/format.sh" --tidy
}

# R9: the committed shader headers are what the pinned toolchain builds
# from shaders/hlsl/ (tools/shaders/check.sh: DXIL in the container leg,
# where DXC runs; MSL through Xcode's Metal compiler on the Mac).  A tree
# without shaders (the tests' throwaway repositories) is a SKIP.
step_shaders()
{
	if [[ ! -d $root/shaders/hlsl ]]; then
		echo "SKIP shaders: no shaders/hlsl"
		return 0
	fi
	"$root/tools/shaders/check.sh"
}

# Build D2X with the dev profile and run the unit tests, under
# no-window.sh: a test that takes focus or crashes into a report fails.
# The compilation database, which `check` alone does not write, is for
# the tidy step.
step_unit_tests()
{
	"$dev_dir/no-window.sh" "$dev_dir/build.sh" dev check build/dev/compile_commands.json
}

# Generate the game's kconfig.udlr.h from clean with the dev profile: the
# step of a full game build that preprocesses kconfig.ui-table.cpp with the
# profile's flags, hardening-exit.h among them, and parses the result
# (generate-kconfig-udlr.py), which any stray declaration breaks.  SCons
# does not see a force-included header as a dependency, so both outputs are
# removed first; the full game build stays out of the check tier.
step_kconfig()
{
	local out=build/dev/d2x-rebirth
	rm -f "$root/$out/kconfig.ui-table.i" "$root/$out/kconfig.udlr.h"
	"$dev_dir/build.sh" dev "$out/kconfig.udlr.h"
}

# Run the rtrace tests (#19) built by profile $1, each through the command
# "${@:2}" (none: directly), with tools/dev/rtrace-tests.sh, which takes the
# same arguments; a missing or non-executable script fails.
rtrace_tests()
{
	if [[ ! -x $dev_dir/rtrace-tests.sh ]]; then
		echo "rtrace tests ($1): tools/dev/rtrace-tests.sh is missing or not executable"
		return 1
	fi
	"$dev_dir/rtrace-tests.sh" "$@"
}

# Run the unit tests and the rtrace tests under AddressSanitizer and
# UndefinedBehaviorSanitizer, with LeakSanitizer except on macOS.
step_sanitizer_tests()
{
	"$dev_dir/no-window.sh" "$dev_dir/build.sh" asan check &&
		rtrace_tests asan "$dev_dir/no-window.sh"
}

# macOS, where LeakSanitizer is unsupported: run the unit tests the
# unit_tests step built, and the rtrace tests, under leaks.sh.  Elsewhere a
# SKIP: sanitizer_tests runs LeakSanitizer.
step_leaks()
{
	local t rc=0 tests=()
	if [[ $dev_os != darwin ]]; then
		echo "SKIP leaks: macOS only; LeakSanitizer covers $dev_os in sanitizer_tests"
		return 0
	fi
	while IFS= read -r t; do
		tests+=("$t")
	done < <(find "$root/build/dev" -maxdepth 2 -type f -perm -u+x -name 'test-*' 2>/dev/null |
		sort)
	if [[ ${#tests[@]} -eq 0 ]]; then
		echo "SKIP leaks: no unit test programs in build/dev; the unit_tests step builds them"
		return 0
	fi
	for t in "${tests[@]}"; do
		"$dev_dir/no-window.sh" "$dev_dir/leaks.sh" "$t" || rc=1
	done
	[[ $rc -eq 0 ]] && rtrace_tests dev "$dev_dir/no-window.sh" "$dev_dir/leaks.sh"
}

# macOS: build the SDL3 unit tests against the pinned SDL3 (build.sh
# sdl3=1) and fail when a test program links an SDL3 library from outside
# the pinned prefix (otool -L): Homebrew's SDL3 beside the pinned
# SDL3_image's would load two SDL3s in one process.  The programs are not
# run: the pinned SDL3_mixer names libfluidsynth by the absolute path of
# the clone that built the prefix, so in any other clone it does not load.
# Elsewhere a SKIP: the container has no second SDL3, and its leg has no
# budget left for an sdl3=1 configure.
step_sdl3_link()
{
	local t lib rc=0 tests=()
	if [[ $dev_os != darwin ]]; then
		echo "SKIP sdl3_link: macOS only; the duplicate SDL3 comes from Homebrew"
		return 0
	fi
	"$dev_dir/build.sh" dev sdl3=1 build/dev-sdl3/common/test-physfsrwops || return 1
	while IFS= read -r t; do
		tests+=("$t")
	done < <(cd "$root" && find build/dev-sdl3 -maxdepth 2 -type f -perm -u+x -name 'test-*' 2>/dev/null |
		sort)
	if [[ ${#tests[@]} -eq 0 ]]; then
		echo "SKIP sdl3_link: no unit test programs in build/dev-sdl3"
		return 0
	fi
	for t in "${tests[@]}"; do
		while IFS= read -r lib; do
			echo "sdl3_link: $t links an SDL3 library outside $dev_deps_prefix: $lib"
			rc=1
		done < <(otool -L "$root/$t" | sed -n '2,$s/^[[:space:]]*\([^ ]*libSDL3[^ ]*\) .*/\1/p' |
			grep -v -e '^@rpath/' -e "^$dev_deps_prefix/")
	done
	return "$rc"
}

pre_commit_steps=(repo_config guard staged secrets gitleaks_allow)
fast_steps=(repo_config doctor guard format staged secrets gitleaks_allow shellcheck stale_hashes
	workflows commit_sizes test_git gpu_gate sdl_arms gl_text shell_tests)
full_steps=(secrets_history shaders unit_tests kconfig leaks gpu_tests tidy sanitizer_tests gl_calls
	sdl3_link)
# The merge gate's steps (ADR 6), with r1 and the gate_beside steps run
# beside them (the budget is wall-clock: see gate_beside's use below).
gate_steps=(repo_config doctor guard gitleaks_allow new_secrets gpu_gate sdl_arms unit_tests
	sanitizer_tests leaks changed_format)
gate_beside=(sdl3_games)
tools_steps=(shellcheck stale_hashes test_git shell_tests)

mode=full
case "${1-}" in
	--pre-commit) mode=pre-commit ;;
	--pre-push) mode=pre-push ;;
	--fast) mode=fast ;;
	--gate)
		mode=gate
		[[ $# -eq 2 && -n $2 ]] || usage
		gate_rev=$(git -C "$root" rev-parse --verify -q "$2^{commit}") ||
			{ echo "check.sh: $2 is no commit" >&2; exit 2; } ;;
	--tools) mode=tools; only_tests=("${@:2}") ;;
	--gpu) mode=gpu ;;
	--doctor) mode=doctor ;;
	'') ;;
	*) usage ;;
esac
[[ $mode == gate || $mode == tools || $# -le 1 ]] || usage

steps=("${fast_steps[@]}")
[[ $mode == pre-commit ]] && steps=("${pre_commit_steps[@]}")
[[ $mode == pre-push ]] && steps=(push_secrets)
[[ $mode == gate ]] && steps=("${gate_steps[@]}")
[[ $mode == tools ]] && steps=("${tools_steps[@]}")
[[ $mode == gpu ]] && steps=(gpu_tests)
[[ $mode == doctor ]] && steps=(doctor)
if [[ $mode == full ]]; then
	steps+=("${full_steps[@]}")
	# The full checks also run the shell tests that build real objects.
	export DXX_BUILD_TESTS=1
fi

# The check tier's budget (ADR 5): the full checks fail when they take
# longer, in either leg.  DXX_CHECK_BUDGET (seconds) overrides it, for the
# tests.
budget=${DXX_CHECK_BUDGET:-$((20 * 60))}

failed=()
if [[ $mode == gate ]]; then
	# The merge gate's budget (ADR 6): 5 minutes, from the measured
	# basis of 2026-10-22 (R1 check 123-161 s beside the other steps,
	# about 3 minutes).  DXX_GATE_BUDGET (seconds) overrides it, for the
	# tests.  A first capture of the base's R1 baseline is outside it.
	budget=${DXX_GATE_BUDGET:-300}
	if [[ -z ${DXX_D2_DATA-} || ! -f ${DXX_D2_DATA-}/DESCENT2.HOG ]]; then
		echo "check.sh: the merge gate needs DXX_D2_DATA, the directory holding DESCENT2.HOG," \
			"for R1 (docs/agents/game-data.md); it is '${DXX_D2_DATA-}'" >&2
		exit 1
	fi
	echo "== r1_baseline"
	step_start=$EPOCHSECONDS
	r1_baseline || failed+=(r1_baseline)
	echo "-- r1_baseline: $((EPOCHSECONDS - step_start)) s"
fi
start=$EPOCHSECONDS
r1_pid=
if [[ $mode == gate && ${#failed[@]} -eq 0 ]]; then
	# R1 runs beside the other steps; its output prints when it is done.
	r1_out=$(mktemp "${TMPDIR:-/tmp}/check-r1.XXXXXX") || exit 1
	{ step_start=$EPOCHSECONDS; step_r1; rc=$?
		echo "-- r1: $((EPOCHSECONDS - step_start)) s"; exit "$rc"; } > "$r1_out" 2>&1 &
	r1_pid=$!
fi
# The gate_beside steps build into a directory of their own (build/dev-sdl3,
# its own .sconsign), so they run beside the others, like R1: the others
# leave the machine's cores idle part of the time.
beside_pids=() beside_outs=()
if [[ $mode == gate ]]; then
	for s in "${gate_beside[@]}"; do
		out=$(mktemp "${TMPDIR:-/tmp}/check-$s.XXXXXX") || exit 1
		{ step_start=$EPOCHSECONDS; "step_$s"; rc=$?
			echo "-- $s: $((EPOCHSECONDS - step_start)) s"; exit "$rc"; } > "$out" 2>&1 &
		beside_pids+=($!) beside_outs+=("$out")
	done
fi
for s in "${steps[@]}"; do
	echo "== $s"
	step_start=$EPOCHSECONDS
	"step_$s" || failed+=("$s")
	[[ $mode == full || $mode == gate ]] && echo "-- $s: $((EPOCHSECONDS - step_start)) s"
done
for i in "${!beside_pids[@]}"; do
	echo "== ${gate_beside[i]}"
	wait "${beside_pids[i]}" || failed+=("${gate_beside[i]}")
	cat "${beside_outs[i]}"
	rm -f "${beside_outs[i]}"
done
if [[ -n $r1_pid ]]; then
	echo "== r1"
	wait "$r1_pid" || failed+=(r1)
	cat "$r1_out"
	rm -f "$r1_out"
elif [[ $mode == gate ]]; then
	failed+=(r1)
fi
if [[ $mode == full || $mode == gate ]]; then
	took=$((EPOCHSECONDS - start))
	if [[ $mode == full ]]; then
		echo "check.sh: full checks took $took s, budget $budget s"
	else
		echo "check.sh: merge gate took $took s, budget $budget s"
	fi
	[[ $took -le $budget ]] || failed+=(budget)
fi

if [[ ${#failed[@]} -ne 0 ]]; then
	echo "check.sh: failed: ${failed[*]}" >&2
	exit 1
fi
echo "check.sh: $mode checks passed"

#!/usr/bin/env bash
# Single entry point for the fork's checks.
#
#   check.sh --pre-commit   the pre-commit tier (ADR 5, under 10 s), run
#                           by the pre-commit hook after the size limit
#   check.sh --fast         quick subset: the pre-commit steps and more,
#                           the shell tests among them
#   check.sh                everything: the fast subset plus the full steps
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
	echo "usage: $0 [--pre-commit|--fast]" >&2
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

# Fail on commit hashes in docs/, CLAUDE.md and GLOSSARY.md that are not in
# upstream/master: fork-only hashes go stale on every rebase or re-sign, so
# fork commits are cited by subject.  Only 7-40 digit hex tokens that name
# a commit here count; other hex (checksums, other repositories' commits)
# passes.  Without upstream/master the check is a SKIP.
step_stale_hashes()
{
	local f tok rc=0 files=()
	if ! git -C "$root" rev-parse -q --verify upstream/master >/dev/null; then
		echo "SKIP stale_hashes: no upstream/master; fetch upstream to run this check"
		return 0
	fi
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

# The one GitHub workflow the fork adds (ADR 4): the secret scan.
secret_scan_workflow=.github/workflows/secret-scan.yml

# Fail unless the secret-scan workflow runs gitleaks alone: no run step,
# only actions/checkout and gitleaks-action pinned by commit, no write
# permission, and GITLEAKS_VERSION the version toolchain.pins pins.
check_secret_scan_workflow()
{
	local f=$root/$secret_scan_workflow w=$secret_scan_workflow pin got hits rc=0
	hits=$(grep -nE '^[[:space:]]*(-[[:space:]]+)?(run|shell)[[:space:]]*:' "$f")
	[[ -z $hits ]] || { printf '%s:%s: runs a command\n' "$w" "$hits"; rc=1; }
	hits=$(grep -nE '(^|[[:space:]])uses[[:space:]]*:' "$f" | grep -vE \
		'uses: (actions/checkout|gitleaks/gitleaks-action)@[0-9a-f]{40}([[:space:]]|$)')
	[[ -z $hits ]] || { printf '%s:%s: not checkout or gitleaks-action pinned by commit\n' "$w" "$hits"; rc=1; }
	hits=$(grep -nE ':[[:space:]]*write([[:space:]]|$)|write-all' "$f")
	[[ -z $hits ]] || { printf '%s:%s: write permission\n' "$w" "$hits"; rc=1; }
	pin=$(awk '$1 == "prebuilt" && $2 == "gitleaks" {print $4; exit}' "$dev_dir/toolchain.pins" |
		sed -nE 's|.*/download/v([0-9.]+)/.*|\1|p')
	got=$(sed -nE 's/^[[:space:]]*GITLEAKS_VERSION:[[:space:]]*"?([^"[:space:]]*)"?.*/\1/p' "$f")
	if [[ -z $pin || $got != "$pin" ]]; then
		echo "$w: GITLEAKS_VERSION is '$got', toolchain.pins pins gitleaks '$pin'"
		rc=1
	fi
	if [[ $rc -ne 0 ]]; then
		echo "$w: the fork's one workflow runs gitleaks only (ADR 4); the rest is tools/dev/ci.sh"
	fi
	return "$rc"
}

# Fail on any file under .github/workflows/ that upstream/master does not
# have, but the secret scan while it runs gitleaks alone: the fork's CI is
# local (ADR 4), and GitHub must not run a workflow of its own.  Without
# upstream/master the check is a SKIP.
step_workflows()
{
	local f rc=0
	if ! git -C "$root" rev-parse -q --verify upstream/master >/dev/null; then
		echo "SKIP workflows: no upstream/master; fetch upstream to run this check"
		return 0
	fi
	while IFS= read -r f; do
		if [[ $f == "$secret_scan_workflow" ]]; then
			check_secret_scan_workflow || rc=1
			continue
		fi
		echo "$f: a GitHub workflow upstream does not have; the fork's CI is tools/dev/ci.sh"
		rc=1
	done < <(comm -23 \
		<(git -C "$root" ls-files -co --exclude-standard -- .github/workflows | sort -u) \
		<(git -C "$root" ls-tree -r --name-only upstream/master -- .github/workflows | sort -u))
	return "$rc"
}

# Run gitleaks (the pinned one in the prefix; CHECK_GITLEAKS overrides it)
# with the repository's .gitleaks.toml (without one, gitleaks' default
# rules) and the remaining arguments, redacted, from the repository root.  A missing gitleaks is a SKIP naming
# it, except in the reference image (DXX_REFERENCE_IMAGE=1), which has
# every pinned tool: there it fails.  $1 names the step.
run_gitleaks()
{
	local step=$1 gitleaks=${CHECK_GITLEAKS:-$dev_deps_prefix/opt/gitleaks/bin/gitleaks} config=()
	shift
	[[ -f $root/.gitleaks.toml ]] && config=(--config .gitleaks.toml)
	if ! command -v "$gitleaks" >/dev/null; then
		if [[ ${DXX_REFERENCE_IMAGE-} == 1 ]]; then
			echo "FAIL $step: gitleaks not found ($gitleaks); the reference image must have it"
			return 1
		fi
		echo "SKIP $step: gitleaks not found ($gitleaks); run tools/dev/deps.sh build"
		return 0
	fi
	(cd "$root" && env -u GITLEAKS_CONFIG -u GITLEAKS_CONFIG_TOML "$gitleaks" git "$@" \
		"${config[@]+"${config[@]}"}" --redact --verbose --no-banner --no-color --log-level warn .)
}

# Fail on a secret in the staged changes (the pre-commit tier).
step_secrets()
{
	run_gitleaks secrets --staged
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
# change to this clone (real_git): the word git, also quoted, escaped, as
# a variable ($git) or assigned (g=git), outside a comment line.  A line
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
		BEGIN { re = "(^|[^[:alnum:]_./-])[\\\"" q "$={(]*git([\"" q "})]|[[:space:];|&]|$)" }
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

# GPU validation is a gate (ADR 5): fail on an SDL_GPU device created
# anywhere outside third_party/ without debug mode (gpu_debug_mode.py), and on a line
# of this file that runs a registered GPU program ($gpu_program) other than
# through gpu-validate.sh.
step_gpu_gate()
{
	local hits rc=0
	python3 "$dev_dir/gpu_debug_mode.py" "$root" || rc=1
	hits=$(grep -nE '\$\{?gpu_program([^[:alnum:]_]|$)' "$dev_dir/check.sh" |
		grep -vE '^[0-9]+:[[:space:]]*#' | grep -v 'gpu-validate\.sh')
	if [[ -n $hits ]]; then
		printf 'tools/dev/check.sh:%s\n' "$hits"
		echo "gpu_gate: run a GPU program in check.sh through gpu-validate.sh"
		rc=1
	fi
	return "$rc"
}

# Run every GPU program tools/dev/gpu-programs registers under
# gpu-validate.sh (and no-window.sh); none yet is a SKIP naming #24.
step_gpu_tests()
{
	local gpu_program line rc=0 programs=()
	while IFS= read -r line; do
		[[ -z $line || $line == \#* ]] || programs+=("$line")
	done < "$dev_dir/gpu-programs"
	if [[ ${#programs[@]} -eq 0 ]]; then
		echo "SKIP gpu_tests: no GPU program in tools/dev/gpu-programs; #24 registers the first"
		return 0
	fi
	for gpu_program in "${programs[@]}"; do
		"$dev_dir/no-window.sh" "$dev_dir/gpu-validate.sh" "$root/$gpu_program" || rc=1
	done
	return "$rc"
}

# Check the toolchain against tools/dev/toolchain.pins.
step_doctor()
{
	"$dev_dir/doctor.sh"
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

# clang-format and clang-tidy on every fork file, with the compilation
# database the dev build (unit_tests) writes.
step_tidy()
{
	"$dev_dir/format.sh" --check && "$dev_dir/format.sh" --tidy
}

# Build D2X with the dev profile and run the unit tests, under
# no-window.sh: a test that takes focus or crashes into a report fails.
# The compilation database, which `check` alone does not write, is for
# the tidy step.
step_unit_tests()
{
	"$dev_dir/no-window.sh" "$dev_dir/build.sh" dev check build/dev/compile_commands.json
}

# Run the rtrace tests (#19) built by profile $1, each through the command
# "${@:2}" (none: directly).  #19 adds tools/dev/rtrace-tests.sh, which takes
# the same arguments; until then this is a SKIP.
rtrace_tests()
{
	if [[ ! -x $dev_dir/rtrace-tests.sh ]]; then
		echo "SKIP rtrace tests ($1): none yet; #19 adds tools/dev/rtrace-tests.sh"
		return 0
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

pre_commit_steps=(repo_config guard staged secrets)
fast_steps=(repo_config doctor guard format staged secrets shellcheck stale_hashes workflows test_git
	gpu_gate shell_tests)
full_steps=(secrets_history unit_tests leaks gpu_tests tidy sanitizer_tests)

mode=full
case "${1-}" in
	--pre-commit) mode=pre-commit ;;
	--fast) mode=fast ;;
	'') ;;
	*) usage ;;
esac

steps=("${fast_steps[@]}")
[[ $mode == pre-commit ]] && steps=("${pre_commit_steps[@]}")
if [[ $mode == full ]]; then
	steps+=("${full_steps[@]}")
	# The full checks also run the shell tests that build real objects.
	export DXX_BUILD_TESTS=1
fi

# The check tier's budget (ADR 5): the full checks fail when they take
# longer, in either leg.  DXX_CHECK_BUDGET (seconds) overrides it, for the
# tests.
budget=${DXX_CHECK_BUDGET:-$((10 * 60))}

failed=()
start=$EPOCHSECONDS
for s in "${steps[@]}"; do
	echo "== $s"
	step_start=$EPOCHSECONDS
	"step_$s" || failed+=("$s")
	[[ $mode == full ]] && echo "-- $s: $((EPOCHSECONDS - step_start)) s"
done
if [[ $mode == full ]]; then
	took=$((EPOCHSECONDS - start))
	echo "check.sh: full checks took $took s, budget $budget s"
	[[ $took -le $budget ]] || failed+=(budget)
fi

if [[ ${#failed[@]} -ne 0 ]]; then
	echo "check.sh: failed: ${failed[*]}" >&2
	exit 1
fi
echo "check.sh: $mode checks passed"

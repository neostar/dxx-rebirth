#!/usr/bin/env bash
# Single entry point for the fork's checks.
#
#   check.sh --fast   quick subset, run by the pre-commit hook
#   check.sh          everything: the fast subset plus the full steps
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
	echo "usage: $0 [--fast]" >&2
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
		[[ $f == *.sh ]] || head -n 1 "$f" | grep -qE '^#!.*[/ ](ba)?sh$' && files+=("$f")
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

# Fail on any file under .github/workflows/ that upstream/master does not
# have: the fork's CI is local (ADR 4), and GitHub must not run a workflow
# of its own.  Without upstream/master the check is a SKIP.
step_workflows()
{
	local f rc=0
	if ! git -C "$root" rev-parse -q --verify upstream/master >/dev/null; then
		echo "SKIP workflows: no upstream/master; fetch upstream to run this check"
		return 0
	fi
	while IFS= read -r f; do
		echo "$f: a GitHub workflow upstream does not have; the fork's CI is tools/dev/ci.sh"
		rc=1
	done < <(comm -23 \
		<(git -C "$root" ls-files -co --exclude-standard -- .github/workflows | sort -u) \
		<(git -C "$root" ls-tree -r --name-only upstream/master -- .github/workflows | sort -u))
	return "$rc"
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

# Build D2X with the dev profile and run the unit tests.
step_unit_tests()
{
	"$dev_dir/build.sh" dev check
}

# Run the unit tests under AddressSanitizer and UndefinedBehaviorSanitizer.
step_sanitizer_tests()
{
	"$dev_dir/build.sh" asan check
}

fast_steps=(repo_config doctor guard format staged shellcheck stale_hashes workflows shell_tests)
full_steps=(unit_tests tidy sanitizer_tests)

mode=full
case "${1-}" in
	--fast) mode=fast ;;
	'') ;;
	*) usage ;;
esac

steps=("${fast_steps[@]}")
if [[ $mode == full ]]; then
	steps+=("${full_steps[@]}")
	# The full checks also run the shell tests that build real objects.
	export DXX_BUILD_TESTS=1
fi

failed=()
for s in "${steps[@]}"; do
	echo "== $s"
	"step_$s" || failed+=("$s")
done

if [[ ${#failed[@]} -ne 0 ]]; then
	echo "check.sh: failed: ${failed[*]}" >&2
	exit 1
fi
echo "check.sh: $mode checks passed"

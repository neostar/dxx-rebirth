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

# Run every tools/dev/tests/*_test.sh.
step_shell_tests()
{
	local t rc=0
	for t in "$dev_dir"/tests/*_test.sh; do
		[[ -e $t ]] || continue
		echo "-- ${t#"$dev_dir"/}"
		# Inside a git hook, git exports GIT_DIR and friends for the
		# repository being committed to; tests must never see them.
		(unset "${dev_git_repo_vars[@]}"; bash "$t") ||
			{ echo "FAILED: ${t#"$dev_dir"/}"; rc=1; }
	done
	return "$rc"
}

# Check fork code against tools/dev/style/clang-format.
step_format()
{
	"$dev_dir/format.sh" --check
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

fast_steps=(repo_config guard format shell_tests)
full_steps=(unit_tests sanitizer_tests)

mode=full
case "${1-}" in
	--fast) mode=fast ;;
	'') ;;
	*) usage ;;
esac

steps=("${fast_steps[@]}")
[[ $mode == full ]] && steps+=("${full_steps[@]}")

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

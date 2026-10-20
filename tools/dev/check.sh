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
# Sanitizer failures exit non-zero rather than abort(), which on macOS
# opens a CrashReporter dialog.
export ASAN_OPTIONS=${ASAN_OPTIONS:-abort_on_error=0:halt_on_error=1:detect_leaks=0}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1:abort_on_error=0}

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

# Run every tools/dev/tests/*_test.sh.
step_shell_tests()
{
	local t rc=0
	for t in "$dev_dir"/tests/*_test.sh; do
		[[ -e $t ]] || continue
		echo "-- ${t#"$dev_dir"/}"
		# Inside a git hook, git exports GIT_DIR and friends for the
		# repository being committed to; tests must never see them.
		env -u GIT_DIR -u GIT_INDEX_FILE -u GIT_WORK_TREE -u GIT_COMMON_DIR \
			-u GIT_OBJECT_DIRECTORY -u GIT_PREFIX bash "$t" ||
			{ echo "FAILED: ${t#"$dev_dir"/}"; rc=1; }
	done
	return "$rc"
}

fast_steps=(guard shell_tests)
full_steps=()

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

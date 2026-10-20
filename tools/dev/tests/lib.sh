# Shared helpers for the shell tests in tools/dev/tests; sourced, not run.
# Each test file defines functions named test_*; run_tests runs them in
# order and exits non-zero if any failed.

DEV_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
REPO_ROOT=$(cd "$DEV_DIR/../.." && pwd)

_failures=0

fail()
{
	printf '    FAIL: %s\n' "$*" >&2
	_test_failed=1
	return 1
}

assert_eq()
{
	[[ "$1" == "$2" ]] || fail "expected '$2', got '$1'${3:+ ($3)}"
}

assert_contains()
{
	[[ "$1" == *"$2"* ]] || fail "expected output to contain '$2', got: $1"
}

assert_not_contains()
{
	[[ "$1" != *"$2"* ]] || fail "expected output not to contain '$2', got: $1"
}

# Make an empty scratch directory removed when the test file exits.
make_scratch()
{
	local d
	d=$(mktemp -d "${TMPDIR:-/tmp}/dxx-dev-test.XXXXXX")
	_scratch_dirs+=("$d")
	printf '%s\n' "$d"
}

_scratch_dirs=()
trap 'rm -rf "${_scratch_dirs[@]}"' EXIT

# Make a throwaway git repository with one empty commit and print its path.
make_repo()
{
	local d
	d=$(make_scratch)
	git -C "$d" init -q -b main
	git -C "$d" config user.email test@example.invalid
	git -C "$d" config user.name test
	git -C "$d" config commit.gpgsign false
	git -C "$d" commit -q --allow-empty -m 'root'
	printf '%s\n' "$d"
}

run_tests()
{
	local t
	for t in $(declare -F | awk '{print $3}' | grep '^test_'); do
		# Asserts record failure in _test_failed, so a later passing
		# assert cannot hide an earlier failing one.
		if ( _test_failed=0; "$t"; exit "$_test_failed" ); then
			printf '  ok   %s\n' "$t"
		else
			printf '  FAIL %s\n' "$t"
			_failures=$((_failures + 1))
		fi
	done
	[[ $_failures -eq 0 ]]
}

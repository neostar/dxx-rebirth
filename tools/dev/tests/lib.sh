# Shared helpers for the shell tests in tools/dev/tests; sourced, not run.
# Each test file defines functions named test_*; run_tests runs them in
# order and exits non-zero if any failed.

DEV_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
REPO_ROOT=$(cd "$DEV_DIR/../.." && pwd)
source "$DEV_DIR/env.sh"

# A test may run inside a git hook, where git exports GIT_DIR, GIT_INDEX_FILE
# and friends for the repository being committed to.  Clear them, so every
# git command here acts only on the throwaway repository named by -C.
unset "${dev_git_repo_vars[@]}"

# Scripts under test call plain git; keep the owner's global config (signing
# with 1Password, identity) out of throwaway repositories without writing
# any config file.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid

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

# Every scratch directory lives under one root made here, in the test
# file's own shell, and removed when it exits.  Tests call make_scratch in
# a command substitution, a subshell that cannot report back what it made.
_scratch_root=$(mktemp -d "${TMPDIR:-/tmp}/dxx-dev-test.XXXXXX")
trap 'rm -rf "$_scratch_root"' EXIT

# Make an empty scratch directory removed when the test file exits.
make_scratch()
{
	mktemp -d "$_scratch_root/XXXXXX"
}

# git with a test identity and no signing, set per command so that no
# repository's config is ever written.  Always pass -C <throwaway repo>.
tgit()
{
	git -c user.name=test -c user.email=test@example.invalid \
		-c commit.gpgsign=false "$@"
}

# Make a throwaway git repository with one empty commit and print its path.
make_repo()
{
	local d
	d=$(make_scratch)
	git -C "$d" init -q -b main
	tgit -C "$d" commit -q --allow-empty -m 'root'
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

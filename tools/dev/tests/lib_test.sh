#!/usr/bin/env bash
# Behaviour of the shared test helpers in tools/dev/tests/lib.sh.
source "$(dirname "$0")/lib.sh"

# Test that scratch directories and repositories made through command
# substitution, as every test does, are removed when the test file exits.
test_scratch_dirs_are_removed_on_exit()
{
	local paths p
	paths=$(bash -c 'source "$1"; d=$(make_scratch); r=$(make_repo); echo "$d"; echo "$r"' \
		_ "$DEV_DIR/tests/lib.sh")
	[[ -n $paths ]] || fail 'no paths printed'
	for p in $paths; do
		[[ ! -e $p ]] || fail "$p left behind"
	done
}

run_tests

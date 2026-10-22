#!/usr/bin/env bash
# Command-line checks of tools/dev/r1.sh that stop before the build: the
# game runs themselves need the licensed data and are not part of the
# tests (docs/agents/game-data.md).
source "$(dirname "$0")/lib.sh"
R1="$DEV_DIR/r1.sh"

# Test that a missing or unknown mode and stray arguments exit 2 with the
# usage.
test_bad_arguments_exit_2()
{
	local out rc a
	for a in '' 'frob' 'check --base x' 'capture --base' 'capture x y'; do
		# shellcheck disable=SC2086  # split the arguments on purpose
		out=$("$R1" $a 2>&1) && rc=0 || rc=$?
		assert_eq "$rc" 2 "$a: $out"
		assert_contains "$out" "usage:"
	done
}

# Test that both modes need DXX_D2_DATA to name the game data.
test_missing_data_exits_2()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$(env -u DXX_D2_DATA R1_DIR="$d/r1" "$R1" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "DXX_D2_DATA must name the directory of DESCENT2.HOG"
	out=$(DXX_D2_DATA="$d" R1_DIR="$d/r1" "$R1" capture 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
}

# Test that check without a baseline exits 2 and says how to make one,
# before it builds anything.
test_check_without_baseline_exits_2()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	d=$(cd "$d" && pwd -P)
	: > "$d/DESCENT2.HOG"
	out=$(DXX_D2_DATA="$d" R1_DIR="$d/r1" "$R1" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "no baseline in $d/r1; run r1.sh capture"
	assert_not_contains "$out" "building"
}

run_tests

#!/usr/bin/env bash
# Gate self-test (ADR 5): every gate goes red on a planted violation.
#
# Each file in gates/ is a planted violation (or, clean.cpp.in, the
# representative fork file) whose header names the gates it trips:
#
#   // gate NAME: EXPECT          gate NAME fails with output containing
#                                 EXPECT, or passes if EXPECT is `pass`
#   // gate NAME (#N): EXPECT     the same, for a gate ticket #N builds
#
# For each such line this file runs a test that copies the planted file
# into a throwaway repository and calls gate_NAME on it.  A gate without a
# gate_NAME runner is a SKIP naming its ticket.  A later ticket adds a gate
# by writing gate_NAME here; the planted files stay out of the fork gates
# because they end in .in.
source "$(dirname "$0")/lib.sh"
GATES="$DEV_DIR/tests/gates"

# The pre-commit gate: stage file $1 as src/<name>.cpp in a fork repository
# and run format.sh --staged.
gate_tidy()
{
	local r name
	name=$(basename "$1" .in)
	r=$(fork_repo)
	cp "$1" "$r/src/$name"
	tgit -C "$r" add "src/$name"
	"$r/tools/dev/format.sh" --staged 2>&1
}

# The strict warnings (fork_warnings.py) as the build adds them: compile
# file $1 with them and the pinned SDL3 headers.
gate_strict()
{
	# shellcheck disable=SC2046  # the flags are words
	"${CXX:-c++}" -std=c++23 $(python3 "$DEV_DIR/fork_warnings.py") \
		-isystem "$dev_deps_prefix/include" -fsyntax-only -x c++ "$1" 2>&1
}

# Run gate $2 on planted file $1; $3 is the owning ticket, $4 the expected
# output or `pass`.
run_gate()
{
	local file=$1 gate=$2 owner=$3 expect=$4 out rc
	if ! declare -F "gate_$gate" >/dev/null; then
		[[ -n $owner ]] || { fail "gate $gate has no runner and no owning ticket"; return; }
		skip "gate $gate is built by $owner"
	fi
	out=$("gate_$gate" "$file") && rc=0 || rc=$?
	if [[ $expect == pass ]]; then
		assert_eq "$rc" 0 "$out"
	else
		[[ $rc -ne 0 ]] || fail "gate $gate passed $(basename "$file"); expected: $expect"
		assert_contains "$out" "$expect"
	fi
}

# One test per gate line of each planted file: test_<file>_<gate> runs
# run_gate with the fields gate_line holds for it, separated by tabs.
declare -A gate_line
run_gate_line()
{
	local file gate owner expect
	IFS=$'\t' read -r file gate owner expect <<<"${gate_line[$1]}"
	run_gate "$file" "$gate" "${owner#-}" "$expect"
}
for f in "$GATES"/*.in; do
	base=${f##*/}
	while IFS= read -r line; do
		[[ $line =~ ^//\ gate\ ([a-z]+)(\ \((#[0-9]+)\))?:\ (.+)$ ]] || continue
		t=test_${base%.cpp.in}_${BASH_REMATCH[1]}
		gate_line[$t]=$f$'\t'${BASH_REMATCH[1]}$'\t'-${BASH_REMATCH[3]}$'\t'${BASH_REMATCH[4]}
		eval "$t() { run_gate_line $t; }"
	done < "$f"
done

# Test that every gate this ticket owns is planted at least once.
test_every_gate_is_planted()
{
	local want
	for want in pro-type-static-cast-downcast narrowing-conversions owning-memory \
		use-after-move 'leak of memory' 'after it is released' 'NOLINT without'; do
		grep -qF "$want" "$GATES"/*.in || fail "no planted file expects '$want'"
	done
}

run_tests

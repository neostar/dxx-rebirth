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

# Print the seconds between the first and the last timestamp of a clangd
# log on the standard input (I[HH:MM:SS.mmm] lines).
clangd_seconds()
{
	awk -F'[][]' '/^[IEV]\[/ {split($2, t, ":"); s = t[1] * 3600 + t[2] * 60 + t[3];
		if (first == "") first = s; last = s} END {printf "%.3f\n", last - first}'
}

# Test the editor tier: clangd --check on a fork file reports the fork
# clang-tidy checks from the fork directory's .clang-tidy, within 1 s; the
# same file in an upstream directory gets none of them.
test_editor_tier_clangd_uses_fork_config_within_budget()
{
	local r out secs
	r=$(fork_repo)
	cp "$GATES/owning_raw_pointer.cpp.in" "$r/src/owner.cpp"
	cp "$GATES/owning_raw_pointer.cpp.in" "$r/common/owner.cpp"
	cp "$REPO_ROOT/.clang-tidy" "$r/.clang-tidy"
	out=$(cd "$r" && "$(dev_llvm_tool clangd)" --check=src/owner.cpp 2>&1)
	assert_contains "$out" "[cppcoreguidelines-owning-memory]"
	secs=$(clangd_seconds <<<"$out")
	awk -v s="$secs" 'BEGIN {exit !(s < 1)}' || fail "clangd took $secs s, over the 1 s budget"
	out=$(cd "$r" && "$(dev_llvm_tool clangd)" --check=common/owner.cpp 2>&1)
	assert_contains "$out" "All checks completed"
	assert_not_contains "$out" "cppcoreguidelines-"
}

# Test the pre-commit tier: the gate on a typical diff (the representative
# file and two small ones) and the licensed-data guard take under 10 s.
test_precommit_tier_within_budget()
{
	local r start secs
	r=$(fork_repo)
	cp "$GATES/clean.cpp.in" "$r/src/clean.cpp"
	printf 'int one()\n{\n\treturn 1;\n}\n' > "$r/src/one.cpp"
	printf 'int two()\n{\n\treturn 2;\n}\n' > "$r/src/two.cpp"
	tgit -C "$r" add src
	start=$EPOCHREALTIME
	"$r/tools/dev/format.sh" --staged >/dev/null 2>&1 || fail "the gate failed on clean files"
	(cd "$r" && tools/dev/guard-licensed-data.sh --staged) || fail "the guard failed"
	secs=$(awk -v a="$start" -v b="$EPOCHREALTIME" 'BEGIN {printf "%.2f", b - a}')
	echo "    pre-commit tier: $secs s"
	awk -v s="$secs" 'BEGIN {exit !(s < 10)}' || fail "the pre-commit tier took $secs s, over 10 s"
}

run_tests

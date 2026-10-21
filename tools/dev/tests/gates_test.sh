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
# test_GATE runs gate_GATE on each planted file that names it, copied into
# a throwaway place under a neutral name, so that an expectation can match
# only the diagnostic, never the file's own name.  A gate without a runner
# is a SKIP naming its ticket.  A later ticket adds a gate by writing
# gate_NAME and test_NAME here; the planted files stay out of the fork
# gates because they end in .in.
source "$(dirname "$0")/lib.sh"
GATES="$DEV_DIR/tests/gates"

# Skip the current test, naming tool $1, unless $1 runs.
need()
{
	local hint='tools/dev/deps.sh installs the LLVM tools'
	[[ $1 == */* ]] || hint='install a C++ compiler'
	command -v "$1" >/dev/null || skip "$1 not found; $hint"
}

# The tool each gate runs, which a test needs before running the gate.
declare -A gate_tool=([tidy]=$(dev_llvm_tool clang-tidy) [strict]=c++)

# The pre-commit gate: stage file $1 as src/plant.cpp in a fork repository
# and run format.sh --staged.
gate_tidy()
{
	local r
	r=$(fork_repo)
	cp "$1" "$r/src/plant.cpp"
	tgit -C "$r" add src/plant.cpp
	"$r/tools/dev/format.sh" --staged 2>&1
}

# The strict warnings (fork_warnings.py) as the build adds them: compile
# file $1 with them and the pinned SDL3 headers.  GCC's [-Werror=NAME] is
# printed as clang's [-Werror,-WNAME], so one expectation fits both.
gate_strict()
{
	local d rc
	d=$(make_scratch)
	cp "$1" "$d/plant.cpp"
	# shellcheck disable=SC2046  # the flags are words
	"${gate_tool[strict]}" -std=c++23 $(python3 "$DEV_DIR/fork_warnings.py") \
		-isystem "$dev_deps_prefix/include" -fsyntax-only "$d/plant.cpp" > "$d/out" 2>&1
	rc=$?
	sed -E 's/\[-Werror=([a-z0-9-]+)\]/[-Werror,-W\1]/' "$d/out"
	return "$rc"
}

# Run gate $1 on every planted file that names it, and fail unless one does.
run_planted()
{
	local gate=$1 f line expect out rc n=0 lines
	need "${gate_tool[$gate]}"
	for f in "$GATES"/*.in; do
		mapfile -t lines < <(grep "^// gate $gate: " "$f")
		for line in "${lines[@]}"; do
			expect=${line#"// gate $gate: "}
			n=$((n + 1))
			out=$("gate_$gate" "$f") && rc=0 || rc=$?
			if [[ $expect == pass ]]; then
				[[ $rc -eq 0 ]] || fail "gate $gate failed ${f##*/}: $out"
			elif [[ $rc -eq 0 ]]; then
				fail "gate $gate passed ${f##*/}; expected: $expect"
			else
				[[ $out == *"$expect"* ]] ||
					fail "gate $gate on ${f##*/}: expected '$expect', got: $out"
			fi
		done
	done
	[[ $n -gt 0 ]] || fail "no planted file names gate $gate"
}

test_tidy()
{
	run_planted tidy
}

test_strict()
{
	run_planted strict
}

# Test that each gate a planted file names without a runner has an owning
# ticket, and skip, naming those gates and tickets.
test_later_gates()
{
	local f line later=() orphans=()
	for f in "$GATES"/*.in; do
		while IFS= read -r line; do
			[[ $line =~ ^//\ gate\ ([a-z]+)(\ \((#[0-9]+)\))?: ]] || continue
			declare -F "gate_${BASH_REMATCH[1]}" >/dev/null && continue
			if [[ -z ${BASH_REMATCH[3]} ]]; then
				orphans+=("${BASH_REMATCH[1]} in ${f##*/}")
			else
				later+=("${BASH_REMATCH[1]} (${BASH_REMATCH[3]})")
			fi
		done < "$f"
	done
	[[ ${#orphans[@]} -eq 0 ]] || { fail "no runner and no ticket: ${orphans[*]}"; return; }
	[[ ${#later[@]} -eq 0 ]] || skip "built later: ${later[*]}"
}

# Test that every check the fork clang-tidy config enables is proven by a
# planted file: some tidy expectation ends with a [check,...] list naming a
# check the pattern matches.
test_every_enabled_check_is_planted()
{
	local line pat name found n=0 names=() listed
	while IFS= read -r line; do
		[[ $line == "// gate tidy: "*"["* ]] || continue
		IFS=, read -ra listed <<<"${line##*[}"
		names+=("${listed[@]}")
	done < <(cat "$GATES"/*.in)
	while IFS= read -r pat; do
		n=$((n + 1))
		found=0
		for name in "${names[@]}"; do
			# shellcheck disable=SC2053  # $pat is a glob, as in the config
			[[ $name == $pat ]] && found=1
		done
		[[ $found == 1 ]] || fail "no planted file proves $pat"
	done < <(sed -nE 's/^  ([a-z][a-z0-9.*-]*),?$/\1/p' "$DEV_DIR/style/clang-tidy")
	[[ $n -gt 0 ]] || fail "no enabled check found in tools/dev/style/clang-tidy"
}

# Run "$@" up to three times, printing each run's wall clock as $2, until
# one takes under $1 s; fail if none does.  The shell tests run in
# parallel, so one slow run can be the load of the others; three are not.
within_budget()
{
	local budget=$1 label=$2 start secs i
	shift 2
	for i in 1 2 3; do
		start=$EPOCHREALTIME
		"$@" || { fail "$label: $* failed"; return; }
		secs=$(awk -v a="$start" -v b="$EPOCHREALTIME" 'BEGIN {printf "%.2f", b - a}')
		echo "    $label, run $i: $secs s"
		awk -v s="$secs" -v b="$budget" 'BEGIN {exit !(s < b)}' && return 0
	done
	fail "$label took over its $budget s budget in three runs"
}

# Test the editor tier: clangd --check on a fork file reports the fork
# clang-tidy checks from the fork directory's .clang-tidy, the clangd
# process taking under 1 s of wall clock; the same file in an upstream
# directory gets none of them.
test_editor_tier_clangd_uses_fork_config_within_budget()
{
	local r clangd
	clangd=$(dev_llvm_tool clangd)
	need "$clangd"
	r=$(fork_repo)
	cp "$GATES/owning_raw_pointer.cpp.in" "$r/src/owner.cpp"
	cp "$GATES/owning_raw_pointer.cpp.in" "$r/common/owner.cpp"
	cp "$REPO_ROOT/.clang-tidy" "$r/.clang-tidy"
	# clangd --check exits non-zero on a file with diagnostics.
	clangd_check()
	{
		(cd "$r" && "$clangd" --check="$1" > "$r/out" 2>&1) || true
	}
	within_budget 1 "editor tier (clangd)" clangd_check src/owner.cpp
	assert_contains "$(cat "$r/out")" "[cppcoreguidelines-owning-memory]"
	clangd_check common/owner.cpp
	assert_contains "$(cat "$r/out")" "All checks completed"
	assert_not_contains "$(cat "$r/out")" "cppcoreguidelines-"
}

# Test the pre-commit tier: the real pre-commit hook, installed by setup.sh
# in a fork repository, commits a typical fork diff (the representative
# file and two small ones) in under 10 s.
test_precommit_tier_within_budget()
{
	local r
	need "${gate_tool[tidy]}"
	r=$(fork_repo)
	(cd "$r" && tools/dev/setup.sh >/dev/null)
	cp "$GATES/clean.cpp.in" "$r/src/clean.cpp"
	printf 'int one()\n{\n\treturn 1;\n}\n' > "$r/src/one.cpp"
	printf 'int two()\n{\n\treturn 2;\n}\n' > "$r/src/two.cpp"
	tgit -C "$r" add src
	# Commit the diff, then undo the commit, keeping the diff staged.
	# shellcheck disable=SC2329  # run by within_budget
	commit_diff()
	{
		tgit -C "$r" commit -q -m 'fork diff' > "$r/out" 2>&1 &&
			tgit -C "$r" reset -q --soft HEAD~1
	}
	within_budget 10 "pre-commit hook" commit_diff
	assert_contains "$(cat "$r/out")" "== staged"
}

run_tests

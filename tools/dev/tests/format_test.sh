#!/usr/bin/env bash
# Command-line behaviour of tools/dev/format.sh and the scope of the
# clang-format configuration: fork directories only, never upstream files.
source "$(dirname "$0")/lib.sh"
FORMAT="$DEV_DIR/format.sh"

# Test that a file in the fork style passes --check.
test_fork_style_file_passes()
{
	local d out
	d=$(make_scratch)
	printf '%s\n' \
		'namespace dcx {' \
		'' \
		'[[nodiscard]]' \
		'static int twice(const int &x)' \
		'{' \
		'	if (x > 0)' \
		'		return x * 2;' \
		'	return 0;' \
		'}' \
		'' \
		'}' > "$d/good.cpp"
	out=$("$FORMAT" --check "$d" 2>&1)
	assert_eq "$?" 0 "$out"
}

# Test that a misformatted file fails --check and is named.
test_misformatted_file_fails()
{
	local d out rc
	d=$(make_scratch)
	printf 'static int twice(const int& x) {\n    return x*2;\n}\n' > "$d/bad.cpp"
	out=$("$FORMAT" --check "$d" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "bad.cpp"
}

# Test that clang-format leaves upstream files alone under the root config.
test_root_config_leaves_upstream_unchanged()
{
	local f
	for f in common/unittest/enumerate.cpp similar/main/ai.cpp common/include/3d.h; do
		(cd "$REPO_ROOT" && clang-format "$f" | cmp -s - "$f") || fail "$f would be reformatted"
	done
}

# Test that clang-tidy runs no checks on upstream files under the root config.
test_root_tidy_config_enables_no_checks()
{
	local out
	local tidy
	tidy=$(command -v clang-tidy || echo "$(brew --prefix llvm)/bin/clang-tidy")
	out=$(cd "$REPO_ROOT" && "$tidy" --list-checks common/unittest/enumerate.cpp -- 2>&1)
	assert_contains "$out" "No checks enabled"
	assert_not_contains "$out" "bugprone-"
	assert_not_contains "$out" "modernize-"
	assert_not_contains "$out" "clang-analyzer-"
}

run_tests

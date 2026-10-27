#!/usr/bin/env bash
# Command-line behaviour of tools/dev/check_gl_calls.py: a GL call outside
# the GL adapter and the platform code fails unless the pending list names
# it (ADR 2).
source "$(dirname "$0")/lib.sh"
CHECK="$DEV_DIR/check_gl_calls.py"

# Make a throwaway source tree with an allow list naming the adapter, an
# empty pending list and one source with no GL call.  Prints the tree's path.
make_text_tree()
{
	local t
	t=$(make_scratch) || die "make_scratch failed"
	printf 'int main() { return 0; }\n' > "$t/main.cpp"
	printf '^common/render/gl_device\\.cpp$\n' > "$t/allow"
	: > "$t/pending"
	printf '%s\n' "$t"
}

run_text()
{
	python3 "$CHECK" text "$1" "$1/allow" "$1/pending" 2>&1
}

# Test that the pending list lets a file keep exactly its count, the entries
# of different reasons adding up: more calls fail, and so do fewer, so the
# list cannot go stale.
test_pending_list_is_exact()
{
	local t out rc
	t=$(make_text_tree) || die "make_text_tree failed"
	mkdir -p "$t/similar/arch"
	printf 'void f() {\n\tglFlush();\n\tglFinish();\n}\n' > "$t/similar/arch/ogl.cpp"
	printf 'similar/arch/ogl.cpp 1 -- #15: blits\nsimilar/arch/ogl.cpp 1 -- textures\n' > "$t/pending"
	out=$(run_text "$t") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	printf 'similar/arch/ogl.cpp 1 -- #15: blits\n' > "$t/pending"
	out=$(run_text "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "1 GL call(s) outside the GL adapter in similar/arch/ogl.cpp (pending 1)"
	printf 'similar/arch/ogl.cpp 3 -- #15: blits\n' > "$t/pending"
	out=$(run_text "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "allows 3 but has 2: lower it"
}

# Test that a pending entry without a reason, or a second entry with the
# same reason for a file, fails.
test_pending_entries_need_a_reason_and_are_unique()
{
	local t out rc
	t=$(make_text_tree) || die "make_text_tree failed"
	printf 'similar/arch/ogl.cpp 0\n' > "$t/pending"
	out=$(run_text "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" 'not "<path> <count> -- <reason>"'
	printf 'similar/arch/ogl.cpp 0 -- a\nsimilar/arch/ogl.cpp 0 -- a\n' > "$t/pending"
	out=$(run_text "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" 'second entry for similar/arch/ogl.cpp with the reason "a"'
}

# Test that a missing allow or pending list fails.
test_missing_lists_fail()
{
	local t out rc
	t=$(make_text_tree) || die "make_text_tree failed"
	rm "$t/allow"
	out=$(run_text "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "allow is missing"
	t=$(make_text_tree) || die "make_text_tree failed"
	rm "$t/pending"
	out=$(run_text "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "pending is missing"
}

# Test that the text pass counts a GL call in any source of the tree, also in
# a preprocessor branch no build takes, against the same lists, and ignores
# comments, strings and #define lines.
test_text_pass_counts_calls_and_ignores_text_that_is_not_code()
{
	local t out rc
	t=$(make_text_tree) || die "make_text_tree failed"
	mkdir -p "$t/similar" "$t/common/render" "$t/third_party/x"
	printf 'void f() { glFlush(); }\n' > "$t/common/render/gl_device.cpp"
	printf 'void g() { glFlush(); }\n' > "$t/third_party/x/x.cpp"
	printf '%s\n' '// glFlush();' '/* glFlush();' ' glFlush(); */' 'const char *s = "glFlush(";' \
		'#define OGL_FLUSH() glFlush()' '#if 0' 'void h() { glClear(1); OGL_ENABLE(TEXTURE_2D); }' '#endif' \
		> "$t/similar/a.cpp"
	out=$(run_text "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "2 GL call(s) outside the GL adapter in similar/a.cpp (pending 0): similar/a.cpp:7:12, similar/a.cpp:7:24"
	assert_contains "$out" "gl_calls (text): 2 GL call(s)"
	printf 'similar/a.cpp 2 -- test\n' > "$t/pending"
	out=$(run_text "$t") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	printf 'similar/a.cpp 3 -- test\n' > "$t/pending"
	out=$(run_text "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "allows 3 but has 2: lower it"
}

# Test that a bad command line is a usage error.
test_bad_usage_is_an_error()
{
	local out rc
	out=$(python3 "$CHECK" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	out=$(python3 "$CHECK" other . 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
}

run_tests "$@"

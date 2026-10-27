#!/usr/bin/env bash
# Command-line behaviour of tools/dev/check_gl_calls.py: a GL call outside
# the GL adapter and the platform code fails unless the pending list names
# it (ADR 2).  clang-query is a stub here that prints the hits a file of the
# stub's directory holds; tools/dev/check.sh's gl_calls step runs the real
# one.
source "$(dirname "$0")/lib.sh"
CHECK="$DEV_DIR/check_gl_calls.py"

# Make a throwaway tree: a compilation database naming each source given
# (created, with a GL call in its text), a stub clang-query that prints the
# hit lines kept in $tree/hits/<source's base name> and then its match
# count, an allow list and an empty pending list.  The stub does what files
# of its directory say: `fail` (a fatal error), `error` (a non-fatal error
# and exit 0), `exit` (exit 3 and nothing else), `nosummary`, `miscount`
# (a count that is not the hits') and `sleep`.  Prints the tree's path.
make_tree()
{
	local tree src entries=""
	tree=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$tree/build" "$tree/hits"
	for src in "$@"; do
		mkdir -p "$tree/$(dirname "$src")"
		printf 'void f() { glFlush(); }\n' > "$tree/$src"
		entries+="${entries:+,}{\"directory\":\"$tree\",\"file\":\"$src\",\"command\":\"g++ -c $src\"}"
	done
	printf '[%s]\n' "$entries" > "$tree/build/compile_commands.json"
	cat > "$tree/clang-query" <<'EOF'
#!/usr/bin/env bash
src=${*: -1}
dir=$(dirname "$0")
hits=$dir/hits/$(basename "$src")
[[ ! -f $dir/sleep ]] || sleep 30
[[ ! -f $dir/exit ]] || exit 3
[[ ! -f $hits ]] || cat "$hits"
[[ ! -f $dir/fail ]] || { echo "$src:1:1: fatal error: 'x.h' file not found"; exit 1; }
[[ ! -f $dir/error ]] || echo "$src:2:3: error: unknown type name 'foo'"
n=0
[[ ! -f $hits ]] || n=$(grep -c 'binds here' "$hits")
[[ ! -f $dir/miscount ]] || n=$((n + 1))
[[ -f $dir/nosummary ]] || { [[ $n == 1 ]] && echo "1 match." || echo "$n matches."; }
exit 0
EOF
	chmod +x "$tree/clang-query"
	printf '^common/render/gl_device\\.cpp$\n^common/render/unittest/rtrace\\.cpp$\n' > "$tree/allow"
	: > "$tree/pending"
	printf '%s\n' "$tree"
}

# Print a hit line of clang-query's diagnostic output for file:line.
hit()
{
	printf '%s:%s:%s: note: "root" binds here\n' "$1" "$2" "${3-3}"
}

run_check()
{
	CHECK_GL_CALLS_SYSROOT=/nonexistent python3 "$CHECK" clang "$1" "$1/build" "$1/clang-query" \
		"$1/allow" "$1/pending" 2>&1
}

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

# Test that a GL call in game code is reported at its line and fails, and
# that one in the adapter or its pixel tests passes.
test_call_outside_adapter_fails()
{
	local t out rc
	t=$(make_tree similar/arch/gr.cpp common/render/gl_device.cpp common/render/unittest/rtrace.cpp) ||
		die "make_tree failed"
	hit "$t/common/render/gl_device.cpp" 10 > "$t/hits/gl_device.cpp"
	hit "$t/common/render/unittest/rtrace.cpp" 11 > "$t/hits/rtrace.cpp"
	out=$(run_check "$t") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "0 GL call(s) outside"
	hit "$t/similar/arch/gr.cpp" 42 5 > "$t/hits/gr.cpp"
	out=$(run_check "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "1 GL call(s) outside the GL adapter in similar/arch/gr.cpp (pending 0): similar/arch/gr.cpp:42:5"
}

# Test that a unit test other than the GL pixel tests is not allowed to
# call GL.
test_other_unit_tests_may_not_call_gl()
{
	local t out rc
	t=$(make_tree common/render/unittest/frame_setup.cpp) || die "make_tree failed"
	hit "$t/common/render/unittest/frame_setup.cpp" 7 > "$t/hits/frame_setup.cpp"
	out=$(run_check "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "frame_setup.cpp:7:3"
}

# Test that a translation unit clang-query cannot parse fails the check
# instead of counting as clean, however it says so: a fatal error, a
# non-fatal error with exit status 0 (clang-query then drops the calls of the
# bodies it could not parse), a non-zero exit with no message, no match
# count, or a count that is not the number of matches printed.
test_unparsable_unit_fails_however_it_says_so()
{
	local t out rc what
	for what in fail error exit nosummary miscount; do
		t=$(make_tree similar/arch/gr.cpp) || die "make_tree failed"
		hit "$t/similar/arch/gr.cpp" 42 > "$t/hits/gr.cpp"
		: > "$t/$what"
		out=$(run_check "$t") && rc=0 || rc=$?
		assert_eq "$rc" 1 "$what: $out"
	done
	t=$(make_tree similar/arch/gr.cpp) || die "make_tree failed"
	: > "$t/error"
	out=$(run_check "$t") && rc=0 || rc=$?
	assert_contains "$out" "error: unknown type name 'foo'"
	t=$(make_tree similar/arch/gr.cpp) || die "make_tree failed"
	: > "$t/exit"
	out=$(run_check "$t") && rc=0 || rc=$?
	assert_contains "$out" "clang-query exited 3"
	t=$(make_tree similar/arch/gr.cpp) || die "make_tree failed"
	: > "$t/miscount"
	out=$(run_check "$t") && rc=0 || rc=$?
	assert_contains "$out" "counted 1 matches, 0 parsed"
}

# Test that a clang-query that takes longer than the timeout fails.
test_slow_query_times_out()
{
	local t out rc
	t=$(make_tree similar/arch/gr.cpp) || die "make_tree failed"
	: > "$t/sleep"
	out=$(CHECK_GL_CALLS_TIMEOUT=1 run_check "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "took more than 1 s"
}

# Test that a missing compilation database and an empty one fail, and that
# a missing clang-query is a SKIP.
test_missing_database_fails_and_missing_tool_skips()
{
	local t out rc
	t=$(make_tree similar/arch/gr.cpp) || die "make_tree failed"
	rm "$t/build/compile_commands.json"
	out=$(run_check "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "compile_commands.json is missing"
	t=$(make_tree) || die "make_tree failed"
	out=$(run_check "$t") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "no translation unit"
	t=$(make_tree similar/arch/gr.cpp) || die "make_tree failed"
	rm "$t/clang-query"
	out=$(run_check "$t") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "SKIP gl_calls: "
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
	out=$(python3 "$CHECK" clang . 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
}

run_tests "$@"

#!/usr/bin/env bash
# Command-line behaviour of tools/dev/fetch-docs.sh with a fixture manifest
# of file:// URLs, so no network is needed.
source "$(dirname "$0")/lib.sh"
FETCH="$DEV_DIR/fetch-docs.sh"

# sha256 of the literal text "hello\n" and "world\n".
HELLO_SHA=5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03
WORLD_SHA=e258d248fda94c63753607f7c4494ee0fcbe92f1a76bfdac795c9d84101eb317

make_fixture()
{
	local d=$1
	mkdir -p "$d/src"
	printf 'hello\n' > "$d/src/hello.md"
	printf 'world\n' > "$d/src/world.h"
}

# Test that every manifest entry lands at its path and is verified.
test_fetches_manifest_into_dest()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"; make_fixture "$d"
	printf '# comment\nsdl/hello.md file://%s %s\nworld.h file://%s %s\n' \
		"$d/src/hello.md" "$HELLO_SHA" "$d/src/world.h" "$WORLD_SHA" > "$d/manifest"
	out=$("$FETCH" --manifest "$d/manifest" --dest "$d/ref" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_eq "$(cat "$d/ref/sdl/hello.md")" "hello"
	assert_eq "$(cat "$d/ref/world.h")" "world"
}

# Test that a checksum mismatch fails, names the file and keeps no copy.
test_checksum_mismatch_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"; make_fixture "$d"
	printf 'hello.md file://%s %s\n' "$d/src/hello.md" "$WORLD_SHA" > "$d/manifest"
	out=$("$FETCH" --manifest "$d/manifest" --dest "$d/ref" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "hello.md"
	[[ ! -e $d/ref/hello.md ]] || fail 'mismatched file was kept'
}

# Test that the default destination is ignored by git.
test_default_dest_is_ignored()
{
	(real_git check-ignore -q docs/reference/SDL_gpu.h) || fail 'docs/reference is not ignored'
}

# Test that an option without its value is a usage error.
test_option_without_value_is_usage_error()
{
	local out rc
	out=$("$FETCH" --manifest 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2
	assert_contains "$out" "usage"
	out=$("$FETCH" --dest 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2
}

run_tests

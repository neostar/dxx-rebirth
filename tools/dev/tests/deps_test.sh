#!/usr/bin/env bash
# Command-line behaviour of tools/dev/deps.sh against fixture manifests
# whose sources are tiny local CMake projects (file:// URLs and throwaway
# git repositories): the prefix layout, the sha256 and commit checks, and
# the refusal to build from anything else.
# Single quotes hold CMake code expanded by CMake.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"
DEPS="$DEV_DIR/deps.sh"

command -v cmake >/dev/null || { echo "  SKIP deps_test.sh: cmake not found; install cmake"; exit 0; }

# Write a CMake project into directory $1 that installs share/$2/hello.
cmake_project()
{
	mkdir -p "$1"
	printf '%s\n' \
		'cmake_minimum_required(VERSION 3.20)' \
		"project($2 NONE)" \
		'file(WRITE ${CMAKE_BINARY_DIR}/hello "hello\n")' \
		"install(FILES \${CMAKE_BINARY_DIR}/hello DESTINATION share/$2)" > "$1/CMakeLists.txt"
}

# Make a source tarball of a CMake project named $2 in directory $1 and
# print its path.
make_tarball()
{
	local d=$1 name=$2
	cmake_project "$d/src/$name-1.0" "$name"
	tar -czf "$d/$name-1.0.tar.gz" -C "$d/src" "$name-1.0"
	printf '%s\n' "$d/$name-1.0.tar.gz"
}

# Run deps.sh with cache and prefix under $1 and the remaining arguments.
run_deps()
{
	local d=$1; shift
	"$DEPS" --manifest "$d/manifest" --cache "$d/cache" --prefix "$d/prefix" "$@" 2>&1
}

# Test that a pinned source is fetched into the cache and installed into
# the prefix, which records the pins it was built from.
test_builds_pinned_source_into_prefix()
{
	local d t out rc
	d=$(make_scratch)
	t=$(make_tarball "$d" demo)
	echo "source demo 1.0 file://$t $(dev_sha256 "$t")" > "$d/manifest"
	out=$(run_deps "$d") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$(cat "$d/prefix/share/demo/hello" 2>&1)" hello
	assert_eq "$(cat "$d/prefix/deps.stamp" 2>&1)" "$(run_deps "$d" stamp)"
	[[ -f $d/cache/demo-1.0.tar.gz ]] || fail "no demo-1.0.tar.gz in the cache"
	[[ ! -e $d/prefix-build ]] || fail "build tree left behind"
}

# Test that the stamp records the pins and a hash of the build options.
test_stamp_records_pins_and_build_options()
{
	local d out
	d=$(make_scratch)
	echo "source demo 1.0 file:///nowhere $(printf '0%.0s' {1..64})" > "$d/manifest"
	out=$(run_deps "$d" stamp)
	assert_eq "$(head -n 1 <<< "$out")" "$(cat "$d/manifest")"
	[[ $(tail -n 1 <<< "$out") =~ ^options\ [0-9a-f]{64}$ ]] ||
		fail "no options hash in the stamp: $out"
}

# Test that a prebuilt archive with a member list installs only those
# members, without the archive's top directory, and records the
# programs' hashes.
test_prebuilt_installs_listed_members()
{
	local d t out rc
	d=$(make_scratch)
	t=$d/src/LLVM-1.0
	mkdir -p "$t/bin" "$t/lib/clang/1/include" "$t/share"
	for f in clangd clang-format clang-tidy clang; do echo "$f" > "$t/bin/$f"; done
	echo header > "$t/lib/clang/1/include/stddef.h"
	echo docs > "$t/share/docs"
	tar -czf "$d/LLVM-1.0.tar.gz" -C "$d/src" LLVM-1.0
	echo "prebuilt llvm $dev_platform file://$d/LLVM-1.0.tar.gz $(dev_sha256 "$d/LLVM-1.0.tar.gz")" \
		> "$d/manifest"
	out=$(run_deps "$d") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$(cat "$d/prefix/opt/llvm/bin/clangd" 2>&1)" clangd
	assert_eq "$(cat "$d/prefix/opt/llvm/lib/clang/1/include/stddef.h" 2>&1)" header
	[[ ! -e $d/prefix/opt/llvm/bin/clang && ! -e $d/prefix/opt/llvm/share ]] ||
		fail "unlisted members installed"
	assert_eq "$(cat "$d/prefix/opt/llvm/bin.sha256" 2>&1)" \
		"$(cd "$t" && for f in bin/clang-format bin/clang-tidy bin/clangd; do
			echo "$(dev_sha256 "$f")  $f"; done)"
}

# Test that a download whose sha256 differs from the pin is refused and
# kept out of the cache and the prefix.
test_refuses_download_with_wrong_sha256()
{
	local d t out rc
	d=$(make_scratch)
	t=$(make_tarball "$d" demo)
	echo "source demo 1.0 file://$t $(printf '0%.0s' {1..64})" > "$d/manifest"
	out=$(run_deps "$d") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "demo: sha256 $(dev_sha256 "$t") does not match the pinned"
	[[ ! -e $d/cache/demo-1.0.tar.gz ]] || fail "mismatching download kept in the cache"
	[[ ! -e $d/prefix ]] || fail "prefix built from a mismatching source"
}

# Test that an offline build refuses a cached source changed after it
# was fetched.
test_offline_build_refuses_tampered_cache()
{
	local d t out rc
	d=$(make_scratch)
	t=$(make_tarball "$d" demo)
	echo "source demo 1.0 file://$t $(dev_sha256 "$t")" > "$d/manifest"
	run_deps "$d" fetch >/dev/null || fail "fetch failed"
	echo tampered >> "$d/cache/demo-1.0.tar.gz"
	out=$(run_deps "$d" build) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "does not match the pinned"
	[[ ! -e $d/prefix ]] || fail "prefix built from a tampered source"
}

# Test that build never downloads: a source missing from the cache is
# refused, naming the fetch step.
test_build_is_offline()
{
	local d t out rc
	d=$(make_scratch)
	t=$(make_tarball "$d" demo)
	echo "source demo 1.0 file://$t $(dev_sha256 "$t")" > "$d/manifest"
	out=$(run_deps "$d" build) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "demo: not in the cache; run deps.sh fetch"
}

# Test that a git source is built from its pinned commit.
test_builds_pinned_git_commit()
{
	local d r c out rc
	d=$(make_scratch)
	r=$(make_repo)
	cmake_project "$r" gitdemo
	tgit -C "$r" add CMakeLists.txt
	tgit -C "$r" commit -q -m project
	c=$(git -C "$r" rev-parse HEAD)
	tgit -C "$r" commit -q --allow-empty -m 'later commit'
	echo "git gitdemo $c $r" > "$d/manifest"
	out=$(run_deps "$d") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$(cat "$d/prefix/share/gitdemo/hello" 2>&1)" hello
}

# Test that a pinned commit the repository does not have is refused.
test_refuses_unknown_git_commit()
{
	local d r out rc
	d=$(make_scratch)
	r=$(make_repo)
	echo "git gitdemo $(printf 'b%.0s' {1..40}) $r" > "$d/manifest"
	out=$(run_deps "$d") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "gitdemo: commit bbbbbbb"
	[[ ! -e $d/prefix ]] || fail "prefix built without the pinned commit"
}

# Test that a `fetchcontent` pin replaces a CMake FetchContent download:
# the build gets the verified checkout and never touches the network.
test_fetchcontent_comes_from_the_cache()
{
	local d x c out rc
	d=$(make_scratch)
	x=$(make_repo)
	echo extra > "$x/data"
	tgit -C "$x" add data
	tgit -C "$x" commit -q -m data
	c=$(git -C "$x" rev-parse HEAD)
	mkdir -p "$d/src/main-1.0"
	printf '%s\n' \
		'cmake_minimum_required(VERSION 3.24)' \
		'project(main NONE)' \
		'include(FetchContent)' \
		'FetchContent_Declare(extra-lib GIT_REPOSITORY https://example.invalid/x.git GIT_TAG main)' \
		'FetchContent_MakeAvailable(extra-lib)' \
		'install(FILES ${extra-lib_SOURCE_DIR}/data DESTINATION share/main)' \
		> "$d/src/main-1.0/CMakeLists.txt"
	tar -czf "$d/main-1.0.tar.gz" -C "$d/src" main-1.0
	{
		echo "fetchcontent extra-lib $c $x"
		echo "source main 1.0 file://$d/main-1.0.tar.gz $(dev_sha256 "$d/main-1.0.tar.gz")"
	} > "$d/manifest"
	out=$(run_deps "$d") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$(cat "$d/prefix/share/main/data" 2>&1)" extra
}

run_tests

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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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

# Test that a prebuilt archive whose programs sit at its top level, as
# gitleaks ships them, installs only the listed programs into bin/ and
# records their hashes.
test_prebuilt_installs_top_level_programs()
{
	local d t out rc
	d=$(make_scratch) || die "make_scratch failed"
	t=$d/src
	mkdir -p "$t"
	printf '#!/bin/sh\necho gitleaks\n' > "$t/gitleaks"
	chmod +x "$t/gitleaks"
	echo license > "$t/LICENSE"
	tar -czf "$d/gitleaks_1.0_any.tar.gz" -C "$t" gitleaks LICENSE
	echo "prebuilt gitleaks $dev_platform file://$d/gitleaks_1.0_any.tar.gz" \
		"$(dev_sha256 "$d/gitleaks_1.0_any.tar.gz")" > "$d/manifest"
	out=$(run_deps "$d") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$("$d/prefix/opt/gitleaks/bin/gitleaks" 2>&1)" gitleaks
	[[ ! -e $d/prefix/opt/gitleaks/LICENSE && ! -e $d/prefix/opt/gitleaks/bin/LICENSE ]] ||
		fail "unlisted members installed"
	assert_eq "$(cat "$d/prefix/opt/gitleaks/bin.sha256" 2>&1)" \
		"$(dev_sha256 "$t/gitleaks")  bin/gitleaks"
}

# Test that a download whose sha256 differs from the pin is refused and
# kept out of the cache and the prefix.
test_refuses_download_with_wrong_sha256()
{
	local d t out rc
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
	r=$(make_repo) || die "make_repo failed"
	cmake_project "$r" gitdemo
	tgit -C "$r" add CMakeLists.txt
	tgit -C "$r" commit -q -m project
	c=$(tgit -C "$r" rev-parse HEAD)
	tgit -C "$r" commit -q --allow-empty -m 'later commit'
	# raw git: a manifest line, not a command
	echo "git gitdemo $c $r" > "$d/manifest"
	out=$(run_deps "$d") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$(cat "$d/prefix/share/gitdemo/hello" 2>&1)" hello
}

# Test that a pinned commit the repository does not have is refused.
test_refuses_unknown_git_commit()
{
	local d r out rc
	d=$(make_scratch) || die "make_scratch failed"
	r=$(make_repo) || die "make_repo failed"
	# raw git: a manifest line, not a command
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
	d=$(make_scratch) || die "make_scratch failed"
	x=$(make_repo) || die "make_repo failed"
	echo extra > "$x/data"
	tgit -C "$x" add data
	tgit -C "$x" commit -q -m data
	c=$(tgit -C "$x" rev-parse HEAD)
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

# Test that two builds of one C project (Objective-C too on the Mac), with
# debug info, at two different prefixes under the unresolved temporary
# directory install byte-identical libraries and programs: no build path,
# prefix path or archive timestamp leaks into them.
test_build_does_not_depend_on_its_path()
{
	local d t a b langs=C srcs=lib.c
	command -v cc >/dev/null || skip "no C compiler"
	d=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$d/src/cdemo-1.0"
	printf '%s\n' \
		'const char *where(void) { return __FILE__; }' > "$d/src/cdemo-1.0/lib.c"
	if [[ $(uname -s) == Darwin ]]; then
		printf '%s\n' \
			'const char *where_objc(void) { return __FILE__; }' > "$d/src/cdemo-1.0/objc.m"
		langs='C OBJC' srcs='lib.c objc.m'
	fi
	printf '%s\n' \
		'const char *where(void);' \
		'int main(void) { return where()[0] == 0; }' > "$d/src/cdemo-1.0/main.c"
	printf '%s\n' \
		'cmake_minimum_required(VERSION 3.20)' \
		"project(cdemo $langs)" \
		'add_compile_options(-g)' \
		"add_library(cdemo SHARED $srcs)" \
		'add_library(cdemo_static STATIC lib.c)' \
		'add_executable(cdemo_main main.c)' \
		'target_link_libraries(cdemo_main cdemo)' \
		'install(TARGETS cdemo cdemo_static cdemo_main)' > "$d/src/cdemo-1.0/CMakeLists.txt"
	tar -czf "$d/cdemo-1.0.tar.gz" -C "$d/src" cdemo-1.0
	echo "source cdemo 1.0 file://$d/cdemo-1.0.tar.gz $(dev_sha256 "$d/cdemo-1.0.tar.gz")" \
		> "$d/manifest"
	for t in a b; do
		"$DEPS" --manifest "$d/manifest" --cache "$d/cache" --prefix "$d/$t/prefix-$t" \
			>/dev/null 2>&1 || fail "build in $t failed"
		# Archive members carry a timestamp to the second.
		[[ $t == a ]] && sleep 1
	done
	a=$(tree_sums "$d/a/prefix-a" bin lib)
	b=$(tree_sums "$d/b/prefix-b" bin lib)
	[[ -n $a ]] || fail "nothing installed"
	assert_eq "$b" "$a" "installed files differ between the two prefixes"
	"$d/b/prefix-b/bin/cdemo_main" || fail "the program does not run from its prefix"
}

# Test that a build records no build time: CMake's TIMESTAMP, as
# SPIRV-Cross's version header uses it, gives the same value every build.
test_build_records_no_build_time()
{
	local d a
	d=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$d/src/stamp-1.0"
	printf '%s\n' \
		'cmake_minimum_required(VERSION 3.20)' \
		'project(stamp NONE)' \
		'string(TIMESTAMP t "%Y-%m-%dT%H:%M:%S")' \
		'file(WRITE ${CMAKE_BINARY_DIR}/when "${t}\n")' \
		'install(FILES ${CMAKE_BINARY_DIR}/when DESTINATION share)' \
		> "$d/src/stamp-1.0/CMakeLists.txt"
	tar -czf "$d/stamp-1.0.tar.gz" -C "$d/src" stamp-1.0
	echo "source stamp 1.0 file://$d/stamp-1.0.tar.gz $(dev_sha256 "$d/stamp-1.0.tar.gz")" \
		> "$d/manifest"
	run_deps "$d" >/dev/null || fail "first build failed"
	a=$(cat "$d/prefix/share/when")
	sleep 1
	run_deps "$d" >/dev/null || fail "second build failed"
	assert_eq "$(cat "$d/prefix/share/when")" "$a"
}

run_tests

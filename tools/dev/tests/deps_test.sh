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

# Test that deps.sh marks the clone's third_party/ out of Time Machine
# backups, once however often it runs.
test_marks_third_party_out_of_time_machine()
{
	local d r tm out rc
	d=$(make_scratch) || die "make_scratch failed"
	r=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$r/tools"
	cp -R "$DEV_DIR" "$r/tools/dev"
	: > "$d/manifest"
	tm=$(fake_tmutil "$d")
	for _ in 1 2; do
		out=$(DXX_TMUTIL=$tm "$r/tools/dev/deps.sh" --manifest "$d/manifest" \
			--cache "$d/cache" --prefix "$d/prefix" fetch 2>&1) && rc=0 || rc=$?
		assert_eq "$rc" 0 "$out"
	done
	assert_eq "$(grep '^addexclusion' "$d/log")" "addexclusion $(cd "$r" && pwd)/third_party"
	[[ -d $r/third_party ]] || fail "third_party/ not made"
}

# Test that without tmutil (the container) deps.sh says so and goes on.
test_skips_time_machine_without_tmutil()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	: > "$d/manifest"
	out=$(DXX_TMUTIL=$d/no-tmutil run_deps "$d" fetch) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "SKIP time machine: $d/no-tmutil not found"
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

# Print each install name (LC_ID_DYLIB) and loaded library (LC_LOAD_DYLIB
# and kin) of Mach-O file $1 that would not load once its prefix moved: an
# absolute path outside /usr/lib and /System, or an @rpath name found in
# none of its LC_RPATH directories (@loader_path is its own directory).
unmovable_refs()
{
	local f=$1 dir rp lib found rpaths=()
	dir=$(dirname "$f")
	while read -r rp; do
		rpaths+=("${rp/@loader_path/$dir}")
	done < <(otool -l "$f" | awk '$1 == "cmd" && $2 == "LC_RPATH" { getline; getline; print $2 }')
	while read -r lib; do
		case $lib in
		/usr/lib/*|/System/*) ;;
		@rpath/*)
			found=
			for rp in "${rpaths[@]+"${rpaths[@]}"}"; do
				[[ -e $rp/${lib#@rpath/} ]] && found=1
			done
			[[ -n $found ]] || echo "$f: $lib does not resolve" ;;
		*) echo "$f: $lib" ;;
		esac
	done < <(otool -l "$f" | awk '$1 == "cmd" &&
		$2 ~ /^LC_(ID_DYLIB|LOAD_DYLIB|LOAD_WEAK_DYLIB|REEXPORT_DYLIB|LAZY_LOAD_DYLIB|LOAD_UPWARD_DYLIB)$/ {
		getline; getline; print $2 }')
}

# Test that on the Mac a library whose CMake sets an absolute install name,
# as FluidSynth's does, and what links it, in its own project and in a
# later dependency (as SDL3_mixer does), name only @rpath and system
# libraries, so they still load once the prefix moves, and that their
# signatures verify: the library is signed again at install, as a project
# may, and install_name_tool re-signs only a linker-signed file.  Read
# with otool and codesign, never run: a library that does not load would
# crash in dyld.
test_prefix_loads_after_it_moves()
{
	local d n f out bad=()
	[[ $(uname -s) == Darwin ]] || skip "install names are Mach-O only"
	command -v cc >/dev/null || skip "no C compiler"
	d=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$d/src/fsdemo-1.0" "$d/src/mixdemo-1.0"
	printf '%s\n' 'int fs(void) { return 0; }' > "$d/src/fsdemo-1.0/fs.c"
	printf '%s\n' 'int fs(void);' 'int main(void) { return fs(); }' \
		> "$d/src/fsdemo-1.0/main.c"
	printf '%s\n' \
		'cmake_minimum_required(VERSION 3.20)' \
		'project(fsdemo C)' \
		'include(GNUInstallDirs)' \
		'set(CMAKE_INSTALL_NAME_DIR ${CMAKE_INSTALL_FULL_LIBDIR})' \
		'add_library(fs SHARED fs.c)' \
		'set_target_properties(fs PROPERTIES VERSION 3.6.1 SOVERSION 3)' \
		'add_executable(fsdemo main.c)' \
		'target_link_libraries(fsdemo fs)' \
		'install(TARGETS fs fsdemo)' \
		'install(CODE "execute_process(COMMAND codesign --force --sign -' \
		'	\${CMAKE_INSTALL_PREFIX}/lib/libfs.3.6.1.dylib COMMAND_ERROR_IS_FATAL ANY)")' \
		> "$d/src/fsdemo-1.0/CMakeLists.txt"
	printf '%s\n' 'int fs(void);' 'int mix(void) { return fs(); }' \
		> "$d/src/mixdemo-1.0/mix.c"
	printf '%s\n' 'int mix(void);' 'int main(void) { return mix(); }' \
		> "$d/src/mixdemo-1.0/main.c"
	printf '%s\n' \
		'cmake_minimum_required(VERSION 3.20)' \
		'project(mixdemo C)' \
		'find_library(FS fs REQUIRED)' \
		'add_library(mix SHARED mix.c)' \
		'target_link_libraries(mix ${FS})' \
		'add_executable(mixdemo main.c)' \
		'target_link_libraries(mixdemo mix)' \
		'install(TARGETS mix mixdemo)' > "$d/src/mixdemo-1.0/CMakeLists.txt"
	: > "$d/manifest"
	for n in fsdemo mixdemo; do
		tar -czf "$d/$n-1.0.tar.gz" -C "$d/src" "$n-1.0"
		echo "source $n 1.0 file://$d/$n-1.0.tar.gz $(dev_sha256 "$d/$n-1.0.tar.gz")" \
			>> "$d/manifest"
	done
	out=$(run_deps "$d") || { fail "build failed: $out"; return; }
	mv "$d/prefix" "$d/moved"
	for f in "$d/moved/bin/fsdemo" "$d/moved/bin/mixdemo" "$d/moved/lib/libfs.3.6.1.dylib" \
		"$d/moved/lib/libmix.dylib"; do
		[[ -f $f ]] || { fail "$f not installed"; continue; }
		out=$(unmovable_refs "$f") || fail "otool failed on $f"
		[[ -z $out ]] || bad+=("$out")
		out=$(codesign -v "$f" 2>&1) || bad+=("$f: signature: $out")
	done
	[[ ${#bad[@]} -eq 0 ]] || fail "not loadable from the moved prefix: ${bad[*]}"
}

# Test that on the Mac an otool or file that fails fails the build, naming
# the file it could not read: the build must not pass with no install name
# renamed.
test_failing_otool_or_file_fails_the_build()
{
	local d t out rc
	[[ $(uname -s) == Darwin ]] || skip "install names are Mach-O only"
	command -v cc >/dev/null || skip "no C compiler"
	d=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$d/src/otdemo-1.0" "$d/bin"
	printf '%s\n' 'int ot(void) { return 0; }' > "$d/src/otdemo-1.0/ot.c"
	printf '%s\n' \
		'cmake_minimum_required(VERSION 3.20)' \
		'project(otdemo C)' \
		'add_library(ot SHARED ot.c)' \
		'install(TARGETS ot)' > "$d/src/otdemo-1.0/CMakeLists.txt"
	tar -czf "$d/otdemo-1.0.tar.gz" -C "$d/src" otdemo-1.0
	echo "source otdemo 1.0 file://$d/otdemo-1.0.tar.gz $(dev_sha256 "$d/otdemo-1.0.tar.gz")" \
		> "$d/manifest"
	for t in otool file; do
		rm -f "$d/bin/"*
		printf '#!/bin/sh\necho "%s: cannot open" >&2\nexit 1\n' "$t" > "$d/bin/$t"
		chmod +x "$d/bin/$t"
		out=$(PATH="$d/bin:$PATH" run_deps "$d") && rc=0 || rc=$?
		assert_eq "$rc" 1 "$t: $out"
		assert_contains "$out" "$t cannot read $d/prefix/lib/libot.dylib"
	done
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

# Test that build refuses to wipe a prefix it did not make: /, $HOME, the
# repository, or a directory with files but no marker, leaving it as it
# was; and replaces one it made, even after a failed build.
test_build_refuses_a_prefix_it_did_not_make()
{
	local d t p out rc
	d=$(make_scratch) || die "make_scratch failed"
	t=$(make_tarball "$d" demo)
	echo "source demo 1.0 file://$t $(dev_sha256 "$t")" > "$d/manifest"
	run_deps "$d" fetch >/dev/null || die "fetch failed"
	# Should the guard fail, an rm that removes nothing stands in for the
	# real one where it matters: it logs its arguments and fails.
	mkdir "$d/bin" && printf '#!/bin/sh\necho "$*" >> "%s/rm.log"\nexit 1\n' "$d" > "$d/bin/rm"
	chmod +x "$d/bin/rm"
	mkdir "$d/precious" && echo keep > "$d/precious/file"
	for p in / "$HOME" "$REPO_ROOT" "$d/precious"; do
		out=$(PATH="$d/bin:$PATH" "$DEPS" --manifest "$d/manifest" --cache "$d/cache" \
			--prefix "$p" build 2>&1) && rc=0 || rc=$?
		assert_eq "$rc" 1 "$p: $out"
		assert_contains "$out" "refusing to remove it"
	done
	[[ ! -e $d/rm.log ]] || fail "rm ran: $(cat "$d/rm.log")"
	assert_eq "$(cat "$d/precious/file")" keep
	out=$(run_deps "$d" build) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	rm "$d/prefix/deps.stamp"
	out=$(run_deps "$d" build) && rc=0 || rc=$?
	assert_eq "$rc" 0 "a prefix with the marker only: $out"
}

run_tests

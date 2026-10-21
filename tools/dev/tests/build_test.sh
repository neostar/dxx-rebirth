#!/usr/bin/env bash
# Command-line behaviour of tools/dev/build.sh and the settings its site
# files give SCons, checked with a fake scons; nothing is compiled.
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"

# Print the arguments build.sh passes to scons for arguments "$@", one per
# line.  With SCONS_PID_FILE set, the fake scons first writes its pid there.
scons_args()
{
	local bin
	bin=$(make_scratch)
	printf '#!/bin/sh\n[ -n "${SCONS_PID_FILE-}" ] && echo $$ > "$SCONS_PID_FILE" && sleep 1\nprintf "%%s\\n" "$@"\n' \
		> "$bin/scons"; chmod +x "$bin/scons"
	PATH="$bin:$PATH" "$DEV_DIR/build.sh" "$@"
}

# Test that `check` runs scons under no-window.sh, so scons taking focus
# fails the build, and that a plain build is not wrapped.
test_check_runs_windowless()
{
	local d rc
	d=$(make_scratch); mkdir "$d/reports"
	NO_WINDOW_FRONTMOST_CMD=$(run_sampler "$d") NO_WINDOW_REPORTS_DIR="$d/reports" \
		SCONS_PID_FILE="$d/pid" scons_args dev >/dev/null 2>&1
	assert_eq "$?" 0 'plain build'
	d=$(make_scratch); mkdir "$d/reports"
	NO_WINDOW_FRONTMOST_CMD=$(run_sampler "$d") NO_WINDOW_REPORTS_DIR="$d/reports" \
		SCONS_PID_FILE="$d/pid" scons_args dev check >/dev/null 2>&1 && rc=0 || rc=$?
	assert_eq "$rc" 1 'check'
}

# Make a fake SCons package whose Defaults holds only the include flags,
# for the site files to install into, and print the directory holding it.
fake_scons()
{
	local fake
	fake=$(make_scratch)
	mkdir "$fake/SCons"
	: > "$fake/SCons/__init__.py"
	printf 'ConstructionEnvironment = {"_CPPINCFLAGS": "$INC"}\n' > "$fake/SCons/Defaults.py"
	printf '%s\n' "$fake"
}

# Python that reads site files sys.argv[2:] as SCons' Variables does (each
# exec'd with its directory on sys.path, later files win) into `values`,
# with the fake SCons package in sys.argv[1].
read_sites='
import os, sys
sys.path.insert(0, sys.argv[1])
values = {}
for path in sys.argv[2:]:
	file_values = {"__name__": path}
	sys.path.insert(0, os.path.dirname(path))
	exec(open(path).read(), {}, file_values)
	del sys.path[0]
	values.update(file_values)
'

# Print the settings SCons reads from site files "$@" as key=value.
site_settings()
{
	python3 -I -c "$read_sites"'
for k in ("builddir", "compilation_database", "debug", "lto", "macos_add_frameworks", "sconf_optimize_builtin_constant_p"):
	print(f"{k}={values.get(k)}")
' "$(fake_scons)" "$@"
}

# Test that the dev profile builds D2X only with site-dev.py.
test_dev_profile_builds_d2x_with_site_dev()
{
	local out
	out=$(scons_args dev check | tr '\n' ' ')
	assert_contains "$out" "d1x=0 d2x=1 site=$DEV_DIR/site-dev.py check "
}

# Test that the asan profile reads site-dev.py, then site-asan.py's
# overrides, and keeps its own build directory.
test_asan_profile_layers_site_asan_over_site_dev()
{
	local out
	out=$(scons_args asan check | tr '\n' ' ')
	assert_contains "$out" "d1x=0 d2x=1 site=$DEV_DIR/site-dev.py site=$DEV_DIR/site-asan.py check "
	out=$(site_settings "$DEV_DIR/site-dev.py" "$DEV_DIR/site-asan.py" | tr '\n' ' ')
	assert_eq "$out" 'builddir=build/asan compilation_database=build/asan/compile_commands.json debug=True lto=0 macos_add_frameworks=False sconf_optimize_builtin_constant_p=force-failure '
}

# Read site files "$@" as SCons reads them (each exec'd with its directory
# on sys.path) into a fake SCons.Defaults, and print the include flags they
# leave there, then the warnings added for a fork source (absolute and
# relative), an upstream source and one in a directory that merely starts
# with a fork directory's name.
site_fork_warnings()
{
	(cd "$REPO_ROOT" && python3 -I -c "$read_sites"'
import SCons.Defaults
env = SCons.Defaults.ConstructionEnvironment
print(env["_CPPINCFLAGS"])
flags_for = env["_DXX_FORK_WARNINGS"]
sources = (os.path.abspath("tools/dev/x.cpp"), "tools/y.cpp", "common/main/x.cpp",
	os.path.abspath("toolsx/z.cpp"))
for source in sources:
	flags = flags_for(target=[], source=[source], env=None, for_signature=False)
	print(source.rsplit("/", 2)[-2] + ": " + " ".join(flags))
' "$(fake_scons)" "$@")
}

# Test that the site files add the strict warnings (ADR 5) to compiles of
# sources in the fork directories only, once, without touching SConstruct.
test_site_files_add_strict_warnings_to_fork_sources_only()
{
	local out
	out=$(site_fork_warnings "$DEV_DIR/site-dev.py" "$DEV_DIR/site-asan.py" 2>&1)
	assert_eq "$out" '$INC $_DXX_FORK_WARNINGS
dev: -Wconversion -Wsign-conversion -Wshadow -Werror
tools: -Wconversion -Wsign-conversion -Wshadow -Werror
main: 
toolsx: '
}

# Test, in a real build of a throwaway clone (slow, opt-in with
# DXX_BUILD_TESTS=1), that a -Wconversion violation fails the compile of a
# fork file and the same code in an upstream file still builds.
test_strict_warnings_fail_fork_file_in_real_build()
{
	local r out rc probe='int dxx_strict_probe(long); int dxx_strict_probe(long v) { return v; }'
	[[ ${DXX_BUILD_TESTS-} == 1 ]] || skip "builds D2X objects; set DXX_BUILD_TESTS=1 to run"
	command -v scons >/dev/null || skip "scons not found"
	# A real build needs the whole source tree (SConstruct, common/,
	# similar/), which make_repo and fork_repo do not hold, so this is a
	# clone of this repository.  --shared reads its objects in place and
	# writes nothing to it; every change below is made in the clone, under
	# the scratch directory.
	r=$(make_scratch)/clone
	git clone -q --shared "$REPO_ROOT" "$r"
	rm -rf "$r/tools/dev" && cp -R "$DEV_DIR" "$r/tools/dev"
	printf '%s\n' "$probe" > "$r/tools/strictprobe.cpp"
	printf '%s\n' "$probe" >> "$r/common/misc/strutil.cpp"
	awk -v anchor="'common/misc/strutil.cpp'," -v probe="'tools/strictprobe.cpp'," \
		'{print} $0 == anchor {print probe}' "$REPO_ROOT/SConstruct" > "$r/SConstruct"
	grep -qxF "'tools/strictprobe.cpp'," "$r/SConstruct" || fail "SConstruct has no strutil.cpp line"
	out=$("$r/tools/dev/build.sh" dev build/dev/common/misc/strutil.o 2>&1)
	assert_eq "$?" 0 "upstream file: $out"
	out=$("$r/tools/dev/build.sh" dev build/dev/tools/strictprobe.o 2>&1) && rc=0 || rc=$?
	[[ $rc -ne 0 ]] || fail "fork file built: $(tail -5 <<<"$out")"
	assert_contains "$out" "strictprobe.cpp:1:"
	# clang: [-Werror,-Wshorten-64-to-32]; GCC: [-Werror=conversion]
	[[ $out =~ \[-Werror(,-Wshorten-64-to-32|=conversion)\] ]] ||
		fail "no strict conversion error: $(tail -5 <<<"$out")"
}

run_tests

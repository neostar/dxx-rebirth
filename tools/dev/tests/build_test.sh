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
	bin=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\n[ -n "${SCONS_PID_FILE-}" ] && echo $$ > "$SCONS_PID_FILE" && sleep 1\nprintf "%%s\\n" "$@"\n' \
		> "$bin/scons"; chmod +x "$bin/scons"
	PATH="$bin:$PATH" "$DEV_DIR/build.sh" "$@"
}

# Test that `check` runs scons under no-window.sh, so scons taking focus
# fails the build, and that a plain build is not wrapped.
test_check_runs_windowless()
{
	local d rc
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	NO_WINDOW_FRONTMOST_CMD=$(run_sampler "$d") NO_WINDOW_REPORTS_DIR="$d/reports" \
		SCONS_PID_FILE="$d/pid" scons_args dev >/dev/null 2>&1
	assert_eq "$?" 0 'plain build'
	d=$(make_scratch) || die "make_scratch failed"; mkdir "$d/reports"
	NO_WINDOW_FRONTMOST_CMD=$(run_sampler "$d") NO_WINDOW_REPORTS_DIR="$d/reports" \
		SCONS_PID_FILE="$d/pid" scons_args dev check >/dev/null 2>&1 && rc=0 || rc=$?
	assert_eq "$rc" 1 'check'
}

# Test that build.sh marks build/ out of Time Machine backups (with lib.sh's
# fake tmutil), once however often it runs.
test_marks_build_out_of_time_machine()
{
	scons_args dev >/dev/null 2>&1 || fail "first build"
	scons_args dev >/dev/null 2>&1 || fail "second build"
	assert_eq "$(grep '^addexclusion' "$_scratch_root/tm/log")" "addexclusion $REPO_ROOT/build"
}

# Make a fake SCons package whose Defaults holds only the include flags,
# for the site files to install into, and print the directory holding it.
fake_scons()
{
	local fake
	fake=$(make_scratch) || die "make_scratch failed"
	mkdir "$fake/SCons"
	: > "$fake/SCons/__init__.py"
	printf 'ConstructionEnvironment = {"_CPPINCFLAGS": "$INC", "_LIBDIRFLAGS": "$LIBDIRS"}\n' \
		> "$fake/SCons/Defaults.py"
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

# The CPPFLAGS setting of a hardened profile, as site_settings prints it,
# from hardening.py, the flags' one source.
hardened=CPPFLAGS=$(python3 "$DEV_DIR/hardening.py")

# Print the settings SCons reads from site files "$@" as key=value.
site_settings()
{
	python3 -I -c "$read_sites"'
for k in ("builddir", "compilation_database", "debug", "lto", "macos_add_frameworks",
		"sconf_optimize_builtin_constant_p", "CPPFLAGS"):
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
	out=$(site_settings "$DEV_DIR/site-dev.py" "$DEV_DIR/site-asan.py" | grep -v '^CPPFLAGS=')
	assert_eq "$(tr '\n' ' ' <<<"$out")" "builddir=build/asan \
compilation_database=build/asan/compile_commands.json debug=True lto=0 \
macos_add_frameworks=False sconf_optimize_builtin_constant_p=force-failure "
}

# Print what build.sh passes to scons for arguments "$@", one argument
# per line, then the variables it exports for pkg-config and site-sdl3.py,
# with a fake brew whose prefix is /brew on PATH and the pinned prefix at
# /pinned.
scons_sdl3_args()
{
	local bin
	bin=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\nprintf "%%s\\n" "$@" "PKG_CONFIG=$PKG_CONFIG" "PKG_CONFIG_PATH=$PKG_CONFIG_PATH" "DXX_SDL3_PREFIX=${DXX_SDL3_PREFIX-}" "DXX_SDL3_SYSTEM_INCLUDE=${DXX_SDL3_SYSTEM_INCLUDE-}"\n' \
		> "$bin/scons"
	printf '#!/bin/sh\necho /brew\n' > "$bin/brew"
	: > "$bin/pkg-config"
	chmod +x "$bin/scons" "$bin/brew" "$bin/pkg-config"
	PATH="$bin:$PATH" DXX_DEPS_PREFIX=/pinned PKG_CONFIG_PATH='' "$DEV_DIR/build.sh" "$@"
}

# Test that sdl3=1 builds each profile in its own directory against the
# pinned SDL3: pkg-config reads the pinned .pc files first and relocates
# their prefix (--define-prefix), named by its full path, and site-sdl3.py,
# read after the profile's site files, gets the pinned prefix and, on
# macOS, Homebrew's include directory.  No flags go on the command line,
# where they would replace the profile's own (site-asan.py's -fsanitize).
test_sdl3_builds_profiles_against_pinned_sdl3()
{
	local out sys=''
	[[ $dev_os == darwin ]] && sys=/brew/include
	out=$(scons_sdl3_args dev sdl3=1 check | tr '\n' ' ')
	assert_contains "$out" "site=$DEV_DIR/site-dev.py site=$DEV_DIR/site-sdl3.py \
builddir=build/dev-sdl3 compilation_database=build/dev-sdl3/compile_commands.json sdl3=1 check "
	[[ $out =~ \ PKG_CONFIG=/[^\ ]+/pkg-config\ --define-prefix\ PKG_CONFIG_PATH=/pinned/lib/pkgconfig\  ]] ||
		fail "pkg-config not the pinned prefix's by full path: $out"
	assert_contains "$out" " DXX_SDL3_PREFIX=/pinned DXX_SDL3_SYSTEM_INCLUDE=$sys "
	assert_not_contains "$out" "FLAGS="
	out=$(scons_sdl3_args asan sdl3=1 | tr '\n' ' ')
	assert_contains "$out" "site=$DEV_DIR/site-dev.py site=$DEV_DIR/site-asan.py \
site=$DEV_DIR/site-sdl3.py builddir=build/asan-sdl3 \
compilation_database=build/asan-sdl3/compile_commands.json sdl3=1 "
	assert_not_contains "$out" "FLAGS="
	out=$(scons_sdl3_args dev check | tr '\n' ' ')
	assert_not_contains "$out" "builddir="
	assert_not_contains "$out" "site-sdl3.py"
	assert_contains "$out" "PKG_CONFIG= PKG_CONFIG_PATH= DXX_SDL3_PREFIX= "
}

# Test that build.sh reads sdl3= as SCons' BoolVariable does: every true
# spelling, in any case, builds against the pinned SDL3, every false one
# does not, the last sdl3= wins, and any other value stops build.sh with
# a message before scons runs.
test_sdl3_accepts_every_scons_bool_spelling()
{
	local v out rc
	for v in 1 y Yes TRUE t on all; do
		out=$(scons_sdl3_args dev "sdl3=$v" | tr '\n' ' ')
		assert_contains "$out" "site=$DEV_DIR/site-sdl3.py builddir=build/dev-sdl3 " "sdl3=$v"
	done
	for v in 0 n No false F off none; do
		out=$(scons_sdl3_args dev "sdl3=$v" | tr '\n' ' ')
		assert_not_contains "$out" "builddir=" "sdl3=$v"
	done
	out=$(scons_sdl3_args dev sdl3=1 sdl3=0 | tr '\n' ' ')
	assert_not_contains "$out" "builddir="
	out=$(scons_sdl3_args dev sdl3=0 sdl3=yes | tr '\n' ' ')
	assert_contains "$out" "builddir=build/dev-sdl3 "
	out=$(scons_sdl3_args dev sdl3=maybe 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_eq "$out" "build.sh: sdl3=maybe is not a SCons boolean (1/0, yes/no, true/false, on/off, ...)"
}

# Read site files "$@" as SCons does (each exec'd with its directory on
# sys.path) into a fake SCons.Defaults, with DXX_SDL3_PREFIX=/pinned, and
# print the link and include flags they leave there, then the settings
# that carry the profile's flags.
site_sdl3_settings()
{
	DXX_SDL3_PREFIX=/pinned python3 -I -c "$read_sites"'
import SCons.Defaults
env = SCons.Defaults.ConstructionEnvironment
print("_LIBDIRFLAGS=" + env["_LIBDIRFLAGS"])
print("_CPPINCFLAGS=" + env["_CPPINCFLAGS"])
for k in ("CPPFLAGS", "CXXFLAGS", "LINKFLAGS"):
	print(f"{k}={values.get(k)}")
' "$(fake_scons)" "$@" 2>&1
}

# Print the CCACHE_BASEDIR build.sh gives scons for arguments "$@", run
# from directory $1.
scons_env()
{
	local bin dir=$1
	shift
	bin=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\necho "CCACHE_BASEDIR=$CCACHE_BASEDIR"\n' > "$bin/scons"
	chmod +x "$bin/scons"
	(cd "$dir" && PATH="$bin:$PATH" "$DEV_DIR/build.sh" "$@")
}

# Test that build.sh, run from anywhere, has ccache rewrite the source
# tree's absolute paths (hardening-exit.h's -include) to relative ones,
# so worktrees share the cache.
test_builds_share_ccache()
{
	local a out
	for a in dev asan; do
		out=$(scons_env "$_scratch_root" "$a" 2>&1)
		assert_eq "$out" "CCACHE_BASEDIR=$REPO_ROOT"
	done
}

# Test that site-sdl3.py puts the pinned prefix's lib directory ahead of
# every other -L, so -lSDL3, -lSDL3_image and -lSDL3_mixer find the pinned
# libraries before Homebrew's (whose lib directory physfs and libpng add),
# and on macOS Homebrew's include directory first as a system one, both
# once however often SCons reads it, and leaves every profile's own flags
# as they are: the asan profile still compiles and links with -fsanitize.
test_site_sdl3_puts_pinned_libraries_first_and_keeps_profile_flags()
{
	local out sdl3=$DEV_DIR/site-sdl3.py
	out=$(site_sdl3_settings "$DEV_DIR/site-dev.py" "$sdl3" "$sdl3")
	assert_eq "$(grep ^_ <<<"$out")" '_LIBDIRFLAGS=-L/pinned/lib $LIBDIRS
_CPPINCFLAGS=$INC $_DXX_FORK_WARNINGS'
	assert_contains "$out" "$hardened"
	out=$(DXX_SDL3_SYSTEM_INCLUDE=/brew/include site_sdl3_settings "$DEV_DIR/site-dev.py" \
		"$DEV_DIR/site-asan.py" "$sdl3" "$sdl3")
	assert_eq "$(grep ^_ <<<"$out")" '_LIBDIRFLAGS=-L/pinned/lib $LIBDIRS
_CPPINCFLAGS=-isystem /brew/include $INC $_DXX_FORK_WARNINGS'
	assert_contains "$out" "$hardened"
	assert_contains "$out" "CXXFLAGS=-O1 -fsanitize=address,undefined "
	assert_contains "$out" "LINKFLAGS=-fsanitize=address,undefined"
	out=$(DXX_SDL3_PREFIX='' python3 -I -c "$read_sites" "$(fake_scons)" "$sdl3" 2>&1)
	assert_contains "$out" "site-sdl3.py: DXX_SDL3_PREFIX is not set; build.sh sets it for sdl3=1"
}

# Test that both profiles build with the hardened standard library
# (ADR 5): libc++'s debug hardening mode and libstdc++'s assertions; and
# with the source tree's path mapped to . in the debug information, so
# ccache, which hashes the directory of a -g compile, shares its results
# across worktrees.
test_profiles_harden_the_standard_library()
{
	local out map=" -fdebug-prefix-map=$REPO_ROOT=."
	out=$(cd "$REPO_ROOT" && site_settings "$DEV_DIR/site-dev.py" | grep '^CPPFLAGS=')
	assert_eq "$out" "$hardened$map"
	out=$(cd "$REPO_ROOT" && site_settings "$DEV_DIR/site-dev.py" "$DEV_DIR/site-asan.py" |
		grep '^CPPFLAGS=')
	assert_eq "$out" "$hardened$map"
	assert_contains "$hardened" "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG"
	assert_contains "$hardened" "-D_GLIBCXX_ASSERTIONS"
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
# sources in the fork directories only, once, without touching SConstruct;
# the asan profile adds UBSan's implicit-conversion check there too, where
# g++ has it (Apple clang, not GCC), and says so where it does not.
test_site_files_add_strict_warnings_to_fork_sources_only()
{
	local out fork='-Wconversion -Wsign-conversion -Wshadow -Werror' note='' missing
	out=$(site_fork_warnings "$DEV_DIR/site-dev.py" 2>&1)
	assert_eq "$out" "\$INC \$_DXX_FORK_WARNINGS
dev: $fork
tools: $fork
main: 
toolsx: "
	# site-asan.py's own probe decides; this test checks what it does with it.
	missing=$(site_asan_twice "$PATH" | sed -n 's/^missing=//p')
	if [[ $missing != *implicit-conversion* ]]; then
		fork+=' -fsanitize=implicit-conversion -fno-sanitize-recover=implicit-conversion'
	fi
	[[ -z $missing ]] || note="site-asan.py: g++ lacks -fsanitize=$missing; skipped
"
	out=$(site_fork_warnings "$DEV_DIR/site-dev.py" "$DEV_DIR/site-asan.py" 2>&1)
	assert_eq "$out" "$note\$INC \$_DXX_FORK_WARNINGS
dev: $fork
tools: $fork
main: 
toolsx: "
}

# Test that `fork_warnings.py --dirs` prints the fork directories it gives
# the strict warnings, one per line, comments and blanks dropped (nightly.sh
# reads them so), fails when tools/dev/fork-dirs lists none, and that an
# unknown argument is a usage error.
test_fork_warnings_prints_fork_dirs()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$d/tools/dev"
	cp "$DEV_DIR/fork_warnings.py" "$d/tools/dev/"
	printf '# fork directories\n  # indented\n\nsrc \t\ncommon/x\n' > "$d/tools/dev/fork-dirs"
	out=$(python3 -I "$d/tools/dev/fork_warnings.py" --dirs)
	assert_eq "$?" 0 "$out"
	assert_eq "$out" "src
common/x"
	printf '# only a comment\n' > "$d/tools/dev/fork-dirs"
	out=$(python3 -I "$d/tools/dev/fork_warnings.py" --dirs 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "fork-dirs lists no directory"
	out=$(python3 -I "$d/tools/dev/fork_warnings.py" --bogus 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
}

# Test that a fork compile gets every include directory on CPPPATH outside
# the repository (Homebrew's: Boost) again as -isystem, so the strict
# warnings stay off third-party headers, and that an upstream compile gets
# nothing.  Directories in the repository stay user ones, given relative,
# top-relative ('#dir', '#/dir') or absolute, also one holding a fork
# directory (common, for common/debug): a system header would hide a fork
# defect spelled through its macros or templates.
test_fork_compiles_see_third_party_includes_as_system()
{
	local out
	out=$(cd "$REPO_ROOT" && python3 -I -c "$read_sites"'
import SCons.Defaults
flags_for = SCons.Defaults.ConstructionEnvironment["_DXX_FORK_WARNINGS"]
env = {"CPPPATH": ["common/include", ".", "build/dev/", "/opt/x/include", "#common/main",
	"#/build/dev", "common", os.getcwd() + "/common/include", "tools/dev"]}
for source in ("tools/y.cpp", "common/main/x.cpp"):
	print(source + ": " + " ".join(flags_for(target=[], source=[source], env=env, for_signature=False)))
' "$(fake_scons)" "$DEV_DIR/site-dev.py" 2>&1)
	assert_eq "$out" "tools/y.cpp: -Wconversion -Wsign-conversion -Wshadow -Werror -isystem /opt/x/include
common/main/x.cpp: "
}

# Exec site-asan.py twice in one python3, as SCons may read a site file
# more than once, with PATH $1, and print CLANG_UBSAN_MISSING; stderr too.
site_asan_twice()
{
	PATH=$1 "$(command -v python3)" -I -c 'import sys, os
sys.path.insert(0, os.path.dirname(sys.argv[1]))
for _ in range(2):
	v = {}
	exec(open(sys.argv[1]).read(), {}, v)
print("missing=" + ",".join(v["CLANG_UBSAN_MISSING"]))' "$DEV_DIR/site-asan.py" 2>&1
}

# Test that site-asan.py probes g++ once per process, and that without g++
# it lists every Clang-only check as missing, with its note, no traceback.
test_site_asan_probes_gxx_once_and_survives_without_it()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	mkdir "$d/bin" "$d/none"
	printf '#!/bin/sh
echo probe >> "%s/calls"
exit 1
' "$d" > "$d/bin/g++"
	chmod +x "$d/bin/g++"
	out=$(site_asan_twice "$d/bin")
	assert_contains "$out" "missing=implicit-conversion,local-bounds,nullability"
	assert_eq "$(wc -l < "$d/calls" | tr -d ' ')" 3 "g++ probes: $(cat "$d/calls")"
	out=$(site_asan_twice "$d/none")
	assert_not_contains "$out" "Traceback"
	assert_contains "$out" "site-asan.py: g++ lacks -fsanitize=implicit-conversion,local-bounds,nullability"
	assert_contains "$out" "missing=implicit-conversion,local-bounds,nullability"
}

# Test, in a real build of a throwaway clone (slow, opt-in with
# DXX_BUILD_TESTS=1), that a -Wconversion violation fails the compile of a
# fork file and the same code in an upstream file still builds; that a fork
# file includes timer.h, vecmat.h and byteutil.h (between upstream_begin.h
# and upstream_end.h) and Boost.Test cleanly; and that fork code narrowing
# through an upstream macro or instantiating an upstream template still
# fails, and through the macro also between upstream_begin.h and
# upstream_end.h.
test_strict_warnings_fail_fork_file_in_real_build()
{
	local d r out rc probe='int dxx_strict_probe(long); int dxx_strict_probe(long v) { return v; }'
	[[ ${DXX_BUILD_TESTS-} == 1 ]] || skip "builds D2X objects; set DXX_BUILD_TESTS=1 to run"
	command -v scons >/dev/null || skip "scons not found"
	# A real build needs the whole source tree (SConstruct, common/,
	# similar/), which make_repo and fork_repo do not hold, so this is a
	# clone of this repository.  --shared reads its objects in place and
	# writes nothing to it; every change below is made in the clone, under
	# the scratch directory.
	d=$(make_scratch) || die "make_scratch failed"
	r=$d/clone
	tgit -C "$d" clone -q --shared "$REPO_ROOT" "$r" || die "clone failed"
	rm -rf "$r/tools/dev" && cp -R "$DEV_DIR" "$r/tools/dev"
	printf '%s\n' "$probe" > "$r/tools/strictprobe.cpp"
	printf '%s\n' "$probe" >> "$r/common/misc/strutil.cpp"
	cat > "$r/tools/strictuser.cpp" <<-'EOF'
	// clang-format off
	#include "tools/dev/upstream_begin.h"
	#include "timer.h"
	#include "vecmat.h"
	#include "byteutil.h"
	#include "tools/dev/upstream_end.h"
	// clang-format on
	#include <boost/test/unit_test.hpp>
	int dxx_strict_user();
	int dxx_strict_user() { return 0; }
	EOF
	cat > "$r/common/include/strictmacro.h" <<-'EOF'
	#pragma once
	#define DXX_STRICT_STORE(dst, v) ((dst) = (v))
	template <class T> int dxx_strict_conv(T t) { return t; }
	EOF
	cat > "$r/tools/strictmacro.cpp" <<-'EOF'
	#include "strictmacro.h"
	int dxx_strict_macro(long v);
	int dxx_strict_macro(long v)
	{
		int i = 0;
		DXX_STRICT_STORE(i, v);
		unsigned u = 0;
		DXX_STRICT_STORE(u, i);
		return i + static_cast<int>(u) + dxx_strict_conv(v);
	}
	EOF
	{ printf '// clang-format off\n#include "tools/dev/upstream_begin.h"\n'
		printf '#include "strictmacro.h"\n#include "tools/dev/upstream_end.h"\n'
		sed 1d "$r/tools/strictmacro.cpp"; } > "$r/tools/strictwrapped.cpp"
	awk -v anchor="'common/misc/strutil.cpp'," \
		-v probe="'tools/strictprobe.cpp','tools/strictuser.cpp','tools/strictmacro.cpp','tools/strictwrapped.cpp'," \
		'{print} $0 == anchor {print probe}' "$REPO_ROOT/SConstruct" > "$r/SConstruct"
	grep -qF "'tools/strictprobe.cpp','tools/strictuser.cpp'," "$r/SConstruct" ||
		fail "SConstruct has no strutil.cpp line"
	out=$("$r/tools/dev/build.sh" dev build/dev/common/misc/strutil.o 2>&1)
	assert_eq "$?" 0 "upstream file: $out"
	out=$("$r/tools/dev/build.sh" dev build/dev/tools/strictuser.o 2>&1)
	assert_eq "$?" 0 "fork file including upstream headers and Boost.Test: $(grep -m5 error <<<"$out")"
	out=$("$r/tools/dev/build.sh" dev build/dev/tools/strictprobe.o 2>&1) && rc=0 || rc=$?
	[[ $rc -ne 0 ]] || fail "fork file built: $(tail -5 <<<"$out")"
	assert_contains "$out" "strictprobe.cpp:1:"
	# clang: [-Werror,-Wshorten-64-to-32]; GCC: [-Werror=conversion]
	[[ $out =~ \[-Werror(,-Wshorten-64-to-32|=conversion)\] ]] ||
		fail "no strict conversion error: $(tail -5 <<<"$out")"
	out=$("$r/tools/dev/build.sh" -k dev build/dev/tools/strictmacro.o build/dev/tools/strictwrapped.o 2>&1)
	assert_contains "$out" "strictmacro.cpp:6:"
	assert_contains "$out" "strictmacro.cpp:8:"
	assert_contains "$out" "strictmacro.h:3:"
	assert_contains "$out" "strictwrapped.cpp:9:"
	assert_contains "$out" "strictwrapped.cpp:11:"
}

# Write into directory $1 an SConstruct that registers each program named
# by `progs=a,b` with the check action of SConstruct's
# _register_runtime_test_link_targets, read from the real SConstruct, after
# reading the site files named by `sites=` (comma-separated) as SCons does.
check_action_sconstruct()
{
	cat > "$1/SConstruct" <<EOF
import ast, os, sys
for site in filter(None, ARGUMENTS.get('sites', '').split(',')):
	sys.path.insert(0, os.path.dirname(site))
	exec(open(site).read(), {}, {})
	del sys.path[0]
tree = ast.parse(open('$REPO_ROOT/SConstruct').read())
action = next(ast.literal_eval(f.args.defaults[-1]) for f in ast.walk(tree)
	if isinstance(f, ast.FunctionDef) and f.name == '_register_runtime_test_link_targets')
env = Environment(ENV = os.environ.copy())
for p in ARGUMENTS['progs'].split(','):
	env.AlwaysBuild(env.Command(target = p + '.check', source = p, action = action))
EOF
}

# Test that the check target runs each unit test program through run.sh
# with the dev profile's site file, so a crash report of the run fails the
# target although the program exits 0, and a library that does not resolve
# fails it without launching the program; a passing program passes.
# Without a site file the program runs directly, as upstream's does.  The
# stand-ins leave no real crash report: the crash is a report file written
# into NO_WINDOW_REPORTS_DIR by a child that then ends by USR1.
test_unit_tests_run_through_run_sh()
{
	local d out rc env=()
	command -v scons >/dev/null || skip "scons not found"
	d=$(make_scratch) || die "make_scratch failed"
	mkdir "$d/reports" "$d/bin"
	check_action_sconstruct "$d"
	printf '#!/bin/sh\nexit 0\n' > "$d/bin/pass"
	cat > "$d/bin/crashy" <<EOF
#!/bin/sh
parent=\$\$
sh -c 'printf "{\"app_name\":\"child\"}\n{\"pid\" : %s, \"parentPid\" : %s}\n" \$\$ '\$parent' \
	> "$d/reports/child.ips"; kill -USR1 \$\$'
exit 0
EOF
	chmod +x "$d/bin/pass" "$d/bin/crashy"
	out=$(cd "$d" && scons -Q -n progs=bin/pass 2>&1)
	assert_eq "$out" "bin/pass" "without a site file"
	out=$(cd "$d" && scons -Q -n progs=bin/pass sites="$DEV_DIR/site-dev.py" 2>&1)
	assert_eq "$out" "$DEV_DIR/run.sh bin/pass" "with site-dev.py" || return
	env=(NO_WINDOW_FRONTMOST_CMD="$(fake_sampler "$d" '1 Finder')" NO_WINDOW_REPORTS_DIR="$d/reports")
	out=$(cd "$d" && env "${env[@]}" scons -Q progs=bin/pass sites="$DEV_DIR/site-dev.py" 2>&1)
	assert_eq "$?" 0 "$out"
	out=$(cd "$d" && env "${env[@]}" scons -Q progs=bin/crashy sites="$DEV_DIR/site-dev.py" 2>&1) &&
		rc=0 || rc=$?
	[[ $rc -ne 0 ]] || fail "a crash report passed: $out"
	assert_contains "$out" "new crash report of the run: $d/reports/child.ips"
	[[ $(uname -s) == Darwin ]] || return 0
	command -v cc >/dev/null || skip 'no cc to build the stand-in with a library'
	printf 'int lib_answer(void) { return 42; }\n' > "$d/lib.c"
	printf 'int lib_answer(void);\nint main(void) { return lib_answer() == 42 ? 0 : 1; }\n' > "$d/main.c"
	if ! cc -dynamiclib "$d/lib.c" -install_name @rpath/libanswer.dylib -o "$d/bin/libanswer.dylib" ||
		! cc "$d/main.c" -L"$d/bin" -lanswer -Wl,-rpath,@executable_path -o "$d/bin/unlinked"; then
		fail 'cannot build the stand-in with a library'; return
	fi
	rm "$d/bin/libanswer.dylib"
	out=$(cd "$d" && env "${env[@]}" scons -Q progs=bin/unlinked sites="$DEV_DIR/site-dev.py" 2>&1) &&
		rc=0 || rc=$?
	[[ $rc -ne 0 ]] || fail "an unresolved library passed: $out"
	assert_contains "$out" "unresolved libraries:"
	assert_contains "$out" "@rpath/libanswer.dylib"
}

run_tests

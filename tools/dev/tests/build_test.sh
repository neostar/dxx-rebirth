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

# Print the settings SCons reads from site files "$@", merged in order as
# SCons' Variables does (each file exec'd, later files win), as key=value.
site_settings()
{
	python3 -I -c '
import sys
values = {}
for path in sys.argv[1:]:
	file_values = {"__name__": path}
	exec(open(path).read(), {}, file_values)
	values.update(file_values)
for k in ("builddir", "compilation_database", "debug", "lto", "macos_add_frameworks", "sconf_optimize_builtin_constant_p"):
	print(f"{k}={values.get(k)}")
' "$@"
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

run_tests

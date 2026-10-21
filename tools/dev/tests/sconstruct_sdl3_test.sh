#!/usr/bin/env bash
# SConstruct's sdl3=1 checks find SDL3, SDL3_image and SDL3_mixer installed
# where the compiler searches by default (/usr/include on most distros):
# pkg-config then prints no -I for them, yet upstream's <SDL.h> lives in
# include/SDL3 (ADR 3).  The pinned SDL3 stands in for the system one: a
# synthetic prefix that the compiler searches through CPLUS_INCLUDE_PATH.
# One configure, with the options that only add checks turned off: each
# costs about two minutes in the emulated container.
source "$(dirname "$0")/lib.sh"

# Make a prefix in scratch directory $1 holding the pinned SDL3 stack, with
# .pc files whose include directory pkg-config treats as a system one.
make_system_prefix()
{
	local d=$1 pc
	mkdir -p "$d/sys/include" "$d/sys/lib/pkgconfig"
	cp -R "$dev_deps_prefix/include/SDL3" "$dev_deps_prefix/include/SDL3_image" \
		"$dev_deps_prefix/include/SDL3_mixer" "$d/sys/include/"
	cp -R "$dev_deps_prefix/lib/"libSDL3* "$d/sys/lib/"
	for pc in sdl3:SDL3 sdl3-image:SDL3_image sdl3-mixer:SDL3_mixer; do
		printf '%s\n' "prefix=$d/sys" "includedir=\${prefix}/include" "libdir=\${prefix}/lib" \
			"Name: ${pc%%:*}" 'Description: synthetic' 'Version: 3.4.0' \
			"Libs: -L\${libdir} -Wl,-rpath,\${libdir} -l${pc#*:}" "Cflags: -I\${includedir}" \
			> "$d/sys/lib/pkgconfig/${pc%%:*}.pc"
	done
}

# Configure sdl3=1 from scratch directory $1, the prefix's include
# directory a compiler default; print the log.  pkg-config finds the
# prefix's .pc files first.
configure_sdl3()
{
	local d=$1
	(cd "$REPO_ROOT" && CPLUS_INCLUDE_PATH="$d/sys/include" \
		PKG_CONFIG_PATH="$d/sys/lib/pkgconfig" \
		PKG_CONFIG_SYSTEM_INCLUDE_PATH="$d/sys/include" \
		scons d1x=0 d2x=1 sdl3=1 macos_add_frameworks=0 builddir="$d/b/" \
		lto=0 screenshot=none use_udp=0 use_tracker=0 \
		"$d/b/dxxsconf.h") 2>&1
}

need_tools()
{
	command -v scons >/dev/null || skip 'no scons in PATH'
	[[ -e $dev_deps_prefix/include/SDL3/SDL.h ]] ||
		skip "no pinned SDL3 in $dev_deps_prefix; run tools/dev/deps.sh build"
}

# Test that sdl3=1 finds SDL3 and its libraries in a compiler default
# directory when pkg-config prints no -I for them.
test_system_sdl3_found_through_pkg_config()
{
	local d out
	need_tools
	command -v pkg-config >/dev/null || skip 'no pkg-config in PATH'
	d=$(make_scratch) || die "make_scratch failed"
	make_system_prefix "$d"
	out=$(configure_sdl3 "$d")
	assert_not_contains "$out" "$d/sys/include'" "pkg-config printed the system -I"
	assert_contains "$out" 'checking for usable library SDL3 without OpenGL...yes'
	assert_contains "$out" 'checking for usable library SDL3_image...yes'
	assert_contains "$out" 'checking for usable library SDL3_mixer...yes'
}

run_tests

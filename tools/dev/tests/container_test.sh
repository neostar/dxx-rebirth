#!/usr/bin/env bash
# Smoke test of the Linux reference container (tools/dev/container.sh):
# check.sh passes in it without a network.  It takes minutes, so it runs
# only with DXX_CONTAINER_TESTS=1 (ci.sh's container leg runs the same
# check); it also needs a running Docker daemon and the image for the
# current inputs (container.sh build).  Otherwise it is a SKIP, as it is
# inside the container itself.
source "$(dirname "$0")/lib.sh"
CONTAINER="$DEV_DIR/container.sh"

# Skip unless asked for, Docker runs and the current image exists.
need_image()
{
	[[ ${DXX_CONTAINER_TESTS-} == 1 ]] ||
		skip "takes minutes; set DXX_CONTAINER_TESTS=1 or run tools/dev/ci.sh"
	command -v docker >/dev/null || skip "docker not found; install OrbStack or Docker"
	docker info >/dev/null 2>&1 || skip "Docker daemon not running (orb start)"
	docker image inspect "$("$CONTAINER" tag)" >/dev/null 2>&1 ||
		skip "no image $("$CONTAINER" tag); run tools/dev/container.sh build"
}

# Test that the container has no network.
test_runs_offline()
{
	local out
	need_image
	out=$("$CONTAINER" run bash -c 'cat /sys/class/net/*/operstate; ls /sys/class/net' 2>&1)
	assert_eq "$out" "unknown
lo"
}

# Test that the container's toolchain matches the pins: GCC 14, Clang 19,
# ccache, scons, the legacy SDL libraries, and clangd, clang-format and
# clang-tidy from the pinned LLVM release.
test_toolchain_matches_pins()
{
	local out rc
	need_image
	out=$("$CONTAINER" run tools/dev/doctor.sh 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "ok   llvm 23.1.3"
	assert_contains "$out" "ok   SDL_mixer"
}

# Test that the fast checks pass in the container, offline.
test_fast_checks_pass()
{
	local out rc
	need_image
	out=$("$CONTAINER" check --fast 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "check.sh: fast checks passed"
}

run_tests

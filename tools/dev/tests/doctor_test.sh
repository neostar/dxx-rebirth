#!/usr/bin/env bash
# Command-line behaviour of tools/dev/doctor.sh against fixture manifests
# and fake tools put first on PATH.
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"
DOCTOR="$DEV_DIR/doctor.sh"

# Write an executable fake tool $2 into directory $1 that prints the
# remaining arguments, one per line, whatever it is asked.
fake_tool()
{
	local d=$1 name=$2; shift 2
	printf '#!/bin/sh\n' > "$d/$name"
	printf "echo '%s'\n" "$@" >> "$d/$name"
	chmod +x "$d/$name"
}

# Run doctor.sh with the fake tools in $1 first on PATH, no venv or prefix, on platform $2,
# with the remaining arguments; print its output, return its status.
run_doctor()
{
	local fakes=$1 platform=$2; shift 2
	PATH="$fakes:$PATH" DXX_VENV="$fakes/no-venv" DXX_DEPS_PREFIX="$fakes/no-prefix" DOCTOR_PLATFORM=$platform "$DOCTOR" "$@" 2>&1
}

# Test that a manifest whose tools all match passes.
test_matching_tools_pass()
{
	local d out rc
	d=$(make_scratch)
	fake_tool "$d" cmake 'cmake version 4.4.4' '' 'CMake suite maintained'
	fake_tool "$d" gcc '13.3.0'
	printf 'tool cmake any 4.4.4\ntool gcc linux 13.3.0\n' > "$d/manifest"
	out=$(run_doctor "$d" linux --manifest "$d/manifest") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "ok   cmake 4.4.4"
}

# Test that a tool at another version fails, naming both versions.
test_drifted_tool_fails()
{
	local d out rc
	d=$(make_scratch)
	fake_tool "$d" cmake 'cmake version 4.5.0'
	printf 'tool cmake any 4.4.4\n' > "$d/manifest"
	out=$(run_doctor "$d" darwin --manifest "$d/manifest") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "cmake: found 4.5.0, manifest pins 4.4.4"
}

# Test that a pinned tool missing from PATH fails.
test_missing_tool_fails()
{
	local d out rc
	d=$(make_scratch)
	printf 'tool cmake any 4.4.4\n' > "$d/manifest"
	out=$(PATH="$d:/usr/bin:/bin" DXX_VENV="$d/no-venv" DXX_DEPS_PREFIX="$d/no-prefix" DOCTOR_PLATFORM=linux "$DOCTOR" --manifest "$d/manifest" 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "cmake: not found, manifest pins 4.4.4"
}

# Test that tools pinned for the other platform are not probed.
test_other_platform_ignored()
{
	local d out rc
	d=$(make_scratch)
	fake_tool "$d" gcc '9.9.9'
	printf 'tool gcc linux 13.3.0\n' > "$d/manifest"
	out=$(run_doctor "$d" darwin --manifest "$d/manifest") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_not_contains "$out" "gcc"
}

# Write fake LLVM tools into prefix $1's opt/llvm/bin: clangd and
# clang-format report version $2 and source commit $3, clang-tidy version
# $4 (default $2); bin.sha256 records them as unpacked.
llvm_fixture()
{
	local b=$1/opt/llvm/bin
	mkdir -p "$b"
	fake_tool "$b" clangd "clangd version $2 (https://github.com/llvm/llvm-project $3)"
	fake_tool "$b" clang-format "clang-format version $2 (https://github.com/llvm/llvm-project $3)"
	fake_tool "$b" clang-tidy 'LLVM (http://llvm.org/):' "  LLVM version ${4:-$2}"
	(cd "$1/opt/llvm" && for f in bin/*; do echo "$(dev_sha256 "$f")  $f"; done > bin.sha256)
}

# Test that clangd, clang-format and clang-tidy from one pinned LLVM
# release pass.
test_one_llvm_passes()
{
	local d out rc
	d=$(make_scratch)
	llvm_fixture "$d/prefix" 23.1.3 0d261d1
	printf 'tool llvm any 23.1.3\n' > "$d/manifest"
	out=$(DXX_DEPS_PREFIX="$d/prefix" DXX_VENV="$d/no-venv" DOCTOR_PLATFORM=linux \
		"$DOCTOR" --manifest "$d/manifest" --prefix "$d/prefix" 2>&1) && rc=0 || rc=$?
	assert_contains "$out" "ok   llvm 23.1.3"
}

# Test that LLVM tools of different releases fail.
test_mixed_llvm_fails()
{
	local d out rc
	d=$(make_scratch)
	llvm_fixture "$d/prefix" 23.1.3 0d261d1 22.1.8
	printf 'tool llvm any 23.1.3\n' > "$d/manifest"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --prefix "$d/prefix") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "llvm: found 23.1.3 (clang-tidy is 22.1.8)"
}

# Test that a tool swapped after deps.sh unpacked the release fails, even
# at the pinned version: the binaries must be the release's own.
test_swapped_llvm_tool_fails()
{
	local d out rc
	d=$(make_scratch)
	llvm_fixture "$d/prefix" 23.1.3 0d261d1
	fake_tool "$d/prefix/opt/llvm/bin" clangd 'Homebrew clangd version 23.1.3'
	printf 'tool llvm any 23.1.3\n' > "$d/manifest"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --prefix "$d/prefix") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "llvm: found 23.1.3 (bin/clangd is not the pinned release's)"
}

# Test that tools at one version built from different sources fail.
test_llvm_from_two_sources_fails()
{
	local d out rc
	d=$(make_scratch)
	llvm_fixture "$d/prefix" 23.1.3 0d261d1
	fake_tool "$d/prefix/opt/llvm/bin" clang-format 'clang-format version 23.1.3 (https://example.invalid/llvm abc1234)'
	(cd "$d/prefix/opt/llvm" && for f in bin/*; do echo "$(dev_sha256 "$f")  $f"; done > bin.sha256)
	printf 'tool llvm any 23.1.3\n' > "$d/manifest"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --prefix "$d/prefix") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "llvm: found 23.1.3 (clang-format is from another source)"
}

# Write a fake pkg-config into directory $1 that knows module $2 at
# version $3.
fake_pkg_config()
{
	printf '#!/bin/sh\n[ "$1" = --modversion ] && [ "$2" = %s ] && { echo %s; exit 0; }\nexit 1\n' \
		"$2" "$3" > "$1/pkg-config"
	chmod +x "$1/pkg-config"
}

# Test that a system library at its pinned version passes.
test_matching_pkg_passes()
{
	local d out rc
	d=$(make_scratch)
	fake_pkg_config "$d" physfs 3.2.0
	printf 'pkg physfs darwin 3.2.0\n' > "$d/manifest"
	out=$(run_doctor "$d" darwin --manifest "$d/manifest") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "ok   physfs 3.2.0"
}

# Test that a system library at another version, or missing, fails.
test_drifted_or_missing_pkg_fails()
{
	local d out rc
	d=$(make_scratch)
	fake_pkg_config "$d" physfs 3.2.1
	printf 'pkg physfs darwin 3.2.0\npkg sdl2 darwin 2.32.74\n' > "$d/manifest"
	out=$(run_doctor "$d" darwin --manifest "$d/manifest") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "physfs: found 3.2.1, manifest pins 3.2.0"
	assert_contains "$out" "sdl2: not found, manifest pins 2.32.74"
}

# Test that scons and ccache are probed.
test_scons_and_ccache_probed()
{
	local d out rc
	d=$(make_scratch)
	fake_tool "$d" scons 'SCons by Steven Knight et al.:' '	SCons: v4.11.1.abc, 2026, by bdbaddog'
	fake_tool "$d" ccache 'ccache version 4.14.2' 'Features: file-storage'
	printf 'tool scons darwin 4.11.1\ntool ccache darwin 4.14.2\n' > "$d/manifest"
	out=$(run_doctor "$d" darwin --manifest "$d/manifest") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "ok   scons 4.11.1"
	assert_contains "$out" "ok   ccache 4.14.2"
}

# Write a manifest with one dependency line into $1/manifest.
dep_manifest()
{
	printf 'source SDL3 3.4.18 https://example.invalid/SDL3.tar.gz %s\n' \
		"$(printf 'a%.0s' {1..64})" > "$1/manifest"
}

# Test that a prefix built from other dependency pins fails.
test_stale_prefix_fails()
{
	local d out rc
	d=$(make_scratch)
	dep_manifest "$d"
	mkdir "$d/prefix"
	echo 'source SDL3 3.4.16 https://example.invalid/old.tar.gz 00' > "$d/prefix/deps.stamp"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --prefix "$d/prefix") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "FAIL deps: $d/prefix was built from other pins"
}

# Test that a prefix built from the manifest's pins and today's build
# options passes.
test_current_prefix_passes()
{
	local d out rc
	d=$(make_scratch)
	dep_manifest "$d"
	mkdir "$d/prefix"
	"$DEV_DIR/deps.sh" --manifest "$d/manifest" stamp > "$d/prefix/deps.stamp"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --prefix "$d/prefix") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "ok   deps"
}

# Test that a prefix built with other build options fails.
test_prefix_with_other_build_options_fails()
{
	local d out rc
	d=$(make_scratch)
	dep_manifest "$d"
	mkdir "$d/prefix"
	{ cat "$d/manifest"; echo "options $(printf '0%.0s' {1..64})"; } > "$d/prefix/deps.stamp"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --prefix "$d/prefix") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "FAIL deps: $d/prefix was built from other pins"
}

# Test that a prefix not built yet is a visible SKIP, not a failure.
test_unbuilt_prefix_skips()
{
	local d out rc
	d=$(make_scratch)
	dep_manifest "$d"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --prefix "$d/none") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "SKIP deps: $d/none not built; run tools/dev/deps.sh"
}

# Write a manifest pinning venv Python $2 and a lockfile into $1, a venv
# in $1/venv whose python3 reports $3, and a fake uv whose freeze lists
# $4 (default: the locked packages).
venv_fixture()
{
	printf 'venv python %s\n' "$2" > "$1/manifest"
	printf '%s \\\n    %s\n' '# comment' '' numpy==2.5.4 --hash=sha256:00 \
		shellcheck-py==0.11.0.1 --hash=sha256:11 > "$1/requirements.lock"
	mkdir -p "$1/venv/bin"
	fake_tool "$1/venv/bin" python3 "$3"
	fake_tool "$1" uv "${4:-numpy==2.5.4}" shellcheck-py==0.11.0.1
}

# Test that a venv whose installed packages differ from the lockfile
# fails, even with a current stamp.
test_venv_packages_differ_from_lock_fails()
{
	local d out rc
	d=$(make_scratch)
	venv_fixture "$d" 3.13.16 3.13.16 numpy==2.5.5
	printf 'python 3.13.16\nlock %s\n' "$(dev_sha256 "$d/requirements.lock")" \
		> "$d/venv/venv.stamp"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --venv "$d/venv") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "FAIL venv: installed packages differ from requirements.lock: -numpy==2.5.4 +numpy==2.5.5"
}

# Test that a venv made from the pinned Python and lockfile passes.
test_current_venv_passes()
{
	local d out rc
	d=$(make_scratch)
	venv_fixture "$d" 3.13.16 3.13.16
	printf 'python 3.13.16\nlock %s\n' "$(dev_sha256 "$d/requirements.lock")" \
		> "$d/venv/venv.stamp"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --venv "$d/venv") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "ok   venv python 3.13.16"
}

# Test that a venv made from an older lockfile fails.
test_stale_venv_fails()
{
	local d out rc
	d=$(make_scratch)
	venv_fixture "$d" 3.13.16 3.13.16
	printf 'python 3.13.16\nlock 0000\n' > "$d/venv/venv.stamp"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --venv "$d/venv") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "FAIL venv: $d/venv was made from another lockfile or Python"
}

# Test that a venv whose Python changed under it fails.
test_venv_python_drift_fails()
{
	local d out rc
	d=$(make_scratch)
	venv_fixture "$d" 3.13.16 3.13.17
	printf 'python 3.13.16\nlock %s\n' "$(dev_sha256 "$d/requirements.lock")" \
		> "$d/venv/venv.stamp"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --venv "$d/venv") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "venv python: found 3.13.17, manifest pins 3.13.16"
}

# Test that a venv not made yet is a visible SKIP.
test_missing_venv_skips()
{
	local d out rc
	d=$(make_scratch)
	venv_fixture "$d" 3.13.16 3.13.16
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --venv "$d/none") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "SKIP venv: $d/none not made; run tools/dev/deps.sh venv"
}

run_tests

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
	[[ -e $fakes/df ]] || fake_df "$fakes"
	PATH="$fakes:$PATH" DXX_VENV="$fakes/no-venv" DXX_DEPS_PREFIX="$fakes/no-prefix" \
		DOCKER_HOST=${DOCKER_HOST:-unix://$fakes/no-docker.sock} \
		DOCTOR_PLATFORM=$platform "$DOCTOR" "$@" 2>&1
}

# Write a fake df into directory $1 reporting $DF_FREE_KB (default 100 GiB)
# free, so that no test depends on this machine's disk.
fake_df()
{
	{
		printf '#!/bin/sh\necho "Filesystem 1024-blocks Used Available Capacity Mounted on"\n'
		printf 'echo "/dev/fake 999999999 1 ${DF_FREE_KB:-104857600} 1%% /"\n'
	} > "$1/df"
	chmod +x "$1/df"
}

# Test that a manifest whose tools all match passes.
test_matching_tools_pass()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
	dep_manifest "$d"
	mkdir "$d/prefix"
	{ cat "$d/manifest"; echo "options $(printf '0%.0s' {1..64})"; } > "$d/prefix/deps.stamp"
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --prefix "$d/prefix") && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "FAIL deps: $d/prefix was built from other pins"
}

# Build into directory $1 a dylib $2 with install name $3, linking the
# libraries "${@:4}" (paths); fail the test if cc cannot.
make_dylib()
{
	local d=$1 out=$2 install_name=$3 src; shift 3
	src=$(mktemp "$d/src.XXXXXX") || die "mktemp failed"
	printf 'int f_%s(void) { return 0; }\n' "$RANDOM" > "$src.c"
	cc -dynamiclib "$src.c" -install_name "$install_name" -Wl,-headerpad_max_install_names "$@" -o "$out" ||
		die "cc cannot build $out"
}

# Test that the Mach-O libraries under the prefix's and the venv's lib/
# pass when they load only through @rpath and from /usr/lib and /System,
# and fail, one line per reference, when one names a library outside the
# prefix (also an existing one), an @rpath library that does not resolve,
# or a library in the prefix that does not exist, and when its own install
# name lies outside (whatever links it would record that place).
test_prefix_libraries_reference_only_allowed_places()
{
	local d p out rc
	[[ $(uname -s) == Darwin ]] || skip 'Mach-O libraries are macOS only'
	command -v cc >/dev/null || skip 'no cc to build the libraries'
	d=$(make_scratch) || die "make_scratch failed"
	# The linker records paths normalized: no double slash from TMPDIR.
	d=$(cd "$d" && pwd -P) p=$d/prefix
	mkdir -p "$p/lib/sub" "$d/elsewhere" "$d/venv/lib/site"
	dep_manifest "$d"
	"$DEV_DIR/deps.sh" --manifest "$d/manifest" stamp > "$p/deps.stamp"
	make_dylib "$d" "$p/lib/libgood.dylib" @rpath/libgood.dylib
	make_dylib "$d" "$p/lib/libuser.dylib" @rpath/libuser.dylib "$p/lib/libgood.dylib"
	make_dylib "$d" "$p/lib/sub/libabs.dylib" @rpath/libabs.dylib "$p/lib/libgood.dylib"
	install_name_tool -change @rpath/libgood.dylib "$p/lib/libgood.dylib" "$p/lib/sub/libabs.dylib"
	out=$(run_doctor "$d" darwin --manifest "$d/manifest" --prefix "$p" --venv "$d/venv") &&
		rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "ok   libs: 3 Mach-O libraries"
	make_dylib "$d" "$d/elsewhere/libout.dylib" "$d/elsewhere/libout.dylib"
	make_dylib "$d" "$p/lib/libgone.dylib" @rpath/libgone.dylib
	make_dylib "$d" "$p/lib/libprefixgone.dylib" "$p/lib/libprefixgone.dylib"
	make_dylib "$d" "$p/lib/libbad.dylib" @rpath/libbad.dylib "$d/elsewhere/libout.dylib" \
		"$p/lib/libgone.dylib" "$p/lib/libprefixgone.dylib"
	make_dylib "$d" "$d/venv/lib/site/ext.so" @rpath/ext.so "$d/elsewhere/libout.dylib"
	make_dylib "$d" "$p/lib/libid.dylib" "$d/elsewhere/libid.dylib"
	rm "$p/lib/libgone.dylib" "$p/lib/libprefixgone.dylib"
	out=$(run_doctor "$d" darwin --manifest "$d/manifest" --prefix "$p" --venv "$d/venv") &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAIL libs lib/libbad.dylib: $d/elsewhere/libout.dylib lies outside $p"
	assert_contains "$out" "FAIL libs lib/libbad.dylib: @rpath/libgone.dylib does not resolve"
	assert_contains "$out" "FAIL libs lib/libbad.dylib: $p/lib/libprefixgone.dylib does not exist"
	assert_contains "$out" "FAIL libs lib/site/ext.so: $d/elsewhere/libout.dylib lies outside $d/venv"
	assert_contains "$out" "FAIL libs lib/libid.dylib: install name $d/elsewhere/libid.dylib lies outside $p"
	assert_not_contains "$out" "libuser"
	assert_not_contains "$out" "ok   libs"
}

# Test that a prefix not built yet is a visible SKIP, not a failure.
test_unbuilt_prefix_skips()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
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
	d=$(make_scratch) || die "make_scratch failed"
	venv_fixture "$d" 3.13.16 3.13.16
	out=$(run_doctor "$d" linux --manifest "$d/manifest" --venv "$d/none") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "SKIP venv: $d/none not made; run tools/dev/deps.sh venv"
}

# Write fakes into directory $1 for the machine probes: sysctl answering
# "name=value" pairs given as the remaining arguments (any other name
# fails), df reporting $DF_FREE_KB free, docker listing $DOCKER_IMAGES,
# logging its calls to $1/docker.log, naming $DOCKER_ROOT as its data root
# and reporting what images and build cache take, and a socket
# $1/docker.sock for DOCKER_HOST to name.
fake_machine()
{
	local d=$1 kv; shift
	{
		echo '#!/bin/sh'
		echo 'case $2 in'
		for kv in "$@"; do echo "${kv%%=*}) echo ${kv#*=} ;;"; done
		echo '*) exit 1 ;;'
		echo 'esac'
	} > "$d/sysctl"
	fake_df "$d"
	cat > "$d/docker" <<DOCKER
#!/bin/sh
echo "\$*" >> "$d/docker.log"
case \$1 in
image) printf '%s\n' \${DOCKER_IMAGES-} ;;
info) echo "\${DOCKER_ROOT-/no-such-docker-root}" ;;
system) printf 'Images 13.99GB\nContainers 145MB\nLocal Volumes 5GB\nBuild Cache 14.09GB\n' ;;
esac
DOCKER
	chmod +x "$d/sysctl" "$d/docker"
	python3 -c 'import socket, sys; socket.socket(socket.AF_UNIX).bind(sys.argv[1])' "$d/docker.sock"
}

# Test that doctor reports the machine: jobs from the performance cores,
# RAM and free disk.
test_reports_cores_ram_and_disk()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	unset DXX_JOBS
	fake_machine "$d" hw.perflevel0.physicalcpu=6 hw.ncpu=10 hw.memsize=34359738368
	: > "$d/manifest"
	out=$(run_doctor "$d" darwin --manifest "$d/manifest")
	assert_contains "$out" "info machine: 6 jobs (DXX_JOBS), 32 GiB RAM"
	assert_contains "$out" "ok   disk host 100 GiB free"
	assert_contains "$out" "SKIP disk docker: daemon not running"
}

# Test that without performance levels the jobs are all the CPUs.
test_jobs_fall_back_to_all_cpus()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	unset DXX_JOBS
	fake_machine "$d" hw.ncpu=10 hw.memsize=34359738368
	: > "$d/manifest"
	out=$(run_doctor "$d" darwin --manifest "$d/manifest")
	assert_contains "$out" "info machine: 10 jobs (DXX_JOBS)"
}

# Test that low free disk warns below 40 GiB and fails below 15 GiB.
test_low_disk_warns_then_fails()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	fake_machine "$d" hw.ncpu=10 hw.memsize=34359738368
	: > "$d/manifest"
	out=$(DF_FREE_KB=$((30 * 1048576)) run_doctor "$d" darwin --manifest "$d/manifest") && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "WARN disk host: 30 GiB free"
	out=$(DF_FREE_KB=$((10 * 1048576)) run_doctor "$d" darwin --manifest "$d/manifest") && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAIL disk host: 10 GiB free"
}

# Test that Docker's disk is read only from a daemon that is already
# running (without its socket docker is never called) and never by
# starting a container: df on its data root when that is local, else what
# images and build cache take (docker system df).
test_docker_disk_only_from_running_daemon()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	fake_machine "$d" hw.ncpu=10 hw.memsize=34359738368
	: > "$d/manifest"
	out=$(DOCKER_ROOT=$d DF_FREE_KB=$((10 * 1048576)) \
		DOCKER_HOST="unix://$d/docker.sock" run_doctor "$d" darwin --manifest "$d/manifest") &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAIL disk docker: 10 GiB free"
	out=$(DOCKER_HOST="unix://$d/docker.sock" run_doctor "$d" darwin --manifest "$d/manifest")
	assert_contains "$out" "info disk docker (VM): images 13.99GB, build cache 14.09GB"
	assert_not_contains "$(cat "$d/docker.log")" "run "
	rm "$d/docker.log"
	out=$(DOCKER_IMAGES=dxx-rebirth-ref:a run_doctor "$d" darwin --manifest "$d/manifest")
	assert_contains "$out" "SKIP disk docker: daemon not running"
	[[ ! -e $d/docker.log ]] || fail "docker called without a running daemon: $(cat "$d/docker.log")"
}

# Test that more than one reference image warns.
test_counts_reference_images()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	fake_machine "$d" hw.ncpu=10 hw.memsize=34359738368
	: > "$d/manifest"
	export DOCKER_HOST="unix://$d/docker.sock"
	out=$(DOCKER_IMAGES='dxx-rebirth-ref:a dxx-rebirth-ref:b' run_doctor "$d" darwin --manifest "$d/manifest")
	assert_contains "$out" "WARN images: 2 dxx-rebirth-ref images"
	out=$(DOCKER_IMAGES='dxx-rebirth-ref:a' run_doctor "$d" darwin --manifest "$d/manifest")
	assert_contains "$out" "ok   images: 1 dxx-rebirth-ref image"
}

run_tests

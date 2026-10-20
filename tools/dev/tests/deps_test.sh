#!/usr/bin/env bash
# Command-line behaviour of tools/dev/deps.sh against fixture manifests
# whose sources are tiny local CMake projects (file:// URLs and throwaway
# git repositories): the sha256 and commit checks.
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

# Run deps.sh with the cache under $1 and the remaining arguments.
run_deps()
{
	local d=$1; shift
	"$DEPS" --manifest "$d/manifest" --cache "$d/cache" "$@" 2>&1
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
}

run_tests

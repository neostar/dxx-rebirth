# shellcheck shell=bash
# Shared helpers for the shell tests in tools/dev/tests; sourced, not run.
# Each test file defines functions named test_*; run_tests runs them in
# order, reports skipped ones, and exits non-zero if any failed.

DEV_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# shellcheck disable=SC2034  # used by the test files
REPO_ROOT=$(cd "$DEV_DIR/../.." && pwd)
# shellcheck source=../env.sh
source "$DEV_DIR/env.sh"
# A test file sees only the allowlisted environment (env.sh), also when run
# directly: it runs itself again under env -i, once (exec keeps its pid).
if [[ ${DXX_TEST_ENV-} != "$$" && -f $0 ]]; then
	_lib_env=()
	dev_test_env _lib_env
	exec env -i "${_lib_env[@]}" DXX_TEST_ENV=$$ bash "$0" "$@"
fi
# Scripts run from a throwaway copy of tools/dev use this clone's pinned
# tools: the dependency prefix (LLVM's among them) and the venv.
export DXX_DEPS_PREFIX=$dev_deps_prefix DXX_VENV=$dev_venv

# A test may run inside a git hook, where git exports GIT_DIR, GIT_INDEX_FILE
# and friends for the repository being committed to.  Clear them, so every
# git command here acts only on the throwaway repository named by -C.
unset "${dev_git_repo_vars[@]}"

# Scripts under test call plain git; keep the owner's global config (signing
# with 1Password, identity) out of throwaway repositories without writing
# any config file.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid

_failures=0

fail()
{
	printf '    FAIL: %s\n' "$*" >&2
	_test_failed=1
	return 1
}

assert_eq()
{
	[[ "$1" == "$2" ]] || fail "expected '$2', got '$1'${3:+ ($3)}"
}

assert_contains()
{
	[[ "$1" == *"$2"* ]] || fail "expected output to contain '$2', got: $1"
}

assert_not_contains()
{
	[[ "$1" != *"$2"* ]] || fail "expected output not to contain '$2', got: $1"
}

# Every scratch directory lives under one root made here, in the test
# file's own shell, and removed when it exits.  Tests call make_scratch in
# a command substitution, a subshell that cannot report back what it made.
_scratch_root=$(mktemp -d "${TMPDIR:-/tmp}/dxx-dev-test.XXXXXX") || exit 1
trap 'rm -rf "$_scratch_root"' EXIT
# Tests run from the scratch root, outside every repository: a git or a
# script under test given an empty path acts on the current directory, and
# there finds no repository to change.
cd "$_scratch_root" || exit 1

# Fail the current test and stop it.  A test runs in a subshell of its own
# (run_tests), so exit ends the test, not the file.
die()
{
	fail "$@"
	exit 1
}

# Skip the rest of the current test, with a reason, when a prerequisite is
# missing.  run_tests reports it as SKIP; it does not fail the file.
skip()
{
	printf '%s\n' "$*" > "$_scratch_root/skip-reason"
	exit 77
}

# Make an empty scratch directory removed when the test file exits.
make_scratch()
{
	mktemp -d "$_scratch_root/XXXXXX"
}

# git with a test identity and no signing, set per command so that no
# repository's config is ever written.  Every git a test runs goes through
# tgit or real_git (check.sh's test_git step enforces it).  Always pass
# -C <throwaway repo>: anything but a path under the scratch root is
# refused, as `git -C ""` would act on the current directory.  A refusal
# also fails the running test (run_tests), even from a command
# substitution whose status nobody checks.
tgit()
{
	if [[ ${1-} != -C || -z ${2-} ||
		$(cd "$2" 2>/dev/null && pwd -P) != "$(cd "$_scratch_root" && pwd -P)"/* ]]; then
		echo "tgit: refusing '${2-}': pass -C and a scratch repository" >&2
		: > "$_scratch_root/refused"
		return 1
	fi
	local id=(-c user.name=test -c user.email=test@example.invalid -c commit.gpgsign=false)
	git "${id[@]}" "$@" # raw git: the helper
}

# Read-only git on this clone (REPO_ROOT): only the subcommands that
# cannot change a repository are let through.
real_git()
{
	case ${1-} in
	rev-parse|check-ignore|ls-files|ls-tree|cat-file) ;;
	*)
		echo "real_git: refusing '${1-}': only read-only subcommands" >&2
		: > "$_scratch_root/refused"
		return 1 ;;
	esac
	git -C "$REPO_ROOT" "$@" # raw git: the helper
}

# Make a throwaway git repository with one empty commit and print its path.
make_repo()
{
	local d
	d=$(make_scratch) && [[ -d $d ]] || die "make_scratch failed"
	tgit -C "$d" init -q -b main || return
	tgit -C "$d" commit -q --allow-empty -m 'root' || return
	printf '%s\n' "$d"
}

# Make a throwaway repository holding a copy of tools/dev whose fork-dirs
# lists src/, with the fork clang-tidy config in src/ and a compilation
# database for src/ in build/dev (the strict warnings fork files get, the
# pinned SDL3 on the include path), and print its path.
fork_repo()
{
	local r strict args
	r=$(make_repo) || die "make_repo failed"
	read -ra strict <<<"$(python3 "$DEV_DIR/fork_warnings.py")"
	mkdir -p "$r/tools" "$r/src" "$r/common" "$r/build/dev"
	cp -R "$DEV_DIR" "$r/tools/dev"
	printf 'src\n' > "$r/tools/dev/fork-dirs"
	cp "$DEV_DIR/style/clang-tidy" "$r/src/.clang-tidy"
	args=$(printf '"%s", ' c++ -std=c++23 "${strict[@]}" -isystem "$dev_deps_prefix/include" -c)
	printf '[{"directory": "%s", "file": "src/anchor.cpp",\n  "arguments": [%s"src/anchor.cpp"]}]\n' \
		"$r" "$args" > "$r/build/dev/compile_commands.json"
	printf '%s\n' "$r"
}

# Write a fake frontmost-app sampler into directory $1 that reports the
# remaining arguments ("PID NAME") in turn, one per call (the last
# repeats), and print its path.
fake_sampler()
{
	local d=$1; shift
	printf '%s\n' "$@" > "$d/apps"
	printf '0\n' > "$d/n"
	cat > "$d/sampler" <<SAMPLER
#!/usr/bin/env bash
n=\$(cat "$d/n"); echo \$((n + 1)) > "$d/n"
total=\$(wc -l < "$d/apps")
[[ \$n -ge \$total ]] && n=\$((total - 1))
sed -n "\$((n + 1))p" "$d/apps"
SAMPLER
	chmod +x "$d/sampler"
	printf '%s\n' "$d/sampler"
}

# Write a fake frontmost-app sampler into directory $1 that reports an
# app outside the run until the run writes its own pid ($$) into $1/pid,
# then that pid as app "intruder"; print its path.
run_sampler()
{
	local d=$1
	cat > "$d/sampler" <<SAMPLER
#!/usr/bin/env bash
if [[ -s "$d/pid" ]]; then echo "\$(cat "$d/pid") intruder"; else echo "1 Terminal"; fi
SAMPLER
	chmod +x "$d/sampler"
	printf '%s\n' "$d/sampler"
}

# Print "sha256  path" for every file under the directories $2... of
# directory $1, in a fixed order.  A text file is hashed with every
# occurrence of $1 (as given, normalized or resolved) replaced by
# @PREFIX@: pkg-config and CMake files name their prefix by design; a
# binary must not.
tree_sums()
{
	local d=$1 f sub; shift
	sub="s|$d|@PREFIX@|g;s|$(cd "$d" && pwd)|@PREFIX@|g;s|$(cd "$d" && pwd -P)|@PREFIX@|g"
	(cd "$d" && find "$@" -type f | LC_ALL=C sort | while IFS= read -r f; do
		if grep -Iq . "$f"; then
			echo "$(sed "$sub" "$f" | dev_sha256)  $f"
		else
			echo "$(dev_sha256 "$f")  $f"
		fi
	done)
}

run_tests()
{
	local t rc
	for t in $(declare -F | awk '{print $3}' | grep '^test_'); do
		# Asserts record failure in _test_failed, so a later passing
		# assert cannot hide an earlier failing one.
		rm -f "$_scratch_root/refused"
		( _test_failed=0; "$t"; exit "$_test_failed" ) && rc=0 || rc=$?
		if [[ $rc -ne 0 && $rc -ne 77 ]] || [[ -e $_scratch_root/refused ]]; then
			rc=1
		fi
		if [[ $rc -eq 0 ]]; then
			printf '  ok   %s\n' "$t"
		elif [[ $rc -eq 77 ]]; then
			printf '  SKIP %s: %s\n' "$t" "$(cat "$_scratch_root/skip-reason")"
		else
			printf '  FAIL %s\n' "$t"
			_failures=$((_failures + 1))
		fi
	done
	[[ $_failures -eq 0 ]]
}

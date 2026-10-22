#!/usr/bin/env bash
# Reproducible builds (ADR 4), slow and opt-in (DXX_REPRO_TESTS=1): build
# the pinned dependencies of tools/dev/toolchain.pins twice, at two
# prefixes in two directories, from the source cache (deps.sh fetch first),
# and compare every installed file but the stamp.
source "$(dirname "$0")/lib.sh"

# Installed files known to differ between two builds at two prefixes, as
# path globs relative to the prefix, with their cause in ADR 4: FluidSynth
# compiles in its prefix (the default soundfont path).
repro_allowlist=(bin/fluidsynth 'lib/libfluidsynth.*')

# Test that two builds of the pinned dependencies at two prefixes install
# the same files, byte for byte, but for the allowlist.
test_pinned_dependencies_build_reproducibly()
{
	local d t a b line path diffs=()
	[[ ${DXX_REPRO_TESTS-} == 1 ]] || skip "slow; set DXX_REPRO_TESTS=1 to run"
	d=$(make_scratch) || die "make_scratch failed"
	for t in one two; do
		"$DEV_DIR/deps.sh" --cache "$dev_deps_cache" --prefix "$d/$t/deps-$t" build \
			> "$d/$t.log" 2>&1 || { tail -n 20 "$d/$t.log" >&2; fail "build $t failed"; return; }
	done
	a=$(tree_sums "$d/one/deps-one" . | grep -v '  ./deps.stamp$')
	b=$(tree_sums "$d/two/deps-two" . | grep -v '  ./deps.stamp$')
	[[ -n $a ]] || fail "nothing installed"
	while IFS= read -r line; do
		path=${line#*  ./}
		for t in "${repro_allowlist[@]+"${repro_allowlist[@]}"}"; do
			# shellcheck disable=SC2053  # the allowlist holds globs
			[[ $path == $t ]] && continue 2
		done
		diffs+=("$path")
	done < <(diff <(echo "$a") <(echo "$b") | sed -n 's/^[<>] //p' | sort -u -k2)
	[[ ${#diffs[@]} -eq 0 ]] || fail "not reproducible: ${diffs[*]}"
}

run_tests

#!/usr/bin/env bash
# Command-line behaviour of tools/shaders/generate.sh and tools/shaders/check.sh
# (R9): headers generated from HLSL are rebuilt byte for byte, and any
# difference fails.  SPIR-V and MSL come from the pinned shadercross (a SKIP
# without it); DXIL from a fake DXC here, as the pinned one runs on Linux
# x86-64 only: the reference container's check step runs the real one.
source "$(dirname "$0")/lib.sh"
SHADERS="$REPO_ROOT/tools/shaders"
SHADERCROSS="$dev_deps_prefix/bin/shadercross"

# Print a shader directory with the vertex and fragment sources of
# shaders/hlsl and no generated headers yet.
make_shader_dir()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$d/hlsl"
	cp "$REPO_ROOT"/shaders/hlsl/*.hlsl "$d/hlsl/"
	printf '%s\n' "$d"
}

# Write a fake dxc into directory $1: it writes a fixed DXBC-like blob,
# named after the profile, to the file after -Fo, and logs its arguments.
fake_dxc()
{
	cat > "$1/dxc" <<FAKE
#!/usr/bin/env bash
echo "\$*" >> "$1/dxc.log"
out= profile=
while [[ \$# -gt 0 ]]; do
	case \$1 in
	-Fo) out=\$2; shift ;;
	-T) profile=\$2; shift ;;
	esac
	shift
done
printf 'DXBC fake %s' "\$profile" > "\$out"
FAKE
	chmod +x "$1/dxc"
}

need_shadercross()
{
	[[ -x $SHADERCROSS ]] || skip "shadercross not found at $SHADERCROSS; run tools/dev/deps.sh build"
}

# Test that generate.sh rejects a source whose name has more dots than
# <name>.<stage>.hlsl, naming it, before it needs any tool: its header name
# would not map back to it.
test_generate_rejects_dotted_name()
{
	local d out rc
	d=$(make_shader_dir) || die "make_shader_dir failed"
	cp "$d/hlsl/pos_color_uv.vert.hlsl" "$d/hlsl/a.b.vert.hlsl"
	out=$(DXX_SHADERCROSS=/nonexistent/shadercross "$SHADERS/generate.sh" "$d" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "generate.sh: a.b.vert.hlsl: name it <name>.vert.hlsl or <name>.frag.hlsl"
	assert_eq "$(ls "$d")" "hlsl"
}

# Test that generate.sh writes one header per HLSL source, named after it,
# compiling DXIL with the stage's 6.0 profile.
test_generate_writes_one_header_per_source()
{
	local d bin out
	need_shadercross
	d=$(make_shader_dir) || die "make_shader_dir failed"; bin=$(make_scratch) || die "make_scratch failed"
	fake_dxc "$bin"
	out=$(DXX_DXC="$bin/dxc" "$SHADERS/generate.sh" "$d" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_eq "$(cd "$d/generated" && ls)" "$(printf 'pos_color_uv_vert.h\ntex_modulate_frag.h')"
	assert_contains "$(cat "$bin/dxc.log")" "-T vs_6_0 -E main"
	assert_contains "$(cat "$bin/dxc.log")" "-T ps_6_0 -E main"
	assert_contains "$(cat "$d/generated/tex_modulate_frag.h")" "std::uint32_t samplers{1};"
}

# Test that check.sh passes on headers that generate.sh just wrote.
test_check_passes_on_fresh_headers()
{
	local d bin out
	need_shadercross
	d=$(make_shader_dir) || die "make_shader_dir failed"; bin=$(make_scratch) || die "make_scratch failed"
	fake_dxc "$bin"
	DXX_DXC="$bin/dxc" "$SHADERS/generate.sh" "$d" >/dev/null 2>&1 || die "generate.sh failed"
	out=$(DXX_DXC="$bin/dxc" "$SHADERS/check.sh" "$d" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "ok   pos_color_uv_vert.h"
}

# Test that a header differing from the rebuild in one byte fails, named.
test_check_fails_on_changed_header()
{
	local d bin out rc
	need_shadercross
	d=$(make_shader_dir) || die "make_shader_dir failed"; bin=$(make_scratch) || die "make_scratch failed"
	fake_dxc "$bin"
	DXX_DXC="$bin/dxc" "$SHADERS/generate.sh" "$d" >/dev/null 2>&1 || die "generate.sh failed"
	sed 's/0x03, 0x02, 0x23, 0x07/0x03, 0x02, 0x23, 0x08/' "$d/generated/pos_color_uv_vert.h" > "$d/x"
	mv "$d/x" "$d/generated/pos_color_uv_vert.h"
	out=$(DXX_DXC="$bin/dxc" "$SHADERS/check.sh" "$d" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAIL pos_color_uv_vert.h"
	assert_contains "$out" "ok   tex_modulate_frag.h"
}

# Test that a source without its header and a header without its source
# both fail.
test_check_fails_on_missing_and_stale_headers()
{
	local d bin out rc
	need_shadercross
	d=$(make_shader_dir) || die "make_shader_dir failed"; bin=$(make_scratch) || die "make_scratch failed"
	fake_dxc "$bin"
	DXX_DXC="$bin/dxc" "$SHADERS/generate.sh" "$d" >/dev/null 2>&1 || die "generate.sh failed"
	rm "$d/generated/tex_modulate_frag.h"
	cp "$d/generated/pos_color_uv_vert.h" "$d/generated/old_vert.h"
	out=$(DXX_DXC="$bin/dxc" "$SHADERS/check.sh" "$d" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAIL tex_modulate_frag.h: missing"
	assert_contains "$out" "FAIL old_vert.h: no source"
}

# Test that without DXC, check.sh still rebuilds SPIR-V and MSL, keeps the
# committed DXIL with a SKIP naming dxc, and fails on a changed MSL byte.
test_check_without_dxc_checks_spirv_and_msl()
{
	local d bin out rc
	need_shadercross
	d=$(make_shader_dir) || die "make_shader_dir failed"; bin=$(make_scratch) || die "make_scratch failed"
	fake_dxc "$bin"
	DXX_DXC="$bin/dxc" "$SHADERS/generate.sh" "$d" >/dev/null 2>&1 || die "generate.sh failed"
	out=$(DXX_DXC="$bin/none" "$SHADERS/check.sh" "$d" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP dxil: dxc not found"
	# The MSL's "using namespace metal;" starts 0x75, 0x73, 0x69, 0x6e, 0x67.
	sed 's/0x75, 0x73, 0x69, 0x6e, 0x67/0x75, 0x73, 0x69, 0x6e, 0x68/' \
		"$d/generated/tex_modulate_frag.h" > "$d/x"
	mv "$d/x" "$d/generated/tex_modulate_frag.h"
	out=$(DXX_DXC="$bin/none" "$SHADERS/check.sh" "$d" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAIL tex_modulate_frag.h"
}

# Test that in the reference image a missing dxc fails instead of a SKIP.
test_check_without_dxc_fails_in_reference_image()
{
	local d bin out rc
	need_shadercross
	d=$(make_shader_dir) || die "make_shader_dir failed"; bin=$(make_scratch) || die "make_scratch failed"
	fake_dxc "$bin"
	DXX_DXC="$bin/dxc" "$SHADERS/generate.sh" "$d" >/dev/null 2>&1 || die "generate.sh failed"
	out=$(DXX_REFERENCE_IMAGE=1 DXX_DXC="$bin/none" "$SHADERS/check.sh" "$d" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAIL dxil: dxc not found"
}

# Test that a missing shadercross is a visible SKIP naming it, and a
# failure in the reference image, which has every pinned tool.
test_missing_shadercross_named()
{
	local d out rc
	d=$(make_shader_dir) || die "make_shader_dir failed"
	out=$(DXX_SHADERCROSS=/nonexistent/shadercross "$SHADERS/check.sh" "$d" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP shaders: shadercross not found at /nonexistent/shadercross"
	out=$(DXX_REFERENCE_IMAGE=1 DXX_SHADERCROSS=/nonexistent/shadercross \
		"$SHADERS/check.sh" "$d" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "FAIL shaders: shadercross not found at /nonexistent/shadercross"
}

# Test that check.sh probes the Metal compiler by running it: a metal that
# xcrun finds (Xcode's shim) but that cannot run is a SKIP naming it.
test_check_skips_metal_that_cannot_run()
{
	local d bin out
	need_shadercross
	[[ $dev_os == darwin ]] || skip "macOS only: the Metal compiler is Xcode's"
	d=$(make_shader_dir) || die "make_shader_dir failed"; bin=$(make_scratch) || die "make_scratch failed"
	fake_dxc "$bin"
	DXX_DXC="$bin/dxc" "$SHADERS/generate.sh" "$d" >/dev/null 2>&1 || die "generate.sh failed"
	printf '#!/bin/sh\n[ "$*" = "-f metal" ] && echo /shim/metal && exit 0\nexit 1\n' > "$bin/xcrun"
	chmod +x "$bin/xcrun"
	out=$(PATH="$bin:$PATH" DXX_DXC="$bin/dxc" "$SHADERS/check.sh" "$d" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP metal: xcrun metal cannot run"
}

# Print a throwaway tree holding a copy of tools/shaders, env.sh, the HLSL
# sources and stand-ins for the committed headers ("committed"), and a fake
# container.sh that runs script $1.  build.sh then takes the container path
# (DXX_DXC names no dxc).
make_build_tree()
{
	local t
	t=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$t/tools/dev" "$t/shaders/generated"
	cp -R "$SHADERS" "$t/tools/"
	cp "$DEV_DIR/env.sh" "$t/tools/dev/"
	cp -R "$REPO_ROOT/shaders/hlsl" "$t/shaders/"
	for h in pos_color_uv_vert.h tex_modulate_frag.h old_vert.h; do
		printf 'committed\n' > "$t/shaders/generated/$h"
	done
	printf '#!/usr/bin/env bash\n%s\n' "$1" > "$t/tools/dev/container.sh"
	chmod +x "$t/tools/dev/container.sh"
	printf '%s\n' "$t"
}

# Test that build.sh leaves the committed headers alone when the container
# fails, and when it delivers an incomplete set of headers.
test_build_keeps_headers_on_failure()
{
	local t out rc
	t=$(make_build_tree 'echo "container: no image" >&2; exit 1') || die "make_build_tree failed"
	# The pipe's status is tar's: 1 from BSD tar, 2 from GNU tar.
	out=$(DXX_DXC=/nonexistent/dxc "$t/tools/shaders/build.sh" 2>&1) && rc=0 || rc=$?
	[[ $rc -ne 0 ]] || fail "build.sh passed: $out"
	assert_eq "$(cat "$t"/shaders/generated/*.h)" "$(printf 'committed\n%.0s' 1 2 3)"
	t=$(make_build_tree "cd '$REPO_ROOT/shaders/generated' && tar -cf - ./pos_color_uv_vert.h") ||
		die "make_build_tree failed"
	out=$(DXX_DXC=/nonexistent/dxc "$t/tools/shaders/build.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "tex_modulate_frag.h"
	assert_eq "$(cat "$t"/shaders/generated/*.h)" "$(printf 'committed\n%.0s' 1 2 3)"
}

# Test that build.sh replaces the headers with a complete set from the
# container and removes a header whose source is gone.
test_build_replaces_headers_from_container()
{
	local t out
	t=$(make_build_tree "cd '$REPO_ROOT/shaders/generated' && tar -cf - ./*.h") ||
		die "make_build_tree failed"
	out=$(DXX_DXC=/nonexistent/dxc "$t/tools/shaders/build.sh" 2>&1)
	assert_eq "$?" 0 "$out"
	assert_eq "$(cd "$t/shaders/generated" && ls)" "$(printf 'pos_color_uv_vert.h\ntex_modulate_frag.h')"
	cmp -s "$t/shaders/generated/tex_modulate_frag.h" "$REPO_ROOT/shaders/generated/tex_modulate_frag.h" ||
		fail "tex_modulate_frag.h is not the container's"
}

run_tests

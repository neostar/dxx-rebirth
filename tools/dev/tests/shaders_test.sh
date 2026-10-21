#!/usr/bin/env bash
# Command-line behaviour of tools/shaders/generate.sh: headers generated
# from HLSL.  SPIR-V and MSL come from the pinned shadercross (a SKIP
# without it); DXIL from a fake DXC here, as the pinned one runs on Linux
# x86-64 only.
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

run_tests

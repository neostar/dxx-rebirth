# shellcheck shell=bash
# Shared by the scripts in tools/shaders, after tools/dev/env.sh; sourced,
# not run.  The pinned tools (tools/dev/toolchain.pins) in the dependency
# prefix, DXX_SHADERCROSS and DXX_DXC overriding them, and the mapping
# between an HLSL source and its generated header.
# shellcheck disable=SC2034  # used by the scripts that source this file
shadercross=${DXX_SHADERCROSS:-$dev_deps_prefix/bin/shadercross}
# shellcheck disable=SC2034
dxc=${DXX_DXC:-$dev_deps_prefix/opt/dxc/bin/dxc}

# Print the header that HLSL source $1, a bare file name, compiles to:
# <name>.<vert|frag>.hlsl gives <name>_<vert|frag>.h.  Fail, naming the file,
# on any other name: <name> is lower snake_case, without dots, so that
# shader_source maps the header back to it.
shader_header()
{
	if [[ ! $1 =~ ^([a-z0-9_]+)\.(vert|frag)\.hlsl$ ]]; then
		echo "${0##*/}: $1: name it <name>.vert.hlsl or <name>.frag.hlsl," \
			"<name> in lower snake_case (a-z, 0-9, _)" >&2
		return 1
	fi
	printf '%s_%s.h\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
}

# Print the HLSL source, a bare file name, that header $1 is compiled from.
shader_source()
{
	local base=${1%.h}
	printf '%s.%s.hlsl\n' "${base%_*}" "${base##*_}"
}

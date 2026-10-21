"""Write and read the generated shader headers (shaders/generated/*.h).

  embed.py render NAME SOURCE STAGE SPIRV MSL DXIL JSON OUT
      write header OUT for shader NAME (pos_color_uv_vert) compiled from
      SOURCE (shown in the header's comment) for STAGE (vertex or fragment):
      the three blobs as byte arrays, and the resource counts from
      shadercross's JSON reflection.
  embed.py extract HEADER FORMAT OUT
      write the bytes of array FORMAT (spirv, msl or dxil) of HEADER to OUT.

The header's layout is fixed here, so the same blobs always give the same
bytes (R9), on any platform.
"""
import json
import re
import sys
from pathlib import Path

BANNER = """/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
"""
FORMATS = ("spirv", "msl", "dxil")
COUNTS = ("samplers", "storage_textures", "storage_buffers", "uniform_buffers")
PER_LINE = 16


def byte_array(name: str, data: bytes) -> str:
    lines = [
        "\t" + " ".join(f"0x{b:02x}," for b in data[i : i + PER_LINE])
        for i in range(0, len(data), PER_LINE)
    ]
    body = "\n".join(lines)
    # Double braces: GCC's -Wmissing-braces wants the inner C array's own.
    return f"inline constexpr std::array<std::uint8_t, {len(data)}> {name}{{{{\n{body}\n}}}};\n"


def render(name, source, stage, spirv, msl, dxil, reflection, out):
    blobs = {f: Path(p).read_bytes() for f, p in zip(FORMATS, (spirv, msl, dxil))}
    counts = json.loads(Path(reflection).read_text())
    if stage not in ("vertex", "fragment"):
        sys.exit(f"embed.py: unknown stage {stage}")
    parts = [
        BANNER,
        "/*\n"
        f" * Generated from {source} by tools/shaders/generate.sh: do not edit.\n"
        " * SPIR-V, MSL and the resource counts come from the pinned SDL_shadercross, DXIL\n"
        " * from the pinned DXC.  tools/shaders/build.sh rebuilds it, and\n"
        " * tools/shaders/check.sh checks that a rebuild gives the same bytes (R9).\n"
        " */\n",
        "#pragma once\n\n",
        "#include <array>\n#include <cstdint>\n#include <string_view>\n\n",
        "// clang-format off\n",
        f"namespace dcx::shaders::{name} {{\n\n",
        "/* The stage SDL_CreateGPUShader is given. */\n",
        f'inline constexpr std::string_view stage{{"{stage}"}};\n\n',
        "/* The entry points: the HLSL main, which SPIRV-Cross renames main0 in MSL. */\n",
        'inline constexpr std::string_view spirv_entry_point{"main"};\n',
        'inline constexpr std::string_view msl_entry_point{"main0"};\n',
        'inline constexpr std::string_view dxil_entry_point{"main"};\n\n',
        "/* The resource counts SDL_CreateGPUShader is given, from shadercross's reflection. */\n",
    ]
    for c in COUNTS:
        parts.append(f"inline constexpr std::uint32_t {c}{{{int(counts[c])}}};\n")
    parts.append("\n")
    for f in FORMATS:
        parts.append(byte_array(f, blobs[f]) + "\n")
    parts.append("}\n// clang-format on\n")
    Path(out).write_text("".join(parts))


def extract(header, fmt, out):
    text = Path(header).read_text()
    m = re.search(
        r"std::array<std::uint8_t, (\d+)> " + re.escape(fmt) + r"\{\{\n(.*?)\n\}\};", text, re.S
    )
    if not m:
        sys.exit(f"embed.py: no {fmt} array in {header}")
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", m.group(2)))
    if len(data) != int(m.group(1)):
        sys.exit(f"embed.py: {fmt} array in {header} holds {len(data)} bytes, not {m.group(1)}")
    Path(out).write_bytes(data)


def main(argv):
    if len(argv) == 10 and argv[1] == "render":
        render(*argv[2:])
    elif len(argv) == 5 and argv[1] == "extract" and argv[3] in FORMATS:
        extract(*argv[2:])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)

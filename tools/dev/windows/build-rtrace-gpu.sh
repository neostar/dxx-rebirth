#!/usr/bin/env bash
# Cross-build rtrace-gpu.exe (R3) with MinGW-w64, in the cross stage of
# tools/dev/windows/Dockerfile; tools/dev/windows.sh runs it there.
#
#   build-rtrace-gpu.sh SRC OUT
#
# SRC is the source tree, OUT an empty or missing directory: OUT gets
# rtrace-gpu.exe and beside it SDL3.dll and WinPixEventRuntime.dll (SDL's
# D3D12 backend warns without it), with the latter's MIT license.  The sources are SConstruct's own
# list for rtrace-gpu (read from it, so the two never drift); SCons itself
# is not used, as its configure wants the whole game's dependencies.
# libstdc++, libgcc and winpthread are linked in statically: SDL3.dll is
# the DLLs beside the exe are SDL's.
set -eu
[[ $# -eq 2 && -n $1 && -n $2 ]] || { echo "usage: $0 SRC OUT" >&2; exit 2; }
src=$1 out=$2
sdl=${SDL3_WIN64:-/opt/sdl3-win64}
[[ -f $src/SConstruct ]] || { echo "build-rtrace-gpu: no SConstruct in $src" >&2; exit 1; }
[[ -f $sdl/bin/SDL3.dll && -f $sdl/lib/libSDL3.dll.a ]] ||
	{ echo "build-rtrace-gpu: no SDL3 cross-build in $sdl" >&2; exit 1; }
pix=${WINPIX:-/opt/winpix}
[[ -f $pix/WinPixEventRuntime.dll && -f $pix/license.txt ]] ||
	{ echo "build-rtrace-gpu: no WinPixEventRuntime.dll in $pix" >&2; exit 1; }

# SConstruct's lines from RuntimeTest('rtrace-gpu', ( to the next )),.
mapfile -t sources < <(awk '
	/RuntimeTest\(.rtrace-gpu., \(/ { on = 1; next }
	on && /^[[:space:]]*\)\),/ { exit }
	on { if (match($0, /[A-Za-z0-9_\/.-]+\.cpp/)) print substr($0, RSTART, RLENGTH) }' "$src/SConstruct")
[[ ${#sources[@]} -gt 0 ]] ||
	{ echo "build-rtrace-gpu: found no rtrace-gpu sources in SConstruct" >&2; exit 1; }

mkdir -p "$out"
obj=$(mktemp -d)
trap 'rm -rf "$obj"' EXIT
cxx=x86_64-w64-mingw32-g++
"$cxx" --version | head -n 1
objects=()
for s in "${sources[@]}"; do
	o=$obj/$(echo "$s" | tr / _).o
	echo "build-rtrace-gpu: $s"
	"$cxx" -std=c++23 -O2 -g0 -Wall -Wextra -Werror -DSDL_ENABLE_OLD_NAMES \
		-I"$src" -I"$src/common/include" -I"$sdl/include" -I"$sdl/include/SDL3" \
		-c "$src/$s" -o "$o"
	objects+=("$o")
done
"$cxx" -o "$out/rtrace-gpu.exe" "${objects[@]}" "$sdl/lib/libSDL3.dll.a" -ld3d12 \
	-static -mconsole
cp "$sdl/bin/SDL3.dll" "$pix/WinPixEventRuntime.dll" "$out/"
cp "$pix/license.txt" "$out/WinPixEventRuntime-license.txt"
echo "build-rtrace-gpu: built $out/rtrace-gpu.exe from ${#sources[@]} sources"

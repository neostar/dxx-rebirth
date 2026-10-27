#!/usr/bin/env bash
# The Windows cross-build of rtrace-gpu (#49) and its D3D12 check under
# Wine, in tools/dev/windows/Dockerfile.
#
#   windows.sh build OUT   cross-build rtrace-gpu.exe and its DLLs into OUT
#   windows.sh wine OUT    run rtrace-d3d12.sh on OUT's build under x64 Wine,
#                          D3D12 through vkd3d-proton on lavapipe
#
# The image is built from toolchain.pins (base image, apt snapshot, SDL3)
# and tagged by a hash of the Dockerfile and the pins, so a stale one is
# never used; it stays local, never pushed.  The .github workflow
# d3d12-check.yml runs `build` on its ubuntu-latest runner.  Both run the
# container with --network none, the source tree mounted read-only.  The
# wine run draws to an Xvfb inside the container (Wine's DXGI needs a
# display); nothing reaches the host's screen and nothing plays sound.
# Wine has no D3D12 debug layer, so its run fails on the missing
# validation (gpu-validate.sh) even when every frame hash matches: it
# proves the build loads and renders, the GitHub run proves the rest.
# The Docker daemon must be running (on the Mac: `orb start`).
set -eu
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
pins=$dev_dir/toolchain.pins

usage() { echo "usage: $0 build|wine OUT" >&2; exit 2; }
[[ $# -eq 2 && -n $2 ]] || usage
mode=$1
case $mode in build|wine) ;; *) usage ;; esac
mkdir -p "$2"
out=$(cd "$2" && pwd)

# Field $3 of the line `$1 $2 ...` in the pins; fails when there is none.
pin()
{
	local v
	v=$(awk -v k="$1" -v n="$2" -v f="$3" '$1 == k && $2 == n { print $f; exit }' "$pins")
	[[ -n $v ]] || { echo "windows.sh: toolchain.pins has no $1 $2" >&2; return 1; }
	echo "$v"
}
base=$(pin image ubuntu 3)
snapshot=$(pin image ubuntu-snapshot 3)
sdl_url=$(pin source SDL3 4)
sdl_sha=$(pin source SDL3 5)
if command -v sha256sum >/dev/null; then sum=(sha256sum); else sum=(shasum -a 256); fi
hash=$(cat "$dev_dir/windows/Dockerfile" "$pins" | "${sum[@]}" | cut -c1-12)
target=cross
[[ $mode == wine ]] && target=wine
image=dxx-rebirth-win-$target:$hash

docker build --platform linux/amd64 -q --target "$target" -t "$image" \
	--build-arg BASE="$base" --build-arg SNAPSHOT="$snapshot" \
	--build-arg SDL_URL="$sdl_url" --build-arg SDL_SHA256="$sdl_sha" \
	"$dev_dir/windows" >/dev/null

run=(docker run --rm --platform linux/amd64 --network none
	-v "$root:/src:ro" -v "$out:/out" "$image")
case $mode in
	build)
		"${run[@]}" bash /src/tools/dev/windows/build-rtrace-gpu.sh /src /out ;;
	wine)
		[[ -f $out/rtrace-gpu.exe && -f $out/SDL3.dll && -f $out/WinPixEventRuntime.dll ]] ||
			{ echo "windows.sh: no rtrace-gpu.exe and its DLLs in $out; run windows.sh build first" >&2; exit 1; }
		# vkd3d-proton's d3d12.dll and d3d12core.dll beside the exe, ahead
		# of Wine's builtin d3d12 (vkd3d 1.10, which refuses DXIL).
		# shellcheck disable=SC2016  # the container's shell expands it
		"${run[@]}" bash -c '
			set -eu
			d=/tmp/rtrace-d3d12
			mkdir -p "$d"
			cp /out/rtrace-gpu.exe /out/SDL3.dll /out/WinPixEventRuntime.dll \
				/opt/vkd3d-proton/x64/d3d12.dll \
				/opt/vkd3d-proton/x64/d3d12core.dll "$d/"
			Xvfb :99 -screen 0 640x480x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
			export DISPLAY=:99 WINEDLLOVERRIDES="mscoree,mshtml=;d3d12,d3d12core=n"
			sleep 1
			wine cmd /c exit >/dev/null 2>&1
			rc=0
			/src/tools/dev/rtrace-d3d12.sh "$d" wine || rc=$?
			wineserver -k || true
			exit "$rc"' ;;
esac

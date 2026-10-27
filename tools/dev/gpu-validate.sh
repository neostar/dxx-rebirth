#!/usr/bin/env bash
# Run an SDL_GPU program with every GPU validation on and fail on any
# validation message (ADR 5).
#
#   gpu-validate.sh CMD [ARG...]
#
# Turns on Metal API validation (MTL_DEBUG_LAYER=1, logging instead of
# aborting), Metal shader validation (MTL_SHADER_VALIDATION=1, reporting to
# stderr instead of os_log), the D3D12 debug layer (SDL's debug mode loads
# it when Windows' Graphics Tools feature is installed and logs its
# messages as errors and warnings) and the Vulkan validation layer's errors,
# warnings and performance warnings, with synchronization and thread-safety
# validation (vk_layer_settings.txt); SDL's GPU debug mode is CMD's own: it
# creates its device with debug_mode true, which on Vulkan also loads the
# layer, and prints the line `sdlgpu: debug mode on` (sdlgpu::device logs
# it), which a run with an SDL_GPU device fails without.  SDL assertions log and continue
# (SDL_ASSERT=ignore), never a dialog; SDL logs warnings, and the GPU
# category its driver lines.  Neither SDL nor the layers change the exit
# status (spike #55, K4), so CMD's output is scanned.  Prints the output,
# then fails with exit status 1, naming the source, on:
#   - SDL's GPU debug mode: a WARNING, ERROR or CRITICAL log line;
#   - the driver's validation layer: Metal's "... Validation" reports
#     (less the warnings SDL's Metal backend causes itself, below),
#     Metal shader validation's "... executing <stage> function: ..." ones
#     (kernel, or compute on macOS 27, for a compute shader; vertex or
#     fragment for a draw's),
#     the Vulkan layer's "Validation Error/Warning" messages;
#   - no SDL_GPU device, its debug mode not on (no `sdlgpu: debug mode
#     on` line), or its validation not active (on D3D12, SDL's
#     "Validation layers enabled" line missing; on Vulkan, the
#     layer's status must list synchronization validation and no disables,
#     naming any it lists).
# Otherwise CMD's own exit status.
# Not counted, each line pair exactly, with a note giving their number:
# the Metal API warnings SDL 3.4.18's Metal backend makes on its own
# (src/gpu/metal/SDL_gpu_metal.m), which no program can avoid.  Every
# pipeline bind sets the pipeline's fill mode, cull mode, winding, depth
# clip, depth bias and depth-stencil state, so a bind of a pipeline that
# shares one with the last is "redundant set<State>."; every render pass
# begins with a full-target viewport, so a draw's own viewport makes
# "previous setViewport was unused." (#26).  The exception holds only while
# toolchain.pins, beside this script, pins SDL3 3.4.18: with another SDL3,
# or none pinned, such a pair counts and the run names the exception to
# re-justify against the new SDL.
set -u
[[ $# -gt 0 ]] || { echo "usage: $0 CMD [ARG...]" >&2; exit 2; }
export MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog MTL_DEBUG_LAYER_WARNING_MODE=nslog
export MTL_SHADER_VALIDATION=1 MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1
VK_LAYER_SETTINGS_PATH=$(cd "$(dirname "$0")" && pwd)/vk_layer_settings.txt
export VK_LAYER_SETTINGS_PATH
export SDL_ASSERT=ignore SDL_LOGGING='assert=warn,gpu=info,*=warn'
log=$(mktemp "${TMPDIR:-/tmp}/gpu-validate.XXXXXX")
trap 'rm -f "$log"' EXIT
# A Windows program's lines, SDL's log among them, end in CR LF: drop the
# CRs, so that every match below sees the line's own end.
"$@" 2>&1 | tr -d '\r' > "$log"
rc=${PIPESTATUS[0]}
cat "$log"

problems=()
# Print the lines of the log matching extended regex $1.
matches() { grep -E -e "$1" "$log"; }
n=$(matches '(^|\] )(WARNING|ERROR|CRITICAL): ' | wc -l)
[[ $n -gt 0 ]] && problems+=("$((n)) from SDL's GPU debug mode")
layer='[^ ] Validation$|failed assertion|Validation [A-Za-z ]*(Error|Warning): '
shader='executing [a-z]+ function: '
n=$(matches "$layer|$shader" | wc -l)
# The pairs of SDL's own Metal warnings (above): a title, then its message.
sdl_own=$(awk '
	pending != "" && $0 == pending { n++ }
	{ pending = "" }
	/\] Set (Triangle Fill Mode|Cull Mode|Front Facing Winding|Depth Clip Mode|Depth Bias|Depth Stencil State) Validation$/ {
		state = $0
		sub(/.*\] Set /, "", state)
		sub(/ Validation$/, "", state)
		gsub(/ /, "", state)
		pending = "redundant set" state "."
	}
	/\] Viewport Validation$/ { pending = "previous setViewport was unused." }
	END { print n + 0 }' "$log") || sdl_own=0
sdl_pin=$(awk '$1 == "source" && $2 == "SDL3" { print $3 }' \
	"$(dirname "$0")/toolchain.pins" 2>/dev/null) || sdl_pin=
if [[ $sdl_own -gt 0 && $sdl_pin != 3.4.18 ]]; then
	problems+=("the exception for SDL's own Metal warnings was justified for SDL3 3.4.18, but toolchain.pins pins SDL3 ${sdl_pin:-nothing}: re-justify it against that SDL before counting them out")
elif [[ $sdl_own -gt 0 ]]; then
	echo "gpu-validate: ${1##*/}: $sdl_own Metal API warnings from SDL's own Metal backend, not counted"
	n=$((n - sdl_own))
fi
[[ $n -gt 0 ]] && problems+=("$((n)) from the driver's validation layer")
# SDL prints nothing that tells debug mode on Metal: a program prints this
# line for each device it created with debug_mode true, as SDL prints its
# driver line for each device.
devices=$(matches 'SDL_GPU Driver: ' | wc -l)
debug=$(matches 'sdlgpu: debug mode on$' | wc -l)
if [[ $devices -gt $debug ]]; then
	problems+=("SDL's GPU debug mode was not on for $((devices - debug)) of $((devices)) devices (no 'sdlgpu: debug mode on' line)")
fi
if matches 'SDL_GPU Driver: Metal' >/dev/null; then
	matches 'Metal API Validation Enabled' >/dev/null ||
		problems+=("Metal API validation was not active")
	matches 'Metal GPU Validation Enabled' >/dev/null ||
		problems+=("Metal shader validation was not active")
elif matches 'SDL_GPU Driver: Vulkan' >/dev/null; then
	matches 'Validation layers enabled' >/dev/null ||
		problems+=("the Vulkan validation layer was not active (debug_mode, vulkan-validationlayers)")
	matches 'Current Enables: .*SYNCHRONIZATION_VALIDATION' >/dev/null ||
		problems+=("Vulkan synchronization validation was not active")
	# The layer disables thread safety by default; the settings clear it.
	disables=$(sed -nE 's/.*Current Disables: (.*)\.$/\1/p' "$log" | head -n 1)
	if [[ -z $disables ]]; then
		problems+=("the Vulkan layer printed no Current Disables status")
	elif [[ $disables != None ]]; then
		problems+=("Vulkan validation disabled: $disables")
	fi
elif matches 'SDL_GPU Driver: D3D12' >/dev/null; then
	# SDL prints this only when both the DXGI and the D3D12 debug layers
	# loaded; with neither or one of them it warns instead.
	matches 'Validation layers enabled' >/dev/null ||
		problems+=("the D3D12 debug layer was not active (Windows' Graphics Tools feature)")
else
	problems+=("no SDL_GPU device was created, so nothing was validated")
fi
if [[ ${#problems[@]} -gt 0 ]]; then
	for p in "${problems[@]}"; do
		printf 'gpu-validate: %s: %s\n' "${1##*/}" "$p" >&2
	done
	exit 1
fi
echo "gpu-validate: ${1##*/}: no validation messages"
exit "$rc"

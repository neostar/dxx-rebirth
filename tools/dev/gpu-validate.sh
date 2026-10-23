#!/usr/bin/env bash
# Run an SDL_GPU program with every GPU validation on and fail on any
# validation message (ADR 5).
#
#   gpu-validate.sh CMD [ARG...]
#
# Turns on Metal API validation (MTL_DEBUG_LAYER=1, logging instead of
# aborting), Metal shader validation (MTL_SHADER_VALIDATION=1, reporting to
# stderr instead of os_log) and the Vulkan validation layer's errors,
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
#   - the driver's validation layer: Metal's "... Validation" reports,
#     Metal shader validation's "... executing kernel|compute function: ..."
#     ones (macOS 27 says "compute"),
#     the Vulkan layer's "Validation Error/Warning" messages;
#   - no SDL_GPU device, its debug mode not on (no `sdlgpu: debug mode
#     on` line), or its validation not active (on Vulkan, the
#     layer's status must list synchronization validation and no disables,
#     naming any it lists).
# Otherwise CMD's own exit status.
set -u
[[ $# -gt 0 ]] || { echo "usage: $0 CMD [ARG...]" >&2; exit 2; }
export MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog MTL_DEBUG_LAYER_WARNING_MODE=nslog
export MTL_SHADER_VALIDATION=1 MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1
VK_LAYER_SETTINGS_PATH=$(cd "$(dirname "$0")" && pwd)/vk_layer_settings.txt
export VK_LAYER_SETTINGS_PATH
export SDL_ASSERT=ignore SDL_LOGGING='assert=warn,gpu=info,*=warn'
log=$(mktemp "${TMPDIR:-/tmp}/gpu-validate.XXXXXX")
trap 'rm -f "$log"' EXIT
"$@" > "$log" 2>&1
rc=$?
cat "$log"

problems=()
# Print the lines of the log matching extended regex $1.
matches() { grep -E -e "$1" "$log"; }
n=$(matches '(^|\] )(WARNING|ERROR|CRITICAL): ' | wc -l)
[[ $n -gt 0 ]] && problems+=("$((n)) from SDL's GPU debug mode")
layer='[^ ] Validation$|failed assertion|Validation [A-Za-z ]*(Error|Warning): '
shader='executing [a-z]+ function: '
n=$(matches "$layer|$shader" | wc -l)
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

#!/usr/bin/env bash
# Run an SDL_GPU program with every GPU validation on and fail on any
# validation message (ADR 5).
#
#   gpu-validate.sh CMD [ARG...]
#
# Turns on Metal API validation (MTL_DEBUG_LAYER=1, logging instead of
# aborting) and the Vulkan validation layer's errors, warnings and
# performance warnings (VK_KHRONOS_VALIDATION_REPORT_FLAGS); SDL's GPU
# debug mode is CMD's own: it creates its device with debug_mode true, which
# on Vulkan also loads the layer.  SDL assertions log and continue
# (SDL_ASSERT=ignore), never a dialog; SDL logs warnings, and the GPU
# category its driver lines.  Neither SDL nor the layers change the exit
# status (spike #55, K4), so CMD's output is scanned.  Prints the output,
# then fails with exit status 1, naming the source, on:
#   - SDL's GPU debug mode: a WARNING, ERROR or CRITICAL log line;
#   - the driver's validation layer: Metal's "... Validation" reports,
#     the Vulkan layer's "Validation Error/Warning" messages;
#   - no SDL_GPU device, or its validation not active.
# Otherwise CMD's own exit status.
set -u
[[ $# -gt 0 ]] || { echo "usage: $0 CMD [ARG...]" >&2; exit 2; }
export MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog MTL_DEBUG_LAYER_WARNING_MODE=nslog
export VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn,perf
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
n=$(matches '[^ ] Validation$|failed assertion|Validation [A-Za-z ]*(Error|Warning): ' | wc -l)
[[ $n -gt 0 ]] && problems+=("$((n)) from the driver's validation layer")
if matches 'SDL_GPU Driver: Metal' >/dev/null; then
	matches 'Metal API Validation Enabled' >/dev/null || problems+=("Metal API validation was not active")
elif matches 'SDL_GPU Driver: Vulkan' >/dev/null; then
	matches 'Validation layers enabled' >/dev/null ||
		problems+=("the Vulkan validation layer was not active (debug_mode, vulkan-validationlayers)")
else
	problems+=("no SDL_GPU device was created, so nothing was validated")
fi
if [[ ${#problems[@]} -gt 0 ]]; then
	printf 'gpu-validate: %s: %s\n' "${1##*/}" "${problems[@]}" >&2
	exit 1
fi
echo "gpu-validate: ${1##*/}: no validation messages"
exit "$rc"

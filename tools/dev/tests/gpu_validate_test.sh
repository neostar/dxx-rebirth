#!/usr/bin/env bash
# gpu-validate.sh's verdicts on fake programs that print what SDL_GPU and
# the validation layers print; gates_test.sh's gate gpu runs real ones.
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"

# Run gpu-validate.sh on a fake program printing $1 and exiting with $2
# (default 0); print its output, then its exit status.
validate()
{
	local d out rc
	d=$(make_scratch)
	printf '#!/bin/sh\nprintf "%%s\\n" "$MSG"\nexit %s\n' "${2:-0}" > "$d/probe"
	chmod +x "$d/probe"
	out=$(MSG=$1 "$DEV_DIR/gpu-validate.sh" "$d/probe" 2>&1) && rc=0 || rc=$?
	printf '%s\nstatus %s\n' "$out" "$rc"
}

metal='2026-10-21 03:39:30.424 probe[1:2] Metal API Validation Enabled
2026-10-21 03:39:30.438 probe[1:2] SDL_GPU Driver: Metal'
vulkan='Validation layers enabled, expect debug level performance!
SDL_GPU Driver: Vulkan'

# Test that a run with validation active and no message passes with the
# program's own exit status.
test_clean_run_passes_with_its_status()
{
	assert_contains "$(validate "$metal")" "probe: no validation messages
status 0"
	assert_contains "$(validate "$vulkan" 3)" "probe: no validation messages
status 3"
}

# Test that a Vulkan layer message fails a run that exits 0.
test_vulkan_layer_message_fails()
{
	assert_contains "$(validate "$vulkan
VUID-x(ERROR / SPEC): msgNum: 1 - Validation Error: [ VUID-x ] bad")" \
		"probe: 1 from the driver's validation layer
status 1"
	assert_contains "$(validate "$vulkan
Validation Performance Warning: [ BestPractices-x ] slow")" "status 1"
}

# Test that an SDL warning, such as a failed debug-mode assertion, fails.
test_sdl_warning_fails()
{
	assert_contains "$(validate "$metal
2026-10-21 03:39:39.671 probe[1:2] WARNING: ")" "probe: 1 from SDL's GPU debug mode
status 1"
}

# Test that a run proves nothing, and fails, without its validation active
# or without a GPU device.
test_inactive_validation_fails()
{
	assert_contains "$(validate 'SDL_GPU Driver: Vulkan')" \
		"probe: the Vulkan validation layer was not active"
	assert_contains "$(validate 'SDL_GPU Driver: Metal')" "probe: Metal API validation was not active"
	assert_contains "$(validate 'hello')" "probe: no SDL_GPU device was created"
}

run_tests

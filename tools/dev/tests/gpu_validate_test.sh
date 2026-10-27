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
	d=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\nprintf "%%s\\n" "$MSG"\nexit %s\n' "${2:-0}" > "$d/probe"
	chmod +x "$d/probe"
	out=$(MSG=$1 "$DEV_DIR/gpu-validate.sh" "$d/probe" 2>&1) && rc=0 || rc=$?
	printf '%s\nstatus %s\n' "$out" "$rc"
}

metal='2026-10-21 03:39:30.424 probe[1:2] Metal API Validation Enabled
2026-10-21 03:39:30.424 probe[1:2] Metal GPU Validation Enabled
2026-10-21 03:39:30.438 probe[1:2] SDL_GPU Driver: Metal
INFO: sdlgpu: debug mode on'
vulkan='Validation layers enabled, expect debug level performance!
WARNING-CreateInstance-status-message(INFO / SPEC): msgNum: 601872502 - Validation Information: [ WARNING-CreateInstance-status-message ] | vkCreateInstance():  Khronos Validation Layer Active:
    Settings File: Found at /x/vk_layer_settings.txt specified by environment variable (VK_LAYER_SETTINGS_PATH).
    Current Enables: VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION.
    Current Disables: None.
SDL_GPU Driver: Vulkan
sdlgpu: debug mode on'

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

# Test that a Metal shader validation report, which names the shader
# function it stopped (macOS 26 says "kernel function", 27 "compute
# function"), fails a run that exits 0.
test_metal_shader_validation_report_fails()
{
	assert_contains "$(validate "$metal
Invalid device store at offset 4096, executing kernel function: \"plant_main\"
buffer: <unnamed>, length:16, resident:Read Write")" "probe: 1 from the driver's validation layer
status 1"
	assert_contains "$(validate "$metal
Invalid Metal usage: Invalid store in 'device' or 'constant' address space at offset 4096, executing compute function: \"plant_main\"")" "probe: 1 from the driver's validation layer
status 1"
}

# Test that the program runs with Metal shader validation on, reporting to
# stderr, not to os_log, where the wrapper would not see it.
test_metal_shader_validation_reports_to_stderr()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\necho "shader validation $MTL_SHADER_VALIDATION,%s"\n' \
		'$MTL_SHADER_VALIDATION_REPORT_TO_STDERR' > "$d/probe"
	chmod +x "$d/probe"
	out=$("$DEV_DIR/gpu-validate.sh" "$d/probe" 2>&1)
	assert_contains "$out" "shader validation 1,1"
}

# Test that the program runs with the Vulkan layer's settings file, which
# turns on synchronization and thread-safety validation.
test_vulkan_settings_turn_on_sync_and_thread_safety()
{
	local d out
	d=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\ncat "$VK_LAYER_SETTINGS_PATH"\n' > "$d/probe"
	chmod +x "$d/probe"
	out=$("$DEV_DIR/gpu-validate.sh" "$d/probe" 2>&1)
	assert_contains "$out" "khronos_validation.validate_sync = true"
	assert_contains "$out" "khronos_validation.thread_safety = true"
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
	assert_contains "$(validate 'SDL_GPU Driver: Metal')" \
		"probe: Metal API validation was not active"
	assert_contains "$(validate "${vulkan/ENABLE_SYNCHRONIZATION_VALIDATION/}")" \
		"probe: Vulkan synchronization validation was not active"
	local disabled
	for disabled in VK_VALIDATION_FEATURE_DISABLE_THREAD_SAFETY_EXT \
		'VK_VALIDATION_FEATURE_DISABLE_SHADERS_EXT, VK_VALIDATION_FEATURE_DISABLE_API_PARAMETERS_EXT'; do
		assert_contains "$(validate "${vulkan/Disables: None/Disables: $disabled}")" \
			"probe: Vulkan validation disabled: $disabled"
	done
	assert_contains "$(validate "${metal/GPU Validation/}")" \
		"probe: Metal shader validation was not active"
	assert_contains "$(validate 'hello')" "probe: no SDL_GPU device was created"
}

# Test that a run whose program did not print that it created its device
# in debug mode fails, on Metal, where nothing else tells (#24), also when
# one of several devices lacks it.
test_debug_mode_off_fails()
{
	local out
	out=$(validate "${metal/INFO: sdlgpu: debug mode on/}")
	assert_contains "$out" "probe: SDL's GPU debug mode was not on"
	assert_contains "$out" "status 1"
	out=$(validate "${vulkan/sdlgpu: debug mode on/}")
	assert_contains "$out" "probe: SDL's GPU debug mode was not on"
	# One line for two devices: one of them ran without debug mode.
	out=$(validate "$metal"$'\n''SDL_GPU Driver: Metal')
	assert_contains "$out" "probe: SDL's GPU debug mode was not on for 1 of 2 devices"
}

# Test that the Metal API warnings SDL's Metal backend causes itself pass,
# counted in a note: every pipeline bind sets all its rasterizer and depth
# state (redundant when two pipelines share it) and every render pass
# begins with a full-target viewport (unused when a draw sets its own).
test_sdl_metal_backend_warnings_pass_with_a_note()
{
	local out
	out=$(validate "$metal
2026-10-27 01:51:21.328 probe[1:2] Set Triangle Fill Mode Validation
redundant setTriangleFillMode.
2026-10-27 01:51:21.328 probe[1:2] Set Cull Mode Validation
redundant setCullMode.
2026-10-27 01:51:21.328 probe[1:2] Set Front Facing Winding Validation
redundant setFrontFacingWinding.
2026-10-27 01:51:21.328 probe[1:2] Set Depth Clip Mode Validation
redundant setDepthClipMode.
2026-10-27 01:51:21.328 probe[1:2] Set Depth Bias Validation
redundant setDepthBias.
2026-10-27 01:51:21.328 probe[1:2] Set Depth Stencil State Validation
redundant setDepthStencilState.
2026-10-27 01:51:21.187 probe[1:2] Viewport Validation
previous setViewport was unused.")
	assert_contains "$out" "probe: 7 Metal API warnings from SDL's own Metal backend, not counted"
	assert_contains "$out" "probe: no validation messages
status 0"
}

# Test that any other Metal API report fails: another message under a
# known title, another redundant state, a title with no message after it,
# another title.
test_other_metal_api_reports_fail()
{
	local report
	for report in 'probe[1:2] Set Depth Bias Validation
depth bias out of range.' 'probe[1:2] Set Vertex Buffer Validation
redundant setVertexBuffer.' 'probe[1:2] Viewport Validation' 'probe[1:2] Draw Validation
previous setViewport was unused.'; do
		assert_contains "$(validate "$metal
$report")" "probe: 1 from the driver's validation layer
status 1"
	done
}

# Test that the exception for SDL's own Metal warnings holds only while
# toolchain.pins pins the SDL3 it was justified for, 3.4.18: with another
# version, or no SDL3 pin, such a warning fails and says to re-justify it.
test_sdl_metal_exception_needs_its_sdl_pin()
{
	local d version out warning rc
	warning="$metal
2026-10-27 01:51:21.328 probe[1:2] Set Depth Bias Validation
redundant setDepthBias."
	for version in 3.4.20 none; do
		d=$(make_scratch) || die "make_scratch failed"
		cp "$DEV_DIR/gpu-validate.sh" "$DEV_DIR/vk_layer_settings.txt" "$d/"
		if [[ $version == none ]]; then
			grep -v '^source SDL3 ' "$DEV_DIR/toolchain.pins" > "$d/toolchain.pins"
		else
			sed "s/^source SDL3 3\.4\.18 /source SDL3 $version /" "$DEV_DIR/toolchain.pins" \
				> "$d/toolchain.pins"
		fi
		printf '#!/bin/sh\nprintf "%%s\\n" "$MSG"\n' > "$d/probe"
		chmod +x "$d/probe"
		out=$(MSG=$warning "$d/gpu-validate.sh" "$d/probe" 2>&1) && rc=0 || rc=$?
		assert_contains "$out" "probe: 1 from the driver's validation layer"
		assert_contains "$out" "justified for SDL3 3.4.18"
		assert_contains "$out" "re-justify"
		[[ $rc -eq 1 ]] || fail "SDL3 $version: status $rc, expected 1"
	done
}

run_tests

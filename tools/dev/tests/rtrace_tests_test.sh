#!/usr/bin/env bash
# Command-line behaviour of tools/dev/rtrace-tests.sh, with a fake build.sh
# and fake rtrace programs: usage errors, the SKIP of SDL 1.2 builds, and
# each failure it must report (build, report, runs, frame lines, time).
source "$(dirname "$0")/lib.sh"

# Make a directory holding tools/dev/rtrace-tests.sh, a fake build.sh, a
# pass-through no-window.sh, a stand-in sample and fake programs, and print
# its path, with a stand-in overbright trace.  The fake build.sh writes
# build/<profile>/config.log naming $FAKE_SDL (default SDL2) and, for the
# programs, fails when FAKE_BUILD_FAIL=1 and else installs them.  The fake
# test-rtrace writes the passing report line unless FAKE_NO_REPORT=1; the
# fake rtrace logs the sample's two lines, frame 0's as $FAKE_FRAME0 when
# set, run 2 an extra line when FAKE_DIFFER=1, and fails on the frameless
# trace (on its header unless FAKE_FRAMELESS_OTHER=1) unless
# FAKE_FRAMELESS_PASSES=1, and on the overbright trace its two lines,
# frame 0's as $FAKE_OVERBRIGHT0 when set; either sleeps $FAKE_SLEEP s, test-rtrace in a
# child whose pid it writes to test-rtrace.child.  With FAKE_GPU=1 it also
# installs a fake rtrace-gpu in build/<profile>-sdl3/common, which prints
# the driver line of $FAKE_GPU_DRIVER (default Metal), none when
# FAKE_GPU_NO_DRIVER=1, and logs the sample's
# two lines, frame 0's as $FAKE_GPU_FRAME0 when set, or the overbright
# trace's, frame 0's as $FAKE_GPU_OVERBRIGHT0 when set; a pass-through
# gpu-validate.sh fails when FAKE_GPU_INVALID=1.
make_rtrace_repo()
{
	local r
	r=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$r/tools/dev" "$r/fakes" "$r/common/render/rtrace/synthetic"
	printf 'a stand-in for the sample trace\n' > "$r/common/render/rtrace/synthetic/sample.rtr"
	printf 'a stand-in for the overbright trace\n' \
		> "$r/common/render/rtrace/synthetic/overbright.rtr"
	printf 'a stand-in for the flash trace\n' > "$r/common/render/rtrace/synthetic/flash.rtr"
	printf 'a stand-in for the walls trace\n' > "$r/common/render/rtrace/synthetic/walls.rtr"
	printf 'a stand-in for the alpha trace\n' > "$r/common/render/rtrace/synthetic/alpha.rtr"
	printf 'a stand-in for the lines trace\n' > "$r/common/render/rtrace/synthetic/lines.rtr"
	printf 'a stand-in for the oversize trace\n' \
		> "$r/common/render/rtrace/synthetic/oversize.rtr"
	printf 'a stand-in for the fonts trace\n' > "$r/common/render/rtrace/synthetic/fonts.rtr"
	cp "$DEV_DIR/rtrace-tests.sh" "$DEV_DIR/rtrace-pins.sh" "$r/tools/dev/"
	cat > "$r/tools/dev/build.sh" <<'EOF'
#!/usr/bin/env bash
cd "$(dirname "$0")/../.." || exit 1
mkdir -p "build/$1/common"
echo "scons: Configure: checking for usable library ${FAKE_SDL:-SDL2} with OpenGL...yes" \
	> "build/$1/config.log"
if [[ "$*" == *test-rtrace* ]]; then
	[[ ${FAKE_BUILD_FAIL-} == 1 ]] && { echo "fake build failed"; exit 1; }
	cp fakes/test-rtrace fakes/rtrace "build/$1/common/"
	if [[ ${FAKE_GPU-} == 1 ]]; then
		mkdir -p "build/$1-sdl3/common"
		cp fakes/rtrace-gpu "build/$1-sdl3/common/"
	fi
fi
exit 0
EOF
	printf '#!/usr/bin/env bash\nexec "$@"\n' > "$r/tools/dev/no-window.sh"
	cat > "$r/tools/dev/gpu-validate.sh" <<'EOF'
#!/usr/bin/env bash
"$@"
rc=$?
[[ ${FAKE_GPU_INVALID-} == 1 ]] && { echo "gpu-validate: fake validation message" >&2; exit 1; }
exit "$rc"
EOF
	cat > "$r/fakes/rtrace-gpu" <<'EOF'
#!/usr/bin/env bash
[[ ${FAKE_GPU_NO_DRIVER-} == 1 ]] || echo "SDL_GPU Driver: ${FAKE_GPU_DRIVER:-Metal}"
if [[ $1 == *overbright.rtr ]]; then
	printf '%s\n' "${FAKE_GPU_OVERBRIGHT0:-0 64x48 5d1b6b445fb7f05a}" '1 64x48 418a435cc9c67e56' \
		> "$2"
	exit 0
fi
if [[ $1 == *flash.rtr ]]; then
	printf '%s\n' "${FAKE_GPU_FLASH0:-0 64x48 a088becd21e46f87}" '1 64x48 4142e7869f743d59' \
		'2 64x48 c33733e407fec502' > "$2"
	exit 0
fi
if [[ $1 == *walls.rtr ]]; then
	printf '%s\n' "${FAKE_GPU_WALLS0:-0 64x48 75020e289509a104}" '1 64x48 01416b4d8a73c8c9' '2 64x48 58f167d16748f0ab' > "$2"
	exit 0
fi
if [[ $1 == *alpha.rtr ]]; then
	printf '%s\n' "${FAKE_GPU_ALPHA0:-0 64x48 184a14cee6aceb25}" '1 64x48 2eea67c587ee7f25' '2 64x48 891d9777e7a7e925' '3 64x48 a80644d0e3157625' '4 64x48 e2bd10483a9fad25' '5 64x48 af7964e69a3f7925' > "$2"
	exit 0
fi
if [[ $1 == *oversize.rtr ]]; then
	printf '%s\n' "${FAKE_GPU_OVERSIZE0:-0 64x48 d7b92cb41fbcf729}" '1 64x48 dbd8d0810c396d39' > "$2"
	exit 0
fi
if [[ $1 == *fonts.rtr ]]; then
	printf '%s\n' "${FAKE_GPU_FONTS0:-0 64x48 49e2bd63b6ae0b25}" '1 64x48 e3483f86e1fe7b25' '2 64x48 49e2bd63b6ae0b25' '3 64x48 e3483f86e1fe7b25' '4 64x48 e3483f86e1fe7b25' '5 64x48 e3483f86e1fe7b25' '6 64x48 e3483f86e1fe7b25' '7 64x48 51a3b262cedf3725' '8 64x48 d4e0fa5984a7c7e5' '9 64x48 9514bc61e4b568fd' '10 64x48 9514bc61e4b568fd' > "$2"
	exit 0
fi
if [[ $1 == *lines.rtr ]]; then
	printf '%s\n' "${FAKE_GPU_LINES0:-0 64x48 c523f67cb3e03eb2}" '1 64x48 26915db7ed30cd45' '2 64x48 9000b78311b0898e' '3 64x48 73ce0ff774ec2268' '4 64x48 dc4a426ae6920903' > "$2"
	exit 0
fi
printf '%s\n' "${FAKE_GPU_FRAME0:-0 64x48 86388f1d5003af2a}" '1 64x48 9f40fbb36ef64325' > "$2"
EOF
	cat > "$r/fakes/test-rtrace" <<'EOF'
#!/usr/bin/env bash
sleep "${FAKE_SLEEP:-0}" > /dev/null 2>&1 &
echo "$!" > "$(dirname "$0")/../../../test-rtrace.child"
wait
for a in "$@"; do
	[[ $a == --report_sink=* && ${FAKE_NO_REPORT-} != 1 ]] &&
		echo 'Test module "Rebirth rtrace" has passed with:' > "${a#--report_sink=}"
done
exit 0
EOF
	cat > "$r/fakes/rtrace" <<'EOF'
#!/usr/bin/env bash
sleep "${FAKE_SLEEP:-0}" > /dev/null 2>&1
if [[ $1 == *no-frames.rtr && ${FAKE_FRAMELESS_PASSES-} != 1 ]]; then
	[[ ${FAKE_FRAMELESS_OTHER-} == 1 ]] && { echo "rtrace: $1: bad magic header" >&2; exit 1; }
	echo "rtrace: $1: no frame presented" >&2
	exit 1
fi
if [[ $1 == *overbright.rtr ]]; then
	printf '%s\n' "${FAKE_OVERBRIGHT0:-0 64x48 5d1b6b445fb7f05a}" '1 64x48 418a435cc9c67e56' > "$2"
	exit 0
fi
if [[ $1 == *flash.rtr ]]; then
	printf '%s\n' "${FAKE_FLASH0:-0 64x48 a088becd21e46f87}" '1 64x48 4142e7869f743d59' \
		'2 64x48 c33733e407fec502' > "$2"
	[[ ${FAKE_FLASH_EXTRA-} == 1 ]] && echo '3 64x48 0000000000000000' >> "$2"
	[[ ${FAKE_FLASH_SHORT-} == 1 ]] && head -n 2 "$2" > "$2.short" && mv "$2.short" "$2"
	exit 0
fi
if [[ $1 == *walls.rtr ]]; then
	printf '%s\n' "${FAKE_WALLS0:-0 64x48 75020e289509a104}" '1 64x48 01416b4d8a73c8c9' '2 64x48 58f167d16748f0ab' > "$2"
	exit 0
fi
if [[ $1 == *alpha.rtr ]]; then
	printf '%s\n' "${FAKE_ALPHA0:-0 64x48 184a14cee6aceb25}" '1 64x48 2eea67c587ee7f25' '2 64x48 891d9777e7a7e925' '3 64x48 a80644d0e3157625' '4 64x48 e2bd10483a9fad25' '5 64x48 af7964e69a3f7925' > "$2"
	exit 0
fi
if [[ $1 == *oversize.rtr ]]; then
	printf '%s\n' "${FAKE_OVERSIZE0:-0 64x48 d7b92cb41fbcf729}" '1 64x48 dbd8d0810c396d39' > "$2"
	exit 0
fi
if [[ $1 == *fonts.rtr ]]; then
	printf '%s\n' "${FAKE_FONTS0:-0 64x48 49e2bd63b6ae0b25}" '1 64x48 e3483f86e1fe7b25' '2 64x48 49e2bd63b6ae0b25' '3 64x48 e3483f86e1fe7b25' '4 64x48 e3483f86e1fe7b25' '5 64x48 e3483f86e1fe7b25' '6 64x48 e3483f86e1fe7b25' '7 64x48 51a3b262cedf3725' '8 64x48 d4e0fa5984a7c7e5' '9 64x48 9514bc61e4b568fd' '10 64x48 9514bc61e4b568fd' > "$2"
	exit 0
fi
if [[ $1 == *lines.rtr ]]; then
	printf '%s\n' "${FAKE_LINES0:-0 64x48 a5d12a818955d06e}" '1 64x48 26915db7ed30cd45' '2 64x48 9000b78311b0898e' '3 64x48 14d868bc00457065' '4 64x48 96e0fb1fb76d65bd' > "$2"
	exit 0
fi
printf '%s\n' "${FAKE_FRAME0:-0 64x48 86388f1d5003af2a}" '1 64x48 9f40fbb36ef64325' > "$2"
[[ ${FAKE_DIFFER-} == 1 && $2 == *run2.log ]] && echo extra >> "$2"
exit 0
EOF
	chmod +x "$r/tools/dev/build.sh" "$r/tools/dev/no-window.sh" "$r/tools/dev/rtrace-tests.sh" \
		"$r/tools/dev/gpu-validate.sh" \
		"$r/fakes/"*
	printf '%s\n' "$r"
}

# Test that the fakes pass, the default path: both programs run and the
# script says it passed.
test_passes_with_good_programs()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$("$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "rtrace-tests (dev): passed"
}

# Test that a missing or unknown profile and a bad time bound are usage
# errors.
test_usage_errors()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$("$r/tools/dev/rtrace-tests.sh" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	out=$("$r/tools/dev/rtrace-tests.sh" release 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	out=$(RTRACE_TEST_TIMEOUT=soon "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "RTRACE_TEST_TIMEOUT=soon is not a positive number"
}

# Test that an SDL 1.2 build is a SKIP naming the open owner decision, and
# that a config.log naming no SDL fails.
test_sdl12_build_is_a_skip()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_SDL=SDL "$r/tools/dev/rtrace-tests.sh" asan 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "SKIP rtrace tests (asan): this build uses SDL 1.2"
	assert_contains "$out" \
		"open: Linux offscreen rtrace needs an SDL 2/3 container build, owner decision pending (#19)"
	out=$(FAKE_SDL=nothing "$r/tools/dev/rtrace-tests.sh" asan 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "cannot tell the SDL of build/asan"
}

# Test that a failed build of the programs fails.
test_build_failure_fails()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_BUILD_FAIL=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: build failed"
}

# Test that test-rtrace exiting 0 without its passing report fails: a
# wrapper such as leaks.sh drops its status.
test_missing_report_fails()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_NO_REPORT=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: test-rtrace reported no pass"
}

# Test that two runs of rtrace logging different lines fail.
test_differing_runs_fail()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_DIFFER=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "the two runs of rtrace logged different or no frame hashes"
}

# Test that a frame line other than the pinned one fails, naming the lines.
test_wrong_frame_line_fails()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_FRAME0='0 64x48 0000000000000000' "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: unexpected frame hashes:"
	assert_contains "$out" "0 64x48 0000000000000000"
}

# Test that an rtrace passing a trace without a frame fails, and so does
# one failing on it for another reason than the missing frame.
test_frameless_trace_must_fail()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_FRAMELESS_PASSES=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace did not fail on a trace without a frame"
	out=$(FAKE_FRAMELESS_OTHER=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace did not fail on a trace without a frame"
}

# Test that a run past RTRACE_TEST_TIMEOUT is killed and fails.
test_timeout_fails()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_SLEEP=30 RTRACE_TEST_TIMEOUT=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: test-rtrace ran past 1 s and was killed"
	# The fake's own child, its sleep, went with it.
	[[ -s $r/test-rtrace.child ]] || fail "the fake test-rtrace recorded no child"
	if kill -0 "$(cat "$r/test-rtrace.child")" 2>/dev/null; then
		fail "test-rtrace's child still runs after the time bound"
	fi
}

# Test that without a build/<profile>-sdl3 rtrace-gpu the R3 run is a
# visible SKIP, and that with one, on Metal, it must log the pinned lines
# of the sample and the overbright trace through gpu-validate.sh: a wrong
# line of either or a validation failure fails, and
# another driver, whose frames the pins do not hold for, is a SKIP.
test_rtrace_gpu_logs_the_pinned_lines()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$("$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "SKIP rtrace-gpu (dev): no build/dev-sdl3/common/rtrace-gpu"
	out=$(FAKE_GPU=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "rtrace-tests (dev): passed"
	[[ $out != *"SKIP rtrace-gpu"* ]] || fail "rtrace-gpu skipped: $out"
	out=$(FAKE_GPU=1 FAKE_GPU_FRAME0='0 64x48 0000000000000000' \
		"$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace-gpu's frame hashes of the sample trace are not GL's:"
	assert_contains "$out" "0 64x48 0000000000000000"
	out=$(FAKE_GPU=1 FAKE_GPU_OVERBRIGHT0='0 64x48 1111111111111111' \
		"$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" \
		"rtrace-tests: rtrace-gpu's frame hashes of the overbright trace are not GL's:"
	assert_contains "$out" "0 64x48 1111111111111111"
	out=$(FAKE_GPU=1 FAKE_GPU_FLASH0='0 64x48 3333333333333333' \
		"$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace-gpu's frame hashes of the flash trace are not GL's:"
	out=$(FAKE_GPU=1 FAKE_GPU_INVALID=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace-gpu failed"
	out=$(FAKE_GPU=1 FAKE_GPU_DRIVER=Vulkan "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "SKIP rtrace-gpu (dev, sample): driver Vulkan"
	assert_contains "$out" "SKIP rtrace-gpu (dev, overbright): driver Vulkan"
}

# Test that an rtrace-gpu run naming no SDL_GPU driver fails rather than
# skipping: it cannot be told apart from a broken run.
test_rtrace_gpu_without_driver_line_fails()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_GPU=1 FAKE_GPU_NO_DRIVER=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace-gpu named no SDL_GPU driver on the sample trace"
	[[ $out != *"SKIP rtrace-gpu"* ]] || fail "a run without a driver skipped: $out"
}

# Test that rtrace logging other lines than the overbright trace's pins
# fails, naming the trace.
test_overbright_lines_are_pinned()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_OVERBRIGHT0='0 64x48 2222222222222222' "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: unexpected frame hashes of the overbright trace:"
	assert_contains "$out" "0 64x48 2222222222222222"
}

# Test that rtrace logging other lines than the flash trace's pins, or an
# extra one, fails, naming the trace.
test_flash_lines_are_pinned()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_FLASH0='0 64x48 4444444444444444' "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: unexpected frame hashes of the flash trace:"
	out=$(FAKE_FLASH_EXTRA=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: unexpected frame hashes of the flash trace:"
	out=$(FAKE_FLASH_SHORT=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: unexpected frame hashes of the flash trace:"
}

# Test that rtrace or rtrace-gpu logging other lines than the walls
# trace's pins fails, naming the trace.
test_walls_lines_are_pinned()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_WALLS0='0 64x48 5555555555555555' "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: unexpected frame hashes of the walls trace:"
	out=$(FAKE_GPU=1 FAKE_GPU_WALLS0='0 64x48 5555555555555555' \
		"$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace-gpu's frame hashes of the walls trace are not GL's:"
}

# Test that rtrace or rtrace-gpu logging other lines than the alpha
# trace's pins fails, naming the trace.
test_alpha_lines_are_pinned()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_ALPHA0='0 64x48 5555555555555555' "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: unexpected frame hashes of the alpha trace:"
	out=$(FAKE_GPU=1 FAKE_GPU_ALPHA0='0 64x48 5555555555555555' \
		"$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace-gpu's frame hashes of the alpha trace are not GL's:"
}

# Test that rtrace logging other lines than the lines trace's GL pins fails,
# and rtrace-gpu logging other lines than its pinned Metal ones, which are
# not GL's, fails, naming the trace.
test_lines_pins_are_gl_and_metal()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_LINES0='0 64x48 5555555555555555' "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: unexpected frame hashes of the lines trace:"
	out=$(FAKE_GPU=1 FAKE_GPU_LINES0='0 64x48 5555555555555555' \
		"$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace-gpu's frame hashes of the lines trace are not the pinned Metal ones:"
	out=$(FAKE_GPU=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
}

# Test that rtrace or rtrace-gpu logging other lines than the oversize
# trace's GL pins fails, naming the trace: SDL_GPU's clamped viewports draw
# GL's pixels.
test_oversize_pins_are_gls()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_OVERSIZE0='0 64x48 5555555555555555' "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: unexpected frame hashes of the oversize trace:"
	out=$(FAKE_GPU=1 FAKE_GPU_OVERSIZE0='0 64x48 5555555555555555' \
		"$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace-gpu's frame hashes of the oversize trace are not GL's:"
	out=$(FAKE_GPU=1 "$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
}

# Test that rtrace and rtrace-gpu logging other lines than the fonts trace's
# pins fail, naming the trace; both log GL's, which SDL_GPU matches.
test_fonts_lines_are_pinned()
{
	local r out rc
	r=$(make_rtrace_repo) || die "make_rtrace_repo failed"
	out=$(FAKE_FONTS0='0 64x48 5555555555555555' "$r/tools/dev/rtrace-tests.sh" dev 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: unexpected frame hashes of the fonts trace:"
	out=$(FAKE_GPU=1 FAKE_GPU_FONTS0='0 64x48 5555555555555555' \
		"$r/tools/dev/rtrace-tests.sh" dev 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "rtrace-tests: rtrace-gpu's frame hashes of the fonts trace are not GL's:"
}

run_tests --parallel

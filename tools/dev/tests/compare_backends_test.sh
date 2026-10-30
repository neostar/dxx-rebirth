#!/usr/bin/env bash
# tools/dev/compare-backends.sh's verdicts and SKIPs on fake rtrace and
# rtrace-gpu programs that write the frames and frame hash log a test asks
# for, and on frames directories written the same way.  FLIP is the real
# one (the pinned flip-evaluator, tools/dev/requirements.lock).
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"
CB="$DEV_DIR/compare-backends.sh"
TRACE="$REPO_ROOT/common/render/rtrace/synthetic/sample.rtr"
# no-window.sh samples the frontmost app: a fake that always sees Finder.
printf '#!/bin/sh\necho "1 Finder"\n' > "$_scratch_root/frontmost"
chmod +x "$_scratch_root/frontmost"
export NO_WINDOW_FRONTMOST_CMD=$_scratch_root/frontmost

# Make a directory holding fake rtrace and rtrace-gpu programs (fake-GL and
# fake-GPU) and print its path.  Each writes, for the trace it is given,
# one frame a fill in $FAKE_FILLS_GL / $FAKE_FILLS_GPU (default "100 100":
# every byte of the frame that value) to the frames directory
# RTRACE_FRAMES_DIR and the frame hash log named by its second argument.
# $FAKE_SIZE_<SIDE> (default 64x48) is the frames' size; $FAKE_POKE_<SIDE>
# "PIXELS DELTA" adds DELTA to the first PIXELS pixels of every frame;
# $FAKE_DROP_<SIDE> deletes that frame's file after the log is written;
# $FAKE_STATUS_<SIDE> is the exit status (default 0).  The GPU program
# prints SDL's driver line for $FAKE_DRIVER (default Metal; empty: none).
# Each call goes to calls.log beside the fakes.
fake_bins()
{
	local d side
	d=$(make_scratch) || die "make_scratch failed"
	for side in GL GPU; do
		cat > "$d/fake-$side" <<'EOF2'
#!/usr/bin/env bash
here=$(cd "$(dirname "$0")" && pwd)
side=@SIDE@
echo "$side ${1##*/} driver=${SDL_GPU_DRIVER-unset} logging=${SDL_LOGGING-unset} frames=${RTRACE_FRAMES_DIR-unset}" >> "$here/calls.log"
if [[ $side == GPU && -n ${FAKE_DRIVER-Metal} ]]; then
	echo "INFO: SDL_GPU Driver: ${FAKE_DRIVER-Metal}"
fi
v=FAKE_FILLS_$side; fills=${!v-100 100}
v=FAKE_SIZE_$side; size=${!v-64x48}
v=FAKE_POKE_$side; poke=${!v-0 0}
v=FAKE_DROP_$side; drop=${!v-}
v=FAKE_STATUS_$side; status=${!v-0}
w=${size%x*}; h=${size#*x}
oct() { printf '\\%03o' "$1"; }
read -r px delta <<< "$poke"
: > "$2"
i=0
for fill in $fills; do
	echo "$i $size 0123456789abcdef" >> "$2"
	{
		[[ $px -eq 0 ]] || head -c $((px * 3)) /dev/zero | LC_ALL=C tr '\0' "$(oct $((fill + delta)))"
		head -c $((w * h * 3 - px * 3)) /dev/zero | LC_ALL=C tr '\0' "$(oct "$fill")"
	} > "${RTRACE_FRAMES_DIR:?}/$i.rgb"
	[[ $drop == "$i" ]] && rm "${RTRACE_FRAMES_DIR:?}/${i:?}.rgb"
	i=$((i + 1))
done
exit "$status"
EOF2
		sed "s/@SIDE@/$side/" "$d/fake-$side" > "$d/fake-$side.new" &&
			mv "$d/fake-$side.new" "$d/fake-$side"
		chmod +x "$d/fake-$side"
	done
	echo "$d"
}

# Make a directory holding a fake uname that prints $1 for -s, and print it.
fake_uname()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\n[ "$1" = -s ] && echo %s || exec /usr/bin/uname "$@"\n' "$1" > "$d/uname"
	chmod +x "$d/uname"
	echo "$d"
}

# Make a repository holding a copy of tools/dev and no build, and print
# the path of its compare-backends.sh: the defaults then find no programs.
bare_script()
{
	local r
	r=$(make_scratch) || die "make_scratch failed"
	mkdir -p "$r/tools"
	cp -R "$DEV_DIR" "$r/tools/dev"
	echo "$r/tools/dev/compare-backends.sh"
}

# Make a frames directory (frames and frames.log) of 64x48 frames filled
# with the values $2..., by the fake GL program of fakes directory $1, and
# print its path.
frames_dir()
{
	local d f=$1
	shift
	d=$(make_scratch) || die "make_scratch failed"
	FAKE_FILLS_GL="$*" RTRACE_FRAMES_DIR=$d "$f/fake-GL" t "$d/frames.log" ||
		die "fake-GL failed"
	echo "$d"
}

# Add $4 to the first $3 pixels' bytes of frame $2 in frames directory $1.
poke_frame()
{
	python3 -c '
import sys
p = sys.argv[1] + "/" + sys.argv[2] + ".rgb"
b = bytearray(open(p, "rb").read())
for k in range(int(sys.argv[3]) * 3):
	b[k] = (b[k] + int(sys.argv[4])) % 256
open(p, "wb").write(b)' "$@" || die "poke_frame failed"
}

# Run compare-backends.sh with the fakes of directory $1 as its programs
# and the arguments $2...; print its output, then its exit status.  The
# frontmost-app sampler of no-window.sh is a fake that sees no intruder.
cb()
{
	local f=$1 out rc
	shift
	out=$("$CB" --rtrace "$f/fake-GL" --rtrace-gpu "$f/fake-GPU" \
		"$@" 2>&1) && rc=0 || rc=$?
	printf '%s\nstatus %s\n' "$out" "$rc"
}

# Test that identical frames pass on Metal, with a verdict per frame and
# per trace, and that both programs ran on the trace, the GPU one asked for
# Metal.
test_metal_identical_frames_pass()
{
	local f out
	f=$(fake_bins)
	out=$(cb "$f" "$TRACE" metal)
	assert_contains "$out" "frame 0 (64x48): identical"
	assert_contains "$out" "frame 1 (64x48): identical"
	assert_contains "$out" "sample: PASS (2 of 2 frames bit-exact)"
	assert_contains "$out" "status 0"
	assert_contains "$(cat "$f/calls.log")" "GL sample.rtr driver=unset"
	assert_contains "$(cat "$f/calls.log")" "GPU sample.rtr driver=metal"
}

# Test that Metal fails a frame that differs by one level in one pixel
# (R4: bit-exact), naming the frame, and exits 1.
test_metal_one_level_difference_fails()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_POKE_GPU="1 1" cb "$f" "$TRACE" metal)
	assert_contains "$out" "frame 0 (64x48): differs in 1 px, up to 1 levels"
	assert_contains "$out" "sample: FAIL (2 of 2 frames fail: 0, 1)"
	assert_contains "$out" "status 1"
}

# Test that Vulkan passes frames within FLIP's bounds, printing the means.
test_vulkan_small_difference_passes()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_DRIVER=Vulkan FAKE_POKE_GPU="3 1" cb "$f" "$TRACE" vulkan)
	assert_contains "$out" "frame 0 (64x48): FLIP mean 0.0000"
	assert_contains "$out" "sample: PASS (p95 frame FLIP mean 0.0000"
	assert_contains "$out" "status 0"
	assert_contains "$(cat "$f/calls.log")" "GPU sample.rtr driver=vulkan"
}

# Test that D3D12 fails frames whose FLIP mean passes 0.03, and asks SDL
# for direct3d12.
test_d3d12_over_threshold_fails()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_DRIVER=D3D12 FAKE_POKE_GPU="300 30" cb "$f" "$TRACE" d3d12)
	assert_contains "$out" "> 0.03  FAIL"
	assert_contains "$out" "sample: FAIL (2 of 2 frames fail: 0, 1)"
	assert_contains "$out" "status 1"
	assert_contains "$(cat "$f/calls.log")" "GPU sample.rtr driver=direct3d12"
}

# Test that a trace whose frames all sit between the p95 bound 0.01 and the
# worst-frame bound 0.03 fails on the p95 bound alone.
test_p95_bound_fails_alone()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_DRIVER=Vulkan FAKE_POKE_GPU="100 30" cb "$f" "$TRACE" vulkan)
	assert_contains "$out" "over the p95 bound"
	assert_contains "$out" "sample: FAIL (p95 frame FLIP mean 0.02"
	assert_not_contains "$out" "FAIL (2 of 2"
	assert_contains "$out" "status 1"
}

# Test the 95th percentile apart from the worst frame (nearest rank): with
# 20 frames one frame at FLIP 0.02 passes (p95 is the 19th ranked frame, 0,
# and the worst is under 0.03); with 5 frames it fails (p95 is the 5th
# ranked, the worst); with 20 frames two such frames fail.
test_p95_is_nearest_rank_apart_from_the_worst_frame()
{
	local f gl gpu out
	f=$(fake_bins)
	# shellcheck disable=SC2046 # the words are the fills
	gl=$(frames_dir "$f" $(printf '100 %.0s' {1..20}))
	# shellcheck disable=SC2046
	gpu=$(frames_dir "$f" $(printf '100 %.0s' {1..20}))
	poke_frame "$gpu" 7 100 30
	out=$("$CB" --gl-frames "$gl" --gpu-frames "$gpu" vulkan 2>&1; echo "status $?")
	assert_contains "$out" "frame 7 (64x48): FLIP mean 0.02"
	assert_contains "$out" "PASS (p95 frame FLIP mean 0.00000 <= 0.01, worst 0.02"
	assert_contains "$out" "20 frames"
	assert_contains "$out" "status 0"
	poke_frame "$gpu" 8 100 30
	out=$("$CB" --gl-frames "$gl" --gpu-frames "$gpu" vulkan 2>&1; echo "status $?")
	assert_contains "$out" "FAIL (p95 frame FLIP mean 0.02"
	assert_contains "$out" "status 1"
	gl=$(frames_dir "$f" 100 100 100 100 100)
	gpu=$(frames_dir "$f" 100 100 100 100 100)
	poke_frame "$gpu" 2 100 30
	out=$("$CB" --gl-frames "$gl" --gpu-frames "$gpu" vulkan 2>&1; echo "status $?")
	assert_contains "$out" "FAIL (p95 frame FLIP mean 0.02"
	assert_contains "$out" "5 frames"
	assert_contains "$out" "status 1"
}

# Test that a frame SDL_GPU never drew (its log has one line fewer) fails.
test_missing_frame_fails()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_FILLS_GPU="100" cb "$f" "$TRACE" metal)
	assert_contains "$out" "frame 1 (64x48): missing from SDL_GPU  FAIL"
	assert_contains "$out" "status 1"
}

# Test that a frame named in the log but missing as a file fails.
test_missing_frame_file_fails()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_DROP_GPU=1 cb "$f" "$TRACE" metal)
	assert_contains "$out" "SDL_GPU: no frame 1"
	assert_contains "$out" "status 1"
}

# Test that a frame SDL_GPU drew in another size than GL fails.
test_size_mismatch_fails()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_SIZE_GPU=32x24 cb "$f" "$TRACE" metal)
	assert_contains "$out" "SDL_GPU drew 32x24, size mismatch  FAIL"
	assert_contains "$out" "status 1"
}

# Test that a frame file shorter than its size says fails.
test_truncated_frame_fails()
{
	local f gl gpu out rc
	f=$(fake_bins)
	gl=$(frames_dir "$f" 100 100)
	gpu=$(frames_dir "$f" 100 100)
	head -c 100 "$gpu/1.rgb" > "$gpu/1.tmp" && mv "$gpu/1.tmp" "$gpu/1.rgb"
	out=$("$CB" --gl-frames "$gl" --gpu-frames "$gpu" metal 2>&1) && rc=0 || rc=$?
	assert_contains "$out" "frame 1 has 100 bytes, not 9216"
	assert_eq "$rc" 1
}

# Test that a frames.log with CR LF line ends (a Windows run) is read.
test_crlf_frame_log_is_read()
{
	local f gl gpu out
	f=$(fake_bins)
	gl=$(frames_dir "$f" 100 100)
	gpu=$(frames_dir "$f" 100 100)
	sed 's/$/\r/' "$gpu/frames.log" > "$gpu/frames.crlf" && mv "$gpu/frames.crlf" "$gpu/frames.log"
	out=$("$CB" --gl-frames "$gl" --gpu-frames "$gpu" metal 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "PASS"
}

# Test that an empty frame file fails, saying it is empty.
test_empty_frame_fails()
{
	local f gl gpu out rc
	f=$(fake_bins)
	gl=$(frames_dir "$f" 100 100)
	gpu=$(frames_dir "$f" 100 100)
	: > "$gpu/1.rgb"
	out=$("$CB" --gl-frames "$gl" --gpu-frames "$gpu" metal 2>&1) && rc=0 || rc=$?
	assert_contains "$out" "frame 1 is empty"
	assert_eq "$rc" 1
}

# Test that a comparison that ends in neither 0 nor 1 (a crash, no python)
# fails the run rather than passing it.
test_comparison_that_cannot_run_fails()
{
	local f cb2 out rc
	f=$(fake_bins)
	cb2=$(bare_script)
	printf 'import sys\nsys.exit(3)\n' > "${cb2%/*}/compare_frames.py"
	out=$("$cb2" --rtrace "$f/fake-GL" --rtrace-gpu "$f/fake-GPU" "$TRACE" metal 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "compare_frames.py failed (status 3)"
}

# Test that an extra frame from SDL_GPU fails.
test_extra_frame_fails()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_FILLS_GPU="100 100 100" cb "$f" "$TRACE" metal)
	assert_contains "$out" "frame 2: extra frame from SDL_GPU"
	assert_contains "$out" "status 1"
}

# Test that a replay that exits non-zero fails the run, naming the program.
test_failing_replay_fails()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_STATUS_GPU=1 cb "$f" "$TRACE" metal)
	assert_contains "$out" "rtrace-gpu failed"
	assert_contains "$out" "status 1"
	out=$(FAKE_STATUS_GL=3 cb "$f" "$TRACE" metal)
	assert_contains "$out" "rtrace failed"
	assert_contains "$out" "status 1"
}

# Test that a replay that writes no frames fails.
test_replay_without_frames_fails()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_FILLS_GPU="" cb "$f" "$TRACE" metal)
	assert_contains "$out" "no frame hash lines"
	assert_contains "$out" "status 1"
}

# Test that a GPU run that names another driver than asked for, or none,
# fails.
test_wrong_or_no_driver_fails()
{
	local f out
	f=$(fake_bins)
	out=$(FAKE_DRIVER=Vulkan cb "$f" "$TRACE" metal)
	assert_contains "$out" "ran on Vulkan, not Metal"
	assert_contains "$out" "status 1"
	out=$(FAKE_DRIVER="" cb "$f" "$TRACE" metal)
	assert_contains "$out" "named no SDL_GPU driver"
	assert_contains "$out" "status 1"
}

# Test that a missing replay binary fails on macOS, naming how to build it.
test_missing_binary_fails_on_macos()
{
	local f out rc u cb2
	f=$(fake_bins)
	u=$(fake_uname Darwin)
	out=$(PATH=$u:$PATH "$CB" --rtrace "$f/none" --rtrace-gpu "$f/fake-GPU" "$TRACE" metal 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "no rtrace at $f/none"
	assert_not_contains "$out" "SKIP"
	out=$(PATH=$u:$PATH "$CB" --rtrace "$f/fake-GL" --rtrace-gpu "$f/none" "$TRACE" metal 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "no rtrace-gpu at $f/none"
	# The defaults, not found, say how to build the programs.
	cb2=$(bare_script)
	out=$(PATH=$u:$PATH "$cb2" --rtrace-gpu "$f/fake-GPU" "$TRACE" metal 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "build.sh dev build/dev/common/rtrace builds it"
	assert_not_contains "$out" "rtrace failed"
	out=$(PATH=$u:$PATH "$cb2" --rtrace "$f/fake-GL" "$TRACE" metal 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "build.sh dev sdl3=1 build/dev-sdl3/common/rtrace-gpu builds it"
}

# Test the default places of the programs: a repository with the scripts
# copied and the programs under build/dev and build/dev-sdl3.
test_default_program_paths()
{
	local r f out rc
	r=$(make_scratch) || die "make_scratch failed"
	f=$(fake_bins)
	mkdir -p "$r/tools" "$r/build/dev/common" "$r/build/dev-sdl3/common"
	cp -R "$DEV_DIR" "$r/tools/dev"
	cp "$f/fake-GL" "$r/build/dev/common/rtrace"
	cp "$f/fake-GPU" "$r/build/dev-sdl3/common/rtrace-gpu"
	out=$("$r/tools/dev/compare-backends.sh" "$TRACE" metal 2>&1) &&
		rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_contains "$out" "sample: PASS"
}

# Test that without the programs, Linux SKIPs the GL replay naming #81 and
# Vulkan naming #25, Windows SKIPs both naming #49, and each SKIP is a
# visible line with no verdict (exit 0).
test_missing_pieces_skip_naming_their_tickets()
{
	local f out u cb2
	f=$(fake_bins)
	cb2=$(bare_script)
	u=$(fake_uname Linux)
	out=$(PATH=$u:$PATH "$cb2" --rtrace-gpu "$f/fake-GPU" \
		"$TRACE" vulkan 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP the GL replay"
	assert_contains "$out" "#81"
	assert_contains "$out" "SKIP: no verdict"
	assert_not_contains "$out" "PASS"
	out=$(PATH=$u:$PATH "$cb2" --rtrace "$f/fake-GL" \
		"$TRACE" vulkan 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP the Vulkan replay"
	assert_contains "$out" "#25"
	u=$(fake_uname MINGW64_NT-10.0)
	out=$(PATH=$u:$PATH "$cb2" "$TRACE" d3d12 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP the GL replay"
	assert_contains "$out" "#49"
	out=$(PATH=$u:$PATH "$cb2" --rtrace "$f/fake-GL" \
		"$TRACE" d3d12 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP the D3D12 replay"
	assert_contains "$out" "#49"
}

# Test that Vulkan and D3D12 on macOS, without their programs, SKIP naming
# #25 and #49, while Metal off macOS, without its program, fails.
test_backend_off_its_platform()
{
	local f out rc u cb2
	f=$(fake_bins)
	cb2=$(bare_script)
	u=$(fake_uname Darwin)
	out=$(PATH=$u:$PATH "$cb2" --rtrace "$f/fake-GL" "$TRACE" vulkan 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "#25"
	out=$(PATH=$u:$PATH "$cb2" --rtrace "$f/fake-GL" "$TRACE" d3d12 2>&1)
	assert_contains "$out" "#49"
	u=$(fake_uname Linux)
	out=$(PATH=$u:$PATH "$cb2" --rtrace "$f/fake-GL" "$TRACE" metal 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "Metal runs only on macOS"
}

# Test that a backend that does not exist on this platform SKIPs even when
# a default rtrace-gpu is built there (Vulkan on macOS, D3D12 on Linux).
test_backend_off_its_platform_skips_despite_a_built_program()
{
	local r f out u
	r=$(make_scratch) || die "make_scratch failed"
	f=$(fake_bins)
	mkdir -p "$r/tools" "$r/build/dev/common" "$r/build/dev-sdl3/common"
	cp -R "$DEV_DIR" "$r/tools/dev"
	cp "$f/fake-GL" "$r/build/dev/common/rtrace"
	cp "$f/fake-GPU" "$r/build/dev-sdl3/common/rtrace-gpu"
	u=$(fake_uname Darwin)
	out=$(PATH=$u:$PATH "$r/tools/dev/compare-backends.sh" "$TRACE" vulkan 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP the Vulkan replay"
	assert_contains "$out" "SKIP: no verdict"
	u=$(fake_uname Linux)
	out=$(PATH=$u:$PATH "$r/tools/dev/compare-backends.sh" "$TRACE" d3d12 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "SKIP the D3D12 replay"
	assert_not_contains "$(cat "$f/calls.log" 2>/dev/null)" "GPU"
}

# Test that frames recorded elsewhere on both sides are judged without any
# program, equal ones passing and a differing one failing.
test_recorded_frames_on_both_sides()
{
	local f gl gpu out rc
	f=$(fake_bins)
	gl=$(frames_dir "$f" 100 100)
	gpu=$(frames_dir "$f" 100 100)
	out=$("$CB" --gl-frames "$gl" --gpu-frames "$gpu" metal 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "frames: PASS (2 of 2 frames bit-exact)"
	printf 'x' | dd of="$gpu/0.rgb" bs=1 seek=0 conv=notrunc 2>/dev/null
	out=$("$CB" --gl-frames "$gl" --gpu-frames "$gpu" metal 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "frame 0 (64x48): differs"
}

# Test that one side's recorded frames are judged against a replay of the
# other, here GL's replayed and SDL_GPU's recorded (a WARP run).
test_recorded_gpu_frames_against_gl_replay()
{
	local f gpu out
	f=$(fake_bins)
	gpu=$(frames_dir "$f" 100 100)
	out=$("$CB" --rtrace "$f/fake-GL" --gpu-frames "$gpu" \
		"$TRACE" d3d12 2>&1)
	assert_eq "$?" 0 "$out"
	assert_contains "$out" "sample: PASS"
	assert_contains "$(cat "$f/calls.log")" "GL sample.rtr"
	assert_not_contains "$(cat "$f/calls.log")" "GPU "
}

# Test that a recorded frames directory without its frames.log, or with an
# empty one, fails rather than passes.
test_recorded_frames_without_log_fail()
{
	local f gl gpu out rc
	f=$(fake_bins)
	gl=$(frames_dir "$f" 100 100)
	gpu=$(frames_dir "$f" 100 100)
	rm "$gpu/frames.log"
	out=$("$CB" --gl-frames "$gl" --gpu-frames "$gpu" metal 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "no frames.log of SDL_GPU"
	: > "$gpu/frames.log"
	out=$("$CB" --gl-frames "$gl" --gpu-frames "$gpu" metal 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "no frame hash lines"
}

# Test that --out keeps both sides' frames and logs, and refuses a
# directory that already holds files (stale frames must never be judged).
test_out_directory()
{
	local f d out
	f=$(fake_bins)
	d=$(make_scratch) || die "make_scratch failed"
	out=$(cb "$f" --out "$d/run" "$TRACE" metal)
	assert_contains "$out" "status 0"
	[[ -s $d/run/gl/0.rgb && -s $d/run/gpu/1.rgb && -s $d/run/gpu/frames.log ]] ||
		fail "--out did not keep the frames"
	out=$(cb "$f" --out "$d/run" "$TRACE" metal)
	assert_contains "$out" "is not empty"
	assert_contains "$out" "status 2"
}

# Test the usage errors, each exit status 2.
test_usage_errors()
{
	local f out args
	f=$(fake_bins)
	for args in "" "metal" "$TRACE" "$TRACE vulkan2" "--nope $TRACE metal" "$TRACE metal extra" \
		"$TRACE.missing metal" "--out"; do
		# shellcheck disable=SC2086 # the words are the arguments
		out=$(cb "$f" $args)
		assert_contains "$out" "status 2" "args: $args"
	done
	out=$(cb "$f" "")
	assert_contains "$out" "usage"
}

# Make the fake SDL_GPU program of fakes directory $1 hang: it writes its
# own pid to $1/pid and its child's to $1/child, then waits 30 s.
hang_fake()
{
	sed 's/^exit "\$status"$/sleep 30 \& echo $! > "$here\/child"; echo $$ > "$here\/pid"; wait; exit 0/' \
		"$1/fake-GPU" > "$1/fake-GPU.new" &&
		mv "$1/fake-GPU.new" "$1/fake-GPU" && chmod +x "$1/fake-GPU"
}

# Print "alive" when process $1 runs, else "dead".
alive()
{
	kill -0 "$1" 2>/dev/null && echo alive || echo dead
}

# Test that a replay past the time limit is killed with everything it
# started, on a system without pgrep (Git Bash), and fails, and that a limit
# that is no positive number is a usage error.
test_timeout_fails()
{
	local f out nopgrep
	f=$(fake_bins)
	hang_fake "$f"
	nopgrep=$(make_scratch) || die "make_scratch failed"
	printf '#!/bin/sh\nexit 1\n' > "$nopgrep/pgrep"
	chmod +x "$nopgrep/pgrep"
	out=$(PATH=$nopgrep:$PATH COMPARE_BACKENDS_TIMEOUT=1 cb "$f" "$TRACE" metal)
	assert_contains "$out" "ran past 1 s and was killed"
	assert_contains "$out" "status 1"
	assert_eq "$(alive "$(cat "$f/pid")")" dead "the replay program lives on"
	assert_eq "$(alive "$(cat "$f/child")")" dead "the replay's child lives on"
	out=$(COMPARE_BACKENDS_TIMEOUT=0 cb "$f" "$TRACE" metal)
	assert_contains "$out" "status 2"
}

# Test that Ctrl-C (INT) and TERM on the script kill the replay running,
# with its children, and end it with status 130 and 143.
test_signals_kill_the_replay()
{
	local f sig pid i rc
	f=$(fake_bins)
	hang_fake "$f"
	for sig in INT TERM; do
		rm -f "$f/pid" "$f/child"
		# A background command ignores INT, and bash cannot trap what it
		# inherits ignored: start the script with the default INT.
		python3 -c 'import os, signal, sys
signal.signal(signal.SIGINT, signal.SIG_DFL)
os.execv(sys.argv[1], sys.argv[1:])' "$CB" --rtrace "$f/fake-GL" --rtrace-gpu "$f/fake-GPU" \
			"$TRACE" metal > /dev/null 2>&1 &
		pid=$!
		for ((i = 0; i < 100; i++)); do
			[[ -s $f/child && -s $f/pid ]] && break
			sleep 0.1
		done
		[[ -s $f/child && -s $f/pid ]] || { kill "$pid"; fail "the replay did not start"; }
		kill -"$sig" "$pid"
		wait "$pid" && rc=0 || rc=$?
		assert_eq "$rc" "$([[ $sig == INT ]] && echo 130 || echo 143)"
		sleep 0.3
		assert_eq "$(alive "$(cat "$f/pid")")" dead "$sig: the replay program lives on"
		assert_eq "$(alive "$(cat "$f/child")")" dead "$sig: the replay's child lives on"
	done
}

# Test that --require-verdict turns a SKIP into a failure, and changes
# nothing for a trace that has a verdict.
test_require_verdict()
{
	local f out cb2 u
	f=$(fake_bins)
	cb2=$(bare_script)
	u=$(fake_uname Linux)
	out=$(PATH=$u:$PATH "$cb2" --require-verdict --rtrace-gpu "$f/fake-GPU" \
		"$TRACE" vulkan 2>&1; echo "status $?")
	assert_contains "$out" "SKIP: no verdict"
	assert_contains "$out" "--require-verdict"
	assert_contains "$out" "status 1"
	out=$(cb "$f" --require-verdict "$TRACE" metal)
	assert_contains "$out" "sample: PASS"
	assert_contains "$out" "status 0"
}

# The tests judge Metal, which runs only on macOS, so every test runs as on
# macOS (check.sh runs this file in the Linux container too); a test that
# fakes another platform puts its own uname first on PATH.
PATH=$(fake_uname Darwin):$PATH

run_tests --parallel

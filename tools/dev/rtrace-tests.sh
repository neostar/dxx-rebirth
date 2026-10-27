#!/usr/bin/env bash
# Build and run the rtrace tests (#19, ADR 5) of a build profile, each
# program through a wrapper command.
#
#   rtrace-tests.sh dev|asan [WRAPPER...]
#
# check.sh runs `rtrace-tests.sh asan no-window.sh` (sanitizer_tests) and
# `rtrace-tests.sh dev no-window.sh leaks.sh` (leaks).  build.sh builds
# build/<profile>/common/test-rtrace and rtrace (seconds when warm), and
# then, each through WRAPPER:
# - test-rtrace, the replay tests on synthetic traces, given the committed
#   sample and overbright traces to compare with their writers, its report
#   written to a file and read back;
# - rtrace on common/render/rtrace/synthetic/sample.rtr, twice: both runs
#   log the same two frame hash lines, each the one pinned below;
# - rtrace on synthetic/overbright.rtr (#27), once: its pinned lines;
# - rtrace on the sample's header alone, which must fail: no frame;
# - R3: when build/<profile>-sdl3/common/rtrace-gpu exists (build.sh
#   <profile> sdl3=1 builds it), rtrace-gpu on the sample, through
#   no-window.sh and gpu-validate.sh rather than WRAPPER, which must log
#   the same pinned lines; the pins are Apple's GL on Metal, so a driver
#   other than Metal is a SKIP (lavapipe compares with FLIP, #25), and so is
#   a missing rtrace-gpu.
# A wrapper may drop the program's status and stdout (leaks.sh), so each
# outcome is read from a file as well as from the status.  Each run is
# killed after RTRACE_TEST_TIMEOUT seconds (default 300), which fails it.
#
# The GL context comes from a hidden window, which the environment lets
# draw unseen (docs/agents/sdl-window.md, section 7): on macOS no-window.sh
# sets SDL_MAC_BACKGROUND_APP=1.  SDL 1.2 has no hidden windows, so a build
# whose configure chose it (build/<profile>/config.log; Linux builds it
# unless told otherwise) is a SKIP, kept open on #19 for the owner.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)

usage() { echo "usage: $0 dev|asan [WRAPPER...]" >&2; exit 2; }
[[ $# -ge 1 ]] || usage
profile=$1
shift
case $profile in
	dev|asan) ;;
	*) usage ;;
esac
wrap=("$@")
limit=${RTRACE_TEST_TIMEOUT:-300}
[[ $limit =~ ^[1-9][0-9]*$ ]] ||
	{
		echo "rtrace-tests: RTRACE_TEST_TIMEOUT=$limit is not a positive number of seconds" >&2
		exit 2
	}

# The frame hash log lines of the sample: frame 1 is 64x48 pixels of
# (255, 255, 0), hashed with 64-bit FNV-1a by python3 when written; frame
# 0, the shaded triangle, as Apple's GL draws it (M1 Max, macOS 26.7).
expected=('0 64x48 86388f1d5003af2a' '1 64x48 9f40fbb36ef64325')
sample=$root/common/render/rtrace/synthetic/sample.rtr
# The overbright trace's lines (#27), as Apple's GL draws them (M1 Max,
# macOS 26.7).
expected_overbright=('0 64x48 5d1b6b445fb7f05a' '1 64x48 418a435cc9c67e56')
overbright=$root/common/render/rtrace/synthetic/overbright.rtr

# Whether log $1 holds exactly the two lines $2 and $3.
logs_lines()
{
	local lines
	[[ -s $1 ]] || return 1
	mapfile -t lines < "$1"
	[[ ${#lines[@]} -eq 2 && ${lines[0]} == "$2" && ${lines[1]} == "$3" ]]
}

# Print process $1 and all its descendants, one pid a line.
tree_pids()
{
	local child
	echo "$1"
	for child in $(pgrep -P "$1"); do
		tree_pids "$child"
	done
}

# Run "$@", killing it and what it started after $limit seconds: status
# 124 then.
bounded()
{
	local pid i pids
	"$@" &
	pid=$!
	for ((i = 0; i < limit * 10; i++)); do
		if ! kill -0 "$pid" 2>/dev/null; then
			wait "$pid"
			return
		fi
		sleep 0.1
	done
	mapfile -t pids < <(tree_pids "$pid")
	kill -TERM "${pids[@]}" 2>/dev/null
	sleep 1
	kill -KILL "${pids[@]}" 2>/dev/null
	wait "$pid"
	echo "rtrace-tests: ${1##*/} ran past $limit s and was killed" >&2
	return 124
}

build=build/$profile
# The library builds in seconds and runs the configure, whose log names the
# SDL the build uses.
"$dev_dir/build.sh" "$profile" "$build/common/libcommon.a" ||
	{ echo "rtrace-tests: build failed" >&2; exit 1; }
sdl=$(grep -oE 'checking for usable library SDL[23]? with' "$root/$build/config.log" 2>/dev/null |
	tail -n 1)
case $sdl in
	*'SDL with')
		printf 'SKIP rtrace tests (%s): this build uses SDL 1.2, which has no hidden windows; %s\n' \
			"$profile" \
			'open: Linux offscreen rtrace needs an SDL 2/3 container build, owner decision pending (#19)'
		exit 0 ;;
	*'SDL2 with'|*'SDL3 with') ;;
	*) echo "rtrace-tests: cannot tell the SDL of $build from its config.log" >&2; exit 1 ;;
esac
"$dev_dir/build.sh" "$profile" "$build/common/test-rtrace" "$build/common/rtrace" ||
	{ echo "rtrace-tests: build failed" >&2; exit 1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/rtrace-tests.XXXXXX") ||
	{ echo "rtrace-tests: mktemp failed" >&2; exit 1; }
trap 'rm -rf "$tmp"' EXIT
rc=0

# The replay tests.
report=$tmp/test-rtrace.report
if ! bounded ${wrap[@]+"${wrap[@]}"} "$root/$build/common/test-rtrace" --report_level=short \
	--report_sink="$report" -- "$sample" "$overbright"; then
	echo "rtrace-tests: test-rtrace failed" >&2
	rc=1
fi
if ! grep -q '^Test module "Rebirth rtrace" has passed with:$' "$report" 2>/dev/null; then
	cat "$report" >&2 2>/dev/null
	echo "rtrace-tests: test-rtrace reported no pass" >&2
	rc=1
fi

# rtrace twice on the sample trace.
for run in 1 2; do
	if ! bounded ${wrap[@]+"${wrap[@]}"} "$root/$build/common/rtrace" "$sample" \
		"$tmp/run$run.log"; then
		echo "rtrace-tests: rtrace run $run failed" >&2
		rc=1
	fi
done
if [[ -s $tmp/run1.log && -s $tmp/run2.log ]] && cmp -s "$tmp/run1.log" "$tmp/run2.log"; then
	mapfile -t lines < "$tmp/run1.log"
	if [[ ${#lines[@]} -ne 2 || ${lines[0]} != "${expected[0]}" ||
		${lines[1]} != "${expected[1]}" ]]; then
		echo "rtrace-tests: unexpected frame hashes:" >&2
		cat "$tmp/run1.log" >&2
		rc=1
	fi
else
	echo "rtrace-tests: the two runs of rtrace logged different or no frame hashes" >&2
	rc=1
fi
# rtrace on the overbright trace.
if ! bounded ${wrap[@]+"${wrap[@]}"} "$root/$build/common/rtrace" "$overbright" \
	"$tmp/overbright.log"; then
	echo "rtrace-tests: rtrace on the overbright trace failed" >&2
	rc=1
elif ! logs_lines "$tmp/overbright.log" "${expected_overbright[@]}"; then
	echo "rtrace-tests: unexpected frame hashes of the overbright trace:" >&2
	cat "$tmp/overbright.log" >&2
	rc=1
fi
# rtrace on the sample's 20-byte header alone, a trace without a frame,
# must fail; through no-window.sh alone, which keeps the status.
head -c 20 "$sample" > "$tmp/no-frames.rtr"
if bounded "$dev_dir/no-window.sh" "$root/$build/common/rtrace" "$tmp/no-frames.rtr" \
	"$tmp/no-frames.log" 2> "$tmp/no-frames.err" ||
	! grep -q 'no frame presented' "$tmp/no-frames.err"; then
	cat "$tmp/no-frames.err" >&2
	echo "rtrace-tests: rtrace did not fail on a trace without a frame" >&2
	rc=1
fi
# R3: rtrace-gpu, the sample through SDL_GPU, logs GL's lines.
rtrace_gpu=$root/build/$profile-sdl3/common/rtrace-gpu
if [[ ! -x $rtrace_gpu ]]; then
	printf 'SKIP rtrace-gpu (%s): no build/%s-sdl3/common/rtrace-gpu; %s\n' "$profile" \
		"$profile" "build.sh $profile sdl3=1 build/$profile-sdl3/common/rtrace-gpu builds it"
else
	gpu_program=$rtrace_gpu
	if ! bounded "$dev_dir/no-window.sh" "$dev_dir/gpu-validate.sh" "$gpu_program" "$sample" \
		"$tmp/gpu.log" > "$tmp/gpu.out" 2>&1; then
		cat "$tmp/gpu.out" >&2
		echo "rtrace-tests: rtrace-gpu failed" >&2
		rc=1
	else
		driver=$(sed -n 's/.*SDL_GPU Driver: //p' "$tmp/gpu.out" | head -n 1)
		if [[ $driver != Metal ]]; then
			printf 'SKIP rtrace-gpu (%s): driver %s; the pinned lines are Metal'"'"'s, %s\n' \
				"$profile" "${driver:-unknown}" 'FLIP compares other drivers (#25)'
		else
			mapfile -t lines < "$tmp/gpu.log"
			if [[ ${#lines[@]} -ne 2 || ${lines[0]} != "${expected[0]}" ||
				${lines[1]} != "${expected[1]}" ]]; then
				echo "rtrace-tests: rtrace-gpu's frame hashes are not GL's:" >&2
				cat "$tmp/gpu.log" >&2
				rc=1
			fi
		fi
	fi
fi
[[ $rc -eq 0 ]] && echo "rtrace-tests ($profile): passed"
exit "$rc"

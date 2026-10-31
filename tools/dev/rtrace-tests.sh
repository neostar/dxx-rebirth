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
#   sample, overbright, flash, walls, alpha, lines, oversize, fonts, filters and overlay traces to compare
#   with their writers, its report written to a file and read back;
# - rtrace on common/render/rtrace/synthetic/sample.rtr, twice: both runs
#   log the same two frame hash lines, each the one pinned below;
# - rtrace on synthetic/overbright.rtr (#27), once: its pinned lines;
# - rtrace on synthetic/flash.rtr (#30), once: its three pinned lines;
# - rtrace on synthetic/walls.rtr (#28), once: its three pinned lines;
# - rtrace on synthetic/alpha.rtr (#29), once: its six pinned lines;
# - rtrace on synthetic/lines.rtr (#32), once: its five pinned lines;
# - rtrace on synthetic/oversize.rtr (#18), once: its two pinned lines;
# - rtrace on synthetic/fonts.rtr (#33), once: its eleven pinned lines;
# - rtrace on synthetic/filters.rtr (#34), once: its six pinned lines;
# - rtrace on synthetic/overlay.rtr (#31), once: its five pinned lines;
# - rtrace on the sample's header alone, which must fail: no frame;
# - R3: when build/<profile>-sdl3/common/rtrace-gpu exists (build.sh
#   <profile> sdl3=1 builds it), rtrace-gpu on the sample, overbright,
#   flash, walls, alpha, oversize, fonts, overlay, lines and filters traces, through
#   no-window.sh and gpu-validate.sh rather than WRAPPER (the oversize
#   trace's viewports reach past the target, which SDL's debug mode asserts
#   on unclamped).  Each must log GL's pinned lines, except two pinned to
#   Metal's own, each with a GL tolerance: the lines trace, which differs
#   from GL's by pixels at line ends within the tolerance test-sdlgpu holds
#   (#32), and the filters trace, whose frames must stay within GL's by the
#   pixels and levels of filters_gl_tolerance in rtrace-pins.sh (#34).  The
#   pins are Apple's GL on Metal, so another driver it names is a SKIP
#   (lavapipe compares with FLIP, #25), and so is a missing rtrace-gpu; a
#   run that names no driver fails.
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

# The pinned frame hash lines and the traces they belong to.
source "$dev_dir/rtrace-pins.sh"

# Whether log $1 holds exactly the lines $2... and no others.
logs_lines()
{
	local lines i
	[[ -s $1 ]] || return 1
	mapfile -t lines < "$1"
	[[ ${#lines[@]} -eq $(($# - 1)) ]] || return 1
	for ((i = 0; i < ${#lines[@]}; i++)); do
		[[ ${lines[i]} == "${*:i+2:1}" ]] || return 1
	done
}

# Whether the frames F.rgb of directory $2, rtrace-gpu's of trace $3, are
# within the tolerances $4... of GL's frames in directory $1, F from 0:
# each "PIXELS LEVELS", at most PIXELS pixels differ and none by more than
# LEVELS in a channel.  A missing frame fails.
frames_within()
{
	local gl=$1 gpu=$2 name=$3 f=0 limit diff status px levels ok=0
	shift 3
	for limit in "$@"; do
		if [[ ! -s $gl/$f.rgb ]]; then
			echo "rtrace-tests: no frame $f of the $name trace from rtrace" >&2
			return 1
		elif [[ ! -s $gpu/$f.rgb ]]; then
			echo "rtrace-tests: no frame $f of the $name trace from rtrace-gpu" >&2
			return 1
		elif [[ $(wc -c < "$gl/$f.rgb") -ne $(wc -c < "$gpu/$f.rgb") ]]; then
			echo "rtrace-tests: frame $f of the $name trace has another size on rtrace-gpu" >&2
			return 1
		fi
		cmp -l "$gl/$f.rgb" "$gpu/$f.rgb" > "$tmp/cmp" && status=0 || status=$?
		if [[ $status -gt 1 ]]; then
			echo "rtrace-tests: cmp failed on frame $f of the $name trace" >&2
			return 1
		fi
		# cmp -l prints each differing byte's offset from 1 and both values in octal.
		diff=$(awk '
			function dec(o, v, i) { v = 0; for (i = 1; i <= length(o); i++) v = v * 8 + substr(o, i, 1); return v }
			{ d = dec($2) - dec($3); if (d < 0) d = -d; if (d > big) big = d; px[int(($1 - 1) / 3)] = 1 }
			END { n = 0; for (p in px) n++; print n, big + 0 }' "$tmp/cmp") ||
			{ echo "rtrace-tests: awk failed on frame $f of the $name trace" >&2; return 1; }
		read -r px levels <<< "$diff"
		if [[ $px -gt ${limit% *} || $levels -gt ${limit#* } ]]; then
			printf "rtrace-tests: rtrace-gpu's frame %s of the %s trace is off GL's by %s px, %s\n" \
				"$f" "$name" "$px" "up to $levels levels (at most ${limit% *} px, ${limit#* } levels)" >&2
			ok=1
		fi
		f=$((f + 1))
	done
	return "$ok"
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
	--report_sink="$report" -- "$sample" "$overbright" "$flash" "$walls" "$alpha" "$lines_rtr" \
	"$oversize" "$fonts" "$filters" "$overlay"; then
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
# rtrace on the flash trace.
if ! bounded ${wrap[@]+"${wrap[@]}"} "$root/$build/common/rtrace" "$flash" "$tmp/flash.log"; then
	echo "rtrace-tests: rtrace on the flash trace failed" >&2
	rc=1
elif ! logs_lines "$tmp/flash.log" "${expected_flash[@]}"; then
	echo "rtrace-tests: unexpected frame hashes of the flash trace:" >&2
	cat "$tmp/flash.log" >&2
	rc=1
fi
# rtrace on the walls trace.
if ! bounded ${wrap[@]+"${wrap[@]}"} "$root/$build/common/rtrace" "$walls" "$tmp/walls.log"; then
	echo "rtrace-tests: rtrace on the walls trace failed" >&2
	rc=1
elif ! logs_lines "$tmp/walls.log" "${expected_walls[@]}"; then
	echo "rtrace-tests: unexpected frame hashes of the walls trace:" >&2
	cat "$tmp/walls.log" >&2
	rc=1
fi
# rtrace on the alpha trace.
if ! bounded ${wrap[@]+"${wrap[@]}"} "$root/$build/common/rtrace" "$alpha" "$tmp/alpha.log"; then
	echo "rtrace-tests: rtrace on the alpha trace failed" >&2
	rc=1
elif ! logs_lines "$tmp/alpha.log" "${expected_alpha[@]}"; then
	echo "rtrace-tests: unexpected frame hashes of the alpha trace:" >&2
	cat "$tmp/alpha.log" >&2
	rc=1
fi
# rtrace on the lines trace.
if ! bounded ${wrap[@]+"${wrap[@]}"} "$root/$build/common/rtrace" "$lines_rtr" "$tmp/lines.log"; then
	echo "rtrace-tests: rtrace on the lines trace failed" >&2
	rc=1
elif ! logs_lines "$tmp/lines.log" "${expected_lines[@]}"; then
	echo "rtrace-tests: unexpected frame hashes of the lines trace:" >&2
	cat "$tmp/lines.log" >&2
	rc=1
fi
# rtrace on the oversize trace.
if ! bounded ${wrap[@]+"${wrap[@]}"} "$root/$build/common/rtrace" "$oversize" \
	"$tmp/oversize.log"; then
	echo "rtrace-tests: rtrace on the oversize trace failed" >&2
	rc=1
elif ! logs_lines "$tmp/oversize.log" "${expected_oversize[@]}"; then
	echo "rtrace-tests: unexpected frame hashes of the oversize trace:" >&2
	cat "$tmp/oversize.log" >&2
	rc=1
fi
# rtrace on the fonts trace.
if ! bounded ${wrap[@]+"${wrap[@]}"} "$root/$build/common/rtrace" "$fonts" "$tmp/fonts.log"; then
	echo "rtrace-tests: rtrace on the fonts trace failed" >&2
	rc=1
elif ! logs_lines "$tmp/fonts.log" "${expected_fonts[@]}"; then
	echo "rtrace-tests: unexpected frame hashes of the fonts trace:" >&2
	cat "$tmp/fonts.log" >&2
	rc=1
fi
# rtrace on the overlay trace.
if ! bounded ${wrap[@]+"${wrap[@]}"} "$root/$build/common/rtrace" "$overlay" "$tmp/overlay.log"; then
	echo "rtrace-tests: rtrace on the overlay trace failed" >&2
	rc=1
elif ! logs_lines "$tmp/overlay.log" "${expected_overlay[@]}"; then
	echo "rtrace-tests: unexpected frame hashes of the overlay trace:" >&2
	cat "$tmp/overlay.log" >&2
	rc=1
fi
# rtrace on the filters trace, its frames kept for rtrace-gpu's tolerance.
mkdir "$tmp/filters-gl" "$tmp/filters-gpu" ||
	{ echo "rtrace-tests: mkdir failed" >&2; exit 1; }
if ! RTRACE_FRAMES_DIR=$tmp/filters-gl bounded ${wrap[@]+"${wrap[@]}"} \
	"$root/$build/common/rtrace" "$filters" "$tmp/filters.log"; then
	echo "rtrace-tests: rtrace on the filters trace failed" >&2
	rc=1
elif ! logs_lines "$tmp/filters.log" "${expected_filters[@]}"; then
	echo "rtrace-tests: unexpected frame hashes of the filters trace:" >&2
	cat "$tmp/filters.log" >&2
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
# R3: rtrace-gpu, each synthetic trace through SDL_GPU, logs GL's lines.
rtrace_gpu=$root/build/$profile-sdl3/common/rtrace-gpu
# Run rtrace-gpu on trace $1, named $2, which must log lines $3...; they are
# GL's unless pinned_as says otherwise.  It sets driver to the SDL_GPU
# driver the run named.
pinned_as="GL's"
check_gpu()
{
	local trace=$1 name=$2 out=$tmp/gpu-$2 gpu_program=$rtrace_gpu
	driver=""
	if ! bounded "$dev_dir/no-window.sh" "$dev_dir/gpu-validate.sh" "$gpu_program" "$trace" \
		"$out.log" > "$out.out" 2>&1; then
		cat "$out.out" >&2
		echo "rtrace-tests: rtrace-gpu failed on the $name trace" >&2
		return 1
	fi
	driver=$(sed -n 's/.*SDL_GPU Driver: //p' "$out.out" | head -n 1)
	if [[ -z $driver ]]; then
		cat "$out.out" >&2
		echo "rtrace-tests: rtrace-gpu named no SDL_GPU driver on the $name trace" >&2
		return 1
	elif [[ $driver != Metal ]]; then
		printf 'SKIP rtrace-gpu (%s, %s): driver %s; the pinned lines are Metal'"'"'s, %s\n' \
			"$profile" "$name" "$driver" 'FLIP compares other drivers (#25)'
	elif ! logs_lines "$out.log" "${@:3}"; then
		echo "rtrace-tests: rtrace-gpu's frame hashes of the $name trace are not $pinned_as:" >&2
		cat "$out.log" >&2
		return 1
	fi
}
if [[ ! -x $rtrace_gpu ]]; then
	printf 'SKIP rtrace-gpu (%s): no build/%s-sdl3/common/rtrace-gpu; %s\n' "$profile" \
		"$profile" "build.sh $profile sdl3=1 build/$profile-sdl3/common/rtrace-gpu builds it"
else
	check_gpu "$sample" sample "${expected[@]}" || rc=1
	check_gpu "$overbright" overbright "${expected_overbright[@]}" || rc=1
	check_gpu "$flash" flash "${expected_flash[@]}" || rc=1
	check_gpu "$walls" walls "${expected_walls[@]}" || rc=1
	check_gpu "$alpha" alpha "${expected_alpha[@]}" || rc=1
	check_gpu "$oversize" oversize "${expected_oversize[@]}" || rc=1
	check_gpu "$fonts" fonts "${expected_fonts[@]}" || rc=1
	check_gpu "$overlay" overlay "${expected_overlay[@]}" || rc=1
	pinned_as="the pinned Metal ones"
	check_gpu "$lines_rtr" lines "${expected_lines_metal[@]}" || rc=1
	if ! RTRACE_FRAMES_DIR=$tmp/filters-gpu check_gpu "$filters" filters \
		"${expected_filters_metal[@]}"; then
		rc=1
	elif [[ $driver == Metal ]] &&
		! frames_within "$tmp/filters-gl" "$tmp/filters-gpu" filters \
			"${filters_gl_tolerance[@]}"; then
		rc=1
	fi
fi
[[ $rc -eq 0 ]] && echo "rtrace-tests ($profile): passed"
exit "$rc"

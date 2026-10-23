#!/usr/bin/env bash
# Behaviour of tools/dev/r1.sh: its command line, and its verdicts with a
# fake scons whose "game" writes a synthetic frame hash log.  The real game
# runs need the licensed data and are not part of the tests
# (docs/agents/game-data.md).
# Single quotes hold scripts expanded by the shells they are written for.
# shellcheck disable=SC2016
source "$(dirname "$0")/lib.sh"
R1="$DEV_DIR/r1.sh"

# Test that a missing or unknown mode and stray arguments exit 2 with the
# usage.
test_bad_arguments_exit_2()
{
	local out rc a
	for a in '' 'frob' 'check --frob x' 'check x' 'capture --base' 'capture x y'; do
		# shellcheck disable=SC2086  # split the arguments on purpose
		out=$("$R1" $a 2>&1) && rc=0 || rc=$?
		assert_eq "$rc" 2 "$a: $out"
		assert_contains "$out" "usage:"
	done
}

# Test that both modes need DXX_D2_DATA to name the game data.
test_missing_data_exits_2()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	out=$(env -u DXX_D2_DATA R1_DIR="$d/r1" "$R1" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "DXX_D2_DATA must name the directory of DESCENT2.HOG"
	out=$(DXX_D2_DATA="$d" R1_DIR="$d/r1" "$R1" capture 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
}

# Test that check without a baseline exits 2 and says how to make one,
# before it builds anything.
test_check_without_baseline_exits_2()
{
	local d out rc
	d=$(make_scratch) || die "make_scratch failed"
	d=$(cd "$d" && pwd -P)
	: > "$d/DESCENT2.HOG"
	out=$(DXX_D2_DATA="$d" R1_DIR="$d/r1" "$R1" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "no baseline in $d/r1; run r1.sh capture"
	assert_not_contains "$out" "building"
}

# The stub game the fake scons builds: it writes a synthetic frame hash
# log for the scene its input script names, or for -autodemo a demo
# between title screens, and then waits for the TERM, as the game does.
# Its hashes depend on the tree's game.seed (from the build), the scene
# and -maxfps (the demo's do not); R1_FAKE_EXIT=SCENE makes it exit 3,
# R1_FAKE_HANG=SCENE hang, R1_FAKE_DEMO_FPS=1 its demo depend on -maxfps.
# Like the game, it rewrites a descent.cfg it finds with two lines of its own,
# dropping the setting R1_FAKE_CFG_DROP names; R1_FAKE_CFG_KEEP=1 leaves
# the file as it was.
# Its home gets the sanitizer settings it saw, in env; @PIDS@ its pid.
stub_game='#!/usr/bin/env bash
echo $$ >> "@PIDS@"
seed=@SEED@ fps=0 scene=demo
while [[ $# -gt 0 ]]; do
	[[ $1 == -maxfps ]] && fps=$2
	shift
done
[[ -n ${DXX_DBG_INPUT_SCRIPT-} ]] && scene=$(basename "$DXX_DBG_INPUT_SCRIPT" .txt)
printf "ASAN_OPTIONS=%s\nUBSAN_OPTIONS=%s\n" "$ASAN_OPTIONS" "$UBSAN_OPTIONS" > "$D2X_REBIRTH_HOME/env"
if [[ -e $D2X_REBIRTH_HOME/descent.cfg ]]; then
	{ grep -v "^${R1_FAKE_CFG_DROP:-^}=" "$D2X_REBIRTH_HOME/descent.cfg"; printf "PlayerFile=harness\nWindowMode=1\n"; } > "$D2X_REBIRTH_HOME/cfg.new"
	[[ -n ${R1_FAKE_CFG_KEEP-} ]] || mv "$D2X_REBIRTH_HOME/cfg.new" "$D2X_REBIRTH_HOME/descent.cfg"
fi
[[ ${R1_FAKE_HANG-} == "$scene" ]] && while :; do sleep 1; done
frames() # header, count, hash base
{
	echo "$1"
	for ((i = 0; i < $2; i++)); do printf "%d 640x480 %016x\n" "$i" $(($3 + i)); done
}
if [[ $scene == demo ]]; then
	{
		frames "# no demo" 3 7
		frames "# demo playback demos/descent2.dem" 5 $((seed * 1000 + ${R1_FAKE_DEMO_FPS:-0} * fps))
		frames "# no demo" 1 7
	} > "$DXX_DBG_FRAMEHASH"
	while :; do sleep 1; done
fi
frames "# scene $scene" 4 $((seed * 1000 + fps * 10 + ${#scene})) > "$DXX_DBG_FRAMEHASH"
[[ ${R1_FAKE_EXIT-} == "$scene" ]] && exit 3
exit 0
'

# Make a throwaway repository for r1.sh: a copy of tools/dev, the scene
# scripts, a stand-in SConstruct and game.seed (1), committed, with
# sdl-gpu-port at that commit.  Beside it, in bin/, a fake scons that
# records its arguments in bin/scons.args and builds the stub game from
# game.seed (it fails in a tree holding fail-build) and records the stub's
# pids in bin/pids, and the fake sampler
# no-window.sh needs; data/ holds an empty DESCENT2.HOG.  Print the
# directory holding repo/, bin/ and data/.
r1_setup()
{
	local d
	d=$(make_scratch) || die "make_scratch failed"
	d=$(cd "$d" && pwd -P)
	mkdir -p "$d/repo/tools" "$d/repo/common/debug" "$d/bin" "$d/data" "$d/reports"
	cp -R "$DEV_DIR" "$d/repo/tools/dev"
	cp -R "$REPO_ROOT/common/debug/scenes" "$d/repo/common/debug/scenes"
	echo '# stand-in' > "$d/repo/SConstruct"
	echo 1 > "$d/repo/game.seed"
	{ tgit -C "$d/repo" init -q -b main && tgit -C "$d/repo" add . &&
		tgit -C "$d/repo" commit -q -m base && tgit -C "$d/repo" branch sdl-gpu-port; } ||
		die "making the repository failed"
	printf '%s' "$stub_game" > "$d/bin/game.in"
	cat > "$d/bin/scons" <<SCONS
#!/bin/sh
printf '%s\n' "\$*" >> "$d/bin/scons.args"
[ -e fail-build ] && { echo "fake scons: failing"; exit 1; }
mkdir -p build/r1/d2x-rebirth || exit 1
sed "s/@SEED@/\$(cat game.seed)/; s|@PIDS@|$d/bin/pids|" "$d/bin/game.in" > build/r1/d2x-rebirth/d2x-rebirth &&
	chmod +x build/r1/d2x-rebirth/d2x-rebirth
SCONS
	chmod +x "$d/bin/scons"
	fake_sampler "$d/bin" "1 Terminal" >/dev/null
	: > "$d/data/DESCENT2.HOG"
	printf '%s\n' "$d"
}

# Skip the test off macOS: r1.sh plays the game on the Mac only, and its
# runs under the container's amd64 emulation take minutes, over the check
# tier's budget; the native leg of ci.sh runs these tests.
r1_mac_only()
{
	[[ $dev_os == darwin ]] || skip "r1.sh runs the game on macOS only; the native leg covers it"
}

# Run the r1.sh of setup $1 (r1_setup) with the remaining arguments, with
# R1_FPS 60 unless set, printing its output; fail the test and stop the
# run, stub games included, when it takes over 180 s.
r1_run()
{
	local d=$1 pid t
	shift
	set -m
	(cd "$d/repo" && PATH="$d/bin:$PATH" DXX_D2_DATA="$d/data" R1_DIR="$d/r1" \
		R1_FPS="${R1_FPS:-60}" NO_WINDOW_FRONTMOST_CMD="$d/bin/sampler" \
		NO_WINDOW_REPORTS_DIR="$d/reports" tools/dev/r1.sh "$@") &
	pid=$!
	set +m
	for ((t = 0; t < 1800; t++)); do
		kill -0 "$pid" 2>/dev/null || break
		sleep 0.1
	done
	if kill -0 "$pid" 2>/dev/null; then
		kill -KILL -- -"$pid" 2>/dev/null
		# shellcheck disable=SC2046  # one pid per line
		kill -KILL $(cat "$d/bin/pids") 2>/dev/null
		fail "r1.sh $* still ran after 180 s"
	fi
	wait "$pid"
}

# Test that capture then check of an unchanged tree prints N/N frames
# identical per scene and fps, and S2 across the fps values, with status 0,
# and that the baseline records its commit.
test_unchanged_tree_passes()
{
	local d out rc
	r1_mac_only
	d=$(r1_setup)
	out=$(R1_FPS='60 200' r1_run "$d" capture 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$(cut -d' ' -f1 "$d/r1/baseline/commit")" \
		"$(tgit -C "$d/repo" rev-parse HEAD)"
	out=$(R1_FPS='60 200' r1_run "$d" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	for l in 'S1 at 60 fps: 4/4' 'S2 at 60 fps: 5/5' 'S3 at 60 fps: 4/4' \
		'S4 at 200 fps: 4/4' 'S2 at 200 fps against 60 fps: 5/5'; do
		assert_contains "$out" "$l frames identical"
	done
}

# Test that check builds the working tree, not the baseline's commit, with
# the dev profile's hardened standard library (no CPPFLAGS of its own), and
# that a frame the tree changes fails check, naming it.
test_changed_tree_fails()
{
	local d out rc
	r1_mac_only
	d=$(r1_setup)
	r1_run "$d" capture >/dev/null 2>&1 || fail "capture failed"
	echo 2 > "$d/repo/game.seed"
	out=$(r1_run "$d" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "S1 at 60 fps: 0/4 frames identical; first difference: section '# scene s1-title-menu', frame 0"
	assert_contains "$out" "S2 at 60 fps: 0/5 frames identical"
	assert_eq "$(grep -c 'dbg_harness=1 builddir=build/r1' "$d/bin/scons.args")" 2
	assert_not_contains "$(cat "$d/bin/scons.args")" CPPFLAGS
}

# Test that a failing build fails capture and check with status 1, naming
# the build log, and plays nothing.
test_failed_build_fails()
{
	local d out rc
	r1_mac_only
	d=$(r1_setup)
	r1_run "$d" capture >/dev/null 2>&1 || fail "capture failed"
	: > "$d/repo/fail-build"
	out=$(r1_run "$d" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "build failed, see $d/r1/build.log"
	assert_not_contains "$out" "playing"
	assert_contains "$(cat "$d/r1/build.log")" "fake scons: failing"
}

# Test that a scene run exiting non-zero fails check with status 1 and
# names the scene, even when its frames match.
test_scene_exit_status_fails()
{
	local d out rc
	r1_mac_only
	d=$(r1_setup)
	r1_run "$d" capture >/dev/null 2>&1 || fail "capture failed"
	out=$(R1_FAKE_EXIT=s3-briefing r1_run "$d" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "s3-briefing at 60 fps exited 3"
}

# Test that a run outlasting R1_TIMEOUT is ended and fails with status 1.
test_hung_run_ends_and_fails()
{
	local d out rc
	r1_mac_only
	d=$(r1_setup)
	r1_run "$d" capture >/dev/null 2>&1 || fail "capture failed"
	out=$(R1_TIMEOUT=2 R1_FAKE_HANG=s1-title-menu r1_run "$d" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "s1-title-menu at 60 fps still runs after 2 s; ending it"
}

# Test that capture fails, keeping no baseline, when the demo differs
# between the fps values.
test_capture_fails_on_demo_fps_mismatch()
{
	local d out rc
	r1_mac_only
	d=$(r1_setup)
	out=$(R1_FPS='60 200' R1_FAKE_DEMO_FPS=1 r1_run "$d" capture 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "S2 at 200 fps against 60 fps: 0/5 frames identical"
	[[ ! -e $d/r1/baseline ]] || fail "capture kept a baseline"
}

# Test that check fails with status 2, before it builds, on a baseline of
# another commit than the branch point (the merge-base of HEAD and
# sdl-gpu-port) or the --base given, and passes with that commit's.
test_check_needs_baseline_of_base()
{
	local d out rc first
	r1_mac_only
	d=$(r1_setup)
	first=$(tgit -C "$d/repo" rev-parse HEAD)
	r1_run "$d" capture >/dev/null 2>&1 || fail "capture failed"
	{ tgit -C "$d/repo" commit -q --allow-empty -m next &&
		tgit -C "$d/repo" branch -f sdl-gpu-port HEAD; } || die "moving sdl-gpu-port failed"
	out=$(r1_run "$d" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "the baseline is of ${first:0:12}, not of the branch point"
	assert_contains "$out" "run r1.sh capture"
	assert_eq "$(grep -c . "$d/bin/scons.args")" 1 "check built"
	out=$(r1_run "$d" check --base HEAD 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "run r1.sh capture --base HEAD"
	out=$(r1_run "$d" check --base "$first" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
}

# Test that the runs get env.sh's sanitizer settings, or the caller's,
# with abort_on_error=0 added.
test_runs_get_sanitizer_settings()
{
	local d out
	r1_mac_only
	d=$(r1_setup)
	(unset ASAN_OPTIONS UBSAN_OPTIONS; r1_run "$d" capture >/dev/null 2>&1) ||
		fail "capture failed"
	out=$(cat "$d/r1/baseline/60/s1-title-menu.home/env" 2>&1)
	assert_contains "$out" "ASAN_OPTIONS=abort_on_error=0:halt_on_error=1:detect_leaks="
	assert_contains "$out" "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=0"
	(export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1
		r1_run "$d" capture >/dev/null 2>&1) || fail "capture failed"
	out=$(cat "$d/r1/baseline/60/s1-title-menu.home/env" 2>&1)
	assert_contains "$out" "ASAN_OPTIONS=detect_leaks=1:abort_on_error=0"$'\n'
	assert_contains "$out" "UBSAN_OPTIONS=print_stacktrace=1:abort_on_error=0"
}

# Test that R1_CFG's settings reach every run's descent.cfg, one per line,
# and the baseline's cfg, and that without it a run gets no descent.cfg.
test_cfg_reaches_each_run()
{
	local d out rc s
	r1_mac_only
	d=$(r1_setup)
	out=$(r1_run "$d" capture 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	[[ ! -e $d/r1/baseline/60/s1-title-menu.home/descent.cfg ]] || fail "a run without R1_CFG got a descent.cfg"
	assert_eq "$(cat "$d/r1/baseline/cfg")" ""
	out=$(R1_CFG=' TexFilt=2  TexAnisotropy=1 ' r1_run "$d" capture 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	for s in s1-title-menu demo s3-briefing s4-automap-cockpit; do
		assert_eq "$(cat "$d/r1/baseline/60/$s.home/descent.cfg")" $'TexFilt=2\nTexAnisotropy=1\nPlayerFile=harness\nWindowMode=1' "$s"
	done
	assert_eq "$(cat "$d/r1/baseline/cfg")" "TexFilt=2 TexAnisotropy=1"
}

# Test that an R1_CFG word not of the form NAME=VALUE exits 2, naming it,
# before anything is built.
test_bad_cfg_exits_2()
{
	local d out rc c
	d=$(make_scratch) || die "make_scratch failed"
	: > "$d/DESCENT2.HOG"
	for c in 'TexFilt' '=1' 'TexFilt=1=2' 'Tex-Filt=1' 'ResolutionX=640' 'TexFilt=x' 'TexFilt=-1'; do
		out=$(DXX_D2_DATA="$d" R1_DIR="$d/r1" R1_CFG="$c" "$R1" capture 2>&1) && rc=0 || rc=$?
		assert_eq "$rc" 2 "$c: $out"
		assert_contains "$out" "R1_CFG: '$c' is not NAME=VALUE"
		assert_not_contains "$out" "building"
	done
	out=$(DXX_D2_DATA="$d" R1_DIR="$d/r1" R1_CFG=$'TexFilt=1\nMovieTexFilt=1' "$R1" capture 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "newline: $out"
	assert_contains "$out" "R1_CFG holds a newline"
	assert_not_contains "$out" "building"
}

# Test that a run fails, naming the scene, when the game drops an R1_CFG
# setting from the descent.cfg it rewrites, or does not rewrite it.
test_cfg_the_game_drops_fails()
{
	local d out rc
	r1_mac_only
	d=$(r1_setup)
	out=$(R1_CFG='TexFilt=2 TexAnisotropy=1' R1_FAKE_CFG_DROP=TexAnisotropy r1_run "$d" capture 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "s1-title-menu at 60 fps: the game did not keep R1_CFG"
	out=$(R1_CFG='TexFilt=2' R1_FAKE_CFG_KEEP=1 r1_run "$d" capture 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "the game did not keep R1_CFG"
}

# Test that check fails with status 2, before it builds, on a baseline
# played with another R1_CFG, and passes with the same one.
test_check_needs_baseline_of_same_cfg()
{
	local d out rc
	r1_mac_only
	d=$(r1_setup)
	R1_CFG='TexFilt=1' r1_run "$d" capture >/dev/null 2>&1 || fail "capture failed"
	out=$(r1_run "$d" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 2 "$out"
	assert_contains "$out" "the baseline was played with R1_CFG='TexFilt=1', not ''"
	assert_eq "$(grep -c . "$d/bin/scons.args")" 1 "check built"
	out=$(R1_CFG='TexFilt=1' r1_run "$d" check 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
}

run_tests --parallel

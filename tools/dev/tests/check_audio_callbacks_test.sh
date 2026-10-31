#!/usr/bin/env bash
# Command-line behaviour of tools/dev/check_audio_callbacks.py: fork code
# outside the tests registers no SDL or SDL3_mixer audio callback
# (sdl-audio.md area 4, rule 3).
source "$(dirname "$0")/lib.sh"
LINT="$DEV_DIR/check_audio_callbacks.py"

# Test that each callback registration in game code is reported at its
# line, and that a mention in a comment or a string is not.
test_callback_registrations_reported()
{
	local r out rc
	r=$(make_repo) || die "make_repo failed"
	mkdir -p "$r/similar/arch/sdl"
	printf '%s\n' '/* MIX_SetTrackStoppedCallback is not used */' \
		'MIX_SetTrackStoppedCallback(t, f, nullptr);' \
		'const char *s = "MIX_SetPostMixCallback";' \
		'MIX_SetPostMixCallback(m, f, nullptr);' \
		'SDL_SetAudioStreamGetCallback(s, f, nullptr);' \
		'SDL_SetAudioPostmixCallback(d, f, nullptr);' > "$r/similar/arch/sdl/a.cpp"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	local line
	for line in 2 4 5 6; do
		assert_contains "$out" "similar/arch/sdl/a.cpp:$line: "
	done
	assert_eq "$(grep -c 'registers an audio callback' <<<"$out")" 4 "$out"
}

# Test that the unit tests, the tools' tests, docs and third_party may
# register callbacks, and that a tree without any passes.
test_tests_and_docs_pass()
{
	local r out rc
	r=$(make_repo) || die "make_repo failed"
	mkdir -p "$r/common/sdl/unittest" "$r/tools/dev/tests" "$r/docs/reference" "$r/third_party/x"
	local f
	for f in common/sdl/unittest/a.cpp tools/dev/tests/b.c docs/reference/c.c third_party/x/d.c; do
		printf '%s\n' 'MIX_SetTrackStoppedCallback(t, f, nullptr);' > "$r/$f"
	done
	printf '%s\n' 'MIX_TrackPlaying(t);' > "$r/game.cpp"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	assert_eq "$out" ""
}

# Test that SDL_OpenAudioDeviceStream is reported when its callback
# argument is not nullptr or NULL, wherever the call wraps, and not when it is.
test_open_stream_callback_argument()
{
	local r out rc
	r=$(make_repo) || die "make_repo failed"
	printf '%s\n' 'auto *s = SDL_OpenAudioDeviceStream(dev, &spec, nullptr, nullptr);' \
		'auto *t = SDL_OpenAudioDeviceStream(' \
		'	dev, &spec, NULL, ud);' > "$r/ok.cpp"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	printf '%s\n' 'auto *s = SDL_OpenAudioDeviceStream(dev, &spec, nullptr, nullptr);' \
		'auto *t = SDL_OpenAudioDeviceStream(dev, std::pair{1, 2}.first, cb, ud);' \
		'auto *u = SDL_OpenAudioDeviceStream(' \
		'	dev, &spec, on_audio(a, b), ud);' > "$r/bad.cpp"
	rm "$r/ok.cpp"
	out=$(python3 "$LINT" "$r" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "bad.cpp:2: "
	assert_contains "$out" "bad.cpp:3: "
	assert_eq "$(grep -c 'registers an audio callback' <<<"$out")" 2 "$out"
}

# Test that the allowlist (<path> <count> -- <reason>) lets a file keep that
# many registrations, and fails a count above the file's, a second entry
# for a path and an entry with no reason.
test_allowlist()
{
	local r out rc allow
	r=$(make_repo) || die "make_repo failed"
	allow="$r/allow"
	mkdir -p "$r/similar/arch/sdl"
	printf '%s\n' 'SDL_SetAudioStreamGetCallback(s, f, nullptr);' \
		'auto *t = SDL_OpenAudioDeviceStream(dev, &spec, cb, ud);' > "$r/similar/arch/sdl/m.cpp"
	printf '%s\n' '# comment' '' 'similar/arch/sdl/m.cpp 2 -- ADLMIDI pulls its samples (#46)' > "$allow"
	out=$(python3 "$LINT" "$r" "$allow" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 0 "$out"
	printf '%s\n' 'similar/arch/sdl/m.cpp 1 -- ADLMIDI pulls its samples (#46)' > "$allow"
	out=$(python3 "$LINT" "$r" "$allow" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "similar/arch/sdl/m.cpp:2: "
	printf '%s\n' 'similar/arch/sdl/m.cpp 3 -- ADLMIDI pulls its samples (#46)' > "$allow"
	out=$(python3 "$LINT" "$r" "$allow" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "allow:1: similar/arch/sdl/m.cpp has 2 registrations, lower the count"
	printf '%s\n' 'similar/arch/sdl/m.cpp 2' > "$allow"
	out=$(python3 "$LINT" "$r" "$allow" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "allow:1: entry needs <path> <count> -- <reason>"
	printf '%s\n' 'similar/arch/sdl/m.cpp 1 -- a' 'similar/arch/sdl/m.cpp 1 -- b' > "$allow"
	out=$(python3 "$LINT" "$r" "$allow" 2>&1) && rc=0 || rc=$?
	assert_contains "$out" "allow:2: second entry for similar/arch/sdl/m.cpp"
}

run_tests

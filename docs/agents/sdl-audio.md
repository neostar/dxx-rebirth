# Audio through SDL3 and SDL3_mixer

What SDL3 3.4.18 and SDL3_mixer 3.2.4 decide per platform backend (CoreAudio, WASAPI,
PipeWire/PulseAudio/ALSA), what they leave to the game, and what to do. Read the area your ticket
names; the design section applies to all of them. Research for #72, 2026-10-22.

**Citations.** `file:line` points into `docs/reference/`, which `tools/dev/fetch-docs.sh` fills
from the pinned raw files in `tools/dev/reference-docs.txt` (block `# Audio research (#72)`): run
it first. Game code is cited from the repo root. `[Wn]`, every source with its version and date,
and the open questions and measurements (M1-M10) are in **`docs/agents/sdl-audio-sources.md`**.
**Proven by the spike, not repeated here** (`~/Developer/dxx-rebirth-spike/notes/T4.md`, K5):
`MIX_SetTrackStereo` is linear; FluidSynth is in the Homebrew SDL3_mixer, TiMidity is not; the
SoundFont property string is internal; a missing SoundFont plays silence; disk-driver capture works.

## Versions: keep both pins

The pins are the latest releases on 2026-10-22: SDL `release-3.4.18` (2026-10-02) and SDL_mixer
`release-3.2.4` (2026-06-03) [W1]. The `release-3.4.x` branch has no audio commit after 3.4.18
[W2]. Unreleased audio changes on SDL `main` that matter to us:

- `c4cfa42f2348` (2026-10-13) "Fixed an audio crash when the device format changes at runtime":
  "Also fixed a potential crash if the format changes from F32 to non-F32 without the work size
  growing, leaving mix_buffer NULL." 3.4.18 has the unfixed code (`sdl3/src/audio/SDL_audio.c:2549-2572`
  reallocates `mix_buffer` only when the work buffer grows). It needs the device to
  change from float to an integer format at runtime (a default-device switch or WASAPI recovery
  could do it; whether it happens on our platforms is unknown). **Move the SDL pin to the first 3.4.x release that
  carries it**; until then a format-change crash is a known SDL bug, not ours.
- `36fed09edb7b` (2026-09-21) "audio: Don't force minimum device spec" and its regression fix
  `ab5541ec5d43`; `0ccb0bf6269a` adds `SDL_GetAudioDeviceProperties()`. Main only (next minor
  release); no action.

SDL_mixer `main` since 3.2.4 [W3]: TiMidity gains SoundFont support (`359465af9ece` and follow-ups)
and CMake can vendor FluidSynth (`408d8b065f00`). Neither is released; nothing to move. If a
release ships SF2 support in TiMidity, it could replace the FluidSynth dependency (#46, open).

## 1. Latency and buffer size

**SDL does.** Picks the device buffer ("sample frames"): 512 at ≤22050 Hz, 1024 at ≤48000 Hz
(`sdl3/src/audio/SDL_audio.c:161-166`), unless `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` is set:
"SDL will try to accommodate this value, but there is no promise you'll get the buffer size
requested" (`sdl3/SDL_hints.h:364`). What each backend does with it:

| Backend | Buffering (pinned source) | Honours the hint |
|---|---|---|
| CoreAudio (macOS) | AudioQueue (`SDL_coreaudio.m:728`), "THREE audio buffers by default" of `sample_frames` each, more if they sum under 15 ms (`:813-828`): about 64 ms queued at 1024@48k | Yes, directly |
| WASAPI | Shared mode only (`SDL_wasapi.c:686`). IAudioClient3: period = `sample_frames` rounded to the engine's fundamental period, clamped to its min..max (`:814-816`); else the device period (`:848-854`) | Within the driver's range; Microsoft: default 10 ms, smaller only with drivers that declare it, inbox HDAudio 128-480 frames [W4] |
| PipeWire | `node.latency = frames/rate` (`SDL_pipewire.c:1192`), at least 32 frames (`:36`, `:1154`); graph defaults quantum 1024, min 32, max 2048 at 48 kHz (`pipewire/src/daemon/pipewire.conf.in:44-48`) | As a request; the graph decides [W5] |
| PulseAudio | `tlength` = one buffer with `PA_STREAM_ADJUST_LATENCY` (`SDL_pulseaudio.c:749-753`): sink latency plus buffer target that size [W6] | Yes, as a target |
| ALSA | period = `sample_frames`, at least 2 periods (`SDL_alsa_audio.c:1021-1029`) | Near |

WASAPI's audio thread runs at MMCSS "Pro Audio" (`SDL_wasapi.c:933-936`). PipeWire's callback is
not on the realtime thread: "this flag is omitted" (`SDL_pipewire.c:1084-1090`).

**Left to us.** Whether to set the hint. The SDL2 build used 1024 frames on the Mac and 2048
elsewhere, at 44.1 kHz (`similar/arch/sdl/digi_mixer.cpp:55-59`), so SDL3's default is already
equal or lower. The SDL migration guide: "most apps should not risk messing with the defaults"
(`sdl3/README-migration.md:217`).

**Recommendation.** Keep SDL's default; the SDL3 branch leaves out the `__APPLE__` buffer size
(`digi_mixer.cpp:55-59`). Set the hint only if #45's measurement (M1) shows trigger-to-ear latency over budget on one platform, and then with
`SDL_SetHint` (normal priority, so a user's environment variable still wins).
Informs #45.

## 2. Format, rate and resampling

**SDL does.** Opens the physical device at the requested spec, raised to at least S16, stereo,
44100 Hz (`SDL_audio.c:1831-1842`, `SDL_sysaudio.h:41-43`); with no spec, at the device's own
format: CoreAudio's nominal rate (`SDL_coreaudio.m:161`), WASAPI's engine mix format
(`sdl3/src/core/windows/SDL_immdevice.c:162-165`). SDL mixes in float. Its resampler is
"bandlimited interpolation" (`SDL_audioresample.c:27`), 6 zero crossings with SSE/NEON (`:34`).
SDL3_mixer always mixes in F32 at the device's rate and channels (`sdl3_mixer/src/SDL_mixer.c:783`)
and its header says "specifying a NULL spec is a reasonable choice" (`sdl3_mixer/SDL_mixer.h:388`).

**Platform catch.** On WASAPI, if the requested rate differs from the mix rate, SDL hands
resampling to Windows: "favor WASAPI's resampler over our own" with
`AUTOCONVERTPCM | SRC_DEFAULT_QUALITY` (`SDL_wasapi.c:737-741`); the comment above (`:733-735`) cites "reports
that WASAPI's resampler introduces distortions". Microsoft lists only `EVENTCALLBACK` as a
supported flag for `InitializeSharedAudioStream` [W7], so a rate mismatch probably also loses the
low-latency IAudioClient3 path (inference; M2).

**Left to us.** The spec passed to `MIX_CreateMixerDevice`, and the source format of each sound.

**Recommendation.** `MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr)`: the
device runs at its native rate and SDL resamples each source once. Drop the SDL2-era "always
request 44Khz" (`digi_mixer.cpp:64-72`). Load effects as raw U8 mono at their own 11025/22050 Hz
(`MIX_LoadRawAudio`, as in K5). Whether the game's internal resamplers
(`DXX_FEATURE_INTERNAL_RESAMPLER*`) stay on SDL3 is decided by R12 envelope parity (M4).
Informs #45, #46, #47.

## 3. Device change and hot-plug

**SDL does.** A device opened as the default follows the default: SDL migrates its logical devices
to the new physical one (`SDL_audio.c:2418`) and keeps them on a zombie device until then
(`:784`). Every desktop backend reports default changes: CoreAudio listener
(`SDL_coreaudio.m:255`), WASAPI `IMMNotificationClient` with role `eConsole`
(`SDL_immdevice.c:36`, `:243`), PipeWire `default.audio.sink` metadata (`SDL_pipewire.c:624`,
`:637`), PulseAudio's default sink (`SDL_pulseaudio.c:885-890`). WASAPI re-activates a lost device
(`SDL_wasapi.c:494-506`). A format change raises `SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED`, on which
SDL3_mixer reformats its output and every track itself (`SDL_mixer.c:189-219`). PulseAudio and
PipeWire leave `DONT_MOVE`/`DONT_RECONNECT` off so users can move the stream
(`SDL_pulseaudio.c:775-779`, `SDL_pipewire.c:1196-1199`). CoreAudio keeps three buffers partly for
Bluetooth headsets (`SDL_coreaudio.m:821-824`).

**Left to us.** Any state that copies the device's rate or channel count: today ADLMIDI is
created at `Mix_QuerySpec`'s rate (`common/arch/sdl/digi_mixer_music.cpp:188-189`) and MVE
converts to it (`d2x-rebirth/libmve/mveplay.cpp:528`). After a switch those would be wrong.

**Recommendation.** Always open the default device; no device picker. Generators (ADLMIDI, MVE)
produce at a rate they choose and feed an `SDL_AudioStream`, so SDL converts after any switch.
Log device name and format at open and on each `SDL_EVENT_AUDIO_DEVICE_*` event. Sleep/wake and
Bluetooth have no backend-specific code in SDL beyond the above: M7. Informs #45, #46, #47.

## 4. Threads, the stream model and #45's command queue

**SDL does.** Audio is pulled: streams are bound to devices, "there is still a callback method
available if needed" (`sdl3/README-migration.md:102`). Stream callbacks "may run from any thread"
(`sdl3/SDL_audio.h:1867`); `SDL_LockAudioDevice` is gone, streams carry their own lock
(`README-migration.md:205`). SDL3_mixer pulls through one get-callback on its output stream
(`SDL_mixer.c:811`); the mixer lock *is* that stream's lock (`:106-108`), held while it mixes, so
every `MIX_*` call is thread-safe and blocks for at most one mix pass. `MIX_LockMixer` groups
changes so they land on the same sample frame; "Do not lock the mixer for significant amounts of
time" (`sdl3_mixer/SDL_mixer.h:553`). A track's stopped callback can run on the mixing thread, with the track locked
(`SDL_mixer.c:222-228`, called from the mix loop at `:367`).

**Today's race.** `Mix_ChannelFinished(digi_mixer_free_channel)` clears bits of `channels` on the
audio thread (`similar/arch/sdl/digi_mixer.cpp:110-114`, `:141`) while the game thread reads and
sets them. This is the data race #45's queue removes.

**Recommendation.** The command queue is a per-frame batch in the game's audio module: the game
thread records start/stop/gain commands, and `digi_sync_sounds()`
(`similar/main/digiobj.cpp:588`) ends the frame with one `MIX_LockMixer`, apply,
`MIX_UnlockMixer`. No game callback on the mixing thread for effects: the game learns a channel
is free by asking `MIX_TrackPlaying` on its own thread. The remaining callbacks (ADLMIDI's stream
get-callback) own their state and take control messages through an SPSC queue. The TSan test
needs no device: `MIX_CreateMixer` plus `MIX_Generate` on a second thread
(`sdl3_mixer/SDL_mixer.h:3161`: `MIX_Generate` "can not be used with mixers from
MIX_CreateMixerDevice()"). Informs #45 (and its TSan step in ADR 5).

## 5. Tracks, panning law and channel counts

**SDL_mixer does.** Tracks replace channels; "just call MIX_CreateTrack() in a loop"
(`sdl3_mixer/README-migration.md:151-163`). Tags group tracks for gain (`MIX_SetTagGain`).
`MIX_SetTrackStereo` multiplies linearly and only clamps negatives (`SDL_mixer.h:2628`,
`SDL_mixer.c:2815-2816`), and puts the track on front left/right only. `MIX_SetTrack3DPosition`
is the alternative: power-normalised VBAP (`sdl3_mixer/src/SDL_mixer_spatialization.c:171`) with
OpenAL inverse-distance (`:193`).

**The SDL2 baseline is linear too.** SDL2_mixer 2.8.2: `left_f = left / 255.0f`
(`sdl2_mixer/src/effect_position.c:2001-2004`), `distance_f = distance / 255.0f` (`:2051`), both
multiplied into each sample (`:144-151`). The game calls `Mix_SetPanning(255 - pan, pan)` and
`Mix_SetDistance(255 - volume)` (`digi_mixer.cpp:549-550`). So the -6 dB centre K5 found is what
players hear today, not a regression.

**Recommendation.** Compute gains in the game with the SDL2 law, including the byte quantisation
of `fix2byte`: `left = (255 - p) / 255 * v / 255`, `right = p / 255 * v / 255`, as a pure kernel
with a unit test (R12 parity). Equal-power panning or 3D positions would change the mix: a
separate, deliberate ticket, not #45. Create 64 tracks at start (`enumerated_bitset<64>`,
`digi_mixer.cpp:73`); tag `sfx` and `voice`; digi volume via `MIX_SetTagGain`, briefing ducking
via the music track's gain. On a surround device the mixer runs at its channel count and effects
stay on front left/right (M8). Informs #45.

## 6. Music: decoders, MIDI and SoundFonts

**SDL_mixer does.** Native MIDI is gone (`sdl3_mixer/README-migration.md:33`). FluidSynth gets the
SoundFont from an IOStream or path property (`sdl3_mixer/src/decoder_fluidsynth.c:121-129`,
names in `SDL_mixer_internal.h:229-230`), on Unix falls back to three system SoundFonts
(`:134-142`), and does not fail without one: "should we fail if there's no soundfont?" (`:145`).
It reads the whole SoundFont into memory per loaded song (`:166`) and parses it again each time a
track starts (`:322`). It sets `synth.gain` 1.0 (`:290`); FluidSynth's own default is 0.2 "to
avoid the saturation of the output" (`fluidsynth/doc/fluidsettings.xml:148-154`). SDL2_mixer also
reached 1.0 at full music volume (`sdl2_mixer/src/codecs/music_fluidsynth.c:265-267`), so that is
parity. Any FluidSynth setting can be passed through the internal
`"SDL_mixer.decoder.fluidsynth.props"` property (`SDL_mixer_internal.h:231`,
`decoder_fluidsynth.c:303`), including the look-ahead limiter of the pinned FluidSynth 2.6.1
(`fluidsettings.xml:164-168`; it needs the Signalsmith libraries, which `toolchain.pins` builds).

**Left to us.** ADLMIDI has no decoder: replace `Mix_HookMusic` with "MIX_SetTrackAudioStream and
set a get-callback on that stream" (`sdl3_mixer/README-migration.md:343`). HMP to MIDI stays in
the game. SDL1.2's `SDL_CD` (`common/arch/sdl/rbaudio.cpp:52`) has no SDL3 counterpart; the CD
jukebox is `jukebox.cpp` playing files.

**Recommendation.** #46: check the SoundFont file is readable before loading (on Linux a missing
one silently plays a different system SoundFont), pass it by path property, define both internal
strings locally. Keep `synth.gain` at SDL_mixer's 1.0 for parity; turn the limiter on only if the
saturation scan flags music. ADLMIDI generates at a fixed rate of its choice into a stream (area 3).
Measure song-start time with a full SoundFont (M5). Informs #46.

## 7. Movie audio and video sync

**Today.** Video is paced by its own wall-clock timer (`d2x-rebirth/libmve/mveplay.cpp:144-200`);
audio goes through an SDL2 `SDL_AudioStream` (`:311-337`) into a ring that a post-mix callback
drains (`:414`). The two clocks are never compared.

**SDL3.** One `SDL_AudioStream` with the MVE format as source, attached to a dedicated track with
`MIX_SetTrackAudioStream`; the game thread pushes decoded blocks with `SDL_PutAudioStreamData`
(thread-safe). No post-mix callback, no ring, no format query. Keep the wall-clock video pacing
for parity; `SDL_GetAudioStreamQueued` bounds how far audio runs ahead. Audio lags video by the
queued amount plus the device buffer (area 1): measure (M9) before adding drift correction.
Informs #47.

## 8. Silent, headless and test runs

**SDL does.** `SDL_AUDIO_DRIVER` names the backend; SDL3 still reads the SDL2 name
`SDL_AUDIODRIVER` when the new one is unset (`sdl3/src/SDL_hints.c:94-99`). A named driver is the
only one tried (`SDL_audio.c:982-1030`); `dummy` and `disk` are demand-only, never picked
otherwise (`:1035`, `SDL_dummyaudio.c:131`, `SDL_diskaudio.c:180`). "Environment variables are
considered to have override priority" over `SDL_SetHint` (`sdl3/SDL_hints.h:4854`).
`MIX_CreateMixerDevice` initialises the audio subsystem itself (`SDL_mixer.c:859`). The disk
driver writes raw PCM to `SDL_AUDIO_DISK_OUTPUT_FILE` (default `sdlaudio.raw`,
`SDL_diskaudio.c:30`); with no requested spec its device is S16, stereo, 44100 Hz
(`SDL_audio.c:702-711`).

**Catch.** Dummy and disk pace with integer milliseconds: `io_delay = sample_frames * 1000 / freq`
(`SDL_dummyaudio.c:52`, `SDL_diskaudio.c:123`). At 1024 frames and 48 kHz that is 21 ms for
21.33 ms of audio, about 1.6 % faster than real time. Captures stay sample-exact; anything that
ties them to wall-clock time drifts.

**Recommendation.** #66: log `SDL_GetCurrentAudioDriver()` after the mixer opens; the game never
calls `SDL_SetHintWithPriority(..., SDL_HINT_OVERRIDE)` on the audio driver. R12 captures record
the device format (`SDL_GetAudioDeviceFormat`, as K5 did) and align by content, not by time.
Unit-level audio tests use `MIX_CreateMixer` + `MIX_Generate`: no driver, no pacing, no file.
Informs #66, #45, #46, #47.

## Superseded or inaccurate advice

| Advice | Status | Applies to us? |
|---|---|---|
| `SDL_HINT_AUDIO_DEVICE_STREAM_ROLE` maps to WASAPI categories (`SDL_hints.h:421`) | The WASAPI code is `#if 0`: "Setting AudioCategory_GameChat breaks audio on several devices" (`SDL_wasapi.c:765-783`) | No effect on Windows; PipeWire uses it, default "Game" (`SDL_pipewire.c:1130-1136`) |
| `SDL_HINT_AUDIO_CATEGORY` "on iOS and macOS" (`SDL_hints.h:299`) | The AVAudioSession code is in the iOS-only section (`SDL_coreaudio.m:312`); Apple lists no macOS for AVAudioSession [W8] | No-op on macOS |
| SDL2 `SDL_AudioSpec.samples`, `SDL_LockAudio`, `SDL_OpenAudio` | Removed (`README-migration.md:205`, `:217`) | Upstream SDL2 code only |
| `SDL_SOUNDFONTS` / `Mix_SetSoundFonts` | Removed; per-load property (`sdl3_mixer/README-migration.md:404`) | #46 |
| Microsoft: use Real-Time Work Queues instead of own threads [W4] | SDL keeps its own thread with MMCSS "Pro Audio" | SDL's choice, not ours |
| FluidSynth web settings page [W9] | Shows libfluidsynth 2.5.4, older than the 2.6.1 pin | Use `fluidsettings.xml` at the pin |
| PulseAudio doxygen [W6] | Shows 16.0; not checked against 17.x | Low risk: SDL prefers PipeWire ≥ 1.0 (`SDL_pipewire.c:1338`) |

## Design: one deep audio module

The game already has a real seam: `sound_function_table_t` (`similar/arch/sdl/digi.cpp:154-191`)
with two adapters, `digi_audio` and `digi_mixer`. Keep it as the interface; put the SDL3
implementation behind it as a fork module (ADR 3: upstream files get `#elif SDL_MAJOR_VERSION == 3`
branches that call it; ADR 5: SDL userdata casts only in `sdl_wrap_audio.cpp`).

- **Interface.** The table's ten functions, plus music (play, stop, pause, volume) and one
  `commit_frame()` called from `digi_sync_sounds()`. Callers learn nothing about tracks, locks,
  rates, decoders or devices.
- **Depth.** Behind it: the mixer and its 64 tracks, the per-frame command batch, the SDL2 gain
  law (a pure, tested kernel), SoundFont checking, ADLMIDI and MVE streams, device-change
  logging, the driver log line for #66. Leverage: every `digi_*` caller and all four tickets use
  it unchanged; locality: a fix to gains, threading or device handling is one file.
- **Seam to the audio thread.** The only crossing is the module's `MIX_LockMixer` batch and
  `SDL_AudioStream` puts. No game data is read on the mixing thread; the ADLMIDI callback owns its
  player and takes messages through an SPSC queue.
- **Adapters at the internal seam.** Device mixer (`MIX_CreateMixerDevice`) in the game; deviceless
  mixer (`MIX_CreateMixer` + `MIX_Generate`) in tests and the TSan test. Two adapters, same `MIX_*`
  calls: a real seam.
- **Platform differences only through SDL.** Driver by environment, buffer by hint (if ever),
  format by query. No `__APPLE__` / `_WIN32` / `__linux__` in game audio code; the one existing
  case (`digi_mixer.cpp:55-59`) goes away on SDL3.

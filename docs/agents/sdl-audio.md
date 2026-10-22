# Audio through SDL3 and SDL3_mixer

What SDL3 3.4.18 and SDL3_mixer 3.2.4 decide per platform backend (CoreAudio, WASAPI,
PipeWire/PulseAudio/ALSA), what they leave to the game, and what to do. Read the area your ticket
names; the design section applies to all of them. Research for #72, 2026-10-22.

**Citations.** `file:line` points into `docs/reference/`, which `tools/dev/fetch-docs.sh` fills
from the pinned raw files in `tools/dev/reference-docs.txt` (block `# Audio research (#72)`): run
it first. Quoted text is verbatim (code joined across line breaks is marked `…`); where a claim is
not quoted, the pinned line range is the cited passage. Game code is cited from the repo root.
`[Wn]`, every source with its version and date, and the open questions and measurements (M1-M10)
are in **`docs/agents/sdl-audio-sources.md`**.

**Spike facts this builds on** (K5; provenance `~/Developer/dxx-rebirth-spike/notes/T4.md`,
outside the repo; SDL3 3.4.18, SDL3_mixer 3.2.4 from Homebrew, FluidSynth 2.6.1):
- A U8 mono 22050 Hz effect panned with `MIX_SetTrackStereo` gains {1,0}, {0.5,0.5}, {0,1}
  measured RMS 0.360/0, 0.180/0.180, 0/0.360: the gains are plain linear multipliers.
- The Homebrew SDL3_mixer has the FLUIDSYNTH decoder and no TIMIDITY.
- A MIDI load with a missing SoundFont succeeds and plays silence.
- The disk driver wrote headerless raw PCM in the device format (that run: F32LE, 2 ch,
  48000 Hz, 1024 frames).

## Versions: keep both pins for now

The pins are the latest releases on 2026-10-22: SDL `release-3.4.18` (2026-10-02) and SDL_mixer
`release-3.2.4` (2026-06-03) [W1]. Unreleased changes that matter to us [W2, W3]:

- **SDL_mixer `4aab76ed760c` "mixer: Lock tracks while mixing them."** (2026-06-17, fixes #869;
  on `release-3.2.x` as `2edc28031a5e`): "there is a lot more state involved with mixing after
  that--including a user callback--that were running unlocked". At 3.2.4 the mix loop reads track
  state without the track lock (`sdl3_mixer/src/SDL_mixer.c:581-621`); area 4 gives the rule that
  makes this safe. **Move the SDL_mixer pin to the first release that carries it** (3.2.6 or
  later; 3.2.5 is the development version).
- **SDL `e72b27af4141`** (`release-3.4.x`, 2026-10-07; wiki sync `f07211bfcebc`) adds to the
  `SDL_LockAudioStream` doc: "when the stream's callback runs, SDL is holding both an internal
  mutex for the audio device and the stream's lock, in that order" and warns of deadlock. It
  documents 3.4.18 behaviour; area 4 applies it.
- **SDL `c4cfa42f2348`** (`main`, 2026-10-13) "Fixed an audio crash when the device format changes
  at runtime": "potential crash if the format changes from F32 to non-F32 without the work size
  growing, leaving mix_buffer NULL". 3.4.18 reallocates `mix_buffer` only when the work buffer
  grows (`sdl3/src/audio/SDL_audio.c:2549-2572`). It needs the device to change from float to an
  integer format at runtime; whether that happens on our platforms is unknown. **Move the SDL pin
  to the first 3.4.x release that carries it.**
- No action (`main` only): SDL `36fed09edb7b` "audio: Don't force minimum device spec." (fix
  `ab5541ec5d43`), `0ccb0bf6269a` adds `SDL_GetAudioDeviceProperties()`; SDL_mixer TiMidity
  SoundFont support (`359465af9ece` and follow-ups) and "cmake: support vendored fluidsynth".

## 1. Latency and buffer size

**SDL does.** Picks the device buffer: `if (freq <= 22050) {` … `return 512;` …
`} else if (freq <= 48000) {` … `return 1024;` (`sdl3/src/audio/SDL_audio.c:161-164`), unless
`SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` is set: "there is no promise you'll get the buffer size
requested. Many platforms won't honor this request at all, or might adjust it."
(`sdl3/SDL_hints.h:364-366`).

| Backend | Buffering at the pin | Honours the hint |
|---|---|---|
| CoreAudio (macOS) | AudioQueue (`SDL_coreaudio.m:728`): "we use THREE audio buffers by default" of `sample_frames` each (`:821-825`), more if one buffer is under 15 ms (`:813`, `:826-828`): about 64 ms queued at 1024@48k | Yes, directly |
| WASAPI | Shared mode only (`SDL_wasapi.c:686`). "IAudioClient3_InitializeSharedAudioStream only accepts the integral multiple of fundamental_period_in_frames" (`:814`), clamped to the engine's min..max (`:816`); else the device period (`:848-854`) | Within the driver's range: default 10 ms, smaller only with drivers that declare it [W4] |
| PipeWire | `PW_KEY_NODE_LATENCY, "%u/%i", device->sample_frames, device->spec.freq` (`SDL_pipewire.c:1192`), at least 32 frames (`:36`, `:1154-1155`); graph quantum default 1024, min 32, max 2048 (`pipewire/src/daemon/pipewire.conf.in:46-48`) | As a request; the graph decides [W5] |
| PulseAudio | `paattr.tlength = device->buffer_size;` with `PA_STREAM_ADJUST_LATENCY` (`SDL_pulseaudio.c:749-753`) | Yes, as a target [W6] |
| ALSA | `ctx->persize = ctx->device->sample_frames;` and "we target a double buffer" (`SDL_alsa_audio.c:1021`, `:1027`) | Near |

WASAPI's thread runs at MMCSS "Pro Audio" (`SDL_wasapi.c:933-936`); PipeWire's callback is off
the realtime thread (`SDL_pipewire.c:1084-1090`).

**Left to us.** Whether to set the hint. The SDL2 build used 1024 frames on the Mac and 2048
elsewhere, at 44.1 kHz (`similar/arch/sdl/digi_mixer.cpp:55-59`), so SDL3's default is equal or
lower. The migration guide: "most apps should not risk messing with the defaults"
(`sdl3/README-migration.md:217`).

**Recommendation.** Keep SDL's default; the SDL3 branch leaves out the `__APPLE__` buffer size.
Set the hint only if M1 shows trigger-to-ear latency over budget, with `SDL_SetHint` so a user's
environment variable still wins (area 8). Informs #45.

## 2. Format, rate and resampling

**SDL does.** Opens the device at the requested spec raised to at least S16, stereo, 44100 Hz
(`SDL_audio.c:1836-1842`, `SDL_sysaudio.h:41-43`): "These are just requests! The backend may
change any of these values during OpenDevice method!" (`SDL_audio.c:1834`). With no spec it uses
the device's own format: CoreAudio's nominal rate (`SDL_coreaudio.m:161`), WASAPI's mix format
(`sdl3/src/core/windows/SDL_immdevice.c:162-165`). "SDL's resampler uses a "bandlimited
interpolation" algorithm" (`SDL_audioresample.c:27`), 6 zero crossings with SIMD (`:34`).
SDL3_mixer: "on our end, we always work in float32 format." (`sdl3_mixer/src/SDL_mixer.c:783`);
"specifying a NULL spec is a reasonable choice" (`sdl3_mixer/SDL_mixer.h:388`).

**Platform catch.** On WASAPI a requested rate other than the mix rate goes to Windows: "favor
WASAPI's resampler over our own" (`SDL_wasapi.c:737`) adds `AUTOCONVERTPCM` and
`SRC_DEFAULT_QUALITY` (`:738-739`), under "we've gotten reports that WASAPI's resampler
introduces distortions" (`:733`). Microsoft lists only `EVENTCALLBACK` as a flag for
`InitializeSharedAudioStream` [W7], so a rate mismatch probably also loses the low-latency path
(inference; M2).

**Left to us.** The spec given to `MIX_CreateMixerDevice`; each sound's source format.

**Recommendation.** `MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr)`: native
device rate, one resample per source in SDL. Drop the SDL2-era "always request 44Khz"
(`digi_mixer.cpp:64-72`). Load effects as raw U8 mono at their own 11025/22050 Hz
(`MIX_LoadRawAudio`). R12 parity (M4) decides whether `DXX_FEATURE_INTERNAL_RESAMPLER*` stays on
SDL3. Informs #45, #46, #47.

## 3. Device change and hot-plug

**SDL does.** A device opened as the default follows it: "migrate any logical devices that were
opened as a default to the new physical device" (`SDL_audio.c:2418`), parked on a zombie device
until then (`:784`). Every desktop backend except ALSA reports default changes: CoreAudio
(`SDL_coreaudio.m:255`), WASAPI with role `eConsole` (`SDL_immdevice.c:36`, `:243`), PipeWire
`default.audio.sink` (`SDL_pipewire.c:624`, `:637`), PulseAudio (`SDL_pulseaudio.c:885-890`).
WASAPI re-activates a lost device (`SDL_wasapi.c:494-506`). On
`SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED` SDL3_mixer reformats its output and tracks
(`SDL_mixer.c:189-219`). PulseAudio and PipeWire let users move the stream
(`SDL_pulseaudio.c:775-779`, `SDL_pipewire.c:1196-1199`). CoreAudio's three buffers help
"Bluetooth headsets when recording and playing at the same time" (`SDL_coreaudio.m:823`).

**Left to us.** State that copies the device format: ADLMIDI is created at `Mix_QuerySpec`'s rate
(`common/arch/sdl/digi_mixer_music.cpp:188-189`), MVE converts to it
(`d2x-rebirth/libmve/mveplay.cpp:528`). After a switch both would be wrong.

**Recommendation.** Always open the default device; no device picker. ADLMIDI and MVE produce at
a rate they choose into an `SDL_AudioStream`, so SDL converts after any switch. Log device name
and format at open and on each `SDL_EVENT_AUDIO_DEVICE_*` event. Sleep/wake and Bluetooth: M7.
Informs #45, #46, #47.

## 4. Threads, locks and #45's command queue

**SDL does.** The device thread takes the device lock, `SDL_LockMutex(device->lock);`
(`SDL_audio.c:1188`), then reads each bound stream with `SDL_GetAudioStreamDataAdjustGain`
(`:1220`), which takes that stream's lock and runs its get-callback. Stream calls take only the
stream lock, `SDL_LockMutex(stream->lock);` (`sdl3/src/audio/SDL_audiocvt.c:600`, `:1461`). So
the order is device, then stream, which `e72b27af4141` (Versions) documents; calling "into
something that wants to obtain the device lock" while holding a stream lock can deadlock.

**SDL_mixer 3.2.4 does.** Its locks are stream locks:
- **Mixer lock**: `SDL_LockAudioStream(mixer->output_stream);` (`SDL_mixer.c:108`), the
  device-bound output stream; `MIX_LockMixer` takes it (`:943-946`). The mix is that stream's
  get-callback (`:811`), so the mixer lock is held for the whole mix, inside the device lock.
- **Track lock**: `SDL_LockAudioStream(track->output_stream);` (`:120`). Track calls take only
  this one, e.g. `MIX_TrackPlaying` (`:2607`) and `MIX_SetTrackStereo` (`:2803`).
- The mix loop reads each track's gains and spatialisation without the track lock (`:581-621`),
  which `4aab76ed760c` fixes. So at 3.2.4 a track call from the game thread races the mix.

Order inside SDL_mixer: device, mixer, track. One path goes the other way: a fire-and-forget track
(`MIX_PlayAudio`) stopping under its track lock calls `LockMixer(mixer);` (`SDL_mixer.c:236`). A
stopped callback runs with the track locked (`:222-228`), on the mixing thread (`:367`) or on the
thread that stopped the track.

**Device-lock check.** The calls #45 needs (`MIX_PlayTrack`, `MIX_StopTrack`,
`MIX_SetTrackStereo`, `MIX_SetTrackGain`, `MIX_SetTagGain`, `MIX_TrackPlaying`,
`MIX_SetTrackLoops`, `MIX_SetTrackAudio`) make no device-level SDL call: in `SDL_mixer.c` those
appear only in mixer create/destroy and the format watcher (`:809`, `:863-869`, `:903-906`), and
track streams are never bound to a device. Open: `MIX_SetTrackAudio` may create a stream
(`SDL_CreateAudioStream`, `:1610`), whose registration lock was not traced.

**Today's race.** `Mix_ChannelFinished(digi_mixer_free_channel)` clears bits of `channels` on the
audio thread (`similar/arch/sdl/digi_mixer.cpp:110-114`, `:141`) while the game thread reads and
sets them.

**Rules for #45.**
1. Every track change happens in one per-frame batch: `MIX_LockMixer`, apply the frame's
   commands, `MIX_UnlockMixer`, from `digi_sync_sounds()` (`similar/main/digiobj.cpp:588`). The
   held mixer lock keeps the mix out, which 3.2.4 needs; keep this rule until a release carries
   `4aab76ed760c`, and keep the batch after that so a frame's starts land on one sample frame.
2. Inside the batch only the calls listed above: no `MIX_PlayAudio`, no track create/destroy, no
   device or mixer-level calls. Set `MIX_SetTrackAudio` outside the batch until the open point
   above is traced.
3. No game callback on the mixing thread for effects: the game polls `MIX_TrackPlaying`. ADLMIDI's
   stream get-callback owns its player; control reaches it through an SPSC queue.
4. The TSan test uses `MIX_CreateMixer` and `MIX_Generate` on a second thread. `MIX_Generate`
   pulls the unbound output stream with `SDL_GetAudioStreamData` (`SDL_mixer.c:644-659`) and never
   takes a device lock, so it finds game-versus-mix races but cannot show a device-lock
   inversion; rule 2 is what prevents those.

Informs #45 (and its TSan step in ADR 5).

## 5. Tracks, panning law and channel counts

**SDL_mixer does.** "just call MIX_CreateTrack() in a loop, storing the pointers in an array"
(`sdl3_mixer/README-migration.md:160-161`). `MIX_SetTrackStereo`: "Negative gains are clamped to
zero; there is no clamp for maximum" (`SDL_mixer.h:2628`), on front left/right only. The
alternative, `MIX_SetTrack3DPosition`, normalises power:
`const float scale = 1.0f / SDL_sqrtf(gain_a * gain_a + gain_b * gain_b);`
(`sdl3_mixer/src/SDL_mixer_spatialization.c:171`), with OpenAL inverse distance (`:193`).

**The SDL2 baseline is linear too.** SDL2_mixer 2.8.2: `args->left_f = ((float) left) / 255.0f;`
(`sdl2_mixer/src/effect_position.c:2002`), `distance = 255 - distance;  /* flip it to our scale. */`
(`:2037`), `args->distance_f = ((float) distance) / 255.0f;` (`:2051`). The game opens
`AUDIO_S16` (`MIX_OUTPUT_FORMAT{AUDIO_S16}`, `digi_mixer.cpp:61`), so the S16 stereo path
applies: `Sint16 swapl = (Sint16) ((((float) (Sint16) SDL_SwapLE16(*(ptr+0))) *` …
`left_f) * dist_f);` (`effect_position.c:799-800`). The game calls
`Mix_SetPanning(channel, 255-mix_pan, mix_pan);` and
`Mix_SetDistance(channel, UINT8_MAX - fix2byte(volume));` (`digi_mixer.cpp:549-550`). So K5's
-6 dB centre is what players hear today.

**Recommendation.** The game computes `left = (255 - p) / 255 * v / 255`,
`right = p / 255 * v / 255` with `fix2byte`'s byte quantisation, as a pure kernel with a unit
test (R12 parity). Equal-power panning or 3D positions change the mix: a separate ticket. 64
tracks at start (`enumerated_bitset<64>`, `digi_mixer.cpp:73`); tags `sfx` and `voice`; digi
volume by `MIX_SetTagGain`; briefing ducking by the music track's gain. On a surround device
effects stay on front left/right (M8). Informs #45.

## 6. Music: decoders, MIDI and SoundFonts

**SDL_mixer does.** "Native MIDI has been removed." (`sdl3_mixer/README-migration.md:33`).
FluidSynth takes the SoundFont from an IOStream or path property
(`sdl3_mixer/src/decoder_fluidsynth.c:121-129`), on Unix falls back to three system SoundFonts
(`:134-142`), and loads without one: "should we fail if there's no soundfont?" (`:145`). It reads
the whole SoundFont into memory per loaded song (`:166`) and parses it again at each track start
(`:322`). It sets `"synth.gain", 1.0` (`:290`); FluidSynth's default is 0.2 (`<def>0.2</def>`)
"to avoid the saturation of the output" (`fluidsynth/doc/fluidsettings.xml:150`, `:154`).
SDL2_mixer also reached 1.0 at full music volume: "Make 1.0 the maximum gain value"
(`sdl2_mixer/src/codecs/music_fluidsynth.c:265`): parity. Three property strings are internal
(`SDL_mixer_internal.h:229-231`): the SoundFont IOStream and path, and
`MIX_PROP_DECODER_FLUIDSYNTH_PROPS_NUMBER` (`"SDL_mixer.decoder.fluidsynth.props"`), a properties
ID whose entries become FluidSynth settings (`decoder_fluidsynth.c:191`, `:303`). That reaches the
look-ahead limiter of FluidSynth 2.6.1 (`fluidsettings.xml:164-168`; it needs Signalsmith, which
`toolchain.pins` builds).

**Left to us.** ADLMIDI: "Mix_HookMusic => MIX_SetTrackAudioStream and set a get-callback on that
stream" (`sdl3_mixer/README-migration.md:343`). HMP to MIDI stays in the game. SDL1.2's `SDL_CD`
(`common/arch/sdl/rbaudio.cpp:52`) has no SDL3 counterpart; the CD jukebox is `jukebox.cpp`.

**Recommendation.** #46: check the SoundFont is readable before loading (on Linux a missing one
silently plays a system SoundFont); define the three strings locally. Keep `synth.gain` 1.0; the
limiter only if the saturation scan flags music (M6). ADLMIDI generates at its own fixed rate into
a stream (area 3). Song-start cost: M5. Informs #46.

## 7. Movie audio and video sync

**Today.** Video is paced by a wall-clock timer (`d2x-rebirth/libmve/mveplay.cpp:144-200`); audio
goes through an SDL2 `SDL_AudioStream` (`:311-337`) into a ring that a post-mix callback drains
(`:414`). The two clocks are never compared.

**Recommendation.** One `SDL_AudioStream` with the MVE format as source on a dedicated track
(`MIX_SetTrackAudioStream`), fed from the game thread with `SDL_PutAudioStreamData`. No post-mix
callback, ring or format query. Keep wall-clock video pacing for parity;
`SDL_GetAudioStreamQueued` bounds how far audio runs ahead. Audio lags by the queue plus the
device buffer (area 1): measure (M9) before adding drift correction. Informs #47.

## 8. Silent, headless and test runs

**SDL does.** SDL3 falls back to the SDL2 variable: `result = SDL_getenv("SDL_AUDIODRIVER");`
(`sdl3/src/SDL_hints.c:99`). A named driver is the only one tried (`SDL_audio.c:982-1030`);
`dummy` and `disk` are demand-only: `if (bootstrap[i]->demand_only) {` … `continue;`
(`:1035-1036`; flags at `SDL_dummyaudio.c:131`, `SDL_diskaudio.c:180`). "Environment variables
are considered to have override priority." (`sdl3/SDL_hints.h:4854`). `MIX_CreateMixerDevice`
initialises audio itself (`SDL_mixer.c:859`). The disk driver writes raw PCM to
`SDL_AUDIO_DISK_OUTPUT_FILE`, default `"sdlaudio.raw"` (`SDL_diskaudio.c:30`); with no requested
spec it opens S16, stereo, 44100 Hz (`SDL_audio.c:702-711`).

**Pacing.** Both drivers sleep
`device->hidden->io_delay = ((device->sample_frames * 1000) / device->spec.freq);`
(`SDL_dummyaudio.c:52`, `SDL_diskaudio.c:123`), an integer: at 1024 frames and 48 kHz, 21 ms per
21.33 ms of audio, so at most about 1.6 % faster than real time (sleep overrun slows it). The
timescale hints `SDL_AUDIO_DISK_TIMESCALE` and `SDL_AUDIO_DUMMY_TIMESCALE`
(`SDL_hints.h:499`, `:526`) multiply that truncated value:
`device->hidden->io_delay = (Uint32)SDL_round(device->hidden->io_delay * scale);`
(`SDL_dummyaudio.c:58`, `SDL_diskaudio.c:129`). So they cannot remove the drift, and `0` gives a
zero delay: an unpaced run. Captures stay sample-exact either way.

**Recommendation.** #66: log `SDL_GetCurrentAudioDriver()` after the mixer opens; the game never
sets the audio driver with `SDL_HINT_OVERRIDE`. R12 captures record the device format
(`SDL_GetAudioDeviceFormat`) and align by content, not time. A timescale of `0` speeds captures
up but decouples audio from the game's clock: use it only where nothing compares against wall
time. Unit-level audio tests use `MIX_CreateMixer` + `MIX_Generate`: no driver, pacing or file.
Informs #66, #45, #46, #47.

## Superseded or inaccurate advice

| Advice | Status | Applies to us? |
|---|---|---|
| `SDL_HINT_AUDIO_DEVICE_STREAM_ROLE` maps to WASAPI categories (`SDL_hints.h:421`) | WASAPI code is `#if 0`: "Setting AudioCategory_GameChat breaks audio on several devices" (`SDL_wasapi.c:765-783`) | No effect on Windows; PipeWire defaults to "Game" (`SDL_pipewire.c:1130-1136`) |
| `SDL_HINT_AUDIO_CATEGORY` "on iOS and macOS" (`SDL_hints.h:299`) | AVAudioSession code is iOS-only (`SDL_coreaudio.m:312`); Apple lists no macOS [W8] | No-op on macOS |
| SDL2 `SDL_AudioSpec.samples`, `SDL_LockAudio`, `SDL_OpenAudio` | Removed (`README-migration.md:205`, `:217`) | Upstream SDL2 code only |
| `SDL_SOUNDFONTS` / `Mix_SetSoundFonts` | Removed; per-load property (`sdl3_mixer/README-migration.md:404`) | #46 |
| Microsoft: Real-Time Work Queues instead of own threads [W4] | SDL keeps its own MMCSS "Pro Audio" thread | SDL's choice |
| FluidSynth web settings page [W9] | Shows 2.5.4, older than the 2.6.1 pin | Use `fluidsettings.xml` at the pin |
| PulseAudio doxygen [W6] | Shows 16.0; 17.x not checked | Low risk: SDL prefers PipeWire ≥ 1.0 (`SDL_pipewire.c:1338`) |

## Design: one deep audio module

The game already has a real seam: `sound_function_table_t` (`similar/arch/sdl/digi.cpp:65-77`,
tables at `:154-178`) with two adapters, `digi_audio` and `digi_mixer`. Keep it as the
interface; put the SDL3 implementation behind it as a fork module (ADR 3: upstream files get
`#elif SDL_MAJOR_VERSION == 3` branches that call it; ADR 5: SDL userdata casts only in
`sdl_wrap_audio.cpp`).

- **Interface.** The table's ten functions, music (play, stop, pause, volume) and
  `commit_frame()` from `digi_sync_sounds()`. Callers learn nothing about tracks, locks, rates,
  decoders or devices.
- **Depth.** Behind it, everything areas 1-8 recommend. Leverage: every `digi_*` caller and the
  four tickets use it unchanged. Locality: gains, locking and device handling each live in one
  place.
- **Seam to the audio thread.** The per-frame batch and `SDL_AudioStream` puts, under area 4's
  rules.
- **Adapters at the internal seam.** `MIX_CreateMixerDevice` in the game, `MIX_CreateMixer` and
  `MIX_Generate` in tests: two adapters behind the same `MIX_*` calls, a real seam.
- **Platform differences only through SDL** (areas 1, 2, 8): no `__APPLE__` / `_WIN32` /
  `__linux__` in game audio code.

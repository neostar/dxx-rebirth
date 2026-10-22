# Audio research: sources and how to extend it

Sources, open questions and measurement-only questions behind `docs/agents/sdl-audio.md` (#72).
Read this to verify a claim there or to extend the research; to implement, read that doc.

## How to extend this research

**Gaps and open questions (need a source).**

- Apple: no primary document found with AudioQueue or HAL latency figures on macOS; Apple's
  documentation JSON has no dates.
- PipeWire: how the graph picks its quantum from several clients' `node.latency`; the
  `pipewire.conf(5)` page [W5] does not say. Look in PipeWire's scheduling docs or
  `src/pipewire/context.c` at 1.6.10.
- PulseAudio 17 buffer-attribute docs (only 16.0 read).
- Whether TiMidity with SF2 (SDL_mixer `main`) would make FluidSynth optional once released.
- SDL wiki pages were not read separately: the headers are their source (SDL_mixer commits "Sync
  SDL3_mixer wiki -> header").

**Questions only a measurement answers** (ticket that should measure):

- M1 Effect trigger to sound out, per platform, at SDL defaults (#45; macOS first, R16).
- M2 Whether WASAPI takes the IAudioClient3 low-latency path at native rate and loses it at 44.1 kHz on a 48 kHz device (#45, real Windows, not CrossOver).
- M3 CPU cost of 64 tracks resampling 11/22 kHz to the device rate (#45, R11).
- M4 R12 envelope parity SDL2 vs SDL3: resampler change, float mixing vs S16, the #877 saturation scan (#45, #47).
- M5 Song start time and memory with a full SoundFont, reloaded per track start (#46).
- M6 Music saturation at `synth.gain` 1.0 with many voices (#46, saturation scan).
- M7 Default-device switch, Bluetooth headset connect/disconnect, sleep/wake on macOS (#45, R16 checklist).
- M8 Surround devices: effects on front left/right only, music upmix (#45, #46, manual).
- M9 Movie audio lead or lag against video on each platform (#47).
- M10 Disk-driver pacing drift (computed 1.6 % at 1024@48k) in R12 scripts (#45).

## Sources

Accessed 2026-10-22. Pinned raw files: path under `docs/reference/`, URL and SHA-256 in
`tools/dev/reference-docs.txt`. Dates are each source's own.

| Source | Version, date | Claims |
|---|---|---|
| SDL `include/SDL3/SDL_audio.h`, `docs/README-migration.md`, `include/SDL3/SDL_hints.h`, `src/SDL_hints.c`, `src/audio/{SDL_audio.c, SDL_sysaudio.h, SDL_audioresample.c}`, `coreaudio/SDL_coreaudio.m`, `wasapi/SDL_wasapi.c`, `src/core/windows/SDL_immdevice.c`, `pipewire/SDL_pipewire.c`, `pulseaudio/SDL_pulseaudio.c`, `alsa/SDL_alsa_audio.c`, `dummy/SDL_dummyaudio.c`, `disk/SDL_diskaudio.c` | tag `release-3.4.18`, released 2026-10-02 | Areas 1-5, 8, superseded table; quotes inline at `file:line` |
| SDL_mixer `include/SDL3_mixer/SDL_mixer.h`, `docs/README-migration.md`, `src/SDL_mixer.c`, `src/SDL_mixer_internal.h`, `src/SDL_mixer_spatialization.c`, `src/decoder_fluidsynth.c` | tag `release-3.2.4`, released 2026-06-03 | Areas 2-8 |
| SDL_mixer `src/effect_position.c`, `src/codecs/music_fluidsynth.c` | tag `release-2.8.2`, released 2026-05-11 | SDL2 pan law and music gain (area 5, 6) |
| FluidSynth `doc/fluidsettings.xml` | commit `71e85b2ca6bf` "Bump to 2.6.1", 2026-09-19 (the `toolchain.pins` pin) | Gain default, limiter (area 6) |
| PipeWire `src/pipewire/keys.h`, `src/daemon/pipewire.conf.in` | tag `1.6.10`, released 2026-10-22 | `node.latency` "the requested latency of the node as a fraction. Ex: 128/48000" (`keys.h:167-168`); quantum defaults (`pipewire.conf.in:44-48`) |
| [W1] GitHub releases API, `repos/libsdl-org/{SDL,SDL_mixer}/releases` | fetched 2026-10-22 | Latest: SDL `release-3.4.18` 2026-10-02T22:43:21Z; SDL_mixer `release-3.2.4` 2026-06-03T00:39:37Z |
| [W2] GitHub commits API, SDL `path=src/audio`, `sha=release-3.4.x` and `main`, since 2026-05-01 | fetched 2026-10-22 | No audio commit on `release-3.4.x` after the tag except PS2 ones it contains; `main` commits listed in Versions, with their messages quoted from `repos/libsdl-org/SDL/commits/<sha>` |
| [W3] GitHub commits API, SDL_mixer `main` since 2026-06-03 | fetched 2026-10-22 | TiMidity SoundFont work, "cmake: support vendored fluidsynth" |
| [W4] https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio | ms.date 2024-12-13, updated 2025-03-26 | "by default all applications in Windows 10 and later will use 10-ms buffers"; "in order for a system to support small buffers it needs to have updated drivers"; "The inbox HDAudio driver has been updated to support buffer sizes between 128 samples (2.66ms@48kHz) and 480 samples (10ms@48kHz)"; "Microsoft recommends for applications that use WASAPI to also use the Real-Time Work Queue API" |
| [W5] https://docs.pipewire.org/page_man_pipewire_conf_5.html | PipeWire 1.6.10, no date stated | `default.clock.quantum`: "Default quantum used when no client specifies one."; no statement on how client latencies combine |
| [W6] https://freedesktop.org/software/pulseaudio/doxygen/structpa__buffer__attr.html | PulseAudio 16.0, no date stated | `PA_STREAM_ADJUST_LATENCY`: "the overall latency of the sink plus the playback buffer size is configured to this value." |
| [W7] https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-initializesharedaudiostream | ms.date 2018-12-05, updated 2025-09-05 | Supported stream flags: "AUDCLNT\_STREAMFLAGS\_EVENTCALLBACK" only; "PeriodInFrames must also be greater than or equal to the value returned in pMinPeriodInFrames" |
| [W8] https://developer.apple.com/tutorials/data/documentation/avfaudio/avaudiosession.json | no date stated | Platforms: iOS, iPadOS, Mac Catalyst, tvOS, visionOS, watchOS; macOS absent |
| [W9] https://www.fluidsynth.org/api/settings_synth.html | libfluidsynth 2.5.4, no date stated | Superseded by the pinned `fluidsettings.xml` |
| [W10] https://learn.microsoft.com/en-us/windows/win32/coreaudio/audclnt-streamflags-xxx-constants | ms.date 2018-05-31, updated 2025-09-03 | `AUTOCONVERTPCM`: "A channel matrixer and a sample rate converter are inserted as necessary"; `SRC_DEFAULT_QUALITY`: "better quality than the default conversion but with a higher performance cost" |
| [W11] https://developer.apple.com/tutorials/data/documentation/audiotoolbox/audioqueuenewoutput(_:_:_:_:_:_:_:).json | no date stated | "For linear PCM, only interleaved formats are supported."; available macOS 10.5 |

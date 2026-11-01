# Sources for game-audio-mixing.md

Fetch date of every entry: 2026-11-01. Each entry gives the date the source states.

## Repository code (Part 1)

Read with `git show <commit>:<path>` in this repository; GPL (Parallax licence banner on the
`main/` files). Code is quoted briefly in the doc with `path:line`.

| Source | Date stated | What it backs |
|---|---|---|
| `b94413b91` "initial import" (zicodxx): `main/digiobj.c`, `main/gameseg.c`, `main/wall.h`, `main/wall.c`, `main/menu.c`, `main/old/windigi.c`, callers in `main/*.c` | commit 2006-03-20; files "COPYRIGHT 1993-1999 PARALLAX SOFTWARE CORPORATION" | the original's classes, 3D model, channel rule |
| `b94413b91:arch/dos/digi.c` (D2X port) | header log 2005-02-25 "rip out digiobj stuff, use d2src system for persistent sounds" | the port's mixer is not HMI SOS |
| D2X CVS commit "use d2src system for persistent sounds" (Bradley Bell) | 2004-11-29 | origin of the port's channel rule |
| `4a98e796a` "Prevent stacking weapon rotation sounds" (Kp), message quoted: "Delete an ugly hack that prevented the patched logic from ever running." | 2018-05-13 | object-linked one-shots tracked |
| `5ca182f4d` "Link robot sounds to the object, not a position" (Kp) | 2021-06-28 | robot sounds tracked |
| Fork `sdl-gpu-port`: `similar/main/digiobj.cpp`, `similar/arch/sdl/digi.cpp`, `digi_mixer.cpp`, `digi_audio.cpp`, `common/sdl/effect_mixer.h`, `common/sdl/audio_gains.h`, `common/include/digi_audio.h`, `similar/main/ai.cpp`, `similar/main/fireball.cpp` | read 2026-11-01 | the "Today's code" table |

## Web sources (Part 2)

Gathered by a research subagent of this run; the method notes below are its own. Entries marked
"Fetch: WebFetch" passed through a summarising model: their quotes match that tool's output and
were not re-checked against the raw page in this run (a re-check is the first step of extending
the doc). Entries marked "Fetch: curl" are the page text.

Fetch date for every entry: 2026-11-01.

Method notes:
- "WebFetch" entries: the page was fetched with WebFetch, which passes the page through a small model. The quotes below are copied from that tool's output. Re-check against the page before publishing a quote.
- "curl" entries: static HTML fetched with curl and stripped of tags. These quotes are the page text (tag stripping left a space before some punctuation; kept as is).
- Audiokinetic (Wwise) docs, blog and Q&A returned HTTP 403 to both WebFetch and curl. No Wwise vendor page could be fetched. See Gaps.
- FMOD docs on fmod.com are a JavaScript app and return no text. The static mirror d1s9dnlmdewoh1.cloudfront.net (the host fmod.com's own docs search results point to) served the text.

---

## A. Voice limiting and virtualisation

### A1. FMOD Studio User Manual 2.02: Event Macros Drawer Reference (max instances, stealing)
- URL: https://d1s9dnlmdewoh1.cloudfront.net/2.02/studio/event-macro-controls-reference.html
- Date stated: "FMOD Studio User Manual 2.02.35 (2026-05-27)"
- Fetch: curl
- Quotes:
  1. "The max instances property sets a limit on how many instances of this event can play simultaneously using the Virtual Voice System ."
  2. "Quietest : Stop the event instance that is the least audible, taking distance attenuation into consideration."
  3. "Virtualize : The quietest event instances will be virtualized, meaning they are created (if needed) and played but produce no output until an existing event instance stops."
  4. "None : No stealing will occur, therefore no new event instances can be played until an existing event instance stops."
  5. "Range : Oldest, Furthest, Quietest, Virtualize, None"
- Establishes: FMOD Studio's per-event limit has five stealing modes, and "Quietest" stops the least audible instance with distance attenuation counted.

### A2. FMOD Studio User Manual 2.02: Advanced Topics, Stealing and Virtualization
- URL: https://d1s9dnlmdewoh1.cloudfront.net/2.02/studio/advanced-topics.html
- Date stated: FMOD Studio User Manual 2.02 (the same 2.02.35 manual build as A1; the fetched text of this page did not print the footer line)
- Fetch: curl
- Quotes:
  1. "Limiting the number of event instances in play at one time provides a less cluttered listening experience for the player and allows you to keep the audible focus on important game items or events."
  2. "Virtualization always steals the quietest event instance."
  3. "The master track of a virtualized event instance is muted but the playback position continues advancing."
  4. "Because the playback position is still advancing through the event, when it becomes real it is as if the event was never muted, giving the illusion of simply being out of earshot."
  5. "Virtualization and quietest stealing behaviors steal the least audible event instances ."
  6. "The audibility of an event is calculated based on the accumulated gain being applied to the event's master track, as well any alterations applied to gain by fades, automation , and modulation ."
- Establishes: FMOD virtualizes or steals the least audible instance first, audibility is computed from accumulated gain, and a virtualized instance keeps advancing so it resumes in place.

### A3. FMOD Studio 2.02 Troubleshooting: stealing makes events end suddenly
- URL: https://d1s9dnlmdewoh1.cloudfront.net/2.02/studio/troubleshooting.html
- Date stated: "FMOD Studio User Manual 2.02.35 (2026-05-27)"
- Fetch: WebFetch
- Quotes:
  1. "Events with the oldest, quietest, virtualize, or furthest stealing behaviors may end suddenly"
  2. "events with the furthest or none stealing behaviors may not start if too many instances of the event are already playing."
- Establishes: FMOD documents that stealing stops an instance abruptly (no stated fade time) and that some modes refuse the new instance instead.

### A4. FMOD Engine User Manual 2.03: Core API Managing Resources, Virtual Voice System
- URL: https://d1s9dnlmdewoh1.cloudfront.net/2.03/api/managing-resources-in-the-core-api.html
- Date stated: "FMOD Engine User Manual 2.03.15 (2026-09-21)", section 9.1
- Fetch: WebFetch
- Quotes:
  1. "A Channel which is playing far away or with a low volume becomes virtual"
  2. "the software channel with the lowest priority is automatically virtualized."
  3. "regardless of its volume, distance, or gain calculation."
  4. "the FMOD_ADVANCEDSETTINGS::vol0virtualvol field is set to a small non-zero amount, such as 0.001."
  5. "When going virtual, the sound's time will still be ticked and any fade points will still continue to interpolate."
  6. "When channels stop being virtual, they resume from their proper place, part-way through the sound."
- Establishes: Core virtualizes by audibility and priority, with a silence threshold (vol0virtualvol), and resumes elapsed-time position on return. The page describes no ramp when voices swap.

### A5. FMOD Engine User Manual 2.02: ChannelControl::getAudibility
- URL: https://d1s9dnlmdewoh1.cloudfront.net/2.02/api/core-api-channelcontrol.html
- Date stated: "FMOD Engine User Manual 2.02.35 (2026-05-27)"
- Fetch: curl
- Quotes:
  1. "Gets the calculated audibility based on all attenuation factors which contribute to the final output volume."
  2. "The estimated volume is calculated using a number of factors detailed in the Audibility Calculation section of the Core API Managing Resources chapter."
- Establishes: Core exposes a per-channel audibility value covering all attenuation factors. (ChannelGroup priority text was not captured.)

### A6. Unreal Engine 5.8 docs: Sound Concurrency Reference Guide
- URL: https://dev.epicgames.com/documentation/en-us/unreal-engine/sound-concurrency-reference-guide
- Date stated: "Unreal Engine 5.8 Documentation" (no publication date)
- Fetch: WebFetch
- Quotes:
  1. "The maximum number of concurrently active sounds allowed in this group before the Resolution Rule takes effect."
  2. "Stop the quietest sound."
  3. "Stop the lowest priority sound. If all sounds have the same priority, stop the oldest sound instead."
  4. "Stop the farthest sound. If all sounds are the same distance, prevent new sounds from starting."
  5. "The scaling factor (ducking) applied to older sounds, which compounds based on the Volume Scale Mode."
  6. "The time (in seconds) to duck using the Volume Scale."
  7. "The time (in seconds) to recover from volume scaling."
  8. "The fade-out time (in seconds) used if a sound is stopped due to another sound in the group starting."
  9. "The wait time (in seconds) between playing sounds."
- Establishes: Unreal's concurrency has a resolution rule set, a Volume Scale that ducks older sounds with duck and recover times, a Voice Steal Release Time fade-out when stealing, and a retrigger wait time. The page does not use the words "attack" or "release"; "Duck Time" and "Recover Time" are the field descriptions. Max Channels was not on the page.
- Related (search snippet only, not a fetched page, so not citable as a quote): Epic issue UE-122403 reports Voice Steal Release Time is ignored under priority-based rules.

### A7. Unreal Engine 5.8 API: EVirtualizationMode
- URL: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/EVirtualizationMode
- Date stated: Unreal Engine 5.8 (page header), no date
- Fetch: WebFetch
- Quotes:
  1. "Sound continues to play when silent and not virtualize, continuing to use a voice."
  2. "If sound is looping, sound restarts from beginning upon realization from being virtual"
  3. "EXPERIMENTAL - Sound fully virtualizes, but attempts to seek to the proper part of its looping duration when realized."
- Establishes: Unreal's virtualization modes (Play When Silent, Restart, Seek Restart). "Restart with delay" was not found in this run.

---

## B. Ducking and loudness control

### B1. DICE: "Adaptive Mixing in Frostbite" (slides, Möllerstedt and Strandberg, EA DICE)
- URL: https://www.slideshare.net/slideshow/adaptive-mixing-in-frostbite/3128152
- Date stated: no date stated on the page (file name contains "agdc07"; the event is not confirmed by the page)
- Fetch: WebFetch
- Quotes:
  1. "Handle the dynamic range from the quietest sound noticeable to the pain threshold, Some 130 dB"
  2. "Measure loudness at Listener position – Scale all sound sources accordingly"
  3. "HDR Audio functions as a culling algorithm"
  4. "Priority is set according to loudness, loudness is a good approximation for 'importance'"
  5. "HDR Audio is not compression, all sounds are played uncompressed"
  6. "Make Loud sounds sound loud, and quieter sounds sound as loud as loud sounds"
  7. "Other alternatives; duckers, snapshot mixers, scene defines"
- Establishes: DICE's HDR audio scales sources by loudness at the listener, uses loudness as the culling priority (quietest go first), and treated duckers as a weaker alternative.

### B2. DICE: Möllerstedt, Develop '08 slides (Bad Company audio)
- URL: https://www.slideshare.net/slideshow/audio-for-multiplayer-beyond-mixing-case-studies-from-battlefield-bad-company-frostbite/3128141
- Date stated: Develop '08 (per title slide); no date stated otherwise
- Fetch: WebFetch
- Quotes:
  1. "Measure loudness at Listener position – scale all sound sources accordingly"
  2. "Functions similar to HDR lighting"
  3. "HDR Audio works on logical loudness values and does not touch the actual audio waveform"
  4. "Priority Based on Loudness"
- Establishes: Same design as B1 from a second DICE deck; HDR acts on logical loudness, not the waveform.

### B3. Audiokinetic staff on Wwise HDR, MCV/Develop, "Getting ready for HDR audio" (Simon Ashby) (secondary: trade press quoting Audiokinetic)
- URL: https://mcvuk.com/development-news/audio-special-getting-ready-for-hdr-audio
- Date stated: 22 May 2013 (Develop magazine, May 2013 edition)
- Fetch: WebFetch
- Quotes:
  1. "The dynamic range is called ‘the window’, which spans the range between two boundaries – the window top and window bottom."
  2. "At runtime, the sounds playing with volumes higher than the HDR bus threshold push the window up"
  3. "All other quieter sounds are ducked down proportionally."
  4. "such as the ratio and release time at the bus level"
- Establishes: Wwise HDR: loud sounds push the window up and quieter ones are ducked down; release time is a bus property (not explained here).

### B4. "HDR Audio in Wwise: Best Practices (so far)", Game Developer (secondary)
- URL: https://www.gamedeveloper.com/audio/hdr-audio-in-wwise-best-practices-so-far-
- Date stated: May 23, 2013, by Rev Dr Bradley Meyer
- Fetch: WebFetch
- Quotes:
  1. "the Wwise implementation of HDR audio is a relative volume scheme."
  2. "often you only want the initial transient to affect the window and the tail to decay naturally"
  3. "preventing pumping of quiet sounds when a loud sound plays"
  4. "It takes CPU to constantly analyze the envelope of every sound."
- Establishes: Practitioner account that envelope tracking limits window movement to the transient, to avoid pumping, at a CPU cost.

### B5. "Finding Your Way With High Dynamic Range Audio In Wwise", Designing Sound (secondary, guest author Louis-Xavier Buffoni)
- URL: https://designingsound.org/2013/06/finding-your-way-with-high-dynamic-range-audio-in-wwise/
- Date stated: June 21, 2013
- Fetch: WebFetch
- Quotes:
  1. "it is possible to let the HDR system peek into the black box by enabling envelope tracking."
  2. "acting on the logical volume of individual voices"
  3. "or pumping, resulting from “too slow or too fast…or too, um, medium” attack and release settings"
- Establishes: HDR works on each voice's logical volume, envelope tracking is optional, and bad attack and release settings cause pumping. Active range and HDR voice volume were not covered.

### B6. FMOD Studio 2.02: Modulator Reference, Sidechain Modulator
- URL: https://d1s9dnlmdewoh1.cloudfront.net/2.02/studio/modulator-reference.html
- Date stated: "FMOD Studio User Manual 2.02.35 (2026-05-27)"
- Fetch: curl
- Quotes:
  1. "The sidechain modulator uses an envelope follower to monitor the signal of a sidechain effect , using that signal's strength to boost or reduce the value of the property being modulated."
  2. "If the sidechain modulator is attached to a property measured in dB, the sidechain modulator will only lower it."
  3. "The amount property works in conjunction with the attack and release properties which dictate how fast or slow the sidechaining effect is applied."
  4. "If the sidechain's source's volume is below the threshold's minimum property, no adjustment is applied to the modulated property."
- Establishes: FMOD ducking is an envelope follower with amount, attack, release and a threshold range. FMOD's compressor-effect attack and release text was not captured.

### B7. Unreal Engine 5.8: Sound Classes in Unreal Engine
- URL: https://dev.epicgames.com/documentation/en-us/unreal-engine/sound-classes-in-unreal-engine
- Date stated: Unreal Engine 5.8 Documentation (no publication date)
- Fetch: WebFetch
- Quotes:
  1. "A Sound Class can have an array of Sound Class Mixes attached that are triggered automatically"
  2. "These are called _passive_ because they do not need to be manually activated."
  3. "The cutoff frequency for the low-pass filter that impacts all sounds within the Class"
- Establishes: Passive Sound Mix modifiers, with per-class EQ and volume adjusters. The page has no fade times and no "ducking" of classes.

### B8. Unreal Engine 5.8: Audio Mixer overview (submixes)
- URL: https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-mixer-overview-in-unreal-engine
- Date stated: Unreal Engine 5.8 Documentation (no publication date)
- Fetch: WebFetch
- Quotes:
  1. "This mixed, interleaved audio is then fed through the submix's DSP effect chain to generate a final output."
- Establishes: Submix effects run after sounds are mixed (so a submix effect cannot act per voice). Nothing on sidechain on this page.

### B9. Unreal Python API 5.1: SubmixEffectDynamicsProcessorSettings
- URL: https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/SubmixEffectDynamicsProcessorSettings?application_version=5.1
- Date stated: Unreal Python 5.1 (Experimental), no date
- Fetch: WebFetch
- Quotes:
  1. "If set, uses output of provided submix as modulator of input signal for dynamics processor"
  2. "The amount of time to ramp into any dynamics processing effect"
  3. "The amount of time to release the dynamics processing effect"
  4. "The threshold at which to perform a dynamics processing operation"
- Establishes: Unreal's submix dynamics processor can be keyed from another submix, with attack, release and threshold.

### B10. Unreal Engine 5.8: Audio Modulation overview
- URL: https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-modulation-overview-in-unreal-engine
- Date stated: Unreal Engine 5.8 Documentation (no publication date)
- Fetch: WebFetch
- Quotes:
  1. "Conceptually, a modulation signal is carried via a connector from a source to a destination."
- Establishes: Only the connector model; control bus mixes appear as term names with no detail. Weak source.

### B11. Designing Sound: Sony ASWG loudness interview (Garry Taylor) (secondary; ASWG-R001 itself not found)
- URL: https://designingsound.org/2012/07/30/video-games-and-loudness-standards-interview-with-sonys-garry-taylor/
- Date stated: July 30, 2012
- Fetch: WebFetch
- Quotes:
  1. "all content should measure an average of -23 LUFS (Loudness Units, relative to digital Full Scale) for PS3"
  2. "and -18 LUFS for PSVita, with a tolerance of +/-2 LU."
- Establishes: Sony's group recommended about -23 LUFS (consoles) and -18 LUFS (handheld), plus or minus 2 LU. The page does not contain the identifier "ASWG-R001".

### B12. Ries, Schlauch, DiGiovanni (2008), "The role of temporal-masking patterns in the determination of subjective duration and loudness for ramped and damped sounds" (primary, peer reviewed)
- URL: https://pmc.ncbi.nlm.nih.gov/articles/PMC2676627
- Date stated: 2008 (per the fetch output)
- Fetch: WebFetch
- Quotes:
  1. "forward masking has a much longer time course for decay than the buildup of backward masking."
  2. "forward-masked thresholds declined more gradually as the probe moved further from the end of the masker."
  3. "The best-fitting value of τ was relatively short (<11 ms), although in some instances longer time constants were found."
- Establishes: Forward masking decays gradually after the masker ends, with a fitted exponential time constant of roughly 11 ms or less in this study. The commonly cited 200 ms figure was not confirmed by a fetched primary page.

---

## C. Attenuation, occlusion, propagation

### C1. Steam Audio Unreal plugin reference: Steam Audio Source
- URL: https://valvesoftware.github.io/steam-audio/doc/unreal/source.html
- Date stated: "© Copyright 2017-2023, Valve Corporation" (no version)
- Fetch: WebFetch
- Quotes:
  1. "If checked, shortest paths taken by sound as it propagates from the source to the listener will be simulated."
  2. "the baked data stored in this probe batch will be used to look up paths from the source to the listener."
  3. "The proportion of rays that are occluded determine how much of the direct sound is considered occluded."
- Establishes: Steam Audio can simulate shortest propagation paths (baked) and partial ray-based occlusion, the nearest modern analogue to Descent's path-through-segments loudness.

---

## Failed fetches

- Audiokinetic: every URL returned "HTTP 403 Forbidden" (WebFetch) and 403 with a 919-byte body (curl). URLs tried:
  - https://www.audiokinetic.com/en/library/edge/?source=Help&id=managing_voices_and_playback_limits
  - https://www.audiokinetic.com/en/library/edge/?source=Help&id=hdr_high_dynamic_range
  - https://www.audiokinetic.com/en/library/edge/?source=Help&id=ducking_bus
  - https://www.audiokinetic.com/en/library/edge/?source=Help&id=understanding_virtual_voices
  - https://www.audiokinetic.com/en/about/news/2013-1/
  - https://blog.audiokinetic.com/qa/6164/virtual-voices-bad-practice
  - https://www.audiokinetic.com/en/blog/, /library/, /robots.txt, and public-library variants
- https://web.archive.org/... (Audiokinetic pages): "Claude Code is unable to fetch from web.archive.org". Not retried or worked around.
- https://www.fmod.com/docs/2.03/... and /2.00/... (event-macro-controls-reference, troubleshooting, core-api-channelcontrol): HTTP 200 but only a JavaScript shell with no text. Used the cloudfront static mirror instead.
- https://docs.unrealengine.com/5.0/en-US/sound-concurrency-in-unreal-engine/: HTTP 403.
- https://dev.epicgames.com/documentation/en-us/unreal-engine/sound-concurrency-in-unreal-engine and .../API/Runtime/Engine/EMaxConcurrentResolutionRule: returned only "Table of Contents" (no body).
- https://www.ea.com/news/how-hdr-audio-makes-battlefield-bad-company-go-boom and https://www.ea.com/frostbite/news/...: HTTP 404 (frostbite.com, careers.ea.com redirect to the first, forums.battlefield.com redirects to answers.ea.com). The GDC 2009 Clerwall slide download link was therefore not reached.
- https://www.slideshare.net/slideshow/how-high-dynamic-range-audio-makes-battlefield-bad-company-go-boom-1292018/1292018 (the Clerwall GDC 2009 deck, 35 slides): fetched, but the page returned no slide text (only title and thumbnails). Date inferred from file names only ("gdc09", "090415"), not stated. Existence of the talk is also listed on https://www.slideshare.net/aclerwall (title only, no date). No quotes taken.
- https://gdconf.com/news/previewing_gdc_2009_inside_the: fetched, no mention of Clerwall or HDR.
- https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BS.2434-2018-PDF-E.pdf: returned a PDF with no extractable text; no quote taken.

Sources fetched with usable quotes: 20 entries (A1-A7 = 7, B1-B12 = 12, C1 = 1; B10 is weak), plus 2 fetched without usable text (Clerwall deck, ITU PDF).

## Gaps

- Q1 (Wwise virtual voices, return behaviour, volume threshold, playback limits, priority offset at max distance): no primary source; audiokinetic.com blocks fetches. Only search snippets (not fetched pages) mention that "Kill if finite else virtual" is the recommended default.
- Q4 (Wwise auto-ducking fields: fade times, curve, maximum ducking volume, recover time): no source.
- Q5 (Wwise HDR official docs: active range, HDR voice volume vs attenuation distance, window release, continuous attenuation of playing voices): only secondary articles (B3-B5), which cover window and envelope tracking but not active range, HDR voice volume or release time definition. No Audiokinetic blog fetched.
- Q2 partly: FMOD Core ChannelGroup priority text not captured; compressor-effect attack/release not captured; Studio sidechain covered.
- Q3 partly: Unreal "Max Channels" (project/platform setting) and "restart with delay" virtualization not found; Volume Scale "attack/release" are named Duck Time and Recover Time on the page; the individual resolution-rule enum page returned no body (rule text came from the reference guide).
- Q7: no Unreal "Sound Class Ducking" documentation found; Audio Modulation control bus mix detail not obtained (page lists terms only); no first-party statement that submix effects can sidechain from a sound class.
- Q8: the DICE GDC 2009 slide text was not reachable (slideshare page gave no text; EA page 404). The two other DICE decks (B1, B2) cover loudness-based culling and the HDR principle but state no window release time and no explicit "loudest sound defines the window" sentence.
- Q9: ASWG-R001 itself not found (only the 2012 interview, secondary). Forward masking: a peer-reviewed paper was fetched, but the often-quoted 200 ms duration was not confirmed by a primary page. No source for dialogue-over-music ducking convention.
- Q10: Wwise attenuation curves, obstruction/occlusion and Spatial Audio rooms/portals/diffraction: no source (Audiokinetic blocked).
- Q11: Steam Audio covered (C1); Unreal path-based propagation not fetched.
- "Voice limit should cut the quietest first": supported by FMOD A1/A2 ("Virtualization always steals the quietest event instance"), Unreal A6 (Stop Quietest), DICE B1 (priority set by loudness). No source states it as a general rule.
- "Fade time when stealing to avoid clicks": only Unreal's Voice Steal Release Time (A6) is documented. FMOD's docs warn that stolen events "may end suddenly" (A3) and state no fade time.

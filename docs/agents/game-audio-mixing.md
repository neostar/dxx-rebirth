# Game audio mixing: the original's sound model and current practice

Grounding for #102's clipping fix (#104: 64 voices, the original's rules, fixed effects and music
gains), asked for in #103's "Grounding first" comment. Four parts: what Descent 2 did with many sounds, read from
its GPL code and measured in DOSBox; how current engines limit voices and control loudness, from
their vendors' docs; a per-class map between the two; and the design #104 took from it. The fork's SDL audio plumbing (devices,
tracks, threads, the pan law as SDL_mixer applies it) is in `sdl-audio.md`; this doc does not
repeat it.

Sources: `game-audio-mixing-sources.md`. Code citations are `path:line`: `b94413b91:main/...` is
the 2006 import of the D2X source (upstream commit, 2006-03-20, Parallax code "COPYRIGHT
1993-1999"); a bare path is the fork's `sdl-gpu-port` as read on 2026-11-01.

Owner's rules (#102–#104, 2026-11-01), the yardstick for Parts 3 and 4:

- **R-clip**: digital clipping is never acceptable.
- **R-duck**: ducking only where it matches how the sound feels in the world.
- **R-pos**: a positional sound's level follows its position; it is never ducked while close and
  raised as it leaves because a channel freed up; a loop never pumps with unrelated sounds.
- **R-balance**: the relative balance between classes of effects stays as it is.
- **R-setting**: no user setting can clip. The defaults (both sliders at 8) keep today's DXX
  balance of effects and music, and the sliders shift that balance either way.
- **Loudness is not a goal**: a quieter game is fine; the player turns the volume up.

## Part 1. The original's sound model, from the code

### Who wrote which layer

| Layer | In the 2006 import | Author |
|---|---|---|
| Sound objects, 3D model, queue, looping sound (`main/digiobj.c`) | yes | Parallax |
| Callers (`main/*.c`) | yes | Parallax |
| Channel allocation and stealing for Windows (`main/old/windigi.c`) | yes, unused by the port | Parallax (banner "COPYRIGHT 1993-1999 PARALLAX") |
| Channel allocation for DOS, over HMI SOS | **no** | Parallax; never released |
| The mixer that sums the channels (HMI SOS, `HMIDRV.386`) | **no** | HMI; never released |
| `arch/dos/digi.c`, `arch/sdl/digi.c` | yes | D2X port (adb 1998, Bradley Bell 2004-2005) |

`b94413b91:arch/dos/digi.c` is not the DOS original: its `digi_init` returns 1 without opening a
device (`:210-218`), its mixer is an SDL-style 8-bit callback with a clamp table "Taken from
SDL/src/SDL_mixer.c" (`:76-80`), and its header log says "rip out digiobj stuff, use d2src system
for persistent sounds" (`:14`, Bradley Bell, 2005-02-25). The channel rule it copies matches
Parallax's Windows code line for line (below), so the **game logic** in this part is Parallax's;
**how SOS summed and clipped** the channels was measured in DOSBox (#103): the voices add plainly,
anything past 16-bit full scale hard-clips, and one centred `F1_0` voice peaks at 0.150 of full
scale. On the D2 autodemo the original clips 9.15 % of frames; the same demo clips 7.85 % in the
SDL2 build and 14.22 % in the SDL3 build. Only demo playback counts: the intro movie (DOS v1.2 has
no `-nomovies`; demos need `-autodemo`) gave the retracted "never clips" figures.

### Channel count

`Max_sound_channels[NUM_DETAIL_LEVELS-1] = { 2,  4,  8, 12, 16};`
(`b94413b91:main/menu.c:556`), set per detail level by
`digi_set_max_channels( Max_sound_channels[ detail_level ] );` (`:576`). The highest detail level
gives 16 channels. Sample rate 11025 or 22050 Hz (`b94413b91:main/inferno.c:618-623`).

### The classes

| Class | How it starts | Tracked after start? | Protected from stealing? | Code |
|---|---|---|---|---|
| **2D sample** | `digi_play_sample(soundno, max_volume)`: centre pan `0xffff/2` | no | only if `max_volume > F1_0` | `b94413b91:main/digiobj.c:194-206` |
| **2D sample, once** | `digi_play_sample_once`: stops the same sound first, through `digi_find_channel` | no | as above | `:173-191` |
| **3D one-shot** | `digi_link_sound_to_pos`/`_to_object` with `forever == 0`: volume and pan computed **once**, then `digi_play_sample_3d`; dropped if `volume < 10` | **no**: "Hack to keep sounds from building up..." | only if volume after distance `> F1_0` | `:374-379`, `:465-470`, `:209-230` |
| **Sound object, linked to an object** | `digi_link_sound_to_object3(..., forever = 1, ...)`, flags `SOF_LINK_TO_OBJ \| SOF_PLAY_FOREVER` | yes, every frame | yes (`soundobj > -1`, looping) | `:354-432` |
| **Sound object, linked to a position** | `digi_link_sound_to_pos2(..., forever = 1, ...)`, flags `SOF_LINK_TO_POS \| SOF_PLAY_FOREVER` | yes | yes | `:445-519` |
| **Permanent** (`SOF_PERMANENT`, "Part of the level, like a waterfall or fan") | any sound object linked while `Dont_start_sound_objects` is set, i.e. during level load | yes; starts silent and first plays when `digi_sync_sounds` sees its volume change | yes | `:64`, `:410-414`, `:496-501` |
| **Queued** (SoundQ) | `digi_start_sound_queued`: a 32-entry FIFO, one sound at a time, each at `F1_0+1` "Make sure its loud so it doesn't get cancelled!"; an entry older than 30 s is dropped | no | yes (`> F1_0`) | `:837-923` |
| **Looping 2D sound** | `digi_play_sample_looping(soundno, max_volume, loop_start, loop_end)`: one global loop, centre pan, volume by `digi_change_looping_volume` | volume only | yes (looping) | `:256-315` |

`SOF_PLAY_FOREVER` is "Play forever (or until level is stopped), otherwise plays once" (`:63`);
`SOF_LINK_TO_OBJ` "If object dies, then finishes play and quits" (`:61`). 150 sound objects
(`:91`).

**Loop points.** `digi_link_sound_to_object3` takes `loop_start`/`loop_end` (`:354`), and so does
`digi_play_sample_looping` (`:274`). Parallax's Windows layer passes them on:
`ssb.loop_start = loop_start;` `ssb.loop_end = loop_end;` (`b94413b91:main/old/windigi.c:486-491`).
The one caller with loop points is the afterburner: `SOUND_AFTERBURNER_IGNITE` linked forever with
`AFTERBURNER_LOOP_START, AFTERBURNER_LOOP_END` (`b94413b91:main/game.c:1351`, values `:1311-1312`),
then replaced by the one-shot `SOUND_AFTERBURNER_PLAY` on release (`:1357-1358`). So the ignition
plays once and the middle of the sample loops.

**Above `F1_0`.** A volume above `F1_0` (1.0) marks a sound as protected (next section) and, in
`digi_get_sound_loc`, keeps it above 1.0 near the listener. How loud that played depended on the
layer below: the Windows 95 code capped it at full scale (Part 1's table), and DOS did not. The callers:

| Volume | Sound | Code |
|---|---|---|
| `F3_0` | reactor countdown voice, siren | `b94413b91:main/cntrlcen.c:197-217` |
| `F3_0` | `SOUND_HUD_KILL` (multiplayer kill) | `b94413b91:main/multi.c:670` |
| `F1_0*2` (queued) | CTF and hoard announcements | `b94413b91:main/multi.c:4481-4569` |
| `F2_0`, max distance 512 | `SOUND_BADASS_EXPLOSION` when a robot or boss finishes dying | `b94413b91:main/ai2.c:2247`, `:2264` |
| `F1_0*4`, max distance 1024 | boss death roll | `b94413b91:main/ai2.c:2242`, `:2216` |
| `death_roll*F1_0/2` | robot death roll (scale from the robot's data) | `b94413b91:main/ai2.c:2260` |
| `F1_0`, max distance 512 | boss see sound, forever ("F1_0*512 means play twice as loud") | `b94413b91:main/ai2.c:2167`, `multibot.c:1102` |

### Which game sounds fall in each class

By caller in `b94413b91:main/*.c` (grep of the six entry points; editor and `old/` skipped):

| Game sound | Class | Volume | Code |
|---|---|---|---|
| Player's own weapon fire | 2D sample | `F1_0`; own Vulcan `F1_0 / 2` "Make your own vulcan gun 1/2 as loud" | `laser.c:566-567`, `:744-747` |
| Other ships' and robots' weapon fire | 3D one-shot at the muzzle | `F1_0` | `laser.c:569`, `:749` |
| Weapon hits (wall, door, robot, player, water, clutter, forcefield) | 3D one-shot at the hit point | `F1_0` | `collide.c:341-2454` (about 30 sites) |
| Player hits wall | 3D one-shot | by impact | `collide.c:383` |
| Explosions: robot `exp1`/`exp2`, walls, lights, reactor | 3D one-shot | `F1_0` | `collide.c:1694`, `fireball.c:1252`, `:1337`, `collide.c:663`, `:1104` |
| Badass (mega, smart, earthshaker) explosions | 3D one-shot linked to the object (position taken once) | `F1_0` | `fireball.c:301`, `:322`, `collide.c:855` |
| Robot see and attack sounds | 3D one-shot at the robot | `Robot_sound_volume = F1_0` | `ai2.c:1589-1638`, `ai.h:150` |
| Robot and boss death roll, final explosion | 3D one-shot linked to the robot, above `F1_0` | see table above | `ai2.c:2216-2264` |
| Boss see sound | sound object, forever, on the boss | `F1_0`, 512 | `ai2.c:2167` |
| Robot materialising at a matcen | 3D one-shot | `F1_0` | `fuelcen.c:517` |
| Doors open, close; cloak walls; walls removed; forcefield off | 3D one-shot at the side's centre | `F1_0` | `wall.c:473-953`, `switch.c:288`, `:401` |
| Forcefield hum | sound object at a position, forever | `F1_0/2` | `switch.c:310`; killed `:290-294` |
| Animated-texture level sounds (falls, machinery) | **permanent** sound objects, created at level load, forever | `F1_0/2` | `gameseq.c:612-647` |
| Lava, water ambience in the player's segment | 2D sample at random times | `d_rand() + f1_0/2` (0.5..1.0) | `game.c:2185-2205` |
| Ship under a lavafall or waterfall | sound object on the ship, forever | `F1_0` | `object.c:2056-2073` |
| Afterburner | sound object on the ship, forever, loop points; release as a one-shot | `F1_0` | `game.c:1335-1360` |
| Other players' afterburners | sound object, forever, loop points | `F1_0` | `multi.c:4381` |
| Earthshaker seismic rumble | the looping 2D sound | `F1_0`, then scaled | `weapon.c:831`, `:911`, `:1263` |
| Pickups, HUD messages, weapon selection, cloak/invulnerability off, homing warning, guide-bot | 2D sample (some `_once`) | `F1_0` | `powerup.c`, `gauges.c:1116`, `weapon.c:270-400`, `game.c:1270-1293`, `escort.c` |
| Fuel centre | 2D sample | `F1_0/2` | `fuelcen.c:687`, `:744` |
| Reactor countdown and siren, multiplayer kill | 2D sample, `F3_0` | | above |
| CTF and hoard announcements | queued | `F1_0*2` | `multi.c:4481-4569` |
| Multiplayer sounds sent by other players | 3D one-shot linked to their ship | sent byte `<< 12` | `multi.c:2143-2155` |

**No sound follows a moving object except loops.** Missiles have no flight sound; a robot's,
missile's or explosion's sound is placed where it starts and keeps that volume and pan until it
ends. Only the forever sounds (afterburners, boss, falls, hums, level sounds) are re-positioned.

### The 3D model

All in `digi_get_sound_loc` (`b94413b91:main/digiobj.c:135-171`) and `find_connected_distance`
(`b94413b91:main/gameseg.c:973-1124`).

**Reach.** `max_distance = (max_distance*5)/4;		// Make all sounds travel 1.25 times as far.`
(`digiobj.c:146`). The default `max_distance` is `256*F1_0` (`:442`, `:523`), so 320 units after
the ×1.25; callers pass 512 or 1024 for boss sounds.

**Path distance through the tunnels.** Only if the straight-line distance is under
`max_distance` (`:151`), the game searches the segment graph breadth-first up to
`num_search_segs = f2i(max_distance/20)` segments (`:152`, 16 segments for the default) with
`WID_RENDPAST_FLAG+WID_FLY_FLAG` (`:155`). The distance returned is listener to the first segment
centre, centre to centre along the path, last centre to source (`gameseg.c:1107-1116`). The search
is breadth-first by segment count, so the path is the one with the fewest segments, not the
shortest in units. No path within the depth: `-1` (`:1050-1053`, `:1063-1066`), and the sound is
silent.

What passes sound: a side passes if `WALL_IS_DOORWAY(seg, side) & (WID_RENDPAST_FLAG|WID_FLY_FLAG)`
is non-zero (`b94413b91:main/wall.h:81-93`, `:185`; `wall.c:139-199`):

| Side | `wall_is_doorway` | Sound |
|---|---|---|
| no wall, open wall, opened door, blasted wall, illusion turned off | fly + render-past | passes |
| illusory wall | fly (+ render-past if transparent) | passes |
| door while opening | `WID_TRANSPARENT_WALL` (render-past) | passes |
| transparent closed door or grate, transparent blastable wall | render-past | passes |
| cloaked wall | render-past | passes |
| closed opaque door, intact opaque blastable wall, solid side | render only | **blocked** |

Blocking is binary: no muffling, no transmission loss. When a door starts to open the sound goes
from silent to its full distance level in one frame.

Results over 80 units are cached for up to 2 s, 8 entries (`gameseg.c:917-918`, `:1011-1023`).

**Volume.** `*volume = max_volume - fixdiv(path_distance,max_distance);` (`digiobj.c:157`): linear
in path distance, 1.0 lost over `max_distance`. With `max_volume = F1_0` the level reaches 0 at the
reach; with `max_volume = F2_0` it is still 1.0 at the reach and then drops to 0 at once (the
straight-line check or the depth limit). In dB the linear law is gentle near and steep far:
half the reach is −6 dB, three quarters −12 dB.

**Pan.** `angle_from_ear = vm_vec_delta_ang_norm(&listener->rvec,&vector_to_sound,&listener->uvec);`
then `*pan = (cosang + F1_0)/2;` (`:160-164`), mirrored by the reverse-stereo option (`:163`). The
direction is the **straight line** to the source, not the path's last opening: a sound heard round a
corner pans towards where the source is, not where the opening is. Right is 1, left 0; ahead,
behind, above and below are all centre. Pan does not depend on distance.

**Update rate.** `digi_sync_sounds()` runs once per game frame (`b94413b91:main/game.c:2349`). For
each used sound object it recomputes volume and pan and calls `digi_set_channel_volume` or
`digi_set_channel_pan` only when they changed (`digiobj.c:611-716`). The change is a step at frame
rate; any smoothing was the mixer's (measurement question).

**Out of earshot and back.** When a tracked sound's volume falls below 1 (`:684`): a forever sound
stops its channel and keeps its sound object (`:687-694`); a once sound is dropped (`:696-699`).
When a forever sound's volume becomes positive again and it has no channel, `digi_start_sound_object`
starts it **from the beginning** (`:702-703`, `:340-345`). The restart is tried only on a frame
where the volume changed (`:683`), so a loop that failed to get a channel waits for the listener or
source to move. A one-shot that cannot get a channel at start is cancelled: "If it's a one-shot
sound effect, and it can't start right away, then just cancel it and be done with it." (`:423-428`).

**Ambient quarter.** `// only use up to half the sound channels for "permanent" sounts` followed by
`if ((SoundObjects[i].flags & SOF_PERMANENT) && (N_active_sound_objects >= max(1, digi_get_max_channels() / 4)))`
`return;` (`:334-336`). The comment says half, the code a quarter: 4 of 16 channels. The count is
of all sound objects holding a channel, not only permanent ones, so active afterburners, hums and
falls also keep level sounds from starting.

**Channel stealing and protection.** Parallax's Windows layer
(`b94413b91:main/old/windigi.c:459-558`; the port's `arch/dos/digi.c:245-308` is the same logic):

1. Start at the round-robin cursor `next_channel`.
2. Take it if it is unused or has finished (`:496-498`).
3. Else, if it is not persistent, stop it and take it: `SSStopChannel(...); break;	// use this channel!`
   (`:500-503`).
4. Else move to the next channel; after a full turn, refuse: `"OUT OF SOUND CHANNELS!!!"`
   (`:504-510`).
5. A stolen sound object loses its channel (`digi_end_soundobj`), a stolen queue entry is ended
   (`:512-519`).
6. Protection: `if ( (soundobj > -1) || (looping) || (volume>F1_0) )` `persistant = 1;`
   (`:549-550`).
7. Advance the cursor (`:552-555`).

The cursor always points at the channel started longest ago, so the rule steals **the oldest
unprotected sound**, even while other channels sit free, and never weighs loudness or distance. A
quiet distant hit can take the channel of a loud close explosion that started earlier. Loops,
sound objects and anything above 1.0 are never stolen; if all 16 are protected, the new sound is
dropped.

`digi_end_sound` (used when a once sound object's object dies or goes out of earshot) only clears
the link and protection and lets the sample finish (`windigi.c:416-425`), as `SOF_LINK_TO_OBJ`'s
comment says.

### Today's code against the original

| Rule | Original | Today | Since |
|---|---|---|---|
| Classes, flags, 150 objects, SoundQ, looping 2D sound | as above | same (`similar/main/digiobj.cpp:61-69`, `:113`, `:782-860`) | |
| Reach ×1.25, search depth, wall mask, linear volume, cos pan, `volume < 10` | as above | same (`digiobj.cpp:210-228`, `:274`) | |
| Update once per frame, release and restart from the beginning | as above | same (`digiobj.cpp:591-700`; `game.cpp:2143`) | |
| Ambient quarter | `digi_get_max_channels() / 4` | `digi_max_channels / 4` with `digi_max_channels` a constant 16 (`common/include/digi_audio.h:18`, `digiobj.cpp:395`): still 4 though the SDL_mixer builds have 64 channels | |
| 3D one-shot linked to an **object** | position taken once | **tracked every frame** as a sound object (`digiobj.cpp:441-479`): upstream deleted the shortcut ("Delete an ugly hack that prevented the patched logic from ever running") | upstream 4a98e796a, 2018-05-13 |
| Robot see and attack sounds | 3D one-shot at a position | linked to the robot, so tracked (`similar/main/ai.cpp:1808-1854`): "Link robot sounds to the object, not a position" | upstream 5ca182f4d, 2021-06-28 |
| 3D one-shot linked to a **position** | position taken once | same (`digiobj.cpp:509-514`) | |
| `digi_play_sample_once` | stops an earlier copy | object-linked `sound_stack::cancel_previous` kills and restarts the earlier copy (`digiobj.cpp:135-165`) | upstream 4a98e796a |
| Channels | 16 at top detail | `-nosdlmixer`: 16 (`digi_audio.cpp:229-237`, `digi_audio.h:18`); SDL2 and SDL3 builds: 64 (`digi_mixer.cpp:320`, `common/sdl/effect_mixer.h:38`) | |
| Full channels | steal oldest unprotected | `-nosdlmixer`: same (`digi_audio.cpp:367-408`); SDL2 and SDL3: **no stealing**, the first free channel or the new sound is refused (`digi_mixer.cpp:323-330`, `:776-782`; `effect_mixer.h:133-152`) | |
| Protection by `soundobj`, looping, `> F1_0` | yes | `-nosdlmixer` only; the SDL_mixer builds ignore the `sound_object *` argument (`digi_mixer.cpp:768`) | |
| Volume above 1.0 | Windows 95: capped at full scale. `windigi.c:476-477` passes `volume × digi_volume`, and `XlatSSToDSVol` caps the DirectSound attenuation at 0 dB (`b94413b91:unused/win95/ds.c:483-495`); at slider 8, 0 dB is already reached at half volume. DOS: `2·F1_0` plays at 2×, but `3·F1_0` at 1× (answer 2 below) | SDL_mixer builds clamp at 1.0: `fix2byte` returns 255 from `UINT8_MAX << 8` (`digi_mixer.cpp:341-350`; `common/sdl/audio_gains.h:29-37`); so the `F3_0` countdown plays at the level of an `F1_0` pickup | |
| Loop points | honoured (`windigi.c:486-491`) | ignored in every build: `(void)loop_start; (void)loop_end;` (`digi_mixer.cpp:790-791`); `-nosdlmixer` wraps to the sample start (`digi_audio.cpp:280-286`). The afterburner loop replays its ignition | lost in the port; the 2006 DOS port already wrapped to the start (`b94413b91:arch/dos/digi.c:190-197`) |
| `digi_end_sound` | unlinks, the sample finishes | `-nosdlmixer`: same (`digi_audio.cpp:468-478`); SDL2 and SDL3: **stops the channel** (`digi_mixer.cpp:830-834`; SDL3 `:200-205`, `MIX_StopTrack(..., 0)` at `effect_mixer.h:446`). A once sound object whose object dies is cut off | |
| Pan law | near side at full gain, far side falls linearly (measured, #103): centre +6 dB per side against SDL2 | linear, −6 dB centre per side (`sdl-audio.md` area 5) | |

The last two object-linked changes matter for #104. Since 2018, explosions and robot sounds follow
their object, which R-pos wants; but with `digi_end_sound` stopping the channel in the SDL_mixer
builds, a sound linked to an object that dies within its length stops early, with a hard cut (no
fade, `MIX_StopTrack(..., 0)`). `explode_badass_weapon` links `SOUND_BADASS_EXPLOSION` to the
weapon object that is exploding (`similar/main/fireball.cpp:622`). Whether that object lives long
enough for the sound to finish is a measurement question.

Today's tracked one-shots also count in `N_active_sound_objects`, so in a fight the ambient quarter
(4) fills with robot and explosion sounds and level sounds stop starting, which the original's
count (mostly loops) did not do.

## Part 2. Current practice, from vendor sources

Source ids (A1, B3, ...) point into `game-audio-mixing-sources.md`. **Audiokinetic's site refused
every fetch (HTTP 403)**, so no Wwise vendor page backs this part; what is said of Wwise comes
from Audiokinetic staff in trade press (B3, B5) and a practitioner (B4), marked secondary.

### Voice limiting and virtualisation

**Limit per sound, cull by audibility.** FMOD Studio caps instances per event: "The max instances
property sets a limit on how many instances of this event can play simultaneously" (A1), with the
stealing modes "Oldest, Furthest, Quietest, Virtualize, None" (A1). "Quietest : Stop the event
instance that is the least audible, taking distance attenuation into consideration." (A1).
"Virtualization always steals the quietest event instance." (A2). Audibility "is calculated based
on the accumulated gain being applied to the event's master track, as well any alterations applied
to gain by fades, automation , and modulation" (A2). FMOD's stated reason: "a less cluttered
listening experience for the player and allows you to keep the audible focus on important game
items or events" (A2).

Unreal's Sound Concurrency gives a per-group maximum ("The maximum number of concurrently active
sounds allowed in this group before the Resolution Rule takes effect.") and rules such as "Stop the
quietest sound.", "Stop the lowest priority sound. If all sounds have the same priority, stop the
oldest sound instead." and "Stop the farthest sound. If all sounds are the same distance, prevent
new sounds from starting." (A6).

DICE's Frostbite put loudness first: "Priority is set according to loudness, loudness is a good
approximation for 'importance'" and "HDR Audio functions as a culling algorithm" (B1).

**Virtual voices: keep time, or restart.** FMOD Core: "A Channel which is playing far away or with
a low volume becomes virtual" (A4); "When going virtual, the sound's time will still be ticked"
and "When channels stop being virtual, they resume from their proper place, part-way through the
sound." (A4). Studio: "when it becomes real it is as if the event was never muted, giving the
illusion of simply being out of earshot." (A2). Unreal offers both: "If sound is looping, sound
restarts from beginning upon realization from being virtual" and an experimental mode that
"attempts to seek to the proper part of its looping duration when realized" (A7). Descent restarts
a loop from the beginning (Part 1, out of earshot); for a seamless loop the two sound alike, for
the afterburner (ignition, then a loop) they do not.

**Stealing: fade or cut.** Unreal: "The fade-out time (in seconds) used if a sound is stopped due
to another sound in the group starting." (A6, Voice Steal Release Time). FMOD warns that stolen
events "may end suddenly" (A3) and states no fade. Descent's `SSStopChannel` cut at once (Part 1).

**Voice-level ducking in a concurrency group.** Unreal's Volume Scale is "The scaling factor
(ducking) applied to older sounds, which compounds based on the Volume Scale Mode", with "The time
(in seconds) to duck using the Volume Scale." and "The time (in seconds) to recover from volume
scaling." (A6). This is the newest-first ducking the closed #105 prototype built, and the one R-pos rules
out for positional sounds.

### Ducking and loudness control

**HDR (DICE, then Wwise).** DICE measured loudness at the listener and scaled every source:
"Measure loudness at Listener position – Scale all sound sources accordingly" (B1, B2), "Handle the
dynamic range from the quietest sound noticeable to the pain threshold, Some 130 dB" (B1), and it
is not a compressor: "HDR Audio is not compression, all sounds are played uncompressed" (B1);
"HDR Audio works on logical loudness values and does not touch the actual audio waveform" (B2).
DICE listed "duckers, snapshot mixers, scene defines" as the "Other alternatives" (B1). The GDC
2009 deck itself (Clerwall) gave no text when fetched; its window release time is not sourced
here.

Wwise HDR as Audiokinetic described it (secondary): "The dynamic range is called ‘the window’",
"the sounds playing with volumes higher than the HDR bus threshold push the window up" and "All
other quieter sounds are ducked down proportionally." with "the ratio and release time at the bus
level" (B3). So the window follows the loudest sound, and a quiet newcomer does not move it. It
acts "on the logical volume of individual voices" (B5). On pumping: "often you only want the
initial transient to affect the window and the tail to decay naturally", which envelope tracking
does, "preventing pumping of quiet sounds when a loud sound plays", at a CPU cost (B4); bad
"attack and release settings" cause "pumping" (B5). **Whether Wwise HDR keeps attenuating playing
voices continuously, and with which release law, has no source in this run** (Gaps).

**Sidechain ducking (FMOD, Unreal).** FMOD's sidechain modulator "uses an envelope follower to
monitor the signal of a sidechain effect", and on a dB property "will only lower it"; it has amount,
attack, release and a threshold below which "no adjustment is applied" (B6). Unreal's submix
dynamics processor can be keyed from another submix ("uses output of provided submix as modulator
of input signal") with attack, release and threshold (B9). Submix effects act on the mix, not on a
voice: the audio "is then fed through the submix's DSP effect chain" (B8). Unreal's sound classes
carry passive mixes "triggered automatically" (B7); no "sound class ducking" page was found.

**Masking.** One peer-reviewed source fetched: "forward masking has a much longer time course for
decay than the buildup of backward masking", with a fitted time constant "relatively short
(<11 ms), although in some instances longer time constants were found" (B12). No source in this
run for simultaneous masking (a faint sound during a loud one); #104 does not rely on masking.

**Loudness targets.** Sony's audio group (interview, secondary): "an average of -23 LUFS ... for
PS3" and "-18 LUFS for PSVita, with a tolerance of +/-2 LU" (B11). This is integrated loudness, not
peak; it does not by itself prevent clipping.

### Attenuation and occlusion, as context

Steam Audio can simulate "shortest paths taken by sound as it propagates from the source to the
listener" from baked probe data, and makes occlusion partial: "The proportion of rays that are
occluded determine how much of the direct sound is considered occluded." (C1). Descent's model is a
coarse ancestor of the first: path length through open segments sets the level, while occlusion is
all or nothing (a closed door silences, Part 1). Wwise rooms, portals and diffraction were not
fetched (Gaps).

## Part 3. Mapping

Columns: what the original did (Part 1); what current engines do for that kind of sound (Part 2);
options that obey R-clip, R-duck and R-pos; where ducking or masking fits.

| Class | Original | Current engines | Options for 64 channels | Ducking or masking |
|---|---|---|---|---|
| **2D samples**: own weapon fire, pickups, HUD, menu | stealable at `F1_0`; own Vulcan at half | per-event max instances, steal oldest (A1, A6) | per-sound instance cap (own Vulcan, pickups); fixed per-voice headroom; steal the oldest copy of the same sound with a short fade (A6) | Not positional, so R-pos does not bind; but they are the player's own feedback and the densest class (Vulcan). Ducking them under explosions changes the feel of firing; a cap is safer than a duck. |
| **3D one-shots at a position**: hits, doors, other ships' fire, explosions | level fixed at start; stealable; oldest stolen first | cull quietest or farthest (A1, A6); loudness priority (B1) | admission by audibility: a new sound below the quietest playing one is not started when full; when a sound must go, the quietest one-shot ends with a short fade (A6) | Fits at **onset only**: a faint distant hit starting under a loud close explosion may be skipped (DICE culling, B1). Lowering one that already plays breaks R-pos (its level would no longer match its fixed position). |
| **3D one-shots on an object**: robot sounds, badass explosions (tracked since 2018) | not tracked in 1996; tracked today | same as above, with distance updates | as above; level follows the object every frame (R-pos asks for it); decide whether a sound ends with its object (today's SDL builds cut) or finishes (original intent, `SOF_LINK_TO_OBJ`) | Same as above. A "missile flying right past" is this class: R-pos forbids lowering it near and raising it as it leaves. |
| **Loud, protected** (`> F1_0`): countdown, siren, boss death, multiplayer kill | never stolen; at full scale (Windows 95) or louder (DOS) | high priority, never culled | reserve headroom for them; restore their extra level (clamped at 1.0 today) within the budget | Countdown and siren are non-positional alarms: other one-shots yielding to them at onset is real-world plausible (an alarm masks). Boss death is positional: R-pos applies. |
| **Sound objects, forever**: afterburners, falls, hums, boss see | protected; tracked; out of earshot stop, back from the beginning; 4-channel quarter for permanents | virtual voices that keep time (A2, A4) or restart (A7) | reserve each at its maximum level; virtualise out of earshot and resume in place, or restart as the original did (afterburner: restart replays ignition) | **Never.** R-pos: "a loop must not pump with unrelated sounds". |
| **Permanent level ambience**: animated-texture sounds | quarter of channels, counted with all sound objects | ambience bed, often a bus of its own | lift the quarter to a quarter of 64 or decouple it from tracked one-shots (today a fight starves it) | Never on the loop's level (R-pos). Starting new ambience during a fight is a choice, not a duck. |
| **Queued announcements** (CTF, hoard) | one at a time, `F1_0+1`, 30 s life | dialogue-style priority | keep the queue; reserve like an alarm | The case where engines duck others (dialogue over music); but the original never lowered effects for them. |
| **Looping 2D**: earthshaker rumble | one global loop, scripted volume | music/ambience bus | as a forever sound | Its level is scripted by the game; leave it. |
| **Music** (FM/MIDI/CD in the original, outside the digital path) | a separate device, own volume | music bus, sidechain ducked under dialogue (B6, B9) | separate gain stage and headroom share (#103's FM capture gives the balance) | **Fits:** music under loud effects or the briefing voice; slow attack and release so it does not pump (B5). Music has no position, so R-pos does not bind. |

### Conflicts between the constraints

- **R-clip against R-pos.** Without changing the level of a playing positional sound, only three
  levers remain: per-voice headroom, admission (refuse or cull at start), and ending a sound.
  Headroom for the theoretical worst case is large: 64 voices at full scale need about −36 dB
  each (20·log10(64)); 16 voices about −24 dB. Measured on the demo, the peak is far lower (Part 4).
- **Admission by audibility against the original's choice.** Quietest-first (A1, A6, B1) keeps
  the loud close sounds the original sometimes lost to oldest-first stealing, but changes which
  sounds the player hears, so it is a deliberate deviation from 1996.
- **Original loudness against headroom.** DOS played `2·F1_0` sounds at twice full volume, and
  the Windows 95 code capped them at full scale. #104 follows Windows 95 (Part 4), which costs no
  headroom.
- **Loop restart against seamlessness.** Restarting from the beginning (original, A7) is faithful
  but replays the afterburner's ignition; resuming in place (A2, A4) is smoother but not what 1996
  did.
- **Fade on end against the original's cut.** A short fade avoids a click (A6) but means two
  sounds overlap for its length, which the budget must count.
- **Tracked one-shots against the cut on object death.** R-pos favours tracking (since 2018), but
  in the SDL builds the sound then stops with its object; fixing that keeps a voice alive whose
  object no longer exists.
- **Linear distance law with `max_volume > F1_0`.** Such a sound is still at 1.0 at its reach and
  then drops to silence: a step that R-pos would call a level not following position.

## Part 4. The design #104 took

#104's criteria are the source of truth for the fix; this part records why.

**Measured with the offline mixer** (#103: an SDL2 event log and the dumped chunks replay the
capture bit-exact on 99.69 % of frames). The demo's peaks come from one shared impact sample
started up to 6 times per buffer, once per projectile hit. Gain each rule set needs for zero
clipping on the demo:

| Rules | Gain for zero clipping |
|---|---|
| Today, 64 voices, 65th refused | −14.5 dB |
| 16 voices, original stealing | −10.1 dB |
| 16 voices, at most 3 copies of a sound | −7.5 dB |
| 16 voices, identical sounds in one buffer dropped | −5.9 dB |
| **64 voices, original rules** (pan law, oldest unprotected stolen; the demo replays every sound at `F1_0`) | **−15.8 dB** (−15.9 dB with music); peak 6.17× full scale |

**Chosen:** 64 voices with the original's rules, effects and music both −20.6 dB against today's
levels (gain 3/32). Offline, the demo's effects then peak at 0.58 of full scale. In the game,
peaks follow the frame timing: a real-time capture peaked at 0.63–0.78, so the worst case with
music at full scale is about 0.78 plus 0.094, 0.88 (1.1 dB to spare). On its own, a volley of up
to about 20 in-phase copies of the impact sound fits; the demo's volleys reach 6. The first
choice, −18 dB (1/8), failed review: one run peaked at 0.888 without music, which music would
have pushed past full scale (#104).

**Volumes above 1.0 follow Parallax's Windows 95 code from the source release** (owner,
2026-11-01: the later Parallax-sanctioned version wins over DOS). A Windows 95 upgrade shipped in
the Vertigo bundle (November 1996), but nothing ties `main/old/windigi.c` and `unused/win95/ds.c`
to that executable; the protection rests on this source, the cap needs none. They play at full scale, stay at full level farther
away, and are protected from stealing. The cap sits at `F1_0`, as today. The Windows code's slider-8
quirk (full level from half volume up, an artefact of DirectSound's attenuation-only scale) is
not copied, nor are two more: a volume change on a playing sound skips the start's `<<2`
(`windigi.c:387` against `:477`), and the attenuation counts 10 dB per halving, not 6. The pan law stays the DOS one (owner, 2026-11-01). Windows 95 faded the far side across 100 dB
(`XlatSSToDSPan`, `b94413b91:unused/win95/ds.c:468-480`): −25 dB 14° off centre, −50 dB at 30°. Music can never exceed full scale, so this bound
holds for any song and any slider setting (R-setting).

**Rejected:**
- **Copy limits and dropping identical sounds** reach about −8 dB, but they do so by making a
  volley of simultaneous hits no louder than one hit (three in-phase copies are 9.5 dB louder
  than one): R-balance.
- **A loudness budget with ducking or a limiter** (#105, closed): ducking by channel count breaks
  R-pos, and SDL2 clamps after each add, so a master limiter cannot work in every build.
- **Restoring 16 channels:** −13.0 dB with the original's rules, still clipping at louder gains,
  and it loses sounds. More voices pay off with 3D placement (#107, after the fix).

## Extending this doc

- Add Wwise from Audiokinetic's own docs once they can be fetched by a permitted route: virtual
  voice behaviour and "on return to physical voice" options, playback limits and priority offset
  by distance, auto-ducking fields, HDR window release and active range, and Spatial Audio portals.
  Each goes into `game-audio-mixing-sources.md` with its quote and dates, then into Part 2.
- Add the DICE GDC 2009 deck's text (window release time) if a copy with text is found.
- Part 1 was read from code; if the fork changes `digiobj.cpp` or the backends, re-check the
  "Today's code" table.

## Measurement answers and open questions

Answered in DOSBox (#103):

1. A 17th sound in DOS takes the oldest channel, as Parallax's Windows layer does
   (`windigi.c:493-511`), and the stolen sound is cut instantly: it clicks.
2. A `2·F1_0` sound plays twice as loud as an `F1_0` one. A `3·F1_0` sound plays at 1×, which suggests the value wraps.
3. The pan law: the near side stays at full gain and the far side falls linearly.

Still open:

4. Does the afterburner loop between its loop points in DOS (no ignition repeat)?
5. Does SOS step a channel's volume and pan at the frame's change, or ramp it?
6. Does `SOUND_BADASS_EXPLOSION` end early in the SDL builds (the weapon object dies, then
   `digi_end_sound` stops the channel), against its full length in `-nosdlmixer`?
7. How often is the ambient quarter full of tracked one-shots, so that level sounds do not start?
8. The `> F1_0` volumes on the fix: `ND_EVENT_SOUND` replays at `F1_0` (`newdemo.cpp:1248`), so
   the demo cannot show them; #104 tests them with a crafted stimulus.

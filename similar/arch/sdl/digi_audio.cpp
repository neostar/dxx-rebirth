/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 *
 * SDL digital audio support
 *
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <digi_audio.h>
#include "dxxerror.h"
#include "fmtcheck.h"
#include "vecmat.h"
#include "digi.h"
#include "sounds.h"
#include "config.h"
#include "args.h"
#include "piggy.h"

#if SDL_MAJOR_VERSION == 3
#include "console.h"
#if DXX_USE_SDLMIXER
#include "digi_mixer.h"
#endif

/* The plain SDL audio backend (SDL_OpenAudio, an audio callback and
 * SDL_LockAudio) is gone in SDL3, which has audio streams on devices.  With
 * SDL3_mixer built in, its entry points are the mixer's (digi_mixer.cpp), so
 * -nosdlmixer plays the effects there too; without it, the init fails as a
 * device that would not open does and the game runs silent (SndNoSound).
 */
namespace dsx {

#if DXX_USE_SDLMIXER
int digi_audio_init()
{
	return digi_mixer_init();
}

void digi_audio_close()
{
	digi_mixer_close();
}

void digi_audio_stop_all_channels()
{
	digi_mixer_stop_all_channels();
}

sound_channel digi_audio_start_sound(const sound_effect soundnum, const fix volume, const sound_pan pan, const int looping, const int loop_start, const int loop_end, sound_object *const soundobj)
{
	return digi_mixer_start_sound(soundnum, volume, pan, looping, loop_start, loop_end, soundobj);
}

int digi_audio_is_channel_playing(const sound_channel channel)
{
	return digi_mixer_is_channel_playing(channel);
}

void digi_audio_set_channel_volume(const sound_channel channel, const int volume)
{
	digi_mixer_set_channel_volume(channel, volume);
}

void digi_audio_set_channel_pan(const sound_channel channel, const sound_pan pan)
{
	digi_mixer_set_channel_pan(channel, pan);
}

void digi_audio_stop_sound(const sound_channel channel)
{
	digi_mixer_stop_sound(channel);
}

void digi_audio_end_sound(const sound_channel channel)
{
	digi_mixer_end_sound(channel);
}

void digi_audio_set_digi_volume(const int dvolume)
{
	digi_mixer_set_digi_volume(dvolume);
}
#else
int digi_audio_init()
{
	con_puts(CON_URGENT, "This SDL3 build has no SDL3_mixer: playing without sound");
	CGameArg.SndNoSound = 1;
	return 1;
}

void digi_audio_close()
{
}

void digi_audio_stop_all_channels()
{
}

sound_channel digi_audio_start_sound(sound_effect, fix, sound_pan, int, int, int, sound_object *)
{
	return sound_channel::None;
}

int digi_audio_is_channel_playing(sound_channel)
{
	return 0;
}

void digi_audio_set_channel_volume(sound_channel, int)
{
}

void digi_audio_set_channel_pan(sound_channel, sound_pan)
{
}

void digi_audio_stop_sound(sound_channel)
{
}

void digi_audio_end_sound(sound_channel)
{
}

void digi_audio_set_digi_volume(int)
{
}
#endif

}
#else
#include "compiler-range_for.h"
#include "d_sdl_audio.h"
#include "d_underlying_value.h"
#include "digi_policy.h"

namespace dcx {

namespace {

static int digi_initialised = 0;

/* The slot volume, 0..SOUND_MAX_VOLUME, for the digi volume, 0..0x7fff
 * (the menu's DigiVolume * 32768 / 8).
 */
constexpr fix digi_audio_volume(const int dvolume)
{
	const auto v{int64_t{dvolume} * (SOUND_MAX_VOLUME) / 0x7fff};
	return static_cast<fix>(std::clamp<int64_t>(v, 0, SOUND_MAX_VOLUME));
}

/* One 8-bit unsigned effect sample at level (a fix of 0..F1_0) added to the
 * signed 16-bit mix, at the effects gain, the sum clamped to 16 bits.
 */
constexpr int16_t digi_audio_mix(const int16_t mix, const uint8_t sample, const fix level)
{
	const int32_t v{(int32_t{sample} - 0x80) * level};
	return static_cast<int16_t>(std::clamp<int32_t>(mix + v * digi_gain_numerator / (digi_gain_denominator << 8), INT16_MIN, INT16_MAX));
}

/* The menu's top effects volume plays a full-scale centred sample at 3/64
 * of full scale, half the gain, as in the SDL_mixer builds; each step 1..8
 * stays distinct; a volume above F1_0 plays at F1_0 (the cap).
 */
constexpr int digi_audio_level(const int step, const fix volume = F1_0)
{
	const auto mul{[](const fix a, const fix b) { return static_cast<fix>((int64_t{a} * b) >> 16); }};
	return -digi_audio_mix(0, 0, mul(digi_pan_levels(0x7fff).left, mul(digi_audio_volume(step * 32768 / 8), digi_effect_level(volume))));
}
static_assert(digi_audio_level(8) == 32768 * 3 / 64);
static_assert(digi_audio_level(8, F3_0) == digi_audio_level(8));
static_assert(digi_audio_level(0) == 0 && digi_audio_level(1) > 0 && digi_audio_level(1) < digi_audio_level(2) && digi_audio_level(2) < digi_audio_level(3) && digi_audio_level(3) < digi_audio_level(4) && digi_audio_level(4) < digi_audio_level(5) && digi_audio_level(5) < digi_audio_level(6) && digi_audio_level(6) < digi_audio_level(7) && digi_audio_level(7) < digi_audio_level(8));
static_assert(digi_audio_mix(INT16_MAX, 0xff, F1_0) == INT16_MAX && digi_audio_mix(INT16_MIN, 0, F1_0) == INT16_MIN);

struct sound_slot {
	sound_effect soundno;
	bool playing;   // Is there a sample playing on this channel?
	bool looped;    // Play this sample looped?
	bool persistent; // This can't be pre-empted
	sound_pan pan;       // 0 = far left, 1 = far right
	fix volume;    // 0 = nothing, 1 = fully on
	//changed on 980905 by adb from char * to unsigned char *
	std::span<const uint8_t> samples;
	sound_object *soundobj;   // Which soundobject is on this channel
	//end changes by adb
	unsigned int position; // Position we are at at the moment.
};

static void digi_audio_stop_sound(sound_slot &s)
{
	s.playing = 0;
	s.soundobj = sound_object_none;
	s.persistent = 0;
}

static enumerated_array<sound_slot, 32, sound_channel> SoundSlots;
static SDL_AudioSpec WaveSpec;
static sound_channel next_channel;

/* Return the next sound_channel after `c`, and roll back to 0 if incrementing
 * `c` exceeds the maximum available channels.
 */
static sound_channel next(const sound_channel c)
{
	const std::underlying_type<sound_channel>::type v = underlying_value(c) + 1;
	return (v >= digi_max_channels) ? sound_channel{} : sound_channel{v};
}

}

}

namespace dsx {

namespace {

/* Audio mixing callback */
//changed on 980905 by adb to cleanup, add pan support and optimize mixer
static void audio_mixcallback(void *, Uint8 *const stream8, int len)
{
	const auto stream = reinterpret_cast<int16_t *>(stream8);
	const auto streamend = stream + len / sizeof(int16_t);
	if (!digi_initialised)
		return;

	memset(stream8, 0, len); // fix "static" sound bug on Mac OS X

	RAII_SDL_LockAudio lock_audio{};

	range_for (auto &sl, SoundSlots)
	{
		if (sl.playing) {
			auto sldata = std::next(sl.samples.begin(), sl.position), slend = sl.samples.end();
			auto sp = stream;
			const auto levels = digi_pan_levels(static_cast<fix>(sl.pan));
			const auto sl_volume{sl.volume};
			const fix vl = fixmul(levels.left, sl_volume);
			const fix vr = fixmul(levels.right, sl_volume);
			while (sp < streamend) {
				if (sldata == slend) {
					if (!sl.looped) {
						sl.playing = 0;
						break;
					}
					sldata = sl.samples.begin();
				}
				const uint8_t v = *(sldata++);
				*sp = digi_audio_mix(*sp, v, vl);
				++sp;
				*sp = digi_audio_mix(*sp, v, vr);
				++sp;
			}
			sl.position = std::distance(sl.samples.begin(), sldata);
		}
	}
}

}
//end changes by adb

/* Initialise audio devices. */
int digi_audio_init()
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO)<0) {
		Error("SDL audio initialisation failed: %s.",SDL_GetError());
	}

#if DXX_BUILD_DESCENT == 1
	/* Descent 1 sounds are always 11Khz. */
	WaveSpec.freq = underlying_value(sound_sample_rate::_11k);
#elif DXX_BUILD_DESCENT == 2
	/* Descent 2 sounds are available in both 11Khz and 22Khz.  The user may
	 * pick at program start time which to use.
	 */
	WaveSpec.freq = underlying_value(GameArg.SndDigiSampleRate);
#endif
	//added/changed by Sam Lantinga on 12/01/98 for new SDL version
	WaveSpec.format = AUDIO_S16SYS;
	WaveSpec.channels = 2;
	//end this section addition/change - SL
	WaveSpec.samples = /* SOUND_BUFFER_SIZE = */ 1024;
	WaveSpec.callback = audio_mixcallback;

	if ( SDL_OpenAudio(&WaveSpec, NULL) < 0 ) {
		//edited on 10/05/98 by Matt Mueller - should keep running, just with no sound.
		Warning("Failed to open audio: %s", SDL_GetError());
		//killed  exit(2);
		return 1;
		//end edit -MM
	}
	SDL_PauseAudio(0);

	digi_initialised = 1;

	digi_audio_set_digi_volume((CGameCfg.DigiVolume * 32768) / 8);
	return 0;
}

/* Shut down audio */
void digi_audio_close()
{
	if (!digi_initialised) return;
	digi_initialised = 0;
#ifdef __MINGW32__
	SDL_Delay(500); // CloseAudio hangs if it's called too soon after opening?
#endif
	SDL_CloseAudio();
}

void digi_audio_stop_all_channels()
{
	range_for (auto &i, SoundSlots)
		digi_audio_stop_sound(i);
}


// Volume 0-F1_0
sound_channel digi_audio_start_sound(sound_effect soundnum, fix volume, sound_pan pan, int looping, int, int, sound_object *const soundobj)
{
	if (!digi_initialised)
		return sound_channel::None;

	if (soundnum == sound_effect::None)
		return sound_channel::None;

	RAII_SDL_LockAudio lock_audio{};

	const auto starting_channel{next_channel};

	while(1)
	{
		if (!SoundSlots[next_channel].playing)
			break;

		if (!SoundSlots[next_channel].persistent)
			break;	// use this channel!

		next_channel = next(next_channel);
		if (next_channel == starting_channel)
		{
			return sound_channel::None;
		}
	}
	if (SoundSlots[next_channel].playing)
	{
		SoundSlots[next_channel].playing = 0;
		if (SoundSlots[next_channel].soundobj != sound_object_none)
		{
			digi_end_soundobj(*SoundSlots[next_channel].soundobj);
		}
		if (SoundQ_channel == next_channel)
			SoundQ_end();
	}

#ifndef NDEBUG
	verify_sound_channel_free(next_channel);
#endif

	SoundSlots[next_channel].soundno = soundnum;
	SoundSlots[next_channel].samples = GameSounds[soundnum].span();
	SoundSlots[next_channel].volume = fixmul(digi_volume, digi_effect_level(volume));
	SoundSlots[next_channel].pan = pan;
	SoundSlots[next_channel].position = 0;
	SoundSlots[next_channel].looped = looping;
	SoundSlots[next_channel].playing = 1;
	SoundSlots[next_channel].soundobj = soundobj;
	SoundSlots[next_channel].persistent = 0;
	if (soundobj || looping || volume > F1_0)
		SoundSlots[next_channel].persistent = 1;

	const auto i{next_channel};
	next_channel = next(next_channel);

	return i;
}

//added on 980905 by adb from original source to make sfx volume work
void digi_audio_set_digi_volume( int dvolume )
{
	digi_volume = digi_audio_volume(dvolume);

	if ( !digi_initialised ) return;

	digi_sync_sounds();
}
//end edit by adb

int digi_audio_is_channel_playing(const sound_channel channel)
{
	if (!digi_initialised)
		return 0;

	return SoundSlots[channel].playing;
}

unsigned digi_audio_get_max_channels()
{
	return digi_max_channels;
}

void digi_audio_set_channel_volume(const sound_channel channel, int volume)
{
	if (!digi_initialised)
		return;

	if (!SoundSlots[channel].playing)
		return;

	SoundSlots[channel].volume = fixmuldiv(digi_effect_level(volume), digi_volume, F1_0);
}

void digi_audio_set_channel_pan(const sound_channel channel, const sound_pan pan)
{
	if (!digi_initialised)
		return;

	if (!SoundSlots[channel].playing)
		return;

	SoundSlots[channel].pan = pan;
}

void digi_audio_stop_sound(const sound_channel channel)
{
	digi_audio_stop_sound(SoundSlots[channel]);
}

void digi_audio_end_sound(const sound_channel channel)
{
	if (!digi_initialised)
		return;

	if (!SoundSlots[channel].playing)
		return;

	SoundSlots[channel].soundobj = sound_object_none;
	SoundSlots[channel].persistent = 0;
}

}
#endif

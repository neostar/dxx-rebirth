/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include "maths.h"

namespace dcx {

/* Effects and music are mixed at 3/32 (-20.6 dB) of their former level,
 * so that 64 voices playing the original's rules, with music at full scale
 * on top, stay below full scale at full sliders, which are also the
 * defaults.  Effects take the gain in their samples, when those are
 * converted to 16 bits: the sources have 8 bits, so every value and every
 * volume step stays.
 */
constexpr int digi_gain_numerator{3};
constexpr int digi_gain_denominator{32};

/* One 16-bit effect sample at the effects gain. */
[[nodiscard]]
constexpr int16_t digi_scale_effect_sample(const int16_t sample)
{
	return static_cast<int16_t>(sample * digi_gain_numerator / digi_gain_denominator);
}

inline void digi_scale_effect_samples(const std::span<int16_t> samples)
{
	for (auto &s : samples)
		s = digi_scale_effect_sample(s);
}

/* The SDL_mixer music volume (of MIX_MAX_VOLUME, 128) for the menu's
 * music volume 0..8, at the music gain, rounded.
 */
constexpr std::array<uint8_t, 9> digi_music_volume_table{{0, 2, 3, 5, 6, 8, 9, 11, 12}};

/* The Windows MIDI synth's channel volume scale (of MIDI_VOLUME_SCALE, 128)
 * for the menu's music volume 0..8, at the music gain.  General MIDI maps
 * CC7 to 40 log10(cc / 127) dB, so the scale is the square root of the
 * gain: 128 sqrt(3/32 * step / 8), rounded.  test-digi-policy checks both
 * tables against their formulas.
 */
constexpr std::array<uint8_t, 9> digi_midi_volume_table{{0, 14, 20, 24, 28, 31, 34, 37, 39}};

[[nodiscard]]
constexpr int digi_music_volume(const int step)
{
	return digi_music_volume_table[static_cast<std::size_t>(std::clamp(step, 0, 8))];
}

[[nodiscard]]
constexpr int digi_midi_volume(const int step)
{
	return digi_midi_volume_table[static_cast<std::size_t>(std::clamp(step, 0, 8))];
}

struct digi_stereo_levels
{
	fix left, right;
};

/* The DOS pan law: the near side plays at full level and the far side
 * falls linearly to silence at the other end.  pan is 0 (left) to 0xffff
 * (right); the centre plays at full level on both sides.
 */
[[nodiscard]]
constexpr digi_stereo_levels digi_pan_levels(const fix pan)
{
	if (pan & 0x8000)
		return {.left = 2 * F1_0 - pan * 2, .right = F1_0};
	return {.left = F1_0, .right = pan * 2};
}

/* The voices every backend plays on: the channels SDL_mixer asks for, the
 * plain backend's slots and the SDL3 build's tracks.
 */
constexpr std::size_t digi_voices{64};

/* Whether a sound keeps its voice when a new sound needs one: sound
 * objects, loops and sounds above full scale, as in Parallax's Windows code
 * (windigi.c digi_start_sound) and the plain SDL backend.
 */
[[nodiscard]]
constexpr bool digi_sound_is_protected(const bool sound_object, const bool looping, const fix volume)
{
	return sound_object || looping || volume > F1_0;
}

/* Which voice each new sound takes, of N voices.  A free voice if there is
 * one; when all are busy, the unprotected sound started longest ago gives
 * way; when every busy voice is protected, the new sound is not played.
 */
template <std::size_t N>
class digi_voice_table
{
	std::array<uint64_t, N> start_order{};
	std::bitset<N> protect;
	uint64_t starts{};
public:
	/* is_playing(i) tells whether voice i still plays; only the first
	 * granted voices are chosen from.
	 */
	template <typename F>
	[[nodiscard]]
	std::optional<std::size_t> choose(F &&is_playing, const std::size_t granted) const
	{
		std::optional<std::size_t> oldest;
		for (std::size_t i{}, e{std::min(granted, N)}; i != e; ++i)
		{
			if (!is_playing(i))
				return i;
			if (!protect[i] && (!oldest || start_order[i] < start_order[*oldest]))
				oldest = i;
		}
		return oldest;
	}
	void start(const std::size_t i, const bool is_protected)
	{
		start_order[i] = ++starts;
		protect[i] = is_protected;
	}
	/* The sound on voice i may now give way, as when its object died. */
	void unprotect(const std::size_t i)
	{
		protect[i] = false;
	}
};

/* How many sound objects may hold a voice before level ambience
 * (permanent sound objects) starts no more: a quarter of the voices the
 * backend was granted, as the original's digi_get_max_channels() / 4.
 */
[[nodiscard]]
constexpr int digi_ambient_sound_limit(const unsigned granted)
{
	return std::max(1, static_cast<int>(granted / 4));
}

/* The part of a looping sound that repeats, in samples of the sound. */
struct digi_loop_range
{
	std::size_t begin, end;
};

/* A looping sound plays from its start, then between its loop points, as
 * Parallax's Windows code passed them on (windigi.c); none when the sound
 * does not loop or the points (-1 for none) do not lie inside its length.
 */
[[nodiscard]]
constexpr std::optional<digi_loop_range> digi_loop_points(const bool looping, const int start, const int end, const std::size_t length)
{
	if (!looping || start < 0 || end <= start || static_cast<std::size_t>(end) > length)
		return std::nullopt;
	return digi_loop_range{static_cast<std::size_t>(start), static_cast<std::size_t>(end)};
}

}

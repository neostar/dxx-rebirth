/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

}

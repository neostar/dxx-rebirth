/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <algorithm>
#include <cstdint>

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "digi_policy.h"
#include "tools/dev/upstream_end.h"
// clang-format on

namespace dcx::sdl {

/* The gains a sound effect plays at, the pan and volume kernel of the SDL3
 * build (docs/agents/sdl-audio.md area 5).  SDL3_mixer's MIX_SetTrackStereo
 * multiplies each side by its gain as given (spike K5), so the game computes
 * the gains itself, with the rules every build shares (digi_policy.h): the
 * DOS pan law, near side at full level, far side falling linearly, and the
 * volume capped at full scale.  The SDL2 build quantises both to a byte for
 * Mix_SetPanning and Mix_SetDistance; here they stay exact.
 */
struct stereo_gains
{
	float left;
	float right;
};

/* volume: 0..F1_0 and above; pan: 0 (left) .. 0xffff (right), as sound_pan
 * holds it.
 */
[[nodiscard]]
constexpr stereo_gains effect_gains(const int32_t volume, const int32_t pan)
{
	const float v{static_cast<float>(std::max(0, digi_effect_level(volume))) / 65536.0f};
	const auto p{digi_pan_levels(pan)};
	return {.left = static_cast<float>(p.left) / 65536.0f * v,
	        .right = static_cast<float>(p.right) / 65536.0f * v};
}

/* The gain of every effect track for the game's digi volume (0..F1_0 / 2 at
 * the menu's 0..8), as the SDL2 build sets it: Mix_Volume(-1,
 * fix2byte(fixmul(d, d))), which SDL2_mixer clamps to MIX_MAX_VOLUME, 128.
 */
[[nodiscard]]
constexpr float effect_volume_gain(const int32_t digi_volume)
{
	/* fixmul(d, d), then fix2byte: a square is never negative. */
	const auto squared{int64_t{digi_volume} * digi_volume / 65536};
	return static_cast<float>(std::min<int64_t>(squared / 256, 128)) / 128.0f;
}

/* The music track's gain for the game's music volume (the menu's 0..8), as
 * the SDL2 build sets it: Mix_VolumeMusic(digi_music_volume(vol)),
 * the music gain of 3/32 included, over MIX_MAX_VOLUME, 128.
 */
[[nodiscard]]
constexpr float music_volume_gain(const int volume)
{
	return static_cast<float>(digi_music_volume(volume)) / 128.0f;
}

/* One 8-bit unsigned effect sample as signed 16 bits at the effects gain,
 * as the SDL3 build loads the effects: (sample - 0x80) * 256 * 3/32, every
 * source value kept.
 */
[[nodiscard]]
constexpr int16_t effect_sample(const uint8_t sample)
{
	return digi_scale_effect_sample(static_cast<int16_t>((sample - 0x80) * 256));
}

/* The briefing voice's share of the effect volume: the effects gain, which
 * the SDL2 build applies to the voice's samples.
 */
inline constexpr float voice_gain{static_cast<float>(digi_gain_numerator) /
                                  static_cast<float>(digi_gain_denominator)};

}

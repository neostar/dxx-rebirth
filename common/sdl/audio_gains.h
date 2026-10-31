/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <algorithm>
#include <cstdint>

namespace dcx::sdl {

/* The gains a sound effect plays at, the pan and volume kernel of the SDL3
 * build (docs/agents/sdl-audio.md area 5).  SDL3_mixer's MIX_SetTrackStereo
 * multiplies each side by its gain as given (spike K5), so the game computes
 * the gains itself, with SDL2_mixer's linear law as the SDL2 build plays it:
 * Mix_SetPanning(255 - p, p) and Mix_SetDistance(255 - v), each side
 * (255 - p) / 255 or p / 255, times v / 255, with p and v quantised to a byte
 * by fix2byte.  The centre is -6 dB per side, as players hear it today.
 */
struct stereo_gains
{
	float left;
	float right;
};

/* digi_mixer.cpp's fix2byte: a fix of 0..1 as a byte, 1.0 and over as 255. */
[[nodiscard]]
constexpr uint8_t gain_byte(const int32_t f)
{
	if (f >= 0xff00)
		return UINT8_MAX;
	/* The low byte of f >> 8, as fix2byte returns it for any f. */
	return static_cast<uint8_t>(static_cast<uint32_t>(f) / 256u);
}

/* volume: 0..F1_0; pan: 0 (left) .. 0xffff (right), as sound_pan holds it. */
[[nodiscard]]
constexpr stereo_gains effect_gains(const int32_t volume, const int32_t pan)
{
	const float p{static_cast<float>(gain_byte(pan))};
	const float v{static_cast<float>(gain_byte(volume))};
	return {.left = (255.0f - p) / 255.0f * v / 255.0f, .right = p / 255.0f * v / 255.0f};
}

/* The gain of every effect track for the game's digi volume (0..F1_0 / 2 at
 * the menu's 0..8), as the SDL2 build sets it: Mix_Volume(-1,
 * fix2byte(fixmul(d, d))), which SDL2_mixer clamps to MIX_MAX_VOLUME, 128.
 */
[[nodiscard]]
constexpr float effect_volume_gain(const int32_t digi_volume)
{
	/* fixmul(d, d): d * d >> 16, which for the square is a division. */
	const auto squared{static_cast<int32_t>((int64_t{digi_volume} * digi_volume) / 65536)};
	return static_cast<float>(std::min<uint8_t>(gain_byte(squared), 128)) / 128.0f;
}

}

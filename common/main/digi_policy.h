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
#include "maths.h"

namespace dcx {

/* The SDL_mixer music volume (of MIX_MAX_VOLUME, 128) for the menu's
 * music volume 0..8.
 */
constexpr std::array<uint8_t, 9> digi_music_volume_table{{0, 16, 32, 48, 64, 80, 96, 112, 128}};

/* The Windows MIDI synth's channel volume scale (of MIDI_VOLUME_SCALE, 128)
 * for the menu's music volume 0..8.
 */
constexpr std::array<uint8_t, 9> digi_midi_volume_table{{0, 16, 32, 48, 64, 80, 96, 112, 128}};

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

}

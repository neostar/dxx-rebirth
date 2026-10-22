/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <string>
#include <string_view>

namespace dcx {

/* The line a hidden run writes to stderr each time it opens audio (digi_init,
 * and the movie player's own SDL_OpenAudio): "DXX_DBG_HIDDEN: audio driver
 * <driver>", driver being the one SDL opened (SDL_GetCurrentAudioDriver),
 * empty when SDL names none.  The game then stops the run at once
 * (audio_driver_refusal); tools/dev/run.sh fails it after the fact too.
 */
[[nodiscard]]
inline std::string audio_driver_report(const char *const driver)
{
	std::string line{"DXX_DBG_HIDDEN: audio driver "};
	if (driver != nullptr)
		line += driver;
	return line;
}

/* Why a hidden run that opened driver must stop, before it plays anything:
 * empty when it may go on.  Only SDL's dummy driver is silent, unless
 * no_window_sound, NO_WINDOW_SOUND as no-window.sh reads it, is "1" and
 * asks for sound.
 */
[[nodiscard]]
inline std::string audio_driver_refusal(const char *const driver, const char *const no_window_sound)
{
	if (no_window_sound != nullptr && std::string_view{no_window_sound} == "1")
		return {};
	if (driver == nullptr)
		return "hidden run opened audio but SDL names no driver: it could play sound";
	if (std::string_view{driver} == "dummy")
		return {};
	std::string refusal{"hidden run opened audio driver \""};
	refusal += driver;
	refusal += "\", not dummy: it could play sound";
	return refusal;
}

}

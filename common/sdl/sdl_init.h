/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "common/sdl/dxx-sdl.h"
#include "common/sdl/macos_defaults.h"

#if SDL_MAJOR_VERSION == 3

namespace dcx::sdl {

/* SDL3 start-up (docs/agents/sdl-window.md section 1): the application
 * metadata, then every pre-init hint, then SDL_Init(SDL_INIT_VIDEO).  SDL3's
 * functions return true on success, SDL2's 0, so `SDL_Init(...) < 0` would
 * never see a failure.
 */

struct app_metadata
{
	const char *name;
	const char *version;
	const char *identifier;
};

struct hint
{
	const char *name;
	const char *value;
};

/* The SDL2 build's hints keep their intent under SDL3's names.  Gameplay
 * continues regardless of focus, so the window stays visible
 * (SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS); the SDL2 build disabled the
 * Alt+F4 close so that Alt+Shift+F4 can rename the Guide-Bot, which is
 * SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4 "0" here.  The keyboard grab hint is
 * gone: SDL_SetWindowKeyboardGrab does it (#42, #43).
 */
inline constexpr std::array pre_init_hints{
    hint{SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0"},
    hint{SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0"},
};

struct sdl_ops
{
	static bool set_app_metadata(const app_metadata &m)
	{
		return SDL_SetAppMetadata(m.name, m.version, m.identifier);
	}
	static const char *last_error()
	{
		return SDL_GetError();
	}
	static void register_early_defaults()
	{
		register_early_user_defaults();
	}
	static bool set_hint(const hint &h)
	{
		return SDL_SetHint(h.name, h.value);
	}
	static bool init_video()
	{
		return SDL_Init(SDL_INIT_VIDEO);
	}
};

/* What start-up did.  A refused hint or metadata does not stop it: the game
 * works without them, so the caller logs the refusals.
 */
struct init_report
{
	/* SDL_Init succeeded. */
	bool initialised{false};
	/* Set when SDL refused the metadata, to its error.  Read at once: the
	 * hints and SDL_Init that follow overwrite SDL's error.
	 */
	std::optional<std::string> metadata_error;
	/* The name of each refused hint. */
	std::vector<const char *> refused_hints;
};

template <typename Ops = sdl_ops>
[[nodiscard]]
init_report init_video(const app_metadata &metadata, Ops ops = {})
{
	init_report report;
	if (!ops.set_app_metadata(metadata))
		report.metadata_error = ops.last_error();
	for (const auto &h : pre_init_hints)
		if (!ops.set_hint(h))
			report.refused_hints.push_back(h.name);
	ops.register_early_defaults();
	report.initialised = ops.init_video();
	return report;
}

}

#endif

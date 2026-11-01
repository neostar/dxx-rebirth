/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

/* The one header through which fork code includes SDL, whichever of
 * SDL1.2, SDL2 and SDL3 the build selects (ADR 3).  Upstream code keeps its
 * own <SDL*.h> includes: an sdl3=1 build searches the SDL3 include
 * directory, so they find SDL3's headers unchanged.  The helpers in
 * dcx::sdl that SDL3 branches call are declared here as the slices add
 * them.
 */
#include <SDL.h>

/* SDL3 compiles upstream's SDL2 names only through SDL_oldnames.h, which
 * SDL.h reads when SDL_ENABLE_OLD_NAMES is defined before it; sdl3=1 puts
 * the define on the command line so it precedes every SDL header.
 */
#if SDL_MAJOR_VERSION == 3 && !defined(SDL_ENABLE_OLD_NAMES)
#error "SDL3 builds need SDL_ENABLE_OLD_NAMES on the command line (ADR 3); configure with sdl3=1"
#endif

#if SDL_MAJOR_VERSION == 3

namespace dcx::sdl {

/* SDL3 split SDL_ShowCursor(SDL_ENABLE / SDL_DISABLE / SDL_QUERY) in three
 * (migration guide, SDL_mouse.h): SDL_ShowCursor(), SDL_HideCursor() and
 * SDL_CursorVisible().  Upstream's mouse code calls these helpers in its
 * SDL3 branches.
 */
inline void show_cursor(const bool show)
{
	if (show)
		SDL_ShowCursor();
	else
		SDL_HideCursor();
}

[[nodiscard]]
inline bool cursor_visible()
{
	return SDL_CursorVisible();
}

/* Starts an SDL subsystem and quits it with the guard; started() is false,
 * with SDL's error, when it did not start.
 */
class sdl_subsystem
{
	SDL_InitFlags flags_;
	bool started_;

  public:
	explicit sdl_subsystem(const SDL_InitFlags flags)
	    : flags_{flags}, started_{SDL_InitSubSystem(flags)}
	{}
	~sdl_subsystem()
	{
		if (started_)
			SDL_QuitSubSystem(flags_);
	}
	sdl_subsystem(const sdl_subsystem &) = delete;
	sdl_subsystem &operator=(const sdl_subsystem &) = delete;
	sdl_subsystem(sdl_subsystem &&) = delete;
	sdl_subsystem &operator=(sdl_subsystem &&) = delete;
	[[nodiscard]]
	bool started() const
	{
		return started_;
	}
};

}

#endif

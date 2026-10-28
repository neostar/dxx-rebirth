/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstdint>
#include <optional>

#include "common/sdl/frame_pacer.h"

namespace dcx::sdl {

/* The SDL3 window events the game acts on (docs/agents/sdl-window.md
 * sections 3 and 6), without SDL's types, so that the bookkeeping below is
 * tested with no SDL header.  display.h translates SDL_Event into these.
 */
enum class window_event : std::uint8_t
{
	shown,
	hidden,
	exposed,
	occluded,
	minimized,
	restored,
	enter_fullscreen,
	leave_fullscreen,
	pixel_size_changed,
};

struct pixel_size
{
	int width{0};
	int height{0};
	constexpr bool operator==(const pixel_size &) const = default;
};

/* What the game does about one window event. */
struct window_reaction
{
	/* Set the canvas to this size: the window's pixel size changed. */
	std::optional<pixel_size> resize;
	/* SDL confirmed a fullscreen change: the new state. */
	std::optional<bool> fullscreen;
};

/* The window's state as its events reported it: its visibility, which
 * only the frame pacer reads, and whether it is fullscreen.  A window no
 * event has reported on counts as visible, since Windows sends no occlusion
 * and Wayland no user minimise (section 6).
 *
 * Fullscreen is kept twice.  SDL changes it asynchronously (section 3), so
 * a toggle works from the last request, not the last confirmation: two
 * toggles before SDL answers end where they started, and a window SDL
 * never confirms (a hidden one keeps the flag pending) still toggles.  A
 * confirmation moves the request too, for a change SDL makes on its own.
 */
class window_state
{
	bool unmapped{false};
	bool occluded{false};
	bool minimised{false};
	bool confirmed_fullscreen;
	bool target_fullscreen;

  public:
	/* `fullscreen` is the window's state as SDL reports it at creation,
	 * pending flags included (open_display).
	 */
	explicit constexpr window_state(const bool fullscreen)
	    : confirmed_fullscreen{fullscreen}, target_fullscreen{fullscreen}
	{}
	[[nodiscard]]
	constexpr visibility vis() const
	{
		return unmapped || occluded || minimised ? visibility::hidden : visibility::visible;
	}
	/* What the game last asked for, or SDL last confirmed. */
	[[nodiscard]]
	constexpr bool requested_fullscreen() const
	{
		return target_fullscreen;
	}
	/* Ask for the opposite of the last request through `request`
	 * (SDL_SetWindowFullscreen's shape: the wanted state, true when SDL
	 * took the request); a refused request leaves the target as it was.
	 */
	template <typename Request>
	constexpr bool toggle_fullscreen(Request &&request)
	{
		const bool target{!target_fullscreen};
		if (!request(target))
			return false;
		target_fullscreen = target;
		return true;
	}
	/* `size` is the event's pixel size (pixel_size_changed only), `canvas`
	 * the size the game draws at now.
	 */
	constexpr window_reaction handle(const window_event e, const pixel_size size,
	                                 const pixel_size canvas)
	{
		window_reaction r;
		switch (e)
		{
		case window_event::shown:
			unmapped = false;
			break;
		case window_event::hidden:
			unmapped = true;
			break;
		case window_event::exposed:
			occluded = false;
			break;
		case window_event::occluded:
			occluded = true;
			break;
		case window_event::minimized:
			minimised = true;
			break;
		case window_event::restored:
			minimised = false;
			break;
		case window_event::enter_fullscreen:
		case window_event::leave_fullscreen:
			if (const bool now{e == window_event::enter_fullscreen}; now != confirmed_fullscreen)
				r.fullscreen = confirmed_fullscreen = target_fullscreen = now;
			break;
		case window_event::pixel_size_changed:
			/* SDL sends one at creation, of the size the canvas
			 * already has; a minimised window may report 0x0.
			 */
			if (size != canvas && size.width > 0 && size.height > 0)
				r.resize = size;
			break;
		}
		return r;
	}
};

}

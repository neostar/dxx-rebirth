/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include "common/sdl/dxx-sdl.h"

namespace dcx::sdl {

/* A point in window coordinates, or the pixel it lands on. */
struct pixel_point
{
	int x;
	int y;
	constexpr bool operator==(const pixel_point &) const = default;
};

/* The pixel a point in window coordinates lands on, in a window of pixel
 * density `density` (pixels per window coordinate,
 * SDL_GetWindowPixelDensity; docs/agents/sdl-window.md section 4).  SDL
 * reports mouse positions in window coordinates, while the menus hit-test
 * canvases in pixels.  A density SDL could not report (0) keeps the point.
 * The one conversion #48 builds on when it turns on high pixel density.
 */
[[nodiscard]]
constexpr pixel_point window_to_pixels(const pixel_point p, const float density)
{
	if (!(density > 0.f))
		return p;
	return {.x = static_cast<int>(static_cast<float>(p.x) * density),
	        .y = static_cast<int>(static_cast<float>(p.y) * density)};
}

#if SDL_MAJOR_VERSION == 3
/* The pixel `p` lands on in `window`. */
[[nodiscard]]
inline pixel_point window_to_pixels(SDL_Window *const window, const pixel_point p)
{
	return window_to_pixels(p, window ? SDL_GetWindowPixelDensity(window) : 0.f);
}
#endif

}

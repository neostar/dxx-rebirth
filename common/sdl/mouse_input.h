/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <array>

#include "common/sdl/dxx-sdl.h"

#if SDL_MAJOR_VERSION >= 2

namespace dcx::sdl {

/* One axis of SDL3's float mouse motion as the game's whole pixels
 * (docs/agents/sdl-input.md, "Mouse motion"; ADR 3): the fraction a delta
 * leaves is kept and added to the next, so slow movement adds up instead of
 * truncating to zero.  SDL2's integer deltas pass through unchanged.
 */
class motion_axis
{
	float remainder{0.f};

  public:
	/* The whole pixels `delta` completes, rounded toward zero. */
	[[nodiscard]]
	int take(const float delta)
	{
		const float sum{remainder + delta};
		const auto whole{static_cast<int>(sum)};
		remainder = sum - static_cast<float>(whole);
		return whole;
	}
};

/* Both axes of mouse motion, each with its own remainder. */
struct motion_accumulator
{
	motion_axis x, y;
	[[nodiscard]]
	std::array<int, 2> take(const float dx, const float dy)
	{
		return {{x.take(dx), y.take(dy)}};
	}
};

/* The game's wheel steps for one wheel event, positive away from the user
 * (z_up): one per whole tick SDL accumulated, turned back when the
 * platform flipped it for natural scrolling, so a binding follows the
 * physical wheel (docs/agents/sdl-input.md, "Mouse wheel").  SDL3 counts the
 * ticks in integer_y (3.2.12); SDL2's y holds them (SDL_events.h of 2.30.0,
 * 2.32.10 and sdl2-compat 2.32.74, pinned in tools/dev/reference-docs.txt),
 * preciseY (2.0.18) the float amount, which the steps do not use.
 */
[[nodiscard]]
inline int wheel_steps(const SDL_MouseWheelEvent &e)
{
#if SDL_MAJOR_VERSION == 3
	const int ticks{e.integer_y};
#else
	const int ticks{e.y};
#endif
	return e.direction == SDL_MOUSEWHEEL_FLIPPED ? -ticks : ticks;
}

}

#endif

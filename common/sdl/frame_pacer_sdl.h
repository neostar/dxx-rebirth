/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstdint>

#include "common/sdl/dxx-sdl.h"
#include "common/sdl/frame_pacer.h"

#if SDL_MAJOR_VERSION == 3

namespace dcx::sdl {

/* SDL's side of the frame pacer (docs/agents/sdl-window.md section 5): the
 * clock is SDL_GetTicksNS and the sleep is SDL_DelayPrecise for the last
 * sleep of a frame, SDL_DelayNS for the slices before it.
 */
[[nodiscard]]
inline std::uint64_t ticks_ns()
{
	return SDL_GetTicksNS();
}

inline void sleep_ns(const std::uint64_t ns, const bool precise)
{
	if (precise)
		SDL_DelayPrecise(ns);
	else
		SDL_DelayNS(ns);
}

[[nodiscard]]
inline auto make_pacer()
{
	return frame_pacer{&ticks_ns, &sleep_ns};
}

/* One sleep toward the end of a period, for a limiter that keeps its own
 * clock in fix seconds (calc_frame_time, the automap) and loops itself.
 * While the window is hidden the period is at least 1/30 s.
 */
inline void sleep_toward(const std::int64_t elapsed_fix, const std::int64_t period_fix,
                         const bool multiplayer, const visibility vis)
{
	const pacing p{.period_ns = ns_from_fix(period_fix), .multiplayer = multiplayer, .vis = vis};
	if (const auto s{next_step(ns_from_fix(elapsed_fix), p)}; s.sleep_ns)
		sleep_ns(s.sleep_ns, s.precise);
}

}

#endif

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace dcx::sdl {

/* Events pumped during a long load (level load, texture upload), so that
 * X11 (_NET_WM_PING) and Windows do not mark the window hung
 * (docs/agents/sdl-window.md section 8): the loader calls the pump at
 * every step, and it pumps at the first and then at most once per
 * interval.  The clock (nanoseconds) and the pump are injected, so a unit
 * test drives it with fakes (common/sdl/unittest/load_pump.cpp), and
 * display.h with SDL_GetTicksNS and SDL_PumpEvents.
 */
inline constexpr std::uint64_t load_pump_interval_ns{100000000};

template <typename Clock, typename Pump>
class load_pump
{
	Clock clock;
	Pump pump;
	std::optional<std::uint64_t> last_pump;

  public:
	constexpr load_pump(Clock c, Pump p) : clock{std::move(c)}, pump{std::move(p)}
	{}
	void operator()()
	{
		const auto now{clock()};
		if (last_pump && now - last_pump.value() < load_pump_interval_ns)
			return;
		last_pump = now;
		pump();
	}
};

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace dcx {

/* Where each frame's FrameTime comes from: the measured wall-clock time
 * (the default), or, with the debug harness, a fixed step of 1/fps seconds
 * set by DXX_DBG_FIXED_FRAMETIME=fps, so game time advances the same way
 * however fast the machine draws.  Determinism runs keep the frame rate
 * display off: show_framerate (gamerend.cpp) still reads the wall clock, on
 * purpose, since it reports how fast frames really come.
 * Times are `fix` values (16.16 fixed point seconds, maths.h), spelt
 * std::int32_t so that this header includes no upstream header: clang-tidy
 * would report those as part of this fork file.
 */
class frame_time_policy
{
	/* 0: use the measured frame time. */
	std::int32_t fixed_step{0};
	constexpr explicit frame_time_policy(const std::int32_t step) : fixed_step{step}
	{}

  public:
	/* F1_0: one second. */
	static constexpr std::int32_t one_second{65536};
	static constexpr unsigned max_fps{1000};
	constexpr frame_time_policy() = default;
	[[nodiscard]]
	static constexpr std::optional<frame_time_policy> parse(const std::string_view value)
	{
		unsigned fps{0};
		const auto first{std::to_address(value.begin())};
		const auto end{std::to_address(value.end())};
		if (const auto [ptr, ec]{std::from_chars(first, end, fps)}; ec != std::errc{} || ptr != end)
			return std::nullopt;
		if (fps < 1 || fps > max_fps)
			return std::nullopt;
		return frame_time_policy{one_second / static_cast<std::int32_t>(fps)};
	}
	[[nodiscard]]
	constexpr bool is_fixed() const
	{
		return fixed_step != 0;
	}
	[[nodiscard]]
	constexpr std::int32_t frame_time(const std::int32_t measured) const
	{
		return is_fixed() ? fixed_step : measured;
	}
};

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>

namespace dcx {

/* One second in fix units (16.16, maths.h's F1_0), spelt out so that this
 * header includes no upstream header.
 */
inline constexpr std::int64_t fix_one{65536};
inline constexpr std::uint64_t ns_per_second{1000000000};

/* Convert nanoseconds since SDL started to fix seconds.  The whole seconds
 * convert first, so the product does not overflow after 78 hours as
 * ns * 65536 would.
 */
[[nodiscard]]
constexpr std::int64_t fix_from_ns(const std::uint64_t ns)
{
	const auto seconds{static_cast<std::int64_t>(ns / ns_per_second)};
	const auto rest{static_cast<std::int64_t>(ns % ns_per_second)};
	return seconds * fix_one + rest * fix_one / static_cast<std::int64_t>(ns_per_second);
}

/* The reverse: a span in fix seconds as nanoseconds; a negative span is
 * zero.
 */
[[nodiscard]]
constexpr std::uint64_t ns_from_fix(const std::int64_t fix)
{
	if (fix <= 0)
		return 0;
	return static_cast<std::uint64_t>(fix / fix_one) * ns_per_second +
	       static_cast<std::uint64_t>(fix % fix_one) * ns_per_second /
	           static_cast<std::uint64_t>(fix_one);
}

/* The pure pacing kernel of docs/agents/sdl-window.md section 5: how long to
 * sleep before a frame has used its period.  It includes no SDL header; the
 * clock and the sleep are injected, so a unit test drives it with a fake
 * (common/sdl/unittest/frame_pacer.cpp) and the SDL3 arm with SDL_GetTicksNS
 * and SDL_DelayNS/SDL_DelayPrecise (frame_pacer_sdl.h).  All times are
 * nanoseconds.
 */

enum class visibility : std::uint8_t
{
	visible,
	/* The window is occluded or minimised: no one sees the frames. */
	hidden,
};

struct pacing
{
	/* 1 / the frame rate cap; the menu limiter passes its caller's bound. */
	std::uint64_t period_ns{0};
	/* Sleep in short slices so the caller can poll packets between them. */
	bool multiplayer{false};
	/* false: -nonicefps, never sleep, spin until the period has passed. */
	bool nice{true};
	visibility vis{visibility::visible};
};

/* The design's cap while the window is hidden: DESIGNATED_GAME_FPS, 30. */
inline constexpr std::uint64_t hidden_period_ns{ns_per_second / 30};
inline constexpr std::uint64_t poll_slice_ns{1000000};

struct step
{
	/* The period has passed. */
	bool elapsed;
	std::uint64_t sleep_ns;
	/* The last sleep of the frame: SDL_DelayPrecise, else SDL_DelayNS. */
	bool precise;
};

[[nodiscard]]
constexpr std::uint64_t effective_period(const pacing &p)
{
	return p.vis == visibility::hidden ? std::max(p.period_ns, hidden_period_ns) : p.period_ns;
}

/* What to do after `elapsed_ns` of a frame: done, or sleep this long. */
[[nodiscard]]
constexpr step next_step(const std::uint64_t elapsed_ns, const pacing &p)
{
	const auto period{effective_period(p)};
	if (elapsed_ns >= period)
		return {true, 0, false};
	if (!p.nice)
		return {false, 0, false};
	const auto remaining{period - elapsed_ns};
	if (p.multiplayer && remaining > poll_slice_ns)
		return {false, poll_slice_ns, false};
	return {false, remaining, true};
}

template <typename Clock, typename Sleep>
class frame_pacer
{
	Clock clock;
	Sleep sleep;

  public:
	constexpr frame_pacer(Clock c, Sleep s) : clock{std::move(c)}, sleep{std::move(s)}
	{}
	/* Sleep until `p.period_ns` has passed since `frame_start` (a clock
	 * reading), calling `poll` before each sleep and once more when the
	 * period has passed, and return the time the frame took.  A clock that
	 * reads before the frame start restarts the frame.
	 */
	template <typename Poll>
	std::uint64_t wait(std::uint64_t frame_start, const pacing &p, Poll &&poll)
	{
		for (;;)
		{
			const auto now{clock()};
			if (now < frame_start)
				frame_start = now;
			const auto elapsed{now - frame_start};
			const auto s{dcx::next_step(elapsed, p)};
			/* At least one poll per call, as SDL2's limiters do, also
			 * when the period has passed already.
			 */
			poll();
			if (s.elapsed)
				return elapsed;
			if (s.sleep_ns)
				sleep(s.sleep_ns, s.precise);
		}
	}
};

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace dcx {

/* The crafted sound stimulus of tools/dev/clip-check.sh (#104): with
 * DXX_DBG_SOUND_STIMULUS=1 in a debug harness build, the game plays one
 * 2D sound at each step's volume once that many milliseconds have passed
 * since the first event loop pass after the sound system started.  The
 * demo replays every sound at F1_0 (newdemo.cpp, ND_EVENT_SOUND), so it
 * cannot show a volume above F1_0; the second step does, at F3_0, the
 * reactor countdown's volume, and must peak no higher than the first.
 * The first step waits 6 s, until the game has loaded its sounds and shows
 * its menu; the steps lie 4 s apart, longer than the sound, so the capture
 * holds them as separate bursts.
 */
struct sound_stimulus_step
{
	int64_t at_ms;
	int32_t volume;
};

inline constexpr std::array<sound_stimulus_step, 2> sound_stimulus_steps{{
    {.at_ms = 6000, .volume = 0x10000},
    {.at_ms = 10000, .volume = 0x30000},
}};

[[nodiscard]]
inline bool read_harness_sound_stimulus()
{
	const char *const v{std::getenv("DXX_DBG_SOUND_STIMULUS")};
	return v != nullptr && std::string_view{v} == "1";
}

/* The end of the steps due at elapsed_ms: the caller plays the steps from
 * its next one up to this index.
 */
[[nodiscard]]
constexpr std::size_t sound_stimulus_due(std::size_t next, const int64_t elapsed_ms)
{
	while (next < sound_stimulus_steps.size() && elapsed_ms >= sound_stimulus_steps[next].at_ms)
		++next;
	return next;
}

/* When the stimulus started and the next step to play: the steps fall due
 * by the time since start_ms.
 */
class sound_stimulus_schedule
{
	int64_t start_ms_;
	std::size_t next_{0};

  public:
	explicit sound_stimulus_schedule(const int64_t start_ms) : start_ms_{start_ms}
	{}
	/* The steps due by now_ms not yet returned, as [first, last). */
	[[nodiscard]]
	std::pair<std::size_t, std::size_t> take_due(const int64_t now_ms)
	{
		const auto first{next_};
		next_ = sound_stimulus_due(next_, now_ms - start_ms_);
		return {first, next_};
	}
};

/* Called once per event loop pass in a debug harness build (event.cpp),
 * when the sound system set it.
 */
inline void (*harness_frame_hook)(){nullptr};

}

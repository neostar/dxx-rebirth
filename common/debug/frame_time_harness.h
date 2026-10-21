/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstdlib>
#include <optional>

#include "common/debug/frame_time_policy.h"

namespace dcx {

/* DXX_DBG_FIXED_FRAMETIME as the debug harness read it. */
struct harness_frame_time
{
	/* The variable's value; nullptr when it is unset. */
	const char *value{nullptr};
	/* The policy it selects: the wall clock when unset, nullopt when the
	 * value is not a frame rate.
	 */
	std::optional<frame_time_policy> policy{};
};

/* Read DXX_DBG_FIXED_FRAMETIME from the environment.  Header-only: in the
 * Linux container the compilation database holds GCC's flags, which
 * clang-tidy rejects for a fork .cpp.
 */
[[nodiscard]]
inline harness_frame_time read_harness_frame_time()
{
	const char *const value{std::getenv("DXX_DBG_FIXED_FRAMETIME")};
	if (value == nullptr)
		return {.value = nullptr, .policy = frame_time_policy{}};
	return {.value = value, .policy = frame_time_policy::parse(value)};
}

/* DXX_DBG_FIXED_FRAMETIME of this run, read and parsed once for every
 * caller (the game's frame time, the harness clock, the input script).
 */
[[nodiscard]]
inline const harness_frame_time &harness_frame_time_of_run()
{
	static const harness_frame_time frame_time{read_harness_frame_time()};
	return frame_time;
}

}

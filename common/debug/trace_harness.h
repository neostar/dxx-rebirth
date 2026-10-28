/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * DXX_DBG_TRACE and DXX_DBG_TRACE_FRAMES: the debug harness's request for
 * a trace of a frame range (common/render/recording_device.h, #18), its
 * frames numbered by presents from the start of the run.
 * Without DXX_DBG_TRACE nothing is recorded and no recorder is made.
 */

#pragma once

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

#include "common/render/recording_device.h"

namespace dcx {

/* FIRST-LAST, two decimal frame indices with LAST not before FIRST,
 * numbered as frame_range numbers them; nullopt for anything else.
 */
[[nodiscard]]
inline std::optional<render::frame_range> parse_frame_range(const std::string_view s)
{
	const auto dash{s.find('-')};
	if (dash == std::string_view::npos)
		return std::nullopt;
	/* A decimal whole number of at most 32 bits, digits alone. */
	const auto number{[](const std::string_view n) -> std::optional<std::uint32_t> {
		std::uint32_t v{0};
		const auto first{std::to_address(n.begin())};
		const auto end{std::to_address(n.end())};
		if (const auto [ptr, ec]{std::from_chars(first, end, v)}; ec != std::errc{} || ptr != end)
			return std::nullopt;
		return v;
	}};
	const auto first{number(s.substr(0, dash))};
	const auto last{number(s.substr(dash + 1))};
	if (!first || !last || last.value() < first.value())
		return std::nullopt;
	return render::frame_range{.first = first.value(), .last = last.value()};
}

/* What the harness was asked to record. */
struct trace_request
{
	/* The file the trace is written to, created or emptied. */
	const char *path;
	/* DXX_DBG_TRACE_FRAMES as given, for the error naming it. */
	const char *frames_text;
	/* nullopt when DXX_DBG_TRACE_FRAMES is missing or malformed: the
	 * caller fails the run, as a recording of the wrong frames would.
	 */
	std::optional<render::frame_range> frames;
};

/* The request of the environment: nullopt without DXX_DBG_TRACE (or with
 * it empty), which records nothing.
 */
[[nodiscard]]
inline std::optional<trace_request> read_harness_trace()
{
	const char *const path{std::getenv("DXX_DBG_TRACE")};
	if (!path || !*path)
		return std::nullopt;
	const char *const frames{std::getenv("DXX_DBG_TRACE_FRAMES")};
	return trace_request{.path = path,
	                     .frames_text = frames ? frames : "",
	                     .frames = frames ? parse_frame_range(frames) : std::nullopt};
}

}

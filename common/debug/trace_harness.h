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

#include "common/render/device.h"
#include "common/render/recording_device.h"
#include "common/render/render_device.h"

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

/* How a harness run reports, given by the game (device_front.h): fail ends
 * the run with the message (UserError) and does not return; note logs a
 * line; exit_note writes one at exit, after the console is gone (stderr).
 */
struct harness_reporter
{
	void (*fail)(const char *message);
	void (*note)(const char *message);
	void (*exit_note)(const char *message);
};

/* The device the front end draws through in a dbg_harness build
 * (common/render/device_front.h): with a trace asked for, a recorder
 * around the device of now, else that device itself.  At exit it is
 * destroyed before the device (declared after it), nulling its pointer
 * first, so the texture releases that later static destructors make (the
 * fonts') reach no device.
 */
class harness_front
{
	harness_reporter report_;
	/* The trace asked for, until the first bind makes its recorder. */
	std::optional<trace_request> request_;
	std::unique_ptr<render::recording_device> recorder_;
	render::device *front_{nullptr};
	/* Declared last, so destroyed first: front_ is null before the
	 * recorder goes.
	 */
	render::front_reset reset_{front_};

  public:
	harness_front(harness_reporter report, std::optional<trace_request> request);
	/* With a recorder, notes at exit what it recorded (recording_summary):
	 * a run that ended before the range's last frame gives a shorter trace.
	 */
	~harness_front();
	harness_front(const harness_front &) = delete;
	harness_front &operator=(const harness_front &) = delete;
	harness_front(harness_front &&) = delete;
	harness_front &operator=(harness_front &&) = delete;
	/* Draws through inner from now on.  The first bind makes the recorder
	 * of the trace asked for, its header of the size target gives when
	 * the range starts; a malformed range or a file that cannot be written
	 * fails the run.
	 */
	void bind(render::device &inner, render::target_size target);
	/* The device the front end draws through; throws std::logic_error
	 * before the first bind.
	 */
	[[nodiscard]]
	render::device &get() const;
	/* Releases slot's image through the front device, once there is one. */
	void release_texture(render::texture_slot slot) const;
};

}

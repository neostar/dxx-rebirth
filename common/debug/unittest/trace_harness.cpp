/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/trace_harness.h"

#include <cstdlib>
#include <optional>
#include <string_view>

#include "common/render/recording_device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth trace harness
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

/* Test that a frame range reads as FIRST-LAST, a single frame included. */
BOOST_AUTO_TEST_CASE(frame_range_reads_first_and_last)
{
	const auto r{dcx::parse_frame_range("120-540")};
	BOOST_REQUIRE(r.has_value());
	BOOST_TEST((r.value() == render::frame_range{.first = 120, .last = 540}));
	const auto one{dcx::parse_frame_range("7-7")};
	BOOST_REQUIRE(one.has_value());
	BOOST_TEST((one.value() == render::frame_range{.first = 7, .last = 7}));
}

/* Test that anything but two whole numbers in order is no range. */
BOOST_AUTO_TEST_CASE(malformed_frame_range_refused)
{
	for (const std::string_view s : {"", "5", "-5", "5-", "a-5", "5-b", "5-3", "1-2-3", " 1-2",
	                                 "1-2 ", "+1-2", "4294967296-1"})
		BOOST_TEST(!dcx::parse_frame_range(s).has_value(), "'" << s << "' read as a range");
}

/* Test that without DXX_DBG_TRACE nothing is asked for, so no recorder is
 * made, even with DXX_DBG_TRACE_FRAMES set; with both, the path and range.
 */
BOOST_AUTO_TEST_CASE(trace_asked_for_only_with_its_path)
{
	BOOST_REQUIRE(setenv("DXX_DBG_TRACE_FRAMES", "1-2", 1) == 0);
	BOOST_REQUIRE(unsetenv("DXX_DBG_TRACE") == 0);
	BOOST_TEST(!dcx::read_harness_trace().has_value());
	BOOST_REQUIRE(setenv("DXX_DBG_TRACE", "", 1) == 0);
	BOOST_TEST(!dcx::read_harness_trace().has_value());
	BOOST_REQUIRE(setenv("DXX_DBG_TRACE", "/x.rtr", 1) == 0);
	const auto t{dcx::read_harness_trace()};
	BOOST_REQUIRE(t.has_value());
	BOOST_TEST(std::string_view{t.value().path} == "/x.rtr");
	BOOST_REQUIRE(t.value().frames.has_value());
	BOOST_TEST((t.value().frames.value() == render::frame_range{.first = 1, .last = 2}));
	BOOST_REQUIRE(unsetenv("DXX_DBG_TRACE_FRAMES") == 0);
	const auto missing{dcx::read_harness_trace()};
	BOOST_REQUIRE(missing.has_value());
	BOOST_TEST(!missing.value().frames.has_value());
}

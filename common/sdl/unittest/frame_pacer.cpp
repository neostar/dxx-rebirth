/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/frame_pacer.h"

#include <cstdint>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth frame pacer
#include <boost/test/unit_test.hpp>

namespace {

constexpr std::uint64_t ms{1000000};

}

/* Test that nanoseconds convert to fix (65536 per second) without overflowing for a year. */
BOOST_AUTO_TEST_CASE(ns_to_fix_is_exact_and_does_not_overflow)
{
	BOOST_TEST(dcx::fix_from_ns(0) == 0);
	BOOST_TEST(dcx::fix_from_ns(1000 * ms) == 65536);
	BOOST_TEST(dcx::fix_from_ns(1500 * ms) == 98304);
	constexpr std::uint64_t year{365ull * 24 * 3600 * 1000 * ms};
	BOOST_TEST(dcx::fix_from_ns(year) == 365ll * 24 * 3600 * 65536);
}

/* Test that fix seconds convert to nanoseconds, a negative span to zero, and back without drift. */
BOOST_AUTO_TEST_CASE(fix_to_ns_round_trips)
{
	BOOST_TEST(dcx::ns_from_fix(0) == 0u);
	BOOST_TEST(dcx::ns_from_fix(65536) == 1000 * ms);
	BOOST_TEST(dcx::ns_from_fix(32768) == 500 * ms);
	BOOST_TEST(dcx::ns_from_fix(-5) == 0u);
	/* 1/200 s in fix is 327 (truncated), which is just under 5 ms. */
	BOOST_TEST(dcx::ns_from_fix(65536 / 200) < 5 * ms);
	BOOST_TEST(dcx::ns_from_fix(65536 / 200) > 4900000u);
}

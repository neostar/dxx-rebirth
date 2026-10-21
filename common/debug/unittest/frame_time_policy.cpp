/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/frame_time_policy.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth frame time policy
#include <boost/test/unit_test.hpp>

/* Test that the default policy passes the measured frame time through. */
BOOST_AUTO_TEST_CASE(wall_clock_policy_uses_measured_time)
{
	constexpr dcx::frame_time_policy policy;
	BOOST_TEST(!policy.is_fixed());
	BOOST_TEST(policy.frame_time(1234) == 1234);
	BOOST_TEST(policy.frame_time(65536) == 65536);
}

/* Test that a frame rate in DXX_DBG_FIXED_FRAMETIME fixes every frame time to one
 * step, however long the frame took.
 */
BOOST_AUTO_TEST_CASE(fixed_rate_ignores_measured_time)
{
	const auto policy{dcx::frame_time_policy::parse("30")};
	BOOST_REQUIRE(policy.has_value());
	BOOST_TEST(policy.value().is_fixed());
	/* F1_0 / 30 = 65536 / 30, truncated. */
	BOOST_TEST(policy.value().frame_time(1092) == 2184);
	BOOST_TEST(policy.value().frame_time(65536) == 2184);
	const auto one_fps{dcx::frame_time_policy::parse("1")};
	BOOST_REQUIRE(one_fps.has_value());
	BOOST_TEST(one_fps.value().frame_time(1) == 65536);
}

/* Test that a value that is not a whole frame rate from 1 to 1000 is rejected, so a
 * typo cannot silently fall back to the wall clock.
 */
BOOST_AUTO_TEST_CASE(invalid_rate_is_rejected)
{
	for (const std::string_view value : {"", "0", "-5", "abc", "30x", " 30", "1001", "99999999999"})
		BOOST_TEST(!dcx::frame_time_policy::parse(value).has_value(), "value \"" << value << "\"");
	const auto max_fps{dcx::frame_time_policy::parse("1000")};
	BOOST_REQUIRE(max_fps.has_value());
	BOOST_TEST(max_fps.value().frame_time(0) == 65);
}

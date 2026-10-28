/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/load_pump.h"

#include <cstdint>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth load pump
#include <boost/test/unit_test.hpp>

namespace {

struct fake_clock
{
	const std::uint64_t *now;
	std::uint64_t operator()() const
	{
		return *now;
	}
};

struct counting_pump
{
	unsigned *pumped;
	void operator()() const
	{
		++*pumped;
	}
};

constexpr std::uint64_t ms{1000000};

}

/* Test that a long load pumps events at its first step and then once each
 * 100 ms, however often it steps (docs/agents/sdl-window.md section 8).
 */
BOOST_AUTO_TEST_CASE(pumps_first_then_once_per_interval)
{
	std::uint64_t now{5 * ms};
	unsigned pumped{0};
	dcx::sdl::load_pump step{fake_clock{&now}, counting_pump{&pumped}};
	step();
	BOOST_TEST(pumped == 1u);
	for (now = 6 * ms; now < 105 * ms; now += ms)
		step();
	BOOST_TEST(pumped == 1u);
	now = 105 * ms;
	step();
	BOOST_TEST(pumped == 2u);
	step();
	BOOST_TEST(pumped == 2u);
}

/* Test that the interval counts from the last pump, not from the start. */
BOOST_AUTO_TEST_CASE(interval_counts_from_the_last_pump)
{
	std::uint64_t now{0};
	unsigned pumped{0};
	dcx::sdl::load_pump step{fake_clock{&now}, counting_pump{&pumped}};
	step();
	now = 150 * ms;
	step();
	now = 220 * ms;
	step();
	BOOST_TEST(pumped == 2u);
	now = 250 * ms;
	step();
	BOOST_TEST(pumped == 3u);
}

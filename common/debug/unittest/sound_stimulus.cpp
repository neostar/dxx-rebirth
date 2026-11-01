/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/sound_stimulus.h"

#include <cstddef>
#include <utility>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth sound stimulus
#include <boost/test/unit_test.hpp>

/* Test that the steps fall due at 6 s and 10 s, and not before. */
BOOST_AUTO_TEST_CASE(steps_fall_due_at_their_times)
{
	BOOST_TEST(dcx::sound_stimulus_due(0, 5999) == 0u);
	BOOST_TEST(dcx::sound_stimulus_due(0, 6000) == 1u);
	BOOST_TEST(dcx::sound_stimulus_due(1, 9999) == 1u);
	BOOST_TEST(dcx::sound_stimulus_due(0, 10000) == 2u);
	BOOST_TEST(dcx::sound_stimulus_due(2, 60000) == 2u);
}

/* Test that the schedule times the steps from its start and returns each
 * step once.
 */
BOOST_AUTO_TEST_CASE(schedule_returns_each_step_once)
{
	dcx::sound_stimulus_schedule s{1000};
	using range = std::pair<std::size_t, std::size_t>;
	BOOST_TEST((s.take_due(6999) == range{0, 0}));
	BOOST_TEST((s.take_due(7000) == range{0, 1}));
	BOOST_TEST((s.take_due(7000) == range{1, 1}));
	BOOST_TEST((s.take_due(20000) == range{1, 2}));
	BOOST_TEST((s.take_due(30000) == range{2, 2}));
}

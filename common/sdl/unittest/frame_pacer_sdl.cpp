/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/frame_pacer_sdl.h"

#include <cstdint>
#include <ctime>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth frame pacer sdl
#include <boost/test/unit_test.hpp>

/* Test that the SDL adapters wait out a 30 ms period on SDL's clock, by
 * sleeping and not by spinning: the process uses far less CPU time than
 * the wait takes.
 */
BOOST_AUTO_TEST_CASE(sdl_pacer_sleeps_the_period)
{
	auto pacer{dcx::sdl::make_pacer()};
	const auto cpu_before{std::clock()};
	const auto start{dcx::sdl::ticks_ns()};
	const auto took{pacer.wait(start, dcx::pacing{.period_ns = 30 * dcx::ns_per_ms}, [] {})};
	const auto cpu_ms{(std::clock() - cpu_before) * 1000 / CLOCKS_PER_SEC};
	BOOST_TEST(took >= 30 * dcx::ns_per_ms);
	BOOST_TEST(took < 200 * dcx::ns_per_ms);
	BOOST_TEST(dcx::sdl::ticks_ns() - start >= 30 * dcx::ns_per_ms);
	BOOST_TEST(cpu_ms < 10);
}

/* Test that sleep_toward, the adapter gameplay and the automap call, sleeps the period when no
 * time has been spent, and sleeps rather than spins.  2000 fix is 30.5 ms.
 */
BOOST_AUTO_TEST_CASE(sleep_toward_sleeps_a_fresh_period)
{
	const auto cpu_before{std::clock()};
	const auto start{dcx::sdl::ticks_ns()};
	dcx::sdl::sleep_toward(0, 2000, false, dcx::visibility::visible);
	const auto cpu_ms{(std::clock() - cpu_before) * 1000 / CLOCKS_PER_SEC};
	BOOST_TEST(dcx::sdl::ticks_ns() - start >= 30 * dcx::ns_per_ms);
	BOOST_TEST(cpu_ms < 10);
}

/* Test that sleep_toward returns at once when the period has already passed. */
BOOST_AUTO_TEST_CASE(sleep_toward_returns_at_once_when_elapsed)
{
	const auto start{dcx::sdl::ticks_ns()};
	dcx::sdl::sleep_toward(2000, 2000, false, dcx::visibility::visible);
	dcx::sdl::sleep_toward(5000, 2000, true, dcx::visibility::visible);
	BOOST_TEST(dcx::sdl::ticks_ns() - start < 5 * dcx::ns_per_ms);
}

/* Test that sleep_toward caps a hidden window (occluded or minimised) at 30
 * frames a second: a 5 ms period (328 fix) sleeps about 33 ms
 * (docs/agents/sdl-window.md section 5).
 */
BOOST_AUTO_TEST_CASE(sleep_toward_caps_a_hidden_window_at_30_fps)
{
	const auto start{dcx::sdl::ticks_ns()};
	dcx::sdl::sleep_toward(0, 328, false, dcx::visibility::hidden);
	BOOST_TEST(dcx::sdl::ticks_ns() - start >= 33 * dcx::ns_per_ms);
}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/frame_pacer.h"

#include <cstdint>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth frame pacer
#include <boost/test/unit_test.hpp>

namespace {

using dcx::frame_pacer;
using dcx::pacing;
using dcx::visibility;

constexpr std::uint64_t ms{1000000};

/* A fake clock that only the fake sleep advances, and a record of each sleep. */
struct fake_time
{
	std::uint64_t now{0};
	struct slept
	{
		std::uint64_t ns;
		bool precise;
	};
	std::vector<slept> sleeps;
	unsigned reads{0};
	/* A budget of clock reads: a broken kernel fails the test instead of looping forever. */
	std::uint64_t read()
	{
		BOOST_REQUIRE(++reads < 10000);
		return now;
	}
};

pacing gameplay(const unsigned fps)
{
	return {.period_ns = 1000 * ms / fps};
}

}

/* Test that a gameplay frame at 200 fps sleeps the rest of its 5 ms period in one precise sleep. */
BOOST_AUTO_TEST_CASE(gameplay_sleeps_the_remainder_precisely)
{
	const auto step{dcx::next_step(1 * ms, gameplay(200))};
	BOOST_TEST(!step.elapsed);
	BOOST_TEST(step.sleep_ns == 4 * ms);
	BOOST_TEST(step.precise);
}

/* Test that a menu frame at the 30 fps period sleeps 1/30 s less the time already spent. */
BOOST_AUTO_TEST_CASE(menu_period_sleeps_the_remainder)
{
	const pacing menu{.period_ns = 33 * ms};
	const auto step{dcx::next_step(3 * ms, menu)};
	BOOST_TEST(!step.elapsed);
	BOOST_TEST(step.sleep_ns == 30 * ms);
}

/* Test that the automap, which paces like gameplay at the vsync cap of 200 fps, sleeps 5 ms from a
 * fresh frame. */
BOOST_AUTO_TEST_CASE(automap_period_from_a_fresh_frame)
{
	const auto step{dcx::next_step(0, gameplay(200))};
	BOOST_TEST(step.sleep_ns == 5 * ms);
}

/* Test that a frame that has used its whole period, or more, sleeps nothing and is elapsed. */
BOOST_AUTO_TEST_CASE(full_period_is_elapsed)
{
	const auto exact{dcx::next_step(5 * ms, gameplay(200))};
	BOOST_TEST(exact.elapsed);
	BOOST_TEST(exact.sleep_ns == 0u);
	const auto over{dcx::next_step(9 * ms, gameplay(200))};
	BOOST_TEST(over.elapsed);
	BOOST_TEST(over.sleep_ns == 0u);
}

/* Test that a multiplayer frame sleeps in 1 ms slices, so the caller can poll packets between
 * them, and ends with a precise sleep for what is left.
 */
BOOST_AUTO_TEST_CASE(multiplayer_sleeps_in_slices)
{
	auto p{gameplay(200)};
	p.multiplayer = true;
	const auto first{dcx::next_step(0, p)};
	BOOST_TEST(first.sleep_ns == 1 * ms);
	BOOST_TEST(!first.precise);
	const auto last{dcx::next_step(4500000, p)};
	BOOST_TEST(last.sleep_ns == 500000u);
	BOOST_TEST(last.precise);
}

/* Test that -nonicefps (a pacing that is not nice) never sleeps but still reports elapsed. */
BOOST_AUTO_TEST_CASE(not_nice_never_sleeps)
{
	auto p{gameplay(200)};
	p.nice = false;
	BOOST_TEST(dcx::next_step(1 * ms, p).sleep_ns == 0u);
	BOOST_TEST(!dcx::next_step(1 * ms, p).elapsed);
	BOOST_TEST(dcx::next_step(5 * ms, p).elapsed);
}

/* Test that a hidden window caps the frame rate at 30 fps, and never speeds up a slower period. */
BOOST_AUTO_TEST_CASE(hidden_window_caps_at_30_fps)
{
	auto p{gameplay(200)};
	p.vis = visibility::hidden;
	BOOST_TEST(dcx::effective_period(p) == 1000 * ms / 30);
	const auto step{dcx::next_step(5 * ms, p)};
	BOOST_TEST(!step.elapsed);
	BOOST_TEST(step.sleep_ns == 1000 * ms / 30 - 5 * ms);
	auto slow{gameplay(20)};
	slow.vis = visibility::hidden;
	BOOST_TEST(dcx::effective_period(slow) == 50 * ms);
}

/* Test that the driver sleeps until the fake clock reaches the period, polling before each slice
 * and once more at the end, and returns the time the frame took.
 */
BOOST_AUTO_TEST_CASE(wait_drives_the_fake_clock_to_the_period)
{
	fake_time t;
	frame_pacer pacer{[&t] { return t.read(); },
	                  [&t](const std::uint64_t ns, const bool precise) {
		                  t.sleeps.push_back({ns, precise});
		                  t.now += ns;
	                  }};
	unsigned polls{0};
	auto p{gameplay(200)};
	p.multiplayer = true;
	const auto took{pacer.wait(0, p, [&polls] { ++polls; })};
	BOOST_TEST(took == 5 * ms);
	BOOST_TEST(t.sleeps.size() == 5u);
	BOOST_TEST(t.sleeps.back().precise);
	BOOST_TEST(polls == 6u);
}

/* Test that a frame whose period has already passed still polls once, as SDL2's menu limiter
 * does, and sleeps not at all.
 */
BOOST_AUTO_TEST_CASE(wait_polls_once_when_the_period_has_passed)
{
	fake_time t;
	t.now = 50 * ms;
	frame_pacer pacer{[&t] { return t.read(); },
	                  [&t](const std::uint64_t ns, const bool precise) {
		                  t.sleeps.push_back({ns, precise});
		                  t.now += ns;
	                  }};
	unsigned polls{0};
	const auto took{pacer.wait(0, gameplay(200), [&polls] { ++polls; })};
	BOOST_TEST(took == 50 * ms);
	BOOST_TEST(polls == 1u);
	BOOST_TEST(t.sleeps.empty());
}

/* Test that a clock that steps backwards restarts the frame instead of hanging or sleeping a
 * huge time.
 */
BOOST_AUTO_TEST_CASE(clock_going_backwards_restarts_the_frame)
{
	fake_time t;
	t.now = 100 * ms;
	frame_pacer pacer{[&t] { return t.read(); },
	                  [&t](const std::uint64_t ns, const bool precise) {
		                  t.sleeps.push_back({ns, precise});
		                  t.now += ns;
	                  }};
	/* The frame started at 200 ms but the clock now reads 100 ms. */
	const auto took{pacer.wait(200 * ms, gameplay(200), [] {})};
	BOOST_TEST(took == 5 * ms);
	BOOST_TEST(t.now == 105 * ms);
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

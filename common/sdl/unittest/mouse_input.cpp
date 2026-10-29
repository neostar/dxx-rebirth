/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* The mouse helpers of #43, built against SDL2 and SDL3: the seam is each helper's interface,
 * a whole SDL event or a float delta in, whole pixels or wheel steps out.
 */

#include "common/sdl/mouse_input.h"

#include <array>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth mouse input
#include <boost/test/unit_test.hpp>

namespace {

/* The whole pixels an accumulator hands out for each delta of `deltas`. */
std::vector<int> pixels_of(const std::vector<float> &deltas)
{
	dcx::sdl::motion_axis axis;
	std::vector<int> out;
	for (const float d : deltas)
		out.push_back(axis.take(d));
	return out;
}

SDL_MouseWheelEvent wheel(const int steps, const float precise, const bool flipped)
{
	SDL_MouseWheelEvent e{};
	e.type = SDL_MOUSEWHEEL;
#if SDL_MAJOR_VERSION == 3
	e.integer_y = steps;
	e.y = precise;
	e.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
#else
	e.y = steps;
	e.preciseY = precise;
	e.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
#endif
	return e;
}

}

/* Test that sub-pixel deltas add up to whole pixels instead of each truncating to zero, the
 * remainder carried from one event to the next.
 */
BOOST_AUTO_TEST_CASE(sub_pixel_deltas_add_up_to_whole_pixels)
{
	const std::vector<int> expected{0, 0, 1, 0, 1};
	BOOST_TEST(pixels_of({0.4f, 0.4f, 0.4f, 0.4f, 0.4f}) == expected,
	           boost::test_tools::per_element());
}

/* Test that negative deltas add up the same way, and that a turn back cancels the remainder
 * rather than handing out a pixel the mouse did not travel.
 */
BOOST_AUTO_TEST_CASE(negative_deltas_add_up_and_cancel)
{
	const std::vector<int> expected{0, -1, 0, 0};
	BOOST_TEST(pixels_of({-0.75f, -0.75f, 0.5f, 0.25f}) == expected,
	           boost::test_tools::per_element());
}

/* Test that whole deltas pass through unchanged, as SDL2's integer motion did. */
BOOST_AUTO_TEST_CASE(whole_deltas_pass_through)
{
	const std::vector<int> expected{3, -7, 0, 120};
	BOOST_TEST(pixels_of({3.f, -7.f, 0.f, 120.f}) == expected, boost::test_tools::per_element());
}

/* Test that the two axes keep their own remainders. */
BOOST_AUTO_TEST_CASE(axes_keep_their_own_remainders)
{
	dcx::sdl::motion_accumulator m;
	BOOST_TEST((m.take(0.5f, -0.5f) == std::array<int, 2>{{0, 0}}));
	BOOST_TEST((m.take(0.5f, 0.f) == std::array<int, 2>{{1, 0}}));
	BOOST_TEST((m.take(0.f, -0.5f) == std::array<int, 2>{{0, -1}}));
}

/* Test that a notched wheel's click is one step up or down. */
BOOST_AUTO_TEST_CASE(notched_wheel_gives_one_step_per_click)
{
	BOOST_TEST(dcx::sdl::wheel_steps(wheel(1, 1.f, false)) == 1);
	BOOST_TEST(dcx::sdl::wheel_steps(wheel(-1, -1.f, false)) == -1);
	BOOST_TEST(dcx::sdl::wheel_steps(wheel(3, 3.f, false)) == 3);
}

/* Test that a precise trackpad's fractions give no step until SDL has accumulated a whole
 * tick: the step count follows the whole ticks, not the float amount.
 */
BOOST_AUTO_TEST_CASE(precise_trackpad_steps_on_whole_ticks)
{
	BOOST_TEST(dcx::sdl::wheel_steps(wheel(0, 0.3f, false)) == 0);
	BOOST_TEST(dcx::sdl::wheel_steps(wheel(1, 0.3f, false)) == 1);
	BOOST_TEST(dcx::sdl::wheel_steps(wheel(0, -0.9f, false)) == 0);
}

/* Test that a flipped event, natural scrolling, is turned back, so the step follows the
 * physical wheel.
 */
BOOST_AUTO_TEST_CASE(flipped_wheel_is_turned_back)
{
	BOOST_TEST(dcx::sdl::wheel_steps(wheel(-1, -1.f, true)) == 1);
	BOOST_TEST(dcx::sdl::wheel_steps(wheel(2, 0.2f, true)) == -2);
}

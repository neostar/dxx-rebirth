/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/window_state.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth window state
#include <boost/test/unit_test.hpp>

namespace {

using dcx::visibility;
using dcx::sdl::pixel_size;
using dcx::sdl::window_event;
using dcx::sdl::window_state;

constexpr pixel_size canvas{640, 480};

/* Feed one event that carries no size. */
dcx::sdl::window_reaction feed(window_state &s, const window_event e)
{
	return s.handle(e, {}, canvas);
}

}

/* Test that a window no event has reported on counts as visible: Windows
 * sends no occlusion, Wayland no minimise (docs/agents/sdl-window.md 6).
 */
BOOST_AUTO_TEST_CASE(a_new_window_is_visible)
{
	const window_state s{false};
	BOOST_TEST((s.vis() == visibility::visible));
}

/* Test that occluded, minimised and hidden each make the window hidden to
 * the pacer, and that their opposite events make it visible again.
 */
BOOST_AUTO_TEST_CASE(each_hiding_event_hides_until_its_opposite)
{
	const std::array<std::pair<window_event, window_event>, 3> pairs{{
	    {window_event::occluded, window_event::exposed},
	    {window_event::minimized, window_event::restored},
	    {window_event::hidden, window_event::shown},
	}};
	for (const auto &[hide, show] : pairs)
	{
		window_state s{false};
		feed(s, hide);
		BOOST_TEST((s.vis() == visibility::hidden));
		feed(s, show);
		BOOST_TEST((s.vis() == visibility::visible));
	}
}

/* Test that a window both minimised and occluded stays hidden until both
 * have ended, in either order.
 */
BOOST_AUTO_TEST_CASE(two_reasons_to_hide_both_have_to_end)
{
	window_state s{false};
	feed(s, window_event::minimized);
	feed(s, window_event::occluded);
	feed(s, window_event::restored);
	BOOST_TEST((s.vis() == visibility::hidden));
	feed(s, window_event::exposed);
	BOOST_TEST((s.vis() == visibility::visible));
}

/* Test that the fullscreen state follows SDL's confirmation events, not the
 * request, and that each change is reported once.
 */
BOOST_AUTO_TEST_CASE(fullscreen_follows_the_confirming_events)
{
	window_state s{false};
	BOOST_TEST(!s.requested_fullscreen());
	const auto entered{feed(s, window_event::enter_fullscreen)};
	BOOST_TEST(s.requested_fullscreen());
	BOOST_TEST((entered.fullscreen == std::optional{true}));
	BOOST_TEST(!feed(s, window_event::enter_fullscreen).fullscreen.has_value());
	const auto left{feed(s, window_event::leave_fullscreen)};
	BOOST_TEST(!s.requested_fullscreen());
	BOOST_TEST((left.fullscreen == std::optional{false}));
}

/* Test that a window created fullscreen starts so. */
BOOST_AUTO_TEST_CASE(a_window_created_fullscreen_is_fullscreen)
{
	const window_state s{true};
	BOOST_TEST(s.requested_fullscreen());
}

namespace {

/* A fake SDL_SetWindowFullscreen: records each request, grants it or not. */
struct fullscreen_requests
{
	std::vector<bool> sent;
	bool granted{true};
	auto request()
	{
		return [this](const bool fullscreen) {
			sent.push_back(fullscreen);
			return granted;
		};
	}
};

}

/* Test that a toggle asks for the opposite of the last request, not of
 * the last confirmation: two toggles before SDL confirms either end where
 * they started, instead of asking for the same change twice.
 */
BOOST_AUTO_TEST_CASE(two_toggles_before_confirmation_end_where_they_started)
{
	window_state s{false};
	fullscreen_requests sdl;
	s.toggle_fullscreen(sdl.request());
	s.toggle_fullscreen(sdl.request());
	BOOST_TEST((sdl.sent == std::vector<bool>{true, false}));
	BOOST_TEST(!s.requested_fullscreen());
}

/* Test that a window created fullscreen that SDL never confirms (a hidden
 * window keeps the flag pending) still toggles to windowed and back.
 */
BOOST_AUTO_TEST_CASE(a_fullscreen_sdl_never_confirms_still_toggles)
{
	window_state s{true};
	fullscreen_requests sdl;
	s.toggle_fullscreen(sdl.request());
	BOOST_TEST(!s.requested_fullscreen());
	s.toggle_fullscreen(sdl.request());
	BOOST_TEST((sdl.sent == std::vector<bool>{false, true}));
	BOOST_TEST(s.requested_fullscreen());
}

/* Test that a request SDL refuses leaves the target where it was, so the
 * next toggle asks for the same change again.
 */
BOOST_AUTO_TEST_CASE(a_refused_request_keeps_the_target)
{
	window_state s{false};
	fullscreen_requests sdl;
	sdl.granted = false;
	BOOST_TEST(!s.toggle_fullscreen(sdl.request()));
	BOOST_TEST(!s.requested_fullscreen());
	sdl.granted = true;
	BOOST_TEST(s.toggle_fullscreen(sdl.request()));
	BOOST_TEST((sdl.sent == std::vector<bool>{true, true}));
}

/* Test that a change SDL confirms without a request (the user left
 * fullscreen through the system) moves the target, so the next toggle
 * goes back instead of asking for the state the window is in.
 */
BOOST_AUTO_TEST_CASE(an_unrequested_change_moves_the_target)
{
	window_state s{true};
	fullscreen_requests sdl;
	feed(s, window_event::leave_fullscreen);
	s.toggle_fullscreen(sdl.request());
	BOOST_TEST((sdl.sent == std::vector<bool>{true}));
}

/* Test that a pixel size other than the canvas's resizes the canvas, and
 * that the size SDL sends at creation, equal to the canvas, does not.
 */
BOOST_AUTO_TEST_CASE(a_new_pixel_size_resizes_the_canvas)
{
	window_state s{false};
	BOOST_TEST(!s.handle(window_event::pixel_size_changed, canvas, canvas).resize.has_value());
	const pixel_size full{1920, 1080};
	const auto r{s.handle(window_event::pixel_size_changed, full, canvas)};
	BOOST_REQUIRE(r.resize.has_value());
	BOOST_TEST(r.resize.value().width == 1920);
	BOOST_TEST(r.resize.value().height == 1080);
}

/* Test that an empty pixel size, as a minimised window may report, never
 * resizes the canvas.
 */
BOOST_AUTO_TEST_CASE(an_empty_pixel_size_is_ignored)
{
	window_state s{false};
	BOOST_TEST(!s.handle(window_event::pixel_size_changed, {0, 0}, canvas).resize.has_value());
}

/* Test that events other than the size change never resize. */
BOOST_AUTO_TEST_CASE(only_the_size_event_resizes)
{
	window_state s{false};
	const pixel_size full{1920, 1080};
	BOOST_TEST(!s.handle(window_event::enter_fullscreen, full, canvas).resize.has_value());
}

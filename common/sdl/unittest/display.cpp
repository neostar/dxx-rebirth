/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/display.h"

#include <memory>
#include <string>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth display
#include <boost/test/unit_test.hpp>

namespace {

using dcx::sdl::display_request;
using dcx::sdl::renderer;

/* A fake SDL that keeps the rules open_display has to respect: the swap
 * interval needs a current GL context (SDL_video.h, SDL_GL_SetSwapInterval),
 * and a window can fail to come up.  Tests read what
 * the fake SDL ended up with, not the calls that got it there.
 */
struct fake_sdl
{
	bool window_possible{true};
	bool swap_interval_supported{true};
	/* What SDL ended up with. */
	std::optional<SDL_WindowFlags> window_flags;
	bool context_current{false};
	std::optional<int> swap_interval;
	const char *error{""};
};

/* A handle of the fake: it lives while its owner does. */
struct fake_handle
{};

struct fake_ops
{
	fake_sdl *sdl;
	using window_ptr = std::unique_ptr<fake_handle>;
	using gl_context_ptr = std::unique_ptr<fake_handle>;

	window_ptr create_window(const char *, dcx::sdl::pixel_size, const SDL_WindowFlags flags) const
	{
		if (!sdl->window_possible)
		{
			sdl->error = "no video device";
			return nullptr;
		}
		sdl->window_flags = flags;
		return std::make_unique<fake_handle>();
	}
	gl_context_ptr create_gl_context(fake_handle *) const
	{
		if (!(sdl->window_flags.value() & SDL_WINDOW_OPENGL))
		{
			sdl->error = "window has no OpenGL";
			return nullptr;
		}
		sdl->context_current = true;
		return std::make_unique<fake_handle>();
	}
	bool make_current(fake_handle *, fake_handle *) const
	{
		sdl->context_current = true;
		return true;
	}
	bool set_swap_interval(const int interval) const
	{
		if (!sdl->context_current)
		{
			sdl->error = "no current context";
			return false;
		}
		if (!sdl->swap_interval_supported)
		{
			sdl->error = "unsupported";
			return false;
		}
		sdl->swap_interval = interval;
		return true;
	}
	void sync_window(fake_handle *) const
	{}
	const char *last_error() const
	{
		return sdl->error;
	}
};

display_request gl_request()
{
	return {.title = "test", .size = {640, 480}, .vsync = true};
}

}

/* Test that a GL window gets OpenGL as its only graphics flag, a current
 * context, and the swap interval the request asks for.
 */
BOOST_AUTO_TEST_CASE(gl_window_has_its_context_and_swap_interval)
{
	fake_sdl sdl;
	const auto d{dcx::sdl::open_display(gl_request(), fake_ops{&sdl})};
	BOOST_TEST(static_cast<bool>(d));
	BOOST_TEST((d.chosen == renderer::gl));
	BOOST_TEST((sdl.window_flags == std::optional<SDL_WindowFlags>{SDL_WINDOW_OPENGL}));
	BOOST_TEST(sdl.context_current);
	BOOST_TEST((sdl.swap_interval == std::optional{1}));
	BOOST_TEST(!d.swap_interval_error.has_value());
}

/* Test that vsync off sets the swap interval to 0, not leaves it alone. */
BOOST_AUTO_TEST_CASE(vsync_off_sets_interval_zero)
{
	fake_sdl sdl;
	auto r{gl_request()};
	r.vsync = false;
	const auto d{dcx::sdl::open_display(r, fake_ops{&sdl})};
	BOOST_TEST((sdl.swap_interval == std::optional{0}));
}

/* Test that a refused swap interval is reported, and the window still
 * opens: the game runs without it.
 */
BOOST_AUTO_TEST_CASE(refused_swap_interval_is_reported)
{
	fake_sdl sdl;
	sdl.swap_interval_supported = false;
	const auto d{dcx::sdl::open_display(gl_request(), fake_ops{&sdl})};
	BOOST_TEST(static_cast<bool>(d));
	BOOST_REQUIRE(d.swap_interval_error.has_value());
	BOOST_TEST(d.swap_interval_error.value() == "unsupported");
}

/* Test that a hidden request adds SDL_WINDOW_HIDDEN and nothing else, and
 * fullscreen and borderless their own flags (sections 3 and 7).
 */
BOOST_AUTO_TEST_CASE(request_flags_become_window_flags)
{
	fake_sdl sdl;
	auto r{gl_request()};
	r.hidden = true;
	const auto hidden{dcx::sdl::open_display(r, fake_ops{&sdl})};
	BOOST_TEST((sdl.window_flags ==
	            std::optional<SDL_WindowFlags>{SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN}));
	r.hidden = false;
	r.fullscreen = true;
	r.borderless = true;
	const auto full{dcx::sdl::open_display(r, fake_ops{&sdl})};
	BOOST_TEST((sdl.window_flags ==
	            std::optional<SDL_WindowFlags>{SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN |
	                                           SDL_WINDOW_BORDERLESS}));
	BOOST_TEST(full.state.fullscreen());
}

/* Test that a window SDL cannot create fails the open, with SDL's error. */
BOOST_AUTO_TEST_CASE(no_window_fails_with_sdl_error)
{
	fake_sdl sdl;
	sdl.window_possible = false;
	const auto d{dcx::sdl::open_display(gl_request(), fake_ops{&sdl})};
	BOOST_TEST(!static_cast<bool>(d));
	BOOST_REQUIRE(d.error.has_value());
	BOOST_TEST(d.error.value().find("no video device") != std::string::npos);
	BOOST_TEST(!sdl.context_current);
}

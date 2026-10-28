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
 * and an SDL_GPU device or a window can fail to come up.  Tests read what
 * the fake SDL ended up with, not the calls that got it there.
 */
struct fake_sdl
{
	bool gpu_available{true};
	bool window_possible{true};
	bool swap_interval_supported{true};
	/* The window manager refuses fullscreen: SDL drops the flag by the
	 * time SDL_SyncWindow returns.
	 */
	bool fullscreen_refused{false};
	/* What SDL ended up with. */
	std::optional<SDL_WindowFlags> window_flags;
	bool window_claimed{false};
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
	using gpu_device_ptr = std::unique_ptr<fake_handle>;
	using window_ptr = std::unique_ptr<fake_handle>;
	using claim_ptr = std::unique_ptr<fake_handle>;
	using gl_context_ptr = std::unique_ptr<fake_handle>;

	gpu_device_ptr create_gpu_device(const char *) const
	{
		if (!sdl->gpu_available)
		{
			sdl->error = "no supported GPU driver";
			return nullptr;
		}
		return std::make_unique<fake_handle>();
	}
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
	claim_ptr claim_window(fake_handle *, fake_handle *) const
	{
		/* SDL_GPU claims only a window without a graphics flag here. */
		if (sdl->window_flags.value() & SDL_WINDOW_OPENGL)
		{
			sdl->error = "window has OpenGL";
			return nullptr;
		}
		sdl->window_claimed = true;
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
	{
		if (sdl->fullscreen_refused)
			sdl->window_flags = sdl->window_flags.value() & ~SDL_WINDOW_FULLSCREEN;
	}
	bool fullscreen(fake_handle *) const
	{
		return sdl->window_flags.value() & SDL_WINDOW_FULLSCREEN;
	}
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
	BOOST_TEST(full.state.requested_fullscreen());
}

/* Test that the window's fullscreen state starts as SDL has it after the
 * sync, not as the request asked: a toggle then asks for a real change.
 */
BOOST_AUTO_TEST_CASE(fullscreen_starts_as_sdl_has_it)
{
	fake_sdl sdl;
	sdl.fullscreen_refused = true;
	auto r{gl_request()};
	r.fullscreen = true;
	const auto d{dcx::sdl::open_display(r, fake_ops{&sdl})};
	BOOST_TEST(!d.state.requested_fullscreen());
}

/* Test that SDL_GPU, when a device comes up, claims a window made without
 * a graphics flag and gets no GL context (section 2).
 */
BOOST_AUTO_TEST_CASE(sdl_gpu_claims_a_window_without_graphics_flag)
{
	fake_sdl sdl;
	auto r{gl_request()};
	r.preferred = renderer::sdl_gpu;
	const auto d{dcx::sdl::open_display(r, fake_ops{&sdl})};
	BOOST_TEST(static_cast<bool>(d));
	BOOST_TEST((d.chosen == renderer::sdl_gpu));
	BOOST_TEST((sdl.window_flags == std::optional<SDL_WindowFlags>{0}));
	BOOST_TEST(sdl.window_claimed);
	BOOST_TEST(!sdl.context_current);
	BOOST_TEST(!d.fallback_reason.has_value());
}

/* Test that without an SDL_GPU device the window opens on GL, with the
 * device's error in the logged reason (#52).
 */
BOOST_AUTO_TEST_CASE(no_gpu_device_falls_back_to_gl_with_reason)
{
	fake_sdl sdl;
	sdl.gpu_available = false;
	auto r{gl_request()};
	r.preferred = renderer::sdl_gpu;
	const auto d{dcx::sdl::open_display(r, fake_ops{&sdl})};
	BOOST_TEST(static_cast<bool>(d));
	BOOST_TEST((d.chosen == renderer::gl));
	BOOST_TEST((sdl.window_flags == std::optional<SDL_WindowFlags>{SDL_WINDOW_OPENGL}));
	BOOST_TEST((sdl.swap_interval == std::optional{1}));
	BOOST_REQUIRE(d.fallback_reason.has_value());
	BOOST_TEST(d.fallback_reason.value().find("no supported GPU driver") != std::string::npos);
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

/* Test that the SDL events the game acts on translate, and others do not. */
BOOST_AUTO_TEST_CASE(sdl_window_events_translate)
{
	using dcx::sdl::window_event;
	SDL_Event e{};
	e.type = SDL_EVENT_WINDOW_OCCLUDED;
	BOOST_TEST((dcx::sdl::window_event_of(e) == std::optional{window_event::occluded}));
	e.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
	BOOST_TEST((dcx::sdl::window_event_of(e) == std::optional{window_event::pixel_size_changed}));
	e.type = SDL_EVENT_WINDOW_MOVED;
	BOOST_TEST(!dcx::sdl::window_event_of(e).has_value());
}

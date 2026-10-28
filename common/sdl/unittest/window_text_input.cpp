/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* Text input and the window stack (#42, R12).  The seam is the game's own window_close
 * (common/arch/sdl/window.cpp) with test windows whose event_handler does what a menu's override
 * does: it answers window_close without calling newmenu::event_handler.
 */

#include <SDL3/SDL.h>
#include <memory>

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "args.h"
#include "console.h"
#include "event.h"
#include "gr.h"
#include "key.h"
#include "timer.h"
#include "window.h"
#include "tools/dev/upstream_end.h"
// clang-format on

namespace dcx {

CArg CGameArg;

// NOLINTNEXTLINE(readability-identifier-naming): the game's name for its window
SDL_Window *g_pRebirthSDLMainWindow;

fix64 timer_query()
{
	return 0;
}

window_event_result event_send(const d_event &)
{
	return window_event_result::ignored;
}

#undef con_printf
void con_printf(const con_priority_wrapper, const char *, ...)
{}

// NOLINTNEXTLINE(readability-identifier-naming): the game's name for the current canvas
grs_canvas *grd_curcanv;

void gr_init_sub_canvas(grs_subcanvas &, grs_canvas &, uint16_t, uint16_t, uint16_t, uint16_t)
{}

void menu_destroy_hook(window *)
{}

}

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth sdl3 window text input
#include <boost/test/unit_test.hpp>

namespace {

/* A hidden window on SDL's dummy video driver, which text input needs. */
struct dummy_window
{
	dummy_window()
	{
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
		BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO));
		dcx::g_pRebirthSDLMainWindow = SDL_CreateWindow("window test", 64, 64, SDL_WINDOW_HIDDEN);
		BOOST_REQUIRE(dcx::g_pRebirthSDLMainWindow);
	}
	dummy_window(const dummy_window &) = delete;
	dummy_window &operator=(const dummy_window &) = delete;
	dummy_window(dummy_window &&) = delete;
	dummy_window &operator=(dummy_window &&) = delete;
	~dummy_window()
	{
		dcx::key_text_input_close();
		SDL_DestroyWindow(dcx::g_pRebirthSDLMainWindow);
		dcx::g_pRebirthSDLMainWindow = nullptr;
		SDL_Quit();
	}
};

/* What a menu does that overrides event_handler for window_close and sends nothing on to
 * newmenu::event_handler: it answers the event and leaves text input as it was.
 */
struct early_return_window : dcx::window
{
	using window::window;
	dcx::window_event_result event_handler(const d_event &) override
	{
		return dcx::window_event_result::ignored;
	}
};

}

/* Test that closing the front window closes text input, whatever its event_handler does with
 * window_close: a menu's override that returns early must not leave the field's text input on.
 */
BOOST_AUTO_TEST_CASE(closing_a_window_that_returns_early_closes_text_input)
{
	const dummy_window sdl;
	grs_subcanvas canvas;
	auto *const w{new early_return_window{canvas, 0, 0, 10, 10}};
	w->send_creation_events();
	dcx::key_text_input_open();
	BOOST_REQUIRE(dcx::key_text_input_active());
	BOOST_TEST(dcx::window_close(w) == 1);
	BOOST_TEST(!dcx::key_text_input_active());
}

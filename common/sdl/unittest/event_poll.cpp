/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* SDL3 events through the game's event loop (#42, R12).  The seam is SDL's own queue: the test
 * pushes events with SDL_PushEvent and calls event_poll, which pumps and drains them through
 * process_event_batch as the game does, and standard_handler, the game's last-resort handler, is a
 * recorder.  With no window open (window_get_front is null), every event the game would send
 * reaches it.  The mouse, joystick and window handlers event.cpp calls are stubs. */

#include <SDL3/SDL.h>
#include <array>
#include <vector>

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "args.h"
#include "console.h"
#include "dxxerror.h"
#include "event.h"
#include "fwd-gr.h"
#include "inferno.h"
#include "joy.h"
#include "key.h"
#include "mouse.h"
#include "timer.h"
#include "window.h"
#include "cmd.h"
#include "config.h"
#include "common/sdl/display.h"
#include "tools/dev/upstream_end.h"
// clang-format on

namespace {

struct seen
{
	event_type type;
	unsigned key;
	/* What key_ascii returned when the key reached the handler. */
	unsigned ascii;
};
std::vector<seen> seen_events;

}

int cmd_queue_process()
{
	return 0;
}

namespace dcx {

CArg CGameArg;

CCfg CGameCfg;

// NOLINTNEXTLINE(readability-identifier-naming): the game's name for its window
SDL_Window *g_pRebirthSDLMainWindow;

fix64 timer_query()
{
	return 0;
}

fix64 timer_update()
{
	return 0;
}

#if DXX_USE_DBG_HARNESS
/* The input script event.cpp reads only in a harness build reports its errors here. */
#undef UserError
void UserError(const char *, ...)
{
	std::abort();
}
#endif

#undef con_printf
void con_printf(const con_priority_wrapper, const char *, ...)
{}

window *window_get_front()
{
	return nullptr;
}

window *window_get_first()
{
	return nullptr;
}

window_event_result window::send_event(const d_event &, const char *, unsigned)
{
	return window_event_result::ignored;
}

void mouse_enable_cursor()
{}

void mouse_disable_cursor()
{}

void mouse_cursor_autohide()
{}

window_event_result mouse_button_handler(const SDL_MouseButtonEvent *)
{
	return window_event_result::ignored;
}

window_event_result mouse_motion_handler(const SDL_MouseMotionEvent *)
{
	return window_event_result::ignored;
}

window_event_result joy_button_handler(const SDL_JoyButtonEvent *)
{
	return window_event_result::ignored;
}

window_event_result joy_hat_handler(const SDL_JoyHatEvent *)
{
	return window_event_result::ignored;
}

window_event_result joy_axis_handler(const SDL_JoyAxisEvent *)
{
	return window_event_result::ignored;
}

window_event_result joy_axisbutton_handler(const SDL_JoyAxisEvent *)
{
	return window_event_result::ignored;
}

void gr_handle_window_event(const SDL_Event &)
{}

void gr_flip()
{}

window_event_result standard_handler(const d_event &event)
{
	const bool is_key{event.type == event_type::key_command ||
	                  event.type == event_type::key_release};
	const bool is_press{event.type == event_type::key_command};
	seen_events.push_back({event.type, is_key ? static_cast<unsigned>(event_key_get(event)) : 0u,
	                       is_press ? key_ascii() : 0u});
	return window_event_result::handled;
}

}

extern "C" const bool *SDL_GetKeyboardState(int *const numkeys)
{
	static const std::array<bool, SDL_SCANCODE_COUNT> none{};
	if (numkeys)
		*numkeys = static_cast<int>(none.size());
	return none.data();
}

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth sdl3 event poll
#include <boost/test/unit_test.hpp>

namespace {

struct sdl_events
{
	sdl_events()
	{
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
		BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO));
		dcx::g_pRebirthSDLMainWindow = SDL_CreateWindow("event test", 64, 64, SDL_WINDOW_HIDDEN);
		BOOST_REQUIRE(dcx::g_pRebirthSDLMainWindow);
		seen_events.clear();
		dcx::key_flush();
	}
	sdl_events(const sdl_events &) = delete;
	sdl_events &operator=(const sdl_events &) = delete;
	sdl_events(sdl_events &&) = delete;
	sdl_events &operator=(sdl_events &&) = delete;
	~sdl_events()
	{
		dcx::key_text_input_close();
		SDL_DestroyWindow(dcx::g_pRebirthSDLMainWindow);
		dcx::g_pRebirthSDLMainWindow = nullptr;
		SDL_Quit();
	}
};

void push_key(const SDL_Keycode key, const bool down)
{
	SDL_Event e{};
	e.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	e.key.key = key;
	e.key.down = down;
	BOOST_REQUIRE(SDL_PushEvent(&e));
}

void push_text(const char *const text)
{
	SDL_Event e{};
	e.text.type = SDL_EVENT_TEXT_INPUT;
	e.text.text = text;
	BOOST_REQUIRE(SDL_PushEvent(&e));
}

/* The game key codes of the key presses event_poll sent. */
std::vector<unsigned> pressed()
{
	std::vector<unsigned> keys;
	for (const auto &e : seen_events)
		if (e.type == event_type::key_command)
			keys.push_back(e.key);
	return keys;
}

}

/* Test that a key pushed as SDL3 delivers it reaches the game as its key code, and that the
 * Command key before it is the modifier.
 */
BOOST_AUTO_TEST_CASE(pushed_keys_reach_the_game_with_the_command_modifier)
{
	const sdl_events sdl;
	push_key(SDLK_LGUI, true);
	push_key(SDLK_P, true);
	dcx::event_poll();
	const std::vector<unsigned> expected{static_cast<unsigned>(KEY_LMETA + KEY_COMMAND),
	                                     static_cast<unsigned>(KEY_P + KEY_COMMAND)};
	BOOST_TEST(pressed() == expected, boost::test_tools::per_element());
}

/* Test that SDL's quit event, which the app menu's Cmd-Q posts, reaches the game's handler as
 * a quit event (the quit dialog), and not as a key: the owner's decision of ADR 7.
 */
BOOST_AUTO_TEST_CASE(sdl_quit_reaches_the_game_as_a_quit_event)
{
	const sdl_events sdl;
	SDL_Event e{};
	e.type = SDL_EVENT_QUIT;
	BOOST_REQUIRE(SDL_PushEvent(&e));
	dcx::event_poll();
	unsigned quits{0};
	for (const auto &s : seen_events)
		if (s.type == event_type::quit)
			++quits;
	BOOST_TEST(quits == 1u);
	BOOST_TEST(pressed().empty());
}

/* Test that, with text input open, the text that follows a key is in key_ascii when that key's
 * handler runs, once: SDL queues the text after its key, and the handler reads it with the key.
 */
BOOST_AUTO_TEST_CASE(text_after_a_key_is_there_when_the_keys_handler_runs)
{
	const sdl_events sdl;
	dcx::key_text_input_open();
	BOOST_REQUIRE(dcx::key_text_input_active());
	push_key(SDLK_A, true);
	push_text("A");
	push_key(SDLK_B, true);
	push_text("B");
	dcx::event_poll();
	std::vector<unsigned> ascii;
	for (const auto &e : seen_events)
		if (e.type == event_type::key_command)
			ascii.push_back(e.ascii);
	const std::vector<unsigned> expected{'A', 'B'};
	BOOST_TEST(ascii == expected, boost::test_tools::per_element());
	BOOST_TEST(dcx::key_ascii() == 255);
}

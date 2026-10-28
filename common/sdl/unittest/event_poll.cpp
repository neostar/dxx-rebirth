/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* SDL3 events through the game's event loop (#42, R12).  The seam is SDL's own queue: the test
 * pushes events with SDL_PushEvent and calls event_poll, which pumps and drains them through
 * process_event_batch as the game does, and standard_handler, the game's last-resort handler,
 * is a recorder.  With no window open (window_get_front is null), every event the game would
 * send reaches it.  The mouse, joystick and window handlers event.cpp calls are stubs.
 */

#include <SDL3/SDL.h>
#include <array>
#include <functional>
#include <ostream>
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
/* The memory a later text event points to, which the handler of an earlier key overwrites. */
std::array<char, 2> reused_memory{};
/* Runs when a key press reaches the game, as a window's handler would. */
std::function<void()> on_key_command;

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
	if (is_press && on_key_command)
		on_key_command();
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
		on_key_command = nullptr;
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
	e.key.timestamp = SDL_GetTicksNS();
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

namespace {

/* A key press the game received: its key code and what key_ascii returned with it. */
struct press
{
	unsigned key;
	unsigned ascii;
	bool operator==(const press &) const = default;
	friend std::ostream &operator<<(std::ostream &o, const press &p)
	{
		return o << "(key " << p.key << ", ascii " << p.ascii << ")";
	}
};

std::vector<press> presses()
{
	std::vector<press> v;
	for (const auto &e : seen_events)
		if (e.type == event_type::key_command)
			v.push_back({e.key, e.ascii});
	return v;
}

}

/* Test that a text event that does not directly follow its key still reaches a handler: Cocoa
 * sends Backspace down and up between a key and its text, so the text arrives alone, as a key
 * command for no key (key code 0) with the text in key_ascii.
 */
BOOST_AUTO_TEST_CASE(text_after_backspace_down_and_up_reaches_the_handler)
{
	const sdl_events sdl;
	dcx::key_text_input_open();
	push_key(SDLK_BACKSPACE, true);
	push_key(SDLK_BACKSPACE, false);
	push_text("x");
	dcx::event_poll();
	const std::vector<press> expected{{KEY_BACKSP, 255}, {0, 'x'}};
	BOOST_TEST(presses() == expected, boost::test_tools::per_element());
}

/* Test that a text event that opens a batch reaches the handler too, as an input method sends
 * its text alone. */
BOOST_AUTO_TEST_CASE(text_that_opens_a_batch_reaches_the_handler)
{
	const sdl_events sdl;
	dcx::key_text_input_open();
	push_text("\xc3\xa9");
	dcx::event_poll();
	const std::vector<press> expected{{0, 0xe9}};
	BOOST_TEST(presses() == expected, boost::test_tools::per_element());
}

/* Test that text is dropped, and no key command sent, with text input closed. */
BOOST_AUTO_TEST_CASE(text_with_text_input_closed_reaches_nobody)
{
	const sdl_events sdl;
	push_text("x");
	dcx::event_poll();
	BOOST_TEST(presses().empty());
}

/* Test that the text events of a batch stay whole while a handler runs a menu of its own and so
 * pumps events: SDL frees the text of its events at the next pump.  The handler of the first key
 * overwrites the memory the later text events point to, and pumps.
 */
BOOST_AUTO_TEST_CASE(text_survives_a_handler_that_pumps_events)
{
	const sdl_events sdl;
	dcx::key_text_input_open();
	reused_memory = {'B', '\0'};
	push_key(SDLK_A, true);
	push_text("A");
	push_key(SDLK_BACKSPACE, true);
	push_key(SDLK_BACKSPACE, false);
	push_text(reused_memory.data());
	on_key_command = [] {
		reused_memory[0] = 'X';
		SDL_PumpEvents();
		on_key_command = nullptr;
	};
	dcx::event_poll();
	const std::vector<press> expected{{KEY_A, 'A'}, {KEY_BACKSP, 255}, {0, 'B'}};
	BOOST_TEST(presses() == expected, boost::test_tools::per_element());
}

/* Test that the keys queued in the same batch as the key that opens a field give their keycodes:
 * SDL queued them when text input was off, so no text event follows. */
BOOST_AUTO_TEST_CASE(keys_queued_before_the_field_opened_give_their_keycodes)
{
	const sdl_events sdl;
	push_key(SDLK_F1, true);
	push_key(SDLK_A, true);
	push_key(SDLK_B, true);
	on_key_command = [] {
		dcx::key_text_input_open();
		on_key_command = nullptr;
	};
	dcx::event_poll();
	const std::vector<press> expected{{KEY_F1, 255}, {KEY_A, 'a'}, {KEY_B, 'b'}};
	BOOST_TEST(presses() == expected, boost::test_tools::per_element());
}

/* Test that a key queued after the field opened and brought no text gives nothing: its text
 * would have come with it. */
BOOST_AUTO_TEST_CASE(a_key_queued_after_the_field_opened_gives_only_its_text)
{
	const sdl_events sdl;
	dcx::key_text_input_open();
	push_key(SDLK_A, true);
	dcx::event_poll();
	const std::vector<press> expected{{KEY_A, 255}};
	BOOST_TEST(presses() == expected, boost::test_tools::per_element());
}

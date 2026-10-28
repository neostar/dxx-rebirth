/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* SDL3 keyboard events into the game's key handler (#42, R12).  The seams:
 *  - key_handler takes the SDL event, so a test builds the event as SDL3
 *    would deliver it and reads the game key code from the event the game
 *    windows receive; event_send is replaced by a recorder, which the
 *    linker prefers to the game's over the library's.
 *  - SDL_GetKeyboardState is replaced the same way, because SDL_PushEvent
 *    leaves the device state unchanged and SDL3 has no public setter.
 */

#include <SDL3/SDL.h>
#include <array>
#include <vector>

#include "common/sdl/sdl_deleter.h"

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "args.h"
#include "console.h"
#include "event.h"
#include "key.h"
#include "timer.h"
#include "tools/dev/upstream_end.h"
// clang-format on

namespace {

std::vector<unsigned> sent_keys;
unsigned sent_other_events{0};
std::array<bool, SDL_SCANCODE_COUNT> fake_keyboard{};

}

namespace dcx {

CArg CGameArg;

// NOLINTNEXTLINE(readability-identifier-naming): the game's name for its window
SDL_Window *g_pRebirthSDLMainWindow;

fix64 timer_query()
{
	return 0;
}

window_event_result event_send(const d_event &event)
{
	if (event.type != event_type::key_command && event.type != event_type::key_release)
		++sent_other_events;
	if (event.type == event_type::key_command)
		sent_keys.push_back(static_cast<unsigned>(event_key_get(event)));
	return window_event_result::handled;
}

#undef con_printf
void con_printf(const con_priority_wrapper, const char *, ...)
{}

}

extern "C" const bool *SDL_GetKeyboardState(int *const numkeys)
{
	if (numkeys)
		*numkeys = static_cast<int>(fake_keyboard.size());
	return fake_keyboard.data();
}

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth sdl3 key
#include <boost/test/unit_test.hpp>

namespace {

SDL_KeyboardEvent key_event(const SDL_Keycode key, const bool down)
{
	SDL_KeyboardEvent e{};
	e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	e.key = key;
	e.down = down;
	return e;
}

/* The game key code the handler sent for one press, or 0 when it sent none. */
unsigned press(const SDL_Keycode key)
{
	sent_keys.clear();
	const auto e{key_event(key, true)};
	dcx::key_handler(&e);
	const auto code{sent_keys.empty() ? 0u : sent_keys.back()};
	const auto r{key_event(key, false)};
	dcx::key_handler(&r);
	return code;
}

}

/* Test that SDL3's keycodes give the game key codes SDL2's keysym.sym gave: the pairs are
 * written out by hand from the game's key.h, not read back from the table under test.
 */
BOOST_AUTO_TEST_CASE(keycodes_reach_the_game_key_codes_sdl2_gave)
{
	struct pair
	{
		SDL_Keycode key;
		unsigned game;
	};
	const std::array pairs{
	    pair{SDLK_A, KEY_A},
	    pair{SDLK_Z, KEY_Z},
	    pair{SDLK_0, KEY_0},
	    pair{SDLK_1, KEY_1},
	    pair{SDLK_RETURN, KEY_ENTER},
	    pair{SDLK_ESCAPE, KEY_ESC},
	    pair{SDLK_SPACE, KEY_SPACEBAR},
	    pair{SDLK_BACKSPACE, KEY_BACKSP},
	    pair{SDLK_TAB, KEY_TAB},
	    pair{SDLK_F1, KEY_F1},
	    pair{SDLK_F12, KEY_F12},
	    pair{SDLK_UP, KEY_UP},
	    pair{SDLK_LEFT, KEY_LEFT},
	    pair{SDLK_HOME, KEY_HOME},
	    pair{SDLK_DELETE, KEY_DELETE},
	    pair{SDLK_KP_ENTER, KEY_PADENTER},
	    pair{SDLK_KP_0, KEY_PAD0},
	    pair{SDLK_KP_PLUS, KEY_PADPLUS},
	    pair{SDLK_MINUS, KEY_MINUS},
	    pair{SDLK_EQUALS, KEY_EQUAL},
	    pair{SDLK_PAUSE, KEY_PAUSE},
	};
	for (const auto &p : pairs)
		BOOST_TEST((press(p.key) & 0xffu) == p.game, "keycode " << p.key);
}

#ifdef __APPLE__
/* Test that the Command keys are modifiers on SDL3 on macOS: SDLK_LGUI and SDLK_RGUI are the Cmd
 * keys, and SDL3's own SDLK_LMETA, a key a Mac never sends, is not.
 */
BOOST_AUTO_TEST_CASE(command_keys_are_the_gui_keys)
{
	BOOST_TEST((press(SDLK_LGUI) & 0xffu) == KEY_LMETA);
	BOOST_TEST((press(SDLK_RGUI) & 0xffu) == KEY_RMETA);
	BOOST_TEST(press(SDLK_LMETA) == 0u);
	const auto cmd{key_event(SDLK_LGUI, true)};
	dcx::key_handler(&cmd);
	sent_other_events = 0;
	BOOST_TEST(press(SDLK_Q) == static_cast<unsigned>(KEY_Q + KEY_COMMAND));
	/* Cmd-Q in play shows the game's quit dialog, which SDL_EVENT_QUIT from the app menu asks for
	 * (owner decision, ADR 7): the key code never makes a quit event of its own.
	 */
	BOOST_TEST(sent_other_events == 0u);
	const auto up{key_event(SDLK_LGUI, false)};
	dcx::key_handler(&up);
	BOOST_TEST(press(SDLK_Q) == KEY_Q);
}
#else
/* Test that the GUI keys, the Windows and Super keys, reach no game key off macOS, as on SDL2:
 * Super+P must not pause the game as Cmd+P does (ADR 7).
 */
BOOST_AUTO_TEST_CASE(gui_keys_are_no_game_key)
{
	BOOST_TEST(press(SDLK_LGUI) == 0u);
	BOOST_TEST(press(SDLK_RGUI) == 0u);
	BOOST_TEST(press(SDLK_LMETA) == 0u);
	const auto super{key_event(SDLK_LGUI, true)};
	dcx::key_handler(&super);
	BOOST_TEST(press(SDLK_P) == KEY_P);
	const auto up{key_event(SDLK_LGUI, false)};
	dcx::key_handler(&up);
}
#endif

/* Test that Shift, Alt and Ctrl still modify the key that follows and stop when released. */
BOOST_AUTO_TEST_CASE(shift_alt_and_ctrl_modify_the_next_key)
{
	const std::array<std::pair<SDL_Keycode, unsigned>, 3> mods{
	    {{SDLK_LSHIFT, KEY_SHIFTED}, {SDLK_RALT, KEY_ALTED}, {SDLK_LCTRL, KEY_CTRLED}}};
	for (const auto &[key, flag] : mods)
	{
		const auto d{key_event(key, true)};
		dcx::key_handler(&d);
		BOOST_TEST(press(SDLK_A) == static_cast<unsigned>(KEY_A + flag));
		const auto u{key_event(key, false)};
		dcx::key_handler(&u);
		BOOST_TEST(press(SDLK_A) == KEY_A);
	}
}

/* Test that key_flush keeps the lock keys' state under the game's key codes: Caps Lock (scancode
 * 57), Scroll Lock (71) and Num Lock (83) must not land on the game keys with those numbers.
 */
BOOST_AUTO_TEST_CASE(key_flush_restores_the_lock_keys_by_game_key_code)
{
	fake_keyboard = {};
	fake_keyboard[SDL_SCANCODE_CAPSLOCK] = true;
	fake_keyboard[SDL_SCANCODE_SCROLLLOCK] = true;
	fake_keyboard[SDL_SCANCODE_NUMLOCKCLEAR] = true;
	dcx::CGameArg.CtlNoStickyKeys = false;
	dcx::key_flush();
	BOOST_TEST(dcx::keyd_pressed[KEY_CAPSLOCK]);
	BOOST_TEST(dcx::keyd_pressed[KEY_SCROLLOCK]);
	BOOST_TEST(dcx::keyd_pressed[KEY_NUMLOCK]);
	BOOST_TEST(!dcx::keyd_pressed[KEY_SPACEBAR]);
	BOOST_TEST(!dcx::keyd_pressed[KEY_PAD7]);
	BOOST_TEST(!dcx::keyd_pressed[KEY_PADPERIOD]);
	fake_keyboard = {};
	dcx::key_flush();
	BOOST_TEST(!dcx::keyd_pressed[KEY_CAPSLOCK]);
}

namespace {

/* A hidden window on SDL's dummy video driver, which text input needs. */
struct dummy_window
{
	dcx::sdl::sdl_unique_ptr<SDL_Window, SDL_DestroyWindow> window;
	dummy_window()
	{
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
		BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO));
		window.reset(SDL_CreateWindow("key test", 64, 64, SDL_WINDOW_HIDDEN));
		BOOST_REQUIRE(window);
		dcx::g_pRebirthSDLMainWindow = window.get();
	}
	dummy_window(const dummy_window &) = delete;
	dummy_window &operator=(const dummy_window &) = delete;
	dummy_window(dummy_window &&) = delete;
	dummy_window &operator=(dummy_window &&) = delete;
	~dummy_window()
	{
		dcx::key_text_input_close();
		dcx::g_pRebirthSDLMainWindow = nullptr;
		window.reset();
		SDL_Quit();
	}
};

SDL_Event text_event(const char *const text)
{
	SDL_Event e{};
	e.text.type = SDL_EVENT_TEXT_INPUT;
	e.text.text = text;
	return e;
}

/* Feed one key down and the events SDL sends after it, as event_poll does: the key's text first,
 * then the key.  Returns the text events the key took.
 */
unsigned key_down_with_text(const SDL_Keycode key, const std::vector<SDL_Event> &after,
                            const bool repeat = false)
{
	SDL_Event down{};
	down.key = key_event(key, true);
	down.key.repeat = repeat;
	const auto taken{dcx::key_take_text(down.key, after)};
	dcx::key_handler(&down.key);
	const auto up{key_event(key, false)};
	dcx::key_handler(&up);
	return taken;
}

}

/* Test that while text input is open the key's text, shifted, is what key_ascii returns, once, and
 * that the key code is still sent.
 */
BOOST_AUTO_TEST_CASE(open_text_input_gives_shifted_text_from_the_text_event)
{
	const dummy_window window;
	dcx::key_flush();
	dcx::key_text_input_open();
	BOOST_TEST(SDL_TextInputActive(dcx::g_pRebirthSDLMainWindow));
	sent_keys.clear();
	BOOST_TEST(key_down_with_text(SDLK_A, {text_event("A")}) == 1u);
	BOOST_TEST(!sent_keys.empty());
	BOOST_TEST(dcx::key_ascii() == 'A');
	BOOST_TEST(dcx::key_ascii() == 255);
	dcx::key_text_input_close();
	BOOST_TEST(!SDL_TextInputActive(dcx::g_pRebirthSDLMainWindow));
}

/* Test that closed text input leaves key_ascii as on SDL2: the unshifted keycode, and a text event
 * with nothing to follow is not taken.
 */
BOOST_AUTO_TEST_CASE(closed_text_input_gives_the_keycode_as_on_sdl2)
{
	const dummy_window window;
	dcx::key_flush();
	BOOST_TEST(key_down_with_text(SDLK_A, {text_event("A")}) == 0u);
	BOOST_TEST(dcx::key_ascii() == 'a');
	BOOST_TEST(dcx::key_ascii() == 255);
}

/* Test that the text of a repeat the game drops is dropped with it, so it does not come back
 * as a ghost character with the next key.
 */
BOOST_AUTO_TEST_CASE(text_of_a_dropped_repeat_is_dropped)
{
	const dummy_window window;
	dcx::key_flush();
	dcx::key_toggle_repeat0();
	dcx::key_text_input_open();
	BOOST_TEST(key_down_with_text(SDLK_A, {text_event("a")}, true) == 1u);
	BOOST_TEST(dcx::key_ascii() == 255);
	dcx::key_toggle_repeat1();
	BOOST_TEST(key_down_with_text(SDLK_A, {text_event("a")}, true) == 1u);
	BOOST_TEST(dcx::key_ascii() == 'a');
	dcx::key_text_input_close();
}

/* Test that a text event that arrives alone sends a key command for no key, with its text in the
 * buffer the handler reads.
 */
BOOST_AUTO_TEST_CASE(a_lone_text_event_is_a_key_command_with_its_text)
{
	const dummy_window window;
	dcx::key_flush();
	dcx::key_text_input_open();
	sent_keys.clear();
	const auto e{text_event("Z")};
	BOOST_TEST((dcx::key_text_command(e.text) == window_event_result::handled));
	const std::vector<unsigned> expected{0};
	BOOST_TEST(sent_keys == expected, boost::test_tools::per_element());
	BOOST_TEST(dcx::key_ascii() == 'Z');
	dcx::key_text_input_close();
	sent_keys.clear();
	BOOST_TEST((dcx::key_text_command(e.text) == window_event_result::ignored));
	BOOST_TEST(sent_keys.empty());
}

/* Test that text input stays closed, and key events keep filling the buffer, when there is no
 * window to start it on.
 */
BOOST_AUTO_TEST_CASE(no_window_leaves_text_input_closed)
{
	dcx::key_flush();
	dcx::key_text_input_open();
	BOOST_TEST(!dcx::key_text_input_active());
	BOOST_TEST(key_down_with_text(SDLK_B, {}) == 0u);
	BOOST_TEST(dcx::key_ascii() == 'b');
}

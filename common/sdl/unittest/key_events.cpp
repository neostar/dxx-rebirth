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
std::array<bool, SDL_SCANCODE_COUNT> fake_keyboard{};

}

namespace dcx {

CArg CGameArg;

fix64 timer_query()
{
	return 0;
}

window_event_result event_send(const d_event &event)
{
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

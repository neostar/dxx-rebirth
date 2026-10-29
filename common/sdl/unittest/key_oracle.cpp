/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* The SDL2 oracle for the key table (#42, R12).  One program for both SDL versions: it presses
 * the key SDL's default keymap puts on every scancode and records the game key code and key_ascii
 * each gives.  Built with SDL2 (test-key-oracle-sdl2, in the reference container: Linux,
 * SDL 2.30.0) with DXX_KEY_TABLE_PRINT=1, it prints the rows that key_oracle_sdl2.inc holds; built
 * with SDL3 (test-key-oracle-sdl3) it compares its own rows with that file.  The seam is
 * key_handler, which takes the SDL event; event_send is a recorder.
 */

/* dxxsconf.h first: it defines the feature macros that the C library headers SDL.h pulls in would
 * define differently.
 */
// clang-format off
#include "dxxsconf.h"
// clang-format on
#include <SDL.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
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
	if (event.type == event_type::key_command)
		sent_keys.push_back(static_cast<unsigned>(event_key_get(event)));
	return window_event_result::handled;
}

#undef con_printf
void con_printf(const con_priority_wrapper, const char *, ...)
{}

}

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth key oracle
#include <boost/test/unit_test.hpp>

namespace {

/* What one key of SDL's default keymap does in the game: the scancode, the game key code the key
 * handler sent (without modifiers) and what key_ascii returned, 255 for none.
 */
struct key_row
{
	unsigned scancode;
	unsigned game;
	unsigned ascii;
	bool operator==(const key_row &) const = default;
	friend std::ostream &operator<<(std::ostream &o, const key_row &r)
	{
		return o << "key_row{" << r.scancode << ", " << r.game << ", " << r.ascii << "}";
	}
};

#if SDL_MAJOR_VERSION == 3
constexpr int scancode_count{SDL_SCANCODE_COUNT};
#else
constexpr int scancode_count{SDL_NUM_SCANCODES};
#endif

SDL_KeyboardEvent key_event(const SDL_Keycode key, const bool down)
{
	SDL_KeyboardEvent e{};
#if SDL_MAJOR_VERSION == 3
	e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	e.key = key;
	e.down = down;
#else
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.keysym.sym = key;
#endif
	return e;
}

/* The game key code the handler sent for one press, or 0 when it sent none. */
unsigned press(const SDL_Keycode key)
{
	sent_keys.clear();
	const auto e{key_event(key, true)};
	::dcx::key_handler(&e);
	const auto code{sent_keys.empty() ? 0u : sent_keys.back()};
	const auto r{key_event(key, false)};
	::dcx::key_handler(&r);
	return code;
}

/* SDL's video subsystem on its dummy driver, which has the default keymap. */
struct sdl_video
{
	sdl_video()
	{
#if SDL_MAJOR_VERSION == 3
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
		BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO));
#else
		SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
		BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
#endif
	}
	sdl_video(const sdl_video &) = delete;
	sdl_video &operator=(const sdl_video &) = delete;
	sdl_video(sdl_video &&) = delete;
	sdl_video &operator=(sdl_video &&) = delete;
	~sdl_video()
	{
		SDL_Quit();
	}
};

/* Press the key SDL's default keymap puts on each scancode, text input closed. */
std::vector<key_row> key_table()
{
	const sdl_video video;
	std::vector<key_row> rows;
	for (int sc{1}; sc < scancode_count; ++sc)
	{
#if SDL_MAJOR_VERSION == 3
		const SDL_Keycode key{
		    SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE, false)};
#else
		const SDL_Keycode key{SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc))};
#endif
		if (key == SDLK_UNKNOWN)
			continue;
		const unsigned game{press(key) & 0xffu};
		rows.push_back({static_cast<unsigned>(sc), game, ::dcx::key_ascii()});
	}
	return rows;
}

}

/* Print the table as key_oracle_sdl2.inc holds it, when DXX_KEY_TABLE_PRINT is set. */
BOOST_AUTO_TEST_CASE(print_the_key_table)
{
	const auto rows{key_table()};
	BOOST_TEST(!rows.empty());
	if (std::getenv("DXX_KEY_TABLE_PRINT"))
		for (const auto &r : rows)
			std::cout << "KEY_ROW " << r << ",\n";
}

#if SDL_MAJOR_VERSION == 3
namespace {

/* What SDL2's default keymap did in the game, row by row: the SDL2 build's output
 * (DXX_KEY_TABLE_PRINT=1 test-key-oracle-sdl2), made in the reference container on Linux with
 * SDL 2.30.0.  No licensed data.
 */
const std::array sdl2_table{
#include "common/sdl/unittest/key_oracle_sdl2.inc"
};

}

/* Test that every key of SDL3's default keymap reaches the game as SDL2's did, the game key code
 * and key_ascii alike, bar the rows that differ on purpose:
 *  - scancode 165 (SDL_SCANCODE_FRONT, a Sun keyboard's Front key) has a keycode on SDL3 and none
 *    on SDL2; the game maps neither, so the row only appears.
 *  - the GUI keys, scancodes 227 and 231, are Cmd on macOS and map to LCMD and RCMD there (ADR 7);
 *    SDL2 mapped them nowhere, and so does SDL3 off macOS.
 */
BOOST_AUTO_TEST_CASE(key_table_matches_what_sdl2_gave)
{
	std::map<unsigned, key_row> expected;
	for (const auto &r : sdl2_table)
		expected.emplace(r.scancode, r);
	expected.emplace(165u, key_row{165, 0, 255});
#ifdef __APPLE__
	expected.at(227u).game = KEY_LMETA;
	expected.at(231u).game = KEY_RMETA;
#endif
	const auto rows{key_table()};
	BOOST_TEST(rows.size() == expected.size());
	for (const auto &r : rows)
	{
		const auto e{expected.find(r.scancode)};
		BOOST_REQUIRE_MESSAGE(e != expected.end(),
		                      "scancode " << r.scancode << " has a key only on SDL3");
		BOOST_TEST(r == e->second);
	}
}
#endif

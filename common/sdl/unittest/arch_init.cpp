/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* arch_init turns the real joystick backends off before SDL_Init in a hidden run (#44).  The
 * seam is the game's own similar/arch/sdl/init.cpp, linked as the game compiles it, with the
 * subsystems it starts after SDL (key, sound, mouse, joystick, graphics) stubbed below.  The
 * test hooks the three backend hints, and a hook runs when SDL sets the hint: it records which
 * SDL subsystems were up then.  SDL_Init(SDL_INIT_VIDEO), through SDL's dummy video driver,
 * is what brings the video subsystem up; a hint set after it sees SDL_INIT_VIDEO in the record.
 * A build without dbg_harness=1 has no call to test, and the test passes with a message.
 */

// clang-format off
#include "dxxsconf.h"
// clang-format on
#include <SDL.h>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "args.h"
#include "console.h"
#include "digi.h"
#include "dxxerror.h"
#include "gr.h"
#include "joy.h"
#include "key.h"
#include "mouse.h"
#include "songs.h"
#include "text.h"
#include "tools/dev/upstream_end.h"
// clang-format on

#include "common/debug/hidden_window.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth arch init
#include <boost/test/unit_test.hpp>

#if DXX_USE_DBG_HARNESS

namespace {

struct hint_set
{
	std::string name;
	/* SDL_WasInit(0): the subsystems up when SDL set the hint. */
	std::uint32_t subsystems_up;
};

std::vector<hint_set> hints_set;

void on_hint(void *, const char *const name, const char *, const char *const value)
{
	/* SDL calls a new hook once with the current value; only a set to "0" counts. */
	if (value && std::string_view{value} == "0")
		hints_set.push_back({name, static_cast<std::uint32_t>(SDL_WasInit(0))});
}

void hook_real_joystick_hints()
{
	hints_set.clear();
	for (const char *const name : dcx::harness_real_joystick_hints)
		SDL_AddHintCallback(name, on_hint, nullptr);
}

void unhook_real_joystick_hints()
{
	for (const char *const name : dcx::harness_real_joystick_hints)
		SDL_DelHintCallback(name, on_hint, nullptr);
}

/* A game with no sound, mouse or joystick, and SDL's dummy video driver: no window opens. */
void start_arch_init()
{
	setenv("SDL_VIDEODRIVER", "dummy", 1);
	/* run.sh turns the backends off in the environment; the harness's own call is under test. */
	for (const char *const name : dcx::harness_real_joystick_hints)
		unsetenv(name);
	dcx::CGameArg.SndNoSound = true;
	dcx::CGameArg.CtlNoMouse = true;
	dcx::CGameArg.CtlNoJoystick = true;
}

/* Runs arch_init with DXX_DBG_HIDDEN set to hidden (or unset when null), hooks in place. */
void run_arch_init(const char *const hidden)
{
	if (hidden)
		setenv("DXX_DBG_HIDDEN", hidden, 1);
	else
		unsetenv("DXX_DBG_HIDDEN");
	start_arch_init();
	hook_real_joystick_hints();
	{
		const auto atexit{dsx::arch_init()};
	}
	unhook_real_joystick_hints();
	unsetenv("DXX_DBG_HIDDEN");
}

}

BOOST_AUTO_TEST_CASE(a_shown_run_leaves_the_real_joystick_backends_on)
{
	run_arch_init(nullptr);
	BOOST_TEST(hints_set.empty());
}

BOOST_AUTO_TEST_CASE(a_hidden_run_turns_them_off_before_sdl_init)
{
	run_arch_init("1");
	BOOST_REQUIRE_EQUAL(hints_set.size(), dcx::harness_real_joystick_hints.size());
	for (const auto &h : hints_set)
		BOOST_TEST((h.subsystems_up & SDL_INIT_VIDEO) == 0u, h.name << " set after SDL_Init");
}

#else

BOOST_AUTO_TEST_CASE(only_a_debug_harness_build_has_the_call)
{
	BOOST_TEST_MESSAGE("build with dbg_harness=1 to test arch_init's call");
}

#endif

/* The rest of the game, which init.cpp names.  A function arch_init reaches without a sound,
 * mouse or joystick throws, which fails the test. */
namespace {

[[noreturn]]
void unreached(const char *const what)
{
	throw std::logic_error{std::string{"arch_init: unreached game code called: "} + what};
}

}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wsuggest-attribute=noreturn"
#endif

namespace dcx {

CArg CGameArg;

void(con_printf)(const con_priority_wrapper, const char *, ...)
{}

void(Error)(const char *, unsigned, const char *, const char *fmt, ...)
{
	unreached(fmt);
}

void(Error_puts)(const char *, unsigned, const char *, const char *str)
{
	unreached(str);
}

void key_init()
{}

void mouse_init()
{
	unreached("mouse_init");
}

void mouse_close()
{
	unreached("mouse_close");
}

void joy_init()
{
	unreached("joy_init");
}

void joy_close()
{
	unreached("joy_close");
}

void gamecontroller_init()
{
	unreached("gamecontroller_init");
}

void gamecontroller_close()
{
	unreached("gamecontroller_close");
}

}

void songs_uninit()
{}

namespace dsx {

// NOLINTNEXTLINE(readability-identifier-naming): the game's name
std::array<const char *, N_TEXT_STRINGS> Text_string;

void digi_select_system()
{}

void gr_close()
{}

int gr_init()
{
	return 0;
}

void digi_close()
{
	unreached("digi_close");
}

int digi_init()
{
	unreached("digi_init");
}

}

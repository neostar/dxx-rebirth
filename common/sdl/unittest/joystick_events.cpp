/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* Virtual joysticks through the game's joystick and gamepad handlers (#44, R12).  One program for
 * SDL2 and SDL3, as key_oracle.cpp is.  The seam is SDL's virtual-joystick driver: the test
 * attaches virtual gamepads and flight sticks (common/sdl/virtual_joystick.h), moves their
 * inputs, and calls event_poll, which pumps SDL and hands the events to the game's own handlers
 * (joy.cpp, gamecontroller.cpp); standard_handler, the game's last-resort handler, records what
 * reaches the game.  With no window open, every event reaches it.
 *
 * A saved binding is the number of a button or axis the game reports, so what each control
 * reports, and the label the controls menu shows for it, is what a pilot file's bindings mean.
 * Built with SDL2 (test-joystick-sdl2, in the reference container: Linux, SDL 2.30.0) with
 * DXX_JOYSTICK_ORACLE_PRINT=1, the program prints the rows joystick_oracle_sdl2.inc holds; every
 * build compares its own rows with that file, so a binding an SDL2 build saved means the same
 * control in the SDL3 build.
 */

/* dxxsconf.h first: it defines the feature macros that the C library headers SDL.h pulls in would
 * define differently.
 */
// clang-format off
#include "dxxsconf.h"
// clang-format on
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/debug/input_script.h"
#include "common/sdl/gamepad_layout.h"
#include "common/sdl/virtual_joystick.h"

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "args.h"
#include "config.h"
#include "console.h"
#include "dxxerror.h"
#include "event.h"
#include "joy.h"
#include "kconfig.h"
#include "key.h"
#include "mouse.h"
#include "physfsrwops.h"
#include "timer.h"
#include "window.h"
#include "cmd.h"
#include "tools/dev/upstream_end.h"
// clang-format on

#if SDL_MAJOR_VERSION == 2
static_assert(SDL_CONTROLLER_BUTTON_MAX == ::dcx::sdl::gamepad_button_count,
              "saved gamepad bindings start the axis-buttons at SDL2's button count");
static_assert(SDL_CONTROLLER_AXIS_MAX == ::dcx::sdl::gamepad_axis_count);
#elif SDL_MAJOR_VERSION == 3
static_assert(SDL_GAMEPAD_BUTTON_COUNT ==
                  ::dcx::sdl::gamepad_button_count + ::dcx::sdl::gamepad_extra_button_count,
              "SDL3's buttons past SDL2's 21 take the numbers after the axis-buttons");
static_assert(SDL_GAMEPAD_AXIS_COUNT == ::dcx::sdl::gamepad_axis_count);
#endif

namespace {

/* What reached the game's last-resort handler. */
struct seen
{
	event_type type;
	/* A joystick button's number or a key's code; a joystick axis's number. */
	unsigned number;
	int value;
};
std::vector<seen> seen_events;

/* The text of the gamecontrollerdb.txt the game finds through PhysicsFS; none when empty. */
std::string controller_db;

}

/* The game's PhysicsFS is not started: the test hands gamecontroller_init its controller
 * database, if it set one.
 */
std::pair<RWops_ptr, PHYSFS_ErrorCode> PHYSFSRWOPS_openRead(const char *const name)
{
	if (controller_db.empty() || std::string_view{name} != "gamecontrollerdb.txt")
		return {nullptr, PHYSFS_ERR_NOT_FOUND};
#if SDL_MAJOR_VERSION == 2
	return {
	    RWops_ptr{SDL_RWFromConstMem(controller_db.data(), static_cast<int>(controller_db.size()))},
	    PHYSFS_ERR_OK};
#elif SDL_MAJOR_VERSION == 3
	return {RWops_ptr{SDL_IOFromConstMem(controller_db.data(), controller_db.size())},
	        PHYSFS_ERR_OK};
#endif
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

/* The controls menu's labels, which kconfig.cpp defines. */
joybutton_text_t joybutton_text;
joyaxis_text_t joyaxis_text;

fix64 timer_query()
{
	return 0;
}

fix64 timer_update()
{
	return 0;
}

#undef UserError
void UserError(const char *, ...)
{
	std::abort();
}

#undef Warning
void Warning(const char *, ...)
{}

#undef con_printf
void con_printf(const con_priority_wrapper, const char *, ...)
{}

void con_puts(const con_priority_wrapper, std::span<const char>)
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

#if SDL_MAJOR_VERSION == 3
void gr_handle_window_event(const SDL_Event &)
{}
#endif

void gr_flip()
{}

window_event_result standard_handler(const d_event &event)
{
	switch (event.type)
	{
	case event_type::joystick_button_down:
	case event_type::joystick_button_up:
		seen_events.push_back({.type = event.type,
		                       .number = static_cast<unsigned>(event_joystick_get_button(event)),
		                       .value = 0});
		break;
	case event_type::joystick_moved: {
		const auto &a{event_joystick_get_axis(event)};
		seen_events.push_back({.type = event.type, .number = a.axis, .value = a.value});
		break;
	}
	case event_type::key_command:
		seen_events.push_back({.type = event.type,
		                       .number = static_cast<unsigned>(event_key_get(event)),
		                       .value = 0});
		break;
	default:
		break;
	}
	return window_event_result::handled;
}

}

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth joystick events
#include <boost/test/unit_test.hpp>

namespace {

using ::dcx::sdl::virtual_joystick;
using ::dcx::sdl::virtual_joystick_shape;

/* One game event a control of a scene produced: the scene, the control moved ("d1 b3 down": device
 * 1, button 3), the game's event, its number and value, and the label the controls menu shows for
 * the number (kconfig.cpp's get_item_text: the gamepad's while a gamepad is open).
 */
struct oracle_row
{
	std::string_view scene;
	std::string_view control;
	std::string_view event;
	unsigned number;
	int value;
	std::string_view label;
};

constexpr auto sdl2_rows{std::to_array<oracle_row>({
#include "common/sdl/unittest/joystick_oracle_sdl2.inc"
})};

struct recorded_row
{
	std::string scene;
	std::string control;
	std::string event;
	unsigned number;
	int value;
	std::string label;
	bool operator==(const recorded_row &) const = default;
};

std::ostream &operator<<(std::ostream &os, const recorded_row &r)
{
	return os << r.scene << " | " << r.control << " | " << r.event << ' ' << r.number << ' '
	          << r.value << " | " << r.label;
}

/* An SDL assertion ends the test run with a failure, never with SDL's dialog. */
SDL_AssertState SDLCALL fail_on_assertion(const SDL_AssertData *const data, void *)
{
	std::cerr << "SDL assertion failed: " << data->condition << " (" << data->filename << ':'
	          << data->linenum << ")\n"
	          << std::flush;
	std::_Exit(3);
}

/* SDL with its joystick and gamepad subsystems on the dummy video driver, joystick events
 * delivered in the background, and the game's joystick and gamepad state started as arch_init
 * does, once the devices `attach` makes are plugged in.
 */
struct sdl_joysticks
{
	sdl_joysticks()
	{
		SDL_SetAssertionHandler(fail_on_assertion, nullptr);
#if SDL_MAJOR_VERSION == 2
		SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
#elif SDL_MAJOR_VERSION == 3
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
#endif
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#if SDL_MAJOR_VERSION == 2
		BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0);
#elif SDL_MAJOR_VERSION == 3
		BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
#endif
		seen_events.clear();
		controller_db.clear();
	}
	/* Start the game's joystick and gamepad code, as arch_init does, and take the events of the
	 * devices plugged in at start.
	 */
	static void start_game()
	{
		::dcx::joy_init();
		::dcx::gamecontroller_init();
		::dcx::event_poll();
	}
	sdl_joysticks(const sdl_joysticks &) = delete;
	sdl_joysticks &operator=(const sdl_joysticks &) = delete;
	sdl_joysticks(sdl_joysticks &&) = delete;
	sdl_joysticks &operator=(sdl_joysticks &&) = delete;
	~sdl_joysticks()
	{
		::dcx::gamecontroller_close();
		::dcx::joy_close();
		SDL_Quit();
	}
};

constexpr virtual_joystick_shape gamepad_shape{
    ::dcx::sdl::virtual_shape_of(::dcx::input_device_shape::gamepad)};
constexpr virtual_joystick_shape stick_shape{
    ::dcx::sdl::virtual_shape_of(::dcx::input_device_shape::stick)};
/* A second, smaller stick, so that two sticks' order shows in what each reports. */
constexpr virtual_joystick_shape small_stick_shape{
    .gamepad = false, .axes = 2, .buttons = 8, .hats = 0, .name = "DXX small stick"};

/* A label as a C string literal's body, so that the rows print as C++. */
std::string escaped(const std::string_view s)
{
	std::string out;
	for (const char c : s)
	{
		const auto u{static_cast<unsigned char>(c)};
		if (u < 32 || u > 126 || c == '"' || c == '\\')
		{
			std::array<char, 5> octal{};
			std::snprintf(octal.data(), octal.size(), "\\%03o", u);
			out += octal.data();
		}
		else
			out += c;
	}
	return out;
}

std::string label_of(const seen &e)
{
	const bool pad{::dcx::num_controllers != 0};
	if (e.type == event_type::joystick_moved)
	{
		if (pad)
			return e.number < ::dcx::gamecontroller_axis_text.size()
			           ? std::string{::dcx::gamecontroller_axis_text[e.number].data()}
					   : std::string{"none"};
		return e.number < ::dcx::joyaxis_text.size()
		           ? std::string{::dcx::joyaxis_text[e.number].data()}
				   : std::string{"none"};
	}
	if (pad)
		return e.number < ::dcx::gcbutton_text.size()
		           ? std::string{::dcx::gcbutton_text[e.number].data()}
				   : std::string{"none"};
	return e.number < ::dcx::joybutton_text.size()
	           ? std::string{::dcx::joybutton_text[e.number].data()}
			   : std::string{"none"};
}

std::string_view event_name(const event_type t)
{
	switch (t)
	{
	case event_type::joystick_button_down:
		return "down";
	case event_type::joystick_button_up:
		return "up";
	case event_type::joystick_moved:
		return "moved";
	case event_type::key_command:
		return "key";
	default:
		return "other";
	}
}

/* Runs a scene: records each game event a step's event_poll produced. */
class scene_recorder
{
	std::string scene;

  public:
	std::vector<recorded_row> rows;
	explicit scene_recorder(std::string name) : scene{std::move(name)}
	{}
	void step(const std::string &control, const std::function<void()> &move)
	{
		seen_events.clear();
		move();
		::dcx::event_poll();
		for (const auto &e : seen_events)
			rows.push_back(
			    {.scene = scene,
				 .control = control,
				 .event = std::string{event_name(e.type)},
				 .number = e.number,
				 .value = e.value,
				 .label = e.type == event_type::key_command ? std::string{} : label_of(e)});
	}
	/* Every control of `j`, device `d` of the scene: each button down and up, each hat around its
	 * four directions, each axis to its ends and back.
	 */
	void sweep(const unsigned d, virtual_joystick &j, const virtual_joystick_shape &shape)
	{
		const auto name{[d](const char kind, const unsigned n, const std::string_view what) {
			return 'd' + std::to_string(d) + ' ' + kind + std::to_string(n) + ' ' +
			       std::string{what};
		}};
		for (unsigned b{0}; b < shape.buttons; ++b)
		{
			step(name('b', b, "down"),
			     [&] { BOOST_TEST(j.set_button(static_cast<int>(b), true)); });
			step(name('b', b, "up"), [&] { BOOST_TEST(j.set_button(static_cast<int>(b), false)); });
		}
		for (unsigned h{0}; h < shape.hats; ++h)
			for (const auto &[what, bits] :
			     {std::pair{"up", SDL_HAT_UP}, std::pair{"right", SDL_HAT_RIGHT},
			      std::pair{"down", SDL_HAT_DOWN}, std::pair{"left", SDL_HAT_LEFT},
			      std::pair{"centered", SDL_HAT_CENTERED}})
				step(name('h', h, what),
				     [&] { BOOST_TEST(j.set_hat(static_cast<int>(h), static_cast<Uint8>(bits))); });
		for (unsigned a{0}; a < shape.axes; ++a)
			for (const auto &[what, value] : {std::pair{"max", 32767}, std::pair{"rest", 0},
			                                  std::pair{"min", -32768}, std::pair{"rest", 0}})
				step(name('a', a, what), [&] {
					BOOST_TEST(j.set_axis(static_cast<int>(a), static_cast<Sint16>(value)));
				});
	}
};

/* A scene's devices, attached in order before the game starts. */
std::vector<recorded_row> sweep_scene(const std::string &name,
                                      const std::vector<virtual_joystick_shape> &shapes)
{
	const sdl_joysticks sdl;
	std::vector<virtual_joystick> devices;
	for (const auto &shape : shapes)
	{
		auto j{virtual_joystick::attach(shape)};
		BOOST_REQUIRE_MESSAGE(j.has_value(), "attach: " << SDL_GetError());
		devices.push_back(std::move(j).value());
	}
	scene_recorder recorder{name};
	recorder.step("start", sdl_joysticks::start_game);
	recorder.rows.push_back({.scene = name,
	                         .control = "start",
	                         .event = "counts",
	                         .number = static_cast<unsigned>(::dcx::num_controllers),
	                         .value = static_cast<int>(::dcx::joybutton_text.size()),
	                         .label = std::to_string(::dcx::joyaxis_text.size())});
	for (std::size_t d{0}; d < devices.size(); ++d)
		recorder.sweep(static_cast<unsigned>(d), devices[d], shapes[d]);
	return recorder.rows;
}

std::vector<recorded_row> all_scenes()
{
	std::vector<recorded_row> rows;
	const auto add{
	    [&rows](std::vector<recorded_row> r) { rows.insert(rows.end(), r.begin(), r.end()); }};
	add(sweep_scene("pad", {gamepad_shape}));
	add(sweep_scene("stick", {stick_shape}));
	add(sweep_scene("no device", {}));
	add(sweep_scene("two sticks", {stick_shape, small_stick_shape}));
	add(sweep_scene("two sticks swapped", {small_stick_shape, stick_shape}));
	add(sweep_scene("pad and stick", {gamepad_shape, stick_shape}));
	return rows;
}

}

/* Test that every control of a virtual gamepad and of virtual flight sticks reaches the game as
 * the number, and with the controls menu's label, that the SDL2 build gives it, in each device
 * shape a pilot's bindings meet at load: a pad alone (the SDL2 axis-button numbers, +RT at 31),
 * a stick alone, the bound stick unplugged, two sticks and the same two swapped (the numbers
 * follow the order the sticks were plugged in, not the stick), a pad plus a stick.  A pilot file
 * saves these numbers, so the rows are its bindings' meaning.
 */
BOOST_AUTO_TEST_CASE(controls_report_what_the_sdl2_build_reports)
{
	auto rows{all_scenes()};
	if (const char *const print{std::getenv("DXX_JOYSTICK_ORACLE_PRINT")};
	    print && std::string_view{print} == "1")
	{
		for (const auto &r : rows)
			std::cout << "oracle_row{\"" << r.scene << "\", \"" << r.control << "\", \"" << r.event
			          << "\", " << r.number << ", " << r.value << ", \"" << escaped(r.label)
			          << "\"},\n";
		return;
	}
	/* The rows SDL3 (and sdl2-compat, the Mac's SDL2) adds or changes against SDL 2.30, none of
	 * which moves a saved number: SDL 2.30's virtual driver maps a virtual gamepad's first 20
	 * buttons only, so the oracle has no rows for button 20, TOUCHPAD, and it does not report the
	 * Guide button's release, button 5 up; SDL3 reports both, as SDL2's numbers 20 and 5.  With a
	 * gamepad open the controls menu shows the gamepad's labels, so a stick's numbers 33-37 show
	 * SDL3's MISC2 to MISC6 ("M2" to "M6"), which SDL2's 33 labels do not reach.
	 */
	std::erase_if(rows, [](const recorded_row &r) {
		const bool pad_scene{r.scene == "pad" || r.scene == "pad and stick"};
		return pad_scene && ((r.control.starts_with("d0 b20 ") && r.number == 20) ||
		                     (r.control == "d0 b5 up" && r.number == 5));
	});
	for (auto &r : rows)
		if (r.number >= ::dcx::sdl::gamepad_extra_button_start &&
		    r.number < ::dcx::sdl::gamepad_virtual_button_count && r.event != "moved" &&
		    r.label.starts_with('M'))
			r.label = "none";
	std::vector<recorded_row> want;
	for (const auto &r : sdl2_rows)
		want.push_back({.scene = std::string{r.scene},
		                .control = std::string{r.control},
		                .event = std::string{r.event},
		                .number = r.number,
		                .value = r.value,
		                .label = std::string{r.label}});
	BOOST_TEST(rows == want, boost::test_tools::per_element());
}

/* Test that a stick plugged in after a gamepad is labelled as the second device, "J2", in the
 * controls menu's stick labels: SDL2's device indices count gamepads, and so does the SDL3 build.
 * The oracle cannot show it, since with a gamepad open the menu shows the gamepad's labels.
 */
BOOST_AUTO_TEST_CASE(a_stick_after_a_gamepad_is_labelled_as_the_second_device)
{
	const sdl_joysticks sdl;
	const auto pad{virtual_joystick::attach(gamepad_shape)};
	BOOST_REQUIRE_MESSAGE(pad.has_value(), "attach: " << SDL_GetError());
	const auto stick{virtual_joystick::attach(stick_shape)};
	BOOST_REQUIRE_MESSAGE(stick.has_value(), "attach: " << SDL_GetError());
	sdl_joysticks::start_game();
	::dcx::event_poll();
	BOOST_REQUIRE(::dcx::joybutton_text.size() != 0);
	BOOST_TEST(std::string_view{::dcx::joybutton_text[0].data()} == "J2 B1");
	BOOST_REQUIRE(::dcx::joyaxis_text.size() != 0);
	BOOST_TEST(std::string_view{::dcx::joyaxis_text[0].data()} == "J2 A1");
}

namespace {

/* What reached the game at a harness tick. */
struct seen_at
{
	unsigned tick;
	seen event;
};

void push_key(const SDL_Keycode sym, const bool down)
{
	SDL_Event e{};
#if SDL_MAJOR_VERSION == 2
	e.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.sym = sym;
	BOOST_REQUIRE(SDL_PushEvent(&e) >= 0);
#elif SDL_MAJOR_VERSION == 3
	e.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	e.key.down = down;
	e.key.key = sym;
	BOOST_REQUIRE(SDL_PushEvent(&e));
#endif
}

/* Play an input script as the debug harness does: at each tick, its key and joystick lines, then
 * one event_poll.  `devices` holds the script's virtual joysticks; `at_tick` runs after each
 * tick's poll.
 */
std::vector<seen_at> play_script(const std::string_view text,
                                 ::dcx::sdl::virtual_joystick_slots &devices,
                                 const std::function<void(unsigned)> &at_tick)
{
	auto parsed{::dcx::parse_input_script(text, [](const std::string_view name) {
		return ::dcx::input_key_named(::dcx::key_properties, name);
	})};
	if (!parsed.has_value())
		BOOST_FAIL("line " << parsed.error().line << ": " << parsed.error().message);
	::dcx::input_script_player player{std::move(parsed).value()};
	std::vector<seen_at> seen_ticks;
	for (unsigned tick{0};; ++tick)
	{
		seen_events.clear();
		for (const auto &e : player.next_tick())
		{
			if (e.action == ::dcx::input_action::end)
				return seen_ticks;
			if (e.action == ::dcx::input_action::key_down ||
			    e.action == ::dcx::input_action::key_up)
				push_key(::dcx::input_key_entry(::dcx::key_properties, e.key).sym,
				         e.action == ::dcx::input_action::key_down);
			else
				BOOST_REQUIRE_MESSAGE(devices.play(e), "tick " << tick << ": " << SDL_GetError());
		}
		::dcx::event_poll();
		for (const auto &e : seen_events)
			seen_ticks.push_back({.tick = tick, .event = e});
		at_tick(tick);
	}
}

bool reached(const std::vector<seen_at> &s, const unsigned tick, const event_type type,
             const unsigned number)
{
	return std::ranges::any_of(s, [&](const seen_at &e) {
		return e.tick == tick && e.event.type == type && e.event.number == number;
	});
}

}

/* Test Xbox XR-115's case on virtual gamepads (docs/agents/input-testing.md, section 5): the
 * gamepad in play is unplugged at a seeded tick and plugged in again.  The game does not crash,
 * the keyboard still works while the pad is gone, a second pad plugged in meanwhile drives the
 * game, and once the first pad is back, with a new instance ID, its button reaches the game as
 * the same number, so its bindings act again.
 */
BOOST_AUTO_TEST_CASE(a_gamepad_unplugged_and_plugged_in_again_works_again)
{
	// NOLINTNEXTLINE(bugprone-random-generator-seed): a fixed seed replays the same ticks
	std::minstd_rand seeds{44};
	for (int run{0}; run < 5; ++run)
	{
		const unsigned seed{static_cast<unsigned>(seeds())};
		const unsigned gone{5 + seed % 11};
		BOOST_TEST_CONTEXT("seed " << seed << ", unplugged at tick " << gone)
		{
			const sdl_joysticks sdl;
			sdl_joysticks::start_game();
			const auto at{[gone](const unsigned after) { return std::to_string(gone + after); }};
			const std::string script{
			    "0 joy_attach 0 gamepad\n2 joy_button 0 0 down\n3 joy_button 0 0 up\n" + at(0) +
			    " joy_detach 0\n" + at(1) + " press ESC\n" + at(3) + " joy_attach 1 gamepad\n" +
			    at(4) + " joy_button 1 3 down\n" + at(5) + " joy_button 1 3 up\n" + at(6) +
			    " joy_attach 0 gamepad\n" + at(7) + " joy_button 0 0 down\n" + at(8) +
			    " joy_button 0 0 up\n" + at(9) + " end\n"};
			::dcx::sdl::virtual_joystick_slots devices;
			std::optional<SDL_JoystickID> first_id, second_id;
			int controllers_while_gone{-1};
			const auto s{play_script(script, devices, [&](const unsigned tick) {
				if (tick == 0 && devices[0])
					first_id = devices[0]->instance_id();
				if (tick == gone + 2)
					controllers_while_gone = ::dcx::num_controllers;
				if (tick == gone + 6 && devices[0])
					second_id = devices[0]->instance_id();
			})};
			BOOST_TEST(reached(s, 2, event_type::joystick_button_down, 0));
			BOOST_TEST(controllers_while_gone == 0);
			BOOST_TEST(reached(s, gone + 1, event_type::key_command, KEY_ESC));
			BOOST_TEST(reached(s, gone + 4, event_type::joystick_button_down, 3));
			BOOST_TEST(reached(s, gone + 7, event_type::joystick_button_down, 0));
			BOOST_TEST(::dcx::num_controllers == 2);
			BOOST_REQUIRE(first_id.has_value());
			BOOST_REQUIRE(second_id.has_value());
			BOOST_TEST(first_id.value() != second_id.value());
		}
	}
}

#if SDL_MAJOR_VERSION == 3
/* Test that on SDL3 a flight stick plugged in after start is opened, appended after the
 * numbers already given out, and that a stick plugged in again takes back the numbers a stick of
 * its shape left, so that no live binding moves and its bindings act again
 * (docs/agents/sdl-input.md, "Hot-plug").  SDL2 opened sticks only at start.
 */
BOOST_AUTO_TEST_CASE(a_stick_plugged_in_later_is_appended_and_gets_its_numbers_back)
{
	const sdl_joysticks sdl;
	sdl_joysticks::start_game();
	::dcx::sdl::virtual_joystick_slots devices;
	const auto s{play_script("0 joy_attach 0 stick\n1 joy_button 0 0 down\n2 joy_attach 1 gamepad\n"
	                         "3 joy_detach 0\n4 joy_attach 2 stick\n5 joy_button 2 0 down\n"
	                         "6 joy_detach 2\n7 joy_attach 0 stick\n8 joy_button 0 1 down\n9 end\n",
	                         devices, [](unsigned) {})};
	BOOST_TEST(reached(s, 1, event_type::joystick_button_down, 0));
	BOOST_TEST(reached(s, 5, event_type::joystick_button_down, 0));
	BOOST_TEST(reached(s, 8, event_type::joystick_button_down, 1));
	/* One stick's numbers: 32 buttons, a hat's 4 and 3 axes' 6, though two were plugged in. */
	BOOST_TEST(::dcx::joybutton_text.size() == 42u);
	BOOST_TEST(::dcx::joyaxis_text.size() == 3u);
}
#endif

#if SDL_MAJOR_VERSION == 3
namespace {

/* A stick's buttons and axes as a gamepad, in gamecontrollerdb.txt's form, for `platform`. */
std::string stick_as_gamepad(const SDL_JoystickID id, const std::string_view platform)
{
	std::array<char, 33> guid{};
	SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid.data(), static_cast<int>(guid.size()));
	return std::string{guid.data()} +
	       ",DXX mapped stick,a:b0,b:b1,x:b2,y:b3,leftx:a0,lefty:a1,platform:" +
	       std::string{platform} + ",\n";
}

}

/* Test that gamecontrollerdb.txt still loads on SDL3 (SDL_AddGamepadMappingsFromIO), and that a
 * line for another platform is dropped without an error, as SDL documents: a mapping for the
 * running platform makes a flight stick a gamepad, whose button 0 is A, number 0; one for
 * another platform leaves it a stick, whose button 0 is "J1 B1" (docs/agents/sdl-input.md,
 * "Platform-only hints and their defaults").
 */
BOOST_AUTO_TEST_CASE(the_controller_database_maps_only_the_running_platform)
{
	for (const bool this_platform : {true, false})
	{
		BOOST_TEST_CONTEXT((this_platform ? "this platform" : "another platform"))
		{
			const sdl_joysticks sdl;
			auto stick{virtual_joystick::attach(stick_shape)};
			BOOST_REQUIRE(stick.has_value());
			const std::string_view platform{SDL_GetPlatform()};
			controller_db =
			    stick_as_gamepad(stick.value().instance_id(), this_platform         ? platform
				                                              : platform == "Linux" ? "Mac OS X"
				                                                                    : "Linux");
			scene_recorder recorder{"db"};
			recorder.step("start", sdl_joysticks::start_game);
			recorder.step("b0 down", [&] { BOOST_TEST(stick->set_button(0, true)); });
			BOOST_TEST(::dcx::num_controllers == (this_platform ? 1 : 0));
			BOOST_REQUIRE(!recorder.rows.empty());
			const auto &pressed{recorder.rows.back()};
			BOOST_TEST(pressed.number == 0u);
			BOOST_TEST(pressed.label == (this_platform ? "A" : "J1 B1"));
		}
	}
}
#endif

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* SDL3 events, and on SDL2 the harness's mouse events, through the game's event loop (#42,
 * R12).  The seam is SDL's own queue: the test
 * pushes events with SDL_PushEvent and calls event_poll, which pumps and drains them through
 * process_event_batch as the game does, and standard_handler, the game's last-resort handler,
 * is a recorder.  With no window open (window_get_front is null), every event the game would
 * send reaches it.  The mouse handlers are the game's own (mouse.cpp, #43); the joystick and
 * window handlers event.cpp calls are stubs.
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
#include <functional>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
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
#if SDL_MAJOR_VERSION == 3
#include "common/sdl/display.h"
#endif
#include "tools/dev/upstream_end.h"
// clang-format on
#if DXX_USE_DBG_HARNESS
#include "common/debug/input_script.h"
#endif

namespace {

struct seen
{
	event_type type;
	unsigned key;
	/* What key_ascii returned when the key reached the handler. */
	unsigned ascii;
	/* A mouse button event's button; a mouse motion's deltas. */
	unsigned button;
	std::array<int, 3> delta;
};
std::vector<seen> seen_events;
#if SDL_MAJOR_VERSION == 3
/* The memory a later text event points to, which the handler of an earlier key overwrites. */
std::array<char, 2> reused_memory{};
#endif
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

#if SDL_MAJOR_VERSION == 3
void gr_handle_window_event(const SDL_Event &)
{}
#else
window_event_result gc_button_handler(const SDL_ControllerButtonEvent *)
{
	return window_event_result::ignored;
}

window_event_result gc_axis_handler(const SDL_ControllerAxisEvent *)
{
	return window_event_result::ignored;
}

window_event_result gc_axisbutton_handler(const SDL_ControllerAxisEvent *)
{
	return window_event_result::ignored;
}

window_event_result gc_device_added(const SDL_ControllerDeviceEvent *)
{
	return window_event_result::ignored;
}

window_event_result gc_device_removed(const SDL_ControllerDeviceEvent *)
{
	return window_event_result::ignored;
}
#endif

void gr_flip()
{}

window_event_result standard_handler(const d_event &event)
{
	const bool is_key{event.type == event_type::key_command ||
	                  event.type == event_type::key_release};
	const bool is_press{event.type == event_type::key_command};
	const bool is_button{event.type == event_type::mouse_button_down ||
	                     event.type == event_type::mouse_button_up};
	seen_events.push_back(
	    {.type = event.type,
		 .key = is_key ? static_cast<unsigned>(event_key_get(event)) : 0u,
		 .ascii = is_press ? key_ascii() : 0u,
		 .button = is_button ? static_cast<unsigned>(event_mouse_get_button(event)) : 0u,
		 .delta = event.type == event_type::mouse_moved ? event_mouse_get_delta(event)
		                                                : std::array<int, 3>{}});
	if (is_press && on_key_command)
		on_key_command();
	return window_event_result::handled;
}

}

#if SDL_MAJOR_VERSION == 3
extern "C" const bool *SDL_GetKeyboardState(int *const numkeys)
{
	static const std::array<bool, SDL_SCANCODE_COUNT> none{};
	if (numkeys)
		*numkeys = static_cast<int>(none.size());
	return none.data();
}
#endif

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth event poll
#include <boost/test/unit_test.hpp>

namespace {

struct sdl_events
{
	sdl_events()
	{
#if SDL_MAJOR_VERSION == 3
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
		BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO));
		::dcx::g_pRebirthSDLMainWindow = SDL_CreateWindow("event test", 64, 64, SDL_WINDOW_HIDDEN);
#else
		SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
		BOOST_REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
		::dcx::g_pRebirthSDLMainWindow =
		    SDL_CreateWindow("event test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 64, 64,
			                 SDL_WINDOW_HIDDEN);
#endif
		BOOST_REQUIRE(::dcx::g_pRebirthSDLMainWindow);
		seen_events.clear();
		on_key_command = nullptr;
		::dcx::key_flush();
		::dcx::mouse_init();
	}
	sdl_events(const sdl_events &) = delete;
	sdl_events &operator=(const sdl_events &) = delete;
	sdl_events(sdl_events &&) = delete;
	sdl_events &operator=(sdl_events &&) = delete;
	~sdl_events()
	{
		::dcx::key_text_input_close();
		SDL_DestroyWindow(::dcx::g_pRebirthSDLMainWindow);
		::dcx::g_pRebirthSDLMainWindow = nullptr;
		SDL_Quit();
	}
};

unsigned count_of(const event_type type)
{
	return static_cast<unsigned>(
	    std::ranges::count(seen_events, type, [](const seen &e) { return e.type; }));
}

/* Read by SDL3's cases and, on SDL2 too, by the harness's. */
#if SDL_MAJOR_VERSION == 3 || DXX_USE_DBG_HARNESS
/* The sum of the mouse deltas event_poll sent. */
std::array<int, 3> moved()
{
	std::array<int, 3> sum{};
	for (const auto &e : seen_events)
		if (e.type == event_type::mouse_moved)
			for (std::size_t i{0}; i != sum.size(); ++i)
				sum.at(i) += e.delta.at(i);
	return sum;
}

/* The mouse buttons that went down, in order. */
std::vector<unsigned> buttons_down()
{
	std::vector<unsigned> v;
	for (const auto &e : seen_events)
		if (e.type == event_type::mouse_button_down)
			v.push_back(e.button);
	return v;
}

constexpr auto z_up{static_cast<unsigned>(::dcx::mbtn::z_up)};
#endif

}

/* Test that SDL's quit event, which the app menu's Cmd-Q posts, reaches the game's handler as
 * a quit event (the quit dialog), and not as a key: the owner's decision of ADR 7.
 */
BOOST_AUTO_TEST_CASE(sdl_quit_reaches_the_game_as_a_quit_event)
{
	const sdl_events sdl;
	SDL_Event e{};
#if SDL_MAJOR_VERSION == 3
	e.type = SDL_EVENT_QUIT;
	BOOST_REQUIRE(SDL_PushEvent(&e));
#else
	e.type = SDL_QUIT;
	BOOST_REQUIRE(SDL_PushEvent(&e) == 1);
#endif
	::dcx::event_poll();
	BOOST_TEST(count_of(event_type::quit) == 1u);
	BOOST_TEST(count_of(event_type::key_command) == 0u);
}

#if SDL_MAJOR_VERSION == 3
namespace {

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
	::dcx::event_poll();
	const std::vector<unsigned> expected{static_cast<unsigned>(KEY_LMETA + KEY_COMMAND),
	                                     static_cast<unsigned>(KEY_P + KEY_COMMAND)};
	BOOST_TEST(pressed() == expected, boost::test_tools::per_element());
}

/* Test that, with text input open, the text that follows a key is in key_ascii when that key's
 * handler runs, once: SDL queues the text after its key, and the handler reads it with the key.
 */
BOOST_AUTO_TEST_CASE(text_after_a_key_is_there_when_the_keys_handler_runs)
{
	const sdl_events sdl;
	::dcx::key_text_input_open();
	BOOST_REQUIRE(::dcx::key_text_input_active());
	push_key(SDLK_A, true);
	push_text("A");
	push_key(SDLK_B, true);
	push_text("B");
	::dcx::event_poll();
	std::vector<unsigned> ascii;
	for (const auto &e : seen_events)
		if (e.type == event_type::key_command)
			ascii.push_back(e.ascii);
	const std::vector<unsigned> expected{'A', 'B'};
	BOOST_TEST(ascii == expected, boost::test_tools::per_element());
	BOOST_TEST(::dcx::key_ascii() == 255);
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
	::dcx::key_text_input_open();
	push_key(SDLK_BACKSPACE, true);
	push_key(SDLK_BACKSPACE, false);
	push_text("x");
	::dcx::event_poll();
	const std::vector<press> expected{{KEY_BACKSP, 255}, {0, 'x'}};
	BOOST_TEST(presses() == expected, boost::test_tools::per_element());
}

/* Test that a text event that opens a batch reaches the handler too, as an input method sends
 * its text alone. */
BOOST_AUTO_TEST_CASE(text_that_opens_a_batch_reaches_the_handler)
{
	const sdl_events sdl;
	::dcx::key_text_input_open();
	push_text("\xc3\xa9");
	::dcx::event_poll();
	const std::vector<press> expected{{0, 0xe9}};
	BOOST_TEST(presses() == expected, boost::test_tools::per_element());
}

/* Test that text is dropped, and no key command sent, with text input closed. */
BOOST_AUTO_TEST_CASE(text_with_text_input_closed_reaches_nobody)
{
	const sdl_events sdl;
	push_text("x");
	::dcx::event_poll();
	BOOST_TEST(presses().empty());
}

/* Test that the text events of a batch stay whole while a handler runs a menu of its own and so
 * pumps events: SDL frees the text of its events at the next pump.  The handler of the first key
 * overwrites the memory the later text events point to, and pumps.
 */
BOOST_AUTO_TEST_CASE(text_survives_a_handler_that_pumps_events)
{
	const sdl_events sdl;
	::dcx::key_text_input_open();
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
	::dcx::event_poll();
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
		::dcx::key_text_input_open();
		on_key_command = nullptr;
	};
	::dcx::event_poll();
	const std::vector<press> expected{{KEY_F1, 255}, {KEY_A, 'a'}, {KEY_B, 'b'}};
	BOOST_TEST(presses() == expected, boost::test_tools::per_element());
}

/* Test that a key queued after the field opened and brought no text gives nothing: its text
 * would have come with it. */
BOOST_AUTO_TEST_CASE(a_key_queued_after_the_field_opened_gives_only_its_text)
{
	const sdl_events sdl;
	::dcx::key_text_input_open();
	push_key(SDLK_A, true);
	::dcx::event_poll();
	const std::vector<press> expected{{KEY_A, 255}};
	BOOST_TEST(presses() == expected, boost::test_tools::per_element());
}

namespace {

void push_motion(const float dx, const float dy)
{
	SDL_Event e{};
	e.motion.type = SDL_EVENT_MOUSE_MOTION;
	e.motion.xrel = dx;
	e.motion.yrel = dy;
	BOOST_REQUIRE(SDL_PushEvent(&e));
}

}

/* Test that SDL3's sub-pixel mouse motion reaches the game as whole pixels that add up, and
 * moves the game's cursor by them: five moves of 0.4 are two pixels, not five truncated zeros.
 */
BOOST_AUTO_TEST_CASE(sub_pixel_motion_adds_up_to_whole_pixels_in_the_game)
{
	const sdl_events sdl;
	const auto [x0, y0, z0]{::dcx::mouse_get_pos()};
	for (int i{0}; i != 5; ++i)
		push_motion(0.4f, -0.6f);
	::dcx::event_poll();
	const std::array<int, 3> expected{2, -3, 0};
	BOOST_TEST(moved() == expected, boost::test_tools::per_element());
	const auto [x1, y1, z1]{::dcx::mouse_get_pos()};
	BOOST_TEST(x1 - x0 == 2);
	BOOST_TEST(y1 - y0 == -3);
}

namespace {

void push_wheel(const int ticks, const float amount, const SDL_MouseWheelDirection direction)
{
	SDL_Event e{};
	e.wheel.type = SDL_EVENT_MOUSE_WHEEL;
	e.wheel.integer_y = ticks;
	e.wheel.y = amount;
	e.wheel.direction = direction;
	BOOST_REQUIRE(SDL_PushEvent(&e));
}

constexpr auto z_down{static_cast<unsigned>(::dcx::mbtn::z_down)};

}

/* Test that a notched wheel sends one z_up or z_down per click, each a press and release, and
 * moves the game's z by one step each.
 */
BOOST_AUTO_TEST_CASE(notched_wheel_sends_one_z_step_per_click)
{
	const sdl_events sdl;
	push_wheel(2, 2.f, SDL_MOUSEWHEEL_NORMAL);
	push_wheel(-1, -1.f, SDL_MOUSEWHEEL_NORMAL);
	::dcx::event_poll();
	const std::vector<unsigned> expected{z_up, z_up, z_down};
	BOOST_TEST(buttons_down() == expected, boost::test_tools::per_element());
	BOOST_TEST(count_of(event_type::mouse_button_up) == 3u);
	BOOST_TEST(moved()[2] == Z_SENSITIVITY);
	BOOST_TEST(std::get<2>(::dcx::mouse_get_pos()) == Z_SENSITIVITY);
}

/* Test that a precise trackpad's fractions send nothing until SDL completes a tick. */
BOOST_AUTO_TEST_CASE(precise_trackpad_sends_a_step_per_whole_tick)
{
	const sdl_events sdl;
	push_wheel(0, 0.4f, SDL_MOUSEWHEEL_NORMAL);
	push_wheel(0, 0.4f, SDL_MOUSEWHEEL_NORMAL);
	push_wheel(1, 0.4f, SDL_MOUSEWHEEL_NORMAL);
	::dcx::event_poll();
	const std::vector<unsigned> expected{z_up};
	BOOST_TEST(buttons_down() == expected, boost::test_tools::per_element());
}

/* Test that a flipped event, natural scrolling, sends the step of the physical wheel. */
BOOST_AUTO_TEST_CASE(flipped_wheel_sends_the_physical_direction)
{
	const sdl_events sdl;
	push_wheel(-1, -1.f, SDL_MOUSEWHEEL_FLIPPED);
	::dcx::event_poll();
	const std::vector<unsigned> expected{z_up};
	BOOST_TEST(buttons_down() == expected, boost::test_tools::per_element());
}

#endif

#if DXX_USE_DBG_HARNESS
namespace dcx {

#if SDL_MAJOR_VERSION == 3
/* event.cpp's harness_push_text, which the harness build defines. */
bool harness_push_text(bool down, SDL_Keycode key);
#endif
/* event.cpp's harness_mouse_event, which the harness build defines. */
SDL_Event harness_mouse_event(const input_event &e);

}

#if SDL_MAJOR_VERSION == 3
namespace {

/* Overwrite the stack below the caller, where a returned function's locals lay. */
[[gnu::noinline]]
void clobber_the_stack()
{
	std::array<volatile char, 16384> junk{};
	for (auto &c : junk)
		c = 'Z';
}

}

/* Test that the text the harness pushes for a printable key is still the key's text after the
 * pushing function returned and the stack below it was reused: SDL_PushEvent copies the event
 * struct, not the text it points to.
 */
BOOST_AUTO_TEST_CASE(harness_text_outlives_the_function_that_pushed_it)
{
	const sdl_events sdl;
	::dcx::key_text_input_open();
	BOOST_REQUIRE(::dcx::key_text_input_active());
	push_key(SDLK_A, true);
	BOOST_REQUIRE(::dcx::harness_push_text(true, SDLK_A));
	clobber_the_stack();
	::dcx::event_poll();
	std::vector<unsigned> ascii;
	for (const auto &e : seen_events)
		if (e.type == event_type::key_command)
			ascii.push_back(e.ascii);
	const std::vector<unsigned> expected{'a'};
	BOOST_TEST(ascii == expected, boost::test_tools::per_element());
}
#endif

/* Test that the harness's events for a script's mouse lines reach the game as the script says:
 * the move's whole pixels, the button down and up, and a step of the wheel up.
 */
BOOST_AUTO_TEST_CASE(harness_mouse_lines_reach_the_game)
{
	const sdl_events sdl;
	const auto parsed{::dcx::parse_input_script(
	    "0 mouse_move 3 -2\n0 mouse_down left\n0 mouse_up left\n0 wheel 1\n1 end\n",
	    [](std::string_view) { return std::optional<::dcx::input_key>{}; })};
	BOOST_REQUIRE(parsed.has_value());
	for (const auto &e : parsed.value())
		if (e.action != ::dcx::input_action::end)
		{
			auto event{::dcx::harness_mouse_event(e)};
#if SDL_MAJOR_VERSION == 3
			BOOST_REQUIRE(SDL_PushEvent(&event));
#else
			BOOST_REQUIRE(SDL_PushEvent(&event) == 1);
#endif
		}
	::dcx::event_poll();
	const std::array<int, 3> expected_motion{{3, -2, Z_SENSITIVITY}};
	BOOST_TEST(moved() == expected_motion, boost::test_tools::per_element());
	const std::vector<unsigned> expected_down{static_cast<unsigned>(::dcx::mbtn::left), z_up};
	BOOST_TEST(buttons_down() == expected_down, boost::test_tools::per_element());
	BOOST_TEST(count_of(event_type::mouse_button_up) == 2u);
}
#endif

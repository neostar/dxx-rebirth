/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 *
 * SDL Event related stuff
 *
 *
 */

#include <ranges>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "event.h"
#include "key.h"
#include "mouse.h"
#include "window.h"
#include "timer.h"
#include "cmd.h"
#include "config.h"
#include "inferno.h"

#include "joy.h"
#include "args.h"
#include "partial_range.h"
#if SDL_MAJOR_VERSION == 3
#include "common/sdl/audio_frame.h"
#include "common/sdl/display.h"
#include "common/sdl/text_input.h"
#endif
#if DXX_USE_DBG_HARNESS
#include <utility>
#include "common/debug/hidden_window.h"
#include "common/debug/input_script_harness.h"
#include "common/sdl/harness_record.h"
#if SDL_MAJOR_VERSION >= 2
#include "common/sdl/virtual_joystick.h"
#endif
#include "console.h"
#include "dxxerror.h"
#endif

namespace dcx {

namespace {

struct event_poll_state
{
	uint8_t clean_uniframe{1};
	const window *const front_window = window_get_front();
	window_event_result highest_result = window_event_result::ignored;
	void process_event_batch(std::ranges::subrange<const SDL_Event *>);
};

}

#if SDL_MAJOR_VERSION >= 2
extern SDL_Window *g_pRebirthSDLMainWindow;
#endif

#if SDL_MAJOR_VERSION == 2
static void windowevent_handler(const SDL_WindowEvent &windowevent)
{
	switch (windowevent.event)
	{
		case SDL_WINDOWEVENT_SIZE_CHANGED:
			{
				const d_window_size_event e{windowevent.data1, windowevent.data2};
				event_send(e);
				break;
			}
	}
}
#elif SDL_MAJOR_VERSION == 3
/* SDL3 sends each window event on its own; process_event_batch hands
 * them to gr_handle_window_event (common/sdl/display.h).
 */
#endif

namespace {

static void event_notify_begin_loop()
{
	const d_event_begin_loop event;
	event_send(event);
}

static void event_notify_end_loop()
{
	event_send(d_event_end_loop{});
}

}

#if DXX_USE_DBG_HARNESS
#if SDL_MAJOR_VERSION == 3
/* The platform sends the text of a printable key after its key down while text input is
 * open; send it too.  False if SDL refused the event.  Not in the anonymous namespace: the
 * test calls it (common/sdl/unittest/event_poll.cpp).
 */
bool harness_push_text(const bool down, const SDL_Keycode key);
bool harness_push_text(const bool down, const SDL_Keycode key)
{
	if (!down || !key_text_input_active() || key < 32 || key > 126)
		return true;
	/* SDL_PushEvent copies the event struct, not the text it points to: the text lives in
	 * static storage.
	 */
	static constexpr auto printable{[] {
		std::array<std::array<char, 2>, 95> table{};
		for (std::size_t i{0}; i < table.size(); ++i)
			table[i] = {static_cast<char>(32 + i), '\0'};
		return table;
	}()};
	SDL_Event event{};
	event.text.type = SDL_EVENT_TEXT_INPUT;
	event.text.text = printable[key - 32].data();
	return SDL_PushEvent(&event);
}
#endif

#if SDL_MAJOR_VERSION >= 2
/* The SDL event of a script's mouse line, as the platform would send it.  Not in the anonymous
 * namespace: the test calls it (common/sdl/unittest/event_poll.cpp).  SDL 1.2 has no wheel
 * event: an SDL 1.2 build refuses mouse lines.
 */
SDL_Event harness_mouse_event(const input_event &e);
SDL_Event harness_mouse_event(const input_event &e)
{
	SDL_Event event{};
	switch (e.action)
	{
		case input_action::mouse_move:
			event.motion.type = SDL_MOUSEMOTION;
#if SDL_MAJOR_VERSION == 3
			event.motion.xrel = static_cast<float>(e.x);
			event.motion.yrel = static_cast<float>(e.y);
#else
			event.motion.xrel = e.x;
			event.motion.yrel = e.y;
#endif
			break;
		case input_action::mouse_down:
		case input_action::mouse_up:
		{
			const bool down{e.action == input_action::mouse_down};
			event.button.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
			event.button.button = std::to_underlying(e.button);
			event.button.clicks = 1;
#if SDL_MAJOR_VERSION == 3
			event.button.down = down;
#else
			event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
#endif
			break;
		}
		case input_action::wheel:
			event.wheel.type = SDL_MOUSEWHEEL;
			event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
#if SDL_MAJOR_VERSION == 3
			event.wheel.integer_y = e.y;
			event.wheel.y = static_cast<float>(e.y);
#else
			event.wheel.y = e.y;
			event.wheel.preciseY = static_cast<float>(e.y);
			/* sdl2-compat (2.32.74, src/sdl2_compat.c:2543ff) turns a pushed SDL2 event into
			 * SDL3's by copying the body byte for byte, which puts mouseY where SDL3 keeps
			 * integer_y; its wheel case (:2578-2586) leaves integer_y alone, and the event the
			 * game polls takes y from integer_y (:2431-2432).  So the steps go in mouseY too.
			 * Real SDL2 passes mouseY, the cursor's row, through untouched, and the wheel
			 * handler reads only y.
			 */
			event.wheel.mouseY = e.y;
#endif
			break;
		default:
			break;
	}
	return event;
}
#endif

namespace {

/* Start the next harness tick of a recording (harness_record.h), then push
 * the events of the input script DXX_DBG_INPUT_SCRIPT names
 * (input_script_harness.h) due at it.
 */
void harness_push_input()
{
	harness_record_next_tick();
	static const char *const path{read_harness_input_script_path()};
	if (!path)
		return;
	static auto script{load_input_script(path, read_harness_clock_step(), [](const std::string_view name) { return input_key_named(key_properties, name); })};
	if (!script)
		UserError("DXX_DBG_INPUT_SCRIPT=\"%s\": %s", path, script.error().c_str());
	for (const auto &e : script->player.next_tick())
	{
		if (e.action == input_action::end)
			harness_end_run(e.tick);
		const bool joystick_line{e.action == input_action::joy_attach || e.action == input_action::joy_detach ||
		                         e.action == input_action::joy_axis || e.action == input_action::joy_button_down ||
		                         e.action == input_action::joy_button_up || e.action == input_action::joy_hat};
		if (joystick_line)
		{
#if SDL_MAJOR_VERSION >= 2
			/* The script's virtual joysticks, on SDL's virtual driver; their events come with the
			 * next pump.  A hidden window is in the background, where SDL drops joystick events
			 * unless told otherwise.
			 */
			static sdl::virtual_joystick_slots devices;
			if (e.action == input_action::joy_attach)
				SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
			if (!devices.play(e))
				UserError("DXX_DBG_INPUT_SCRIPT: a joystick line at tick %u failed: %s", std::to_underlying(e.tick), SDL_GetError());
			continue;
#else
			UserError_puts("DXX_DBG_INPUT_SCRIPT: joystick lines need an SDL 2 or SDL 3 build");
#endif
		}
		if (e.action != input_action::key_down && e.action != input_action::key_up)
		{
#if SDL_MAJOR_VERSION >= 2
			auto event{harness_mouse_event(e)};
#if SDL_MAJOR_VERSION == 3
			if (!SDL_PushEvent(&event))
#else
			if (SDL_PushEvent(&event) < 0)
#endif
				UserError("DXX_DBG_INPUT_SCRIPT: pushing an event failed: %s", SDL_GetError());
			continue;
#else
			UserError_puts("DXX_DBG_INPUT_SCRIPT: mouse lines need an SDL 2 or SDL 3 build");
#endif
		}
		SDL_Event event{};
		const bool down{e.action == input_action::key_down};
#if SDL_MAJOR_VERSION == 3
		event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
		event.key.down = down;
		event.key.key = input_key_entry(key_properties, e.key).sym;
		if (!SDL_PushEvent(&event) || !harness_push_text(down, event.key.key))
#else
		event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
		event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
		event.key.keysym.sym = input_key_entry(key_properties, e.key).sym;
		if (SDL_PushEvent(&event) < 0)
#endif
			UserError("DXX_DBG_INPUT_SCRIPT: pushing an event failed: %s", SDL_GetError());
	}
}

}

#endif
window_event_result event_poll()
{
	event_poll_state state;
	event_notify_begin_loop();

	for (;;)
	{
	// If the front window changes, exit this loop, otherwise unintended behavior can occur
	// like pressing 'Return' really fast at 'Difficulty Level' causing multiple games to be started
		if (state.front_window != window_get_front())
			break;
		std::array<SDL_Event, 128> events;

		SDL_PumpEvents();
#if SDL_MAJOR_VERSION == 1
		const auto peep = SDL_PeepEvents(events.data(), events.size(), SDL_GETEVENT, SDL_ALLEVENTS);
#elif SDL_MAJOR_VERSION >= 2
		const auto peep = SDL_PeepEvents(events.data(), events.size(), SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
#endif
		if (peep <= 0)
			break;
#if SDL_MAJOR_VERSION == 3
		/* A handler may pump events in the middle of the batch, which frees SDL's text. */
		const sdl::event_text_copies text_copies{std::span{events.data(), static_cast<std::size_t>(peep)}};
#endif
		state.process_event_batch(unchecked_partial_range(events, static_cast<unsigned>(peep)));
		if (state.highest_result == window_event_result::deleted)
			break;
	}
	// Send the idle event if there were no other events (or they were ignored)
	if (state.highest_result == window_event_result::ignored)
	{
		const d_event ievent{event_type::idle};
		state.highest_result = std::max(event_send(ievent), state.highest_result);
	}
	else
	{
#if DXX_USE_EDITOR
		event_reset_idle_seconds();
#endif
	}
	mouse_cursor_autohide();
	event_notify_end_loop();
	return state.highest_result;
}

void event_poll_state::process_event_batch(const std::ranges::subrange<const SDL_Event *> events)
{
#if SDL_MAJOR_VERSION == 3
	/* The text events that key_take_text took ahead of their key's handler. */
	unsigned text_taken{0};
#endif
	for (auto &&event : events)
	{
#if DXX_USE_DBG_HARNESS && SDL_MAJOR_VERSION >= 2
		harness_record_input(event);
#endif
		window_event_result result;
		switch(event.type) {
#if SDL_MAJOR_VERSION == 2
			case SDL_WINDOWEVENT:
				windowevent_handler(event.window);
				continue;
#elif SDL_MAJOR_VERSION == 3
			case SDL_EVENT_WINDOW_SHOWN:
			case SDL_EVENT_WINDOW_HIDDEN:
			case SDL_EVENT_WINDOW_EXPOSED:
			case SDL_EVENT_WINDOW_OCCLUDED:
			case SDL_EVENT_WINDOW_MINIMIZED:
			case SDL_EVENT_WINDOW_RESTORED:
			case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
			case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				gr_handle_window_event(event);
				continue;
#endif
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				if (clean_uniframe)
				{
					clean_uniframe=0;
					unicode_frame_buffer = {};
				}
#if SDL_MAJOR_VERSION == 3
				/* SDL sends a key's text after the key, but the key's handler reads it. */
				text_taken += key_take_text(event.key, std::span<const SDL_Event>{&event + 1, events.end()});
#endif
				result = key_handler(&event.key);
				break;
#if SDL_MAJOR_VERSION == 3
			case SDL_EVENT_TEXT_INPUT:
				if (text_taken)
				{
					--text_taken;
					continue;
				}
				/* A text event no key event brought along: after Backspace down and up on Cocoa,
				 * alone from an input method, or at the start of a batch.
				 */
				if (clean_uniframe)
				{
					clean_uniframe=0;
					unicode_frame_buffer = {};
				}
				result = key_text_command(event.text);
				break;
#endif
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				if (CGameArg.CtlNoMouse)
					continue;
				result = mouse_button_handler(&event.button);
				break;
			case SDL_MOUSEMOTION:
				if (CGameArg.CtlNoMouse)
					continue;
				result = mouse_motion_handler(&event.motion);
				break;
#if SDL_MAJOR_VERSION >= 2
			case SDL_MOUSEWHEEL:
				if (CGameArg.CtlNoMouse)
					continue;
				result = mouse_wheel_handler(&event.wheel);
				break;
#endif
#if DXX_MAX_JOYSTICKS
#if SDL_MAJOR_VERSION == 2
#if DXX_MAX_BUTTONS_PER_JOYSTICK
			case SDL_CONTROLLERBUTTONDOWN:
			case SDL_CONTROLLERBUTTONUP:
				if (CGameArg.CtlNoJoystick)
					continue;
				result = gc_button_handler(&event.cbutton);
				break;
#endif
#if DXX_MAX_AXES_PER_JOYSTICK
			case SDL_CONTROLLERAXISMOTION:
				if (CGameArg.CtlNoJoystick)
					continue;
#if (DXX_MAX_BUTTONS_PER_JOYSTICK || DXX_MAX_HATS_PER_JOYSTICK)
				highest_result = std::max(gc_axisbutton_handler(&event.caxis), highest_result);
#endif
				result = gc_axis_handler(&event.caxis);
				break;
#endif
			case SDL_CONTROLLERDEVICEADDED:
				result = gc_device_added(&event.cdevice);
				break;
			case SDL_CONTROLLERDEVICEREMOVED:
				result = gc_device_removed(&event.cdevice);
				break;
#elif SDL_MAJOR_VERSION == 3
#if DXX_MAX_BUTTONS_PER_JOYSTICK
			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			case SDL_EVENT_GAMEPAD_BUTTON_UP:
				if (CGameArg.CtlNoJoystick)
					continue;
				result = gc_button_handler(&event.gbutton);
				break;
#endif
#if DXX_MAX_AXES_PER_JOYSTICK
			case SDL_EVENT_GAMEPAD_AXIS_MOTION:
				if (CGameArg.CtlNoJoystick)
					continue;
#if (DXX_MAX_BUTTONS_PER_JOYSTICK || DXX_MAX_HATS_PER_JOYSTICK)
				highest_result = std::max(gc_axisbutton_handler(&event.gaxis), highest_result);
#endif
				result = gc_axis_handler(&event.gaxis);
				break;
#endif
			case SDL_EVENT_GAMEPAD_ADDED:
				result = gc_device_added(&event.gdevice);
				break;
			case SDL_EVENT_GAMEPAD_REMOVED:
				result = gc_device_removed(&event.gdevice);
				break;
			/* SDL3 sends these for every joystick, gamepads too; SDL2's
			 * joysticks were opened once at start (joy_init).
			 */
			case SDL_EVENT_JOYSTICK_ADDED:
				result = joy_device_added(&event.jdevice);
				break;
			case SDL_EVENT_JOYSTICK_REMOVED:
				result = joy_device_removed(&event.jdevice);
				break;
#endif
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
				if (CGameArg.CtlNoJoystick)
					continue;
				result = joy_button_handler(&event.jbutton);
				break;
			case SDL_JOYAXISMOTION:
				if (CGameArg.CtlNoJoystick)
					continue;
#if DXX_MAX_BUTTONS_PER_JOYSTICK || DXX_MAX_HATS_PER_JOYSTICK
				highest_result = std::max(joy_axisbutton_handler(&event.jaxis), highest_result);
#endif
				result = joy_axis_handler(&event.jaxis);
				break;
			case SDL_JOYHATMOTION:
				if (CGameArg.CtlNoJoystick)
					continue;
				result = joy_hat_handler(&event.jhat);
				break;
			case SDL_JOYBALLMOTION:
				continue;
#endif
			case SDL_QUIT: {
				result = call_default_handler(d_event{event_type::quit});
				break;
			}
			default:
				continue;
		}
		highest_result = std::max(result, highest_result);
	}
}

void event_flush()
{
	std::array<SDL_Event, 128> events;
	for (;;)
	{
		SDL_PumpEvents();
#if SDL_MAJOR_VERSION == 1
		const auto peep = SDL_PeepEvents(events.data(), events.size(), SDL_GETEVENT, SDL_ALLEVENTS);
#elif SDL_MAJOR_VERSION >= 2
		const auto peep = SDL_PeepEvents(events.data(), events.size(), SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
#endif
		if (peep != events.size())
			break;
	}
}

window_event_result call_default_handler(const d_event &event)
{
	return standard_handler(event);
}

window_event_result event_send(const d_event &event)
{
	window *wind;
	window_event_result handled = window_event_result::ignored;

	for (wind = window_get_front(); wind && handled == window_event_result::ignored; wind = window_get_prev(*wind))
		if (wind->is_visible())
		{
			handled = wind->send_event(event);

			if (handled == window_event_result::deleted) // break away if necessary: window_send_event() could have closed wind by now
				break;
			if (wind->is_modal())
				break;
		}
	
	if (handled == window_event_result::ignored)
		return call_default_handler(event);

	return handled;
}

// Process the first event in queue, sending to the appropriate handler
// This is the new object-oriented system
// Uses the old system for now, but this may change
window_event_result event_process(void)
{
	window *wind = window_get_front();
	window_event_result highest_result;

	timer_update();
#if SDL_MAJOR_VERSION == 3
	dcx::sdl::audio_commit_frame();
#endif
#if DXX_USE_DBG_HARNESS
	harness_push_input();
#endif

	highest_result = event_poll();	// send input events first

	cmd_queue_process();

	// Doing this prevents problems when a draw event can create a newmenu,
	// such as some network menus when they report a problem
	// Also checking for window_event_result::deleted in case a window was created
	// with the same pointer value as the deleted one
	if ((highest_result == window_event_result::deleted) || (window_get_front() != wind))
		return highest_result;

	const d_event event{event_type::window_draw};	// then draw all visible windows
	for (wind = window_get_first(); wind != nullptr;)
	{
		if (wind->is_visible())
		{
			auto prev = window_get_prev(*wind);
			auto result = wind->send_event(event);
			highest_result = std::max(result, highest_result);
			if (result == window_event_result::deleted)
			{
				if (!prev)
				{
					wind = window_get_first();
					continue;
				}
				wind = prev;	// take the previous window and get the next one from that (if prev isn't nullptr)
			}
		}
		wind = window_get_next(*wind);
	}

	gr_flip();

	return highest_result;
}

namespace {

template <bool activate_focus>
static void event_change_focus()
{
#if DXX_USE_DBG_HARNESS
	/* A hidden run never takes the owner's real cursor. */
	static const bool harness_grab{harness_allows_grab(read_harness_hidden())};
	if (activate_focus && !harness_grab)
	{
		static bool reported;
		if (!std::exchange(reported, true))
			con_puts(CON_NORMAL, "DXX_DBG_HIDDEN: the mouse and keyboard stay ungrabbed");
	}
	const auto enable_grab = activate_focus && harness_grab && CGameCfg.Grabinput && likely(!CGameArg.DbgForbidConsoleGrab);
#else
	const auto enable_grab = activate_focus && CGameCfg.Grabinput && likely(!CGameArg.DbgForbidConsoleGrab);
#endif
#if SDL_MAJOR_VERSION == 1
	SDL_WM_GrabInput(enable_grab ? SDL_GRAB_ON : SDL_GRAB_OFF);
#elif SDL_MAJOR_VERSION == 2
	SDL_SetWindowGrab(g_pRebirthSDLMainWindow, enable_grab ? SDL_TRUE : SDL_FALSE);
	SDL_SetRelativeMouseMode(enable_grab ? SDL_TRUE : SDL_FALSE);
#elif SDL_MAJOR_VERSION == 3
	SDL_SetWindowMouseGrab(g_pRebirthSDLMainWindow, enable_grab);
	SDL_SetWindowKeyboardGrab(g_pRebirthSDLMainWindow, enable_grab);
	SDL_SetWindowRelativeMouseMode(g_pRebirthSDLMainWindow, enable_grab);
#endif
	if (activate_focus)
		mouse_disable_cursor();
	else
		mouse_enable_cursor();
}

}

void event_enable_focus()
{
	event_change_focus<true>();
}

void event_disable_focus()
{
	event_change_focus<false>();
}

#if DXX_USE_EDITOR
static fix64 last_event = 0;

void event_reset_idle_seconds()
{
	last_event = timer_query();
}

fix event_get_idle_seconds()
{
	return (timer_query() - last_event)/F1_0;
}
#endif

}

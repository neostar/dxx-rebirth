/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* The input recorder at the SDL seam (#99), linked into dbg_harness builds
 * alone; the unit test links it with event.cpp
 * (common/sdl/unittest/event_poll.cpp).  Without dbg_harness it
 * compiles to nothing.
 */

// clang-format off
#include "dxxsconf.h"
// clang-format on

#if DXX_USE_DBG_HARNESS

#include "common/sdl/harness_record.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "key.h"
#include "tools/dev/upstream_end.h"
// clang-format on

namespace dcx {

/* Whether key_handler takes repeated keys (key.cpp). */
bool key_repeat_active();

#if SDL_MAJOR_VERSION >= 2
std::expected<std::optional<input_event>, unrecorded_input>
harness_recorded_event(const SDL_Event &event, const harness_tick tick,
                       // NOLINTNEXTLINE(misc-const-correctness): only SDL3 writes it
                       [[maybe_unused]] harness_record_state &state)
{
	/* SDL2's events carry nothing from one to the next: state is SDL3's. */
	switch (event.type)
	{
	case SDL_KEYDOWN:
	case SDL_KEYUP: {
#if SDL_MAJOR_VERSION == 3
		const auto sym{event.key.key};
		const bool down{event.key.down};
#else
		const auto sym{event.key.keysym.sym};
		const bool down{event.key.state != SDL_RELEASED};
#endif
		/* key_handler drops these two before it reads the key. */
		if ((event.key.repeat && !key_repeat_active()) || sym == SDLK_UNKNOWN)
			return std::nullopt;
		const auto re{key_properties.rend()};
		const auto fi{std::ranges::find(key_properties.rbegin(), re, sym, &key_props::sym)};
		const auto code{fi == re ? 0 : std::distance(key_properties.begin(), std::next(fi).base())};
		/* A key with no code: key_handler drops it, after typing its character. */
		if (code == 0)
		{
			if (down)
				return std::unexpected{unrecorded_input::text};
			return std::nullopt;
		}
#if SDL_MAJOR_VERSION == 3
		state.typed = down ? sym : SDLK_UNKNOWN;
#endif
		return input_event{.tick = tick,
		                   .action = down ? input_action::key_down : input_action::key_up,
		                   .key = input_key{static_cast<std::uint8_t>(code)}};
	}
#if SDL_MAJOR_VERSION == 3
	case SDL_EVENT_TEXT_INPUT: {
		if (!key_text_input_active())
			return std::nullopt;
		/* The replay types the key's own character after its key down. */
		const std::string_view text{event.text.text ? event.text.text : ""};
		const auto typed{std::exchange(state.typed, SDLK_UNKNOWN)};
		if (typed >= 32 && typed <= 126 && text.size() == 1 &&
		    text.front() == static_cast<char>(typed))
			return std::nullopt;
		return std::unexpected{unrecorded_input::text};
	}
#endif
	case SDL_MOUSEMOTION: {
#if SDL_MAJOR_VERSION == 3
		const auto [x, y]{state.motion.take(event.motion.xrel, event.motion.yrel)};
#else
		const auto x{event.motion.xrel};
		const auto y{event.motion.yrel};
#endif
		return input_event{
		    .tick = tick, .action = input_action::mouse_move, .key = {}, .x = x, .y = y};
	}
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP: {
		/* mouse_button_handler maps SDL's buttons 1 to 17. */
		if (event.button.button < 1 || event.button.button > 17)
			return std::nullopt;
#if SDL_MAJOR_VERSION == 3
		const bool down{event.button.down};
#else
		const bool down{event.button.state != SDL_RELEASED};
#endif
		return input_event{.tick = tick,
		                   .action = down ? input_action::mouse_down : input_action::mouse_up,
		                   .key = {},
		                   .x = 0,
		                   .y = 0,
		                   .button = input_button{event.button.button}};
	}
	case SDL_MOUSEWHEEL: {
		const auto steps{sdl::wheel_steps(event.wheel)};
		if (steps == 0)
			return std::nullopt;
		return input_event{
		    .tick = tick, .action = input_action::wheel, .key = {}, .x = 0, .y = steps};
	}
	case SDL_QUIT:
		return std::unexpected{unrecorded_input::quit};
	default:
		/* SDL2 and SDL3 number every joystick and gamepad event from 0x600 to 0x6ff. */
		if (event.type >= SDL_JOYAXISMOTION && event.type < SDL_JOYAXISMOTION + 0x100)
			return std::unexpected{unrecorded_input::joystick};
		return std::nullopt;
	}
}
#endif

}

#endif

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <optional>

namespace dcx::sdl {

/* The gamepad's virtual button layout, the numbers saved pilot bindings
 * hold (docs/agents/sdl-input.md, "Saved bindings across SDL2 and SDL3").
 * SDL2's layout, which every pilot file written so far uses: buttons
 * 0-20 are SDL2's 21 gamepad buttons (A to TOUCHPAD), then two
 * axis-buttons per axis, positive first, 21-32.  SDL3 has 26 buttons
 * (SOUTH to MISC6, the first 21 in SDL2's order); its five new ones,
 * MISC2 to MISC6, come after the axis-buttons, 33-37, so no saved number
 * moves.  An SDL2 build checks gamepad_button_count against its header's
 * SDL_CONTROLLER_BUTTON_MAX (common/sdl/unittest/joystick_events.cpp).
 */
constexpr unsigned gamepad_button_count{21};
constexpr unsigned gamepad_axis_count{6};
constexpr unsigned gamepad_axis_button_start{gamepad_button_count};
constexpr unsigned gamepad_extra_button_start{gamepad_axis_button_start + 2 * gamepad_axis_count};
constexpr unsigned gamepad_extra_button_count{5};
constexpr unsigned gamepad_virtual_button_count{gamepad_extra_button_start +
                                                gamepad_extra_button_count};

/* The axis-button of `axis` (SDL's axis number) moved positive or negative. */
[[nodiscard]]
constexpr unsigned gamepad_axis_button(const unsigned axis, const bool negative)
{
	return gamepad_axis_button_start + (axis * 2) + (negative ? 1u : 0u);
}

/* The virtual button of SDL's gamepad button `button`; nullopt past
 * SDL3's 26.
 */
[[nodiscard]]
constexpr std::optional<unsigned> gamepad_virtual_button(const unsigned button)
{
	if (button < gamepad_button_count)
		return button;
	if (button < gamepad_button_count + gamepad_extra_button_count)
		return gamepad_extra_button_start + (button - gamepad_button_count);
	return std::nullopt;
}

}

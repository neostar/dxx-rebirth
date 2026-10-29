/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/gamepad_layout.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth gamepad layout
#include <boost/test/unit_test.hpp>

namespace {

/* What each saved gamepad number 0-32 means in SDL2's layout: the SDL2 button (its enum
 * order in SDL_gamecontroller.h, 2.30.0) or the axis-button, sign and axis.  A pilot file
 * written by an SDL2 build holds these numbers.
 */
constexpr std::array<std::string_view, 33> sdl2_meaning{{
    "A",
    "B",
    "X",
    "Y",
    "BACK",
    "GUIDE",
    "START",
    "LEFTSTICK",
    "RIGHTSTICK",
    "LEFTSHOULDER",
    "RIGHTSHOULDER",
    "DPAD_UP",
    "DPAD_DOWN",
    "DPAD_LEFT",
    "DPAD_RIGHT",
    "MISC1",
    "PADDLE1",
    "PADDLE2",
    "PADDLE3",
    "PADDLE4",
    "TOUCHPAD",
    "+LEFTX",
    "-LEFTX",
    "+LEFTY",
    "-LEFTY",
    "+RIGHTX",
    "-RIGHTX",
    "+RIGHTY",
    "-RIGHTY",
    "+TRIGGERLEFT",
    "-TRIGGERLEFT",
    "+TRIGGERRIGHT",
    "-TRIGGERRIGHT",
}};

constexpr std::array<std::string_view, 21> button_names{{
    "A",
    "B",
    "X",
    "Y",
    "BACK",
    "GUIDE",
    "START",
    "LEFTSTICK",
    "RIGHTSTICK",
    "LEFTSHOULDER",
    "RIGHTSHOULDER",
    "DPAD_UP",
    "DPAD_DOWN",
    "DPAD_LEFT",
    "DPAD_RIGHT",
    "MISC1",
    "PADDLE1",
    "PADDLE2",
    "PADDLE3",
    "PADDLE4",
    "TOUCHPAD",
}};

constexpr std::array<std::string_view, 6> axis_names{{
    "LEFTX",
    "LEFTY",
    "RIGHTX",
    "RIGHTY",
    "TRIGGERLEFT",
    "TRIGGERRIGHT",
}};

}

/* Test that the layout gives every SDL2 button and axis-button the number an SDL2 build saved
 * for it, 0-32: the axis-buttons start at SDL2's 21, not SDL3's 26, so the D1 default "Fire
 * primary" (+TRIGGERRIGHT) stays 31 and does not become -RIGHTX.
 */
BOOST_AUTO_TEST_CASE(numbers_0_to_32_keep_sdl2s_meaning)
{
	std::array<std::string, 33> meaning{};
	for (unsigned b{0}; b < button_names.size(); ++b)
	{
		const auto n{dcx::sdl::gamepad_virtual_button(b)};
		BOOST_REQUIRE(n.has_value());
		BOOST_REQUIRE(n.value() < meaning.size());
		meaning[n.value()] = button_names[b];
	}
	for (unsigned a{0}; a < axis_names.size(); ++a)
		for (const bool negative : {false, true})
		{
			const auto n{dcx::sdl::gamepad_axis_button(a, negative)};
			BOOST_REQUIRE(n < meaning.size());
			meaning[n] = (negative ? "-" : "+") + std::string{axis_names[a]};
		}
	for (std::size_t i{0}; i < meaning.size(); ++i)
		BOOST_TEST(meaning[i] == sdl2_meaning[i], "number " << i);
	BOOST_TEST(dcx::sdl::gamepad_axis_button(5, false) == 31u);
}

/* Test that SDL3's five new buttons, MISC2 to MISC6 (21-25), come after the axis-buttons, at
 * 33-37, and that a number past SDL3's 26 buttons has no place.
 */
BOOST_AUTO_TEST_CASE(sdl3s_new_buttons_follow_the_axis_buttons)
{
	for (unsigned b{21}; b < 26; ++b)
		BOOST_TEST(dcx::sdl::gamepad_virtual_button(b).value_or(0) == 12 + b);
	BOOST_TEST(!dcx::sdl::gamepad_virtual_button(26).has_value());
	BOOST_TEST(dcx::sdl::gamepad_virtual_button_count == 38u);
}

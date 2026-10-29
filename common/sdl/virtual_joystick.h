/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "common/debug/input_script.h"
#include "common/sdl/dxx-sdl.h"
#include "common/sdl/gamepad_layout.h"
#include "common/sdl/sdl_deleter.h"

namespace dcx::sdl {

static_assert(SDL_HAT_CENTERED == input_hat_centered && SDL_HAT_UP == input_hat_up &&
              SDL_HAT_RIGHT == input_hat_right && SDL_HAT_DOWN == input_hat_down &&
              SDL_HAT_LEFT == input_hat_left && SDL_HAT_RIGHTUP == input_hat_up_right &&
              SDL_HAT_RIGHTDOWN == input_hat_down_right &&
              SDL_HAT_LEFTDOWN == input_hat_down_left && SDL_HAT_LEFTUP == input_hat_up_left);

/* The input script's gamepad has the controls the game's layout numbers. */
static_assert(input_controls_of(input_device_shape::gamepad).axes == gamepad_axis_count &&
              input_controls_of(input_device_shape::gamepad).buttons == gamepad_button_count);

/* A virtual joystick's shape: SDL's gamepad type, which SDL maps itself
 * (its buttons and axes in SDL's gamepad order), or a flight stick.
 */
struct virtual_joystick_shape
{
	bool gamepad;
	std::uint8_t axes;
	std::uint8_t buttons;
	std::uint8_t hats;
	const char *name;
};

/* The shape of an input script's device (input_device_shape). */
[[nodiscard]]
constexpr virtual_joystick_shape virtual_shape_of(const input_device_shape shape)
{
	const auto controls{input_controls_of(shape)};
	const bool gamepad{shape == input_device_shape::gamepad};
	return {.gamepad = gamepad,
	        .axes = controls.axes,
	        .buttons = controls.buttons,
	        .hats = controls.hats,
	        .name = gamepad ? "DXX virtual gamepad" : "DXX virtual flight stick"};
}

/* A virtual joystick on SDL2's or SDL3's virtual driver (R12): SDL sees it
 * plugged in, with its added events, from attach until this is destroyed,
 * which unplugs it.  It holds its own SDL_Joystick to set the inputs, as
 * SDL's virtual-joystick API needs; SDL sends the events on its next
 * joystick update (SDL_PumpEvents).
 */
class virtual_joystick
{
	sdl_unique_ptr<SDL_Joystick, &SDL_JoystickClose> handle;
	SDL_JoystickID id;
	virtual_joystick(SDL_Joystick *const j, const SDL_JoystickID i) : handle{j}, id{i}
	{}
	void detach()
	{
		if (!handle)
			return;
		handle.reset();
#if SDL_MAJOR_VERSION == 2
		/* SDL2 detaches by device index, which a removal before it shifts. */
		for (int i{0}, n{SDL_NumJoysticks()}; i < n; ++i)
			if (SDL_JoystickGetDeviceInstanceID(i) == id)
			{
				SDL_JoystickDetachVirtual(i);
				return;
			}
#elif SDL_MAJOR_VERSION == 3
		SDL_DetachVirtualJoystick(id);
#endif
	}

  public:
	[[nodiscard]]
	static std::optional<virtual_joystick> attach(const virtual_joystick_shape &shape)
	{
		SDL_VirtualJoystickDesc desc{};
#if SDL_MAJOR_VERSION == 2
		desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
		desc.type = static_cast<Uint16>(shape.gamepad ? SDL_JOYSTICK_TYPE_GAMECONTROLLER
		                                              : SDL_JOYSTICK_TYPE_FLIGHT_STICK);
#elif SDL_MAJOR_VERSION == 3
		/* SDL_INIT_INTERFACE's version stamp; desc{} zeroed the rest. */
		desc.version = sizeof(desc);
		desc.type = static_cast<Uint16>(shape.gamepad ? SDL_JOYSTICK_TYPE_GAMEPAD
		                                              : SDL_JOYSTICK_TYPE_FLIGHT_STICK);
#endif
		desc.naxes = shape.axes;
		desc.nbuttons = shape.buttons;
		desc.nhats = shape.hats;
		desc.name = shape.name;
#if SDL_MAJOR_VERSION == 2
		const int index{SDL_JoystickAttachVirtualEx(&desc)};
		if (index < 0)
			return std::nullopt;
		SDL_Joystick *const j{SDL_JoystickOpen(index)};
		if (!j)
		{
			SDL_JoystickDetachVirtual(index);
			return std::nullopt;
		}
		return virtual_joystick{j, SDL_JoystickInstanceID(j)};
#elif SDL_MAJOR_VERSION == 3
		const SDL_JoystickID i{SDL_AttachVirtualJoystick(&desc)};
		if (!i)
			return std::nullopt;
		SDL_Joystick *const j{SDL_OpenJoystick(i)};
		if (!j)
		{
			SDL_DetachVirtualJoystick(i);
			return std::nullopt;
		}
		return virtual_joystick{j, i};
#endif
	}
	virtual_joystick(virtual_joystick &&other) noexcept
	    : handle{std::move(other.handle)}, id{std::exchange(other.id, {})}
	{}
	virtual_joystick &operator=(virtual_joystick &&other) noexcept
	{
		if (this != &other)
		{
			detach();
			handle = std::move(other.handle);
			id = std::exchange(other.id, {});
		}
		return *this;
	}
	virtual_joystick(const virtual_joystick &) = delete;
	virtual_joystick &operator=(const virtual_joystick &) = delete;
	~virtual_joystick()
	{
		detach();
	}
	/* SDL's instance ID of the device; a reattached device gets a new one. */
	[[nodiscard]]
	SDL_JoystickID instance_id() const
	{
		return id;
	}
	/* False when SDL refuses the input (SDL_GetError says why). */
	bool set_axis(const int axis, const Sint16 value)
	{
#if SDL_MAJOR_VERSION == 2
		return SDL_JoystickSetVirtualAxis(handle.get(), axis, value) == 0;
#elif SDL_MAJOR_VERSION == 3
		return SDL_SetJoystickVirtualAxis(handle.get(), axis, value);
#endif
	}
	bool set_button(const int button, const bool down)
	{
#if SDL_MAJOR_VERSION == 2
		return SDL_JoystickSetVirtualButton(handle.get(), button,
		                                    down ? SDL_PRESSED : SDL_RELEASED) == 0;
#elif SDL_MAJOR_VERSION == 3
		return SDL_SetJoystickVirtualButton(handle.get(), button, down);
#endif
	}
	bool set_hat(const int hat, const Uint8 value)
	{
#if SDL_MAJOR_VERSION == 2
		return SDL_JoystickSetVirtualHat(handle.get(), hat, value) == 0;
#elif SDL_MAJOR_VERSION == 3
		return SDL_SetJoystickVirtualHat(handle.get(), hat, value);
#endif
	}
};

/* The devices an input script attaches, by slot. */
class virtual_joystick_slots
{
	std::array<std::optional<virtual_joystick>, input_device_count> slots;

  public:
	/* Play one of the script's joystick lines (parse_input_script checked
	 * its slot, shape and numbers); false when SDL refuses it.
	 */
	bool play(const input_event &e)
	{
		auto &slot{slots[e.device]};
		switch (e.action)
		{
		case input_action::joy_attach:
			slot = virtual_joystick::attach(virtual_shape_of(e.shape));
			return slot.has_value();
		case input_action::joy_detach:
			slot.reset();
			return true;
		case input_action::joy_axis:
			return slot && slot->set_axis(e.control, static_cast<Sint16>(e.value));
		case input_action::joy_button_down:
		case input_action::joy_button_up:
			return slot && slot->set_button(e.control, e.action == input_action::joy_button_down);
		case input_action::joy_hat:
			return slot && slot->set_hat(e.control, static_cast<Uint8>(e.value));
		default:
			return false;
		}
	}
	/* The device in `slot`, if one is attached. */
	[[nodiscard]]
	const std::optional<virtual_joystick> &operator[](const std::size_t slot) const
	{
		return slots.at(slot);
	}
};

}

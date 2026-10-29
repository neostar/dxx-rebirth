/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <span>
#include <string_view>

namespace dcx {

/* How the debug harness creates the game window. */
enum class harness_window : std::uint8_t
{
	shown,
	/* SDL_WINDOW_HIDDEN: nothing on screen, no focus taken. */
	hidden,
};

/* DXX_DBG_HIDDEN as the debug harness read it. */
struct harness_hidden
{
	/* The variable's value; nullptr when it is unset. */
	const char *value{nullptr};
	/* hidden for "1", shown when unset, nullopt for any other value. */
	std::optional<harness_window> window{};
};

/* Read DXX_DBG_HIDDEN from the environment.  Header-only, as
 * frame_time_harness.h is, for the same reason.
 */
[[nodiscard]]
inline harness_hidden read_harness_hidden()
{
	const char *const value{std::getenv("DXX_DBG_HIDDEN")};
	if (value == nullptr)
		return {.value = nullptr, .window = harness_window::shown};
	if (std::string_view{value} == "1")
		return {.value = value, .window = harness_window::hidden};
	return {.value = value, .window = std::nullopt};
}

/* Whether DXX_DBG_HIDDEN asks for anything: a build that cannot create its
 * window hidden (SDL 1, or no OpenGL) refuses any value rather than show a
 * window the run expected to stay hidden.
 */
[[nodiscard]]
constexpr bool harness_hidden_requested(const harness_hidden &hidden)
{
	return hidden.value != nullptr;
}

/* Whether the game may grab the mouse and keyboard and turn on relative
 * mouse mode (event_change_focus): never in a run that asked to be
 * hidden, with any value, so a hidden run cannot take the owner's real
 * cursor, whatever focus SDL gives its window.
 */
[[nodiscard]]
constexpr bool harness_allows_grab(const harness_hidden &hidden)
{
	return !harness_hidden_requested(hidden);
}

/* SDL's joystick backends that open real devices on the Mac: HIDAPI, IOKit
 * and the GameController framework.  Their hint names are the same in SDL2
 * (sdl2-compat passes them on) and SDL3.  SDL's virtual driver, which input
 * scripts attach to, is not among them.
 */
inline constexpr std::array<const char *, 3> harness_real_joystick_hints{{
    "SDL_JOYSTICK_HIDAPI",
    "SDL_JOYSTICK_IOKIT",
    "SDL_JOYSTICK_MFI",
}};

/* The hints the harness sets to "0" before SDL_Init: a run that asked to be
 * hidden, with any value, turns the real backends off, so it never opens
 * the owner's controller (opening one changes a HIDAPI pad's LEDs and
 * report mode, and its input would reach the game); a shown run sets none.
 */
[[nodiscard]]
constexpr std::span<const char *const> harness_joystick_hints_off(const harness_hidden &hidden)
{
	if (harness_hidden_requested(hidden))
		return harness_real_joystick_hints;
	return {};
}

/* Where the debug harness sends a message that would open a message box. */
enum class harness_report : std::uint8_t
{
	message_box,
	/* stderr: a run that asked to be hidden opens no dialog either. */
	standard_error,
};

/* Any DXX_DBG_HIDDEN value asks for a hidden run, so even the error that
 * refuses a bad value reaches stderr rather than a dialog.
 */
[[nodiscard]]
constexpr harness_report harness_report_for(const harness_hidden &hidden)
{
	return harness_hidden_requested(hidden) ? harness_report::standard_error
	                                        : harness_report::message_box;
}

/* Write message, which need not end in a NUL, and a newline to stream. */
inline void write_harness_report(std::FILE *const stream, const std::span<const char> message)
{
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-libc-call): a span's own size
	std::fwrite(message.data(), 1, message.size(), stream);
	std::fputc('\n', stream);
	std::fflush(stream);
}

}

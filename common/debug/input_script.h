/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace dcx {

enum class input_action : std::uint8_t
{
	key_down,
	key_up,
	/* Relative mouse motion of x, y. */
	mouse_move,
	/* A mouse button, SDL's number `button`, goes down or comes up. */
	mouse_down,
	mouse_up,
	/* y steps of the wheel, positive away from the user. */
	wheel,
	/* A virtual joystick of shape `shape` is attached in slot `device`, or
	 * the one there is detached.
	 */
	joy_attach,
	joy_detach,
	/* Axis `control` of the device in slot `device` moves to `value`. */
	joy_axis,
	/* Its button `control` goes down or comes up. */
	joy_button_down,
	joy_button_up,
	/* Its hat `control` points to `value`, SDL's hat bits (input_hat_*). */
	joy_hat,
	/* End the run: the game exits with status 0, closing nothing, so a
	 * scene ends at the same frame whatever screen it is on.
	 */
	end,
};

/* A harness tick: the number of a call of event_process, from 0. */
enum class harness_tick : std::uint32_t
{
};

/* The game has 256 key codes (key_properties in key.h). */
constexpr std::size_t input_key_count{256};

/* A key's code, its index in key_properties: one byte, so always a valid
 * index into a table of input_key_count keys.
 */
enum class input_key : std::uint8_t
{
};

/* A mouse button, SDL's number for it: 1 left, 2 middle, 3 right, up to
 * 17, the buttons mouse_button_handler maps; 0 for the events of no button.
 */
enum class input_button : std::uint8_t
{
};

/* The shape of a virtual joystick an input script attaches: SDL's virtual
 * gamepad, with SDL2's 21 buttons and its 6 axes (the layout saved bindings
 * use), or a flight stick with 3 axes, 32 buttons and 1 hat (sdl-input.md,
 * "Injected events and virtual devices").
 */
enum class input_device_shape : std::uint8_t
{
	none,
	gamepad,
	stick,
};

/* The slots a script attaches devices in, as many as the game's default
 * DXX_MAX_JOYSTICKS.
 */
constexpr std::size_t input_device_count{8};

/* The axes, buttons and hats a device of `shape` has. */
struct input_device_controls
{
	std::uint8_t axes;
	std::uint8_t buttons;
	std::uint8_t hats;
};

[[nodiscard]]
constexpr input_device_controls input_controls_of(const input_device_shape shape)
{
	switch (shape)
	{
	case input_device_shape::gamepad:
		return {.axes = 6, .buttons = 21, .hats = 0};
	case input_device_shape::stick:
		return {.axes = 3, .buttons = 32, .hats = 1};
	case input_device_shape::none:
		break;
	}
	return {.axes = 0, .buttons = 0, .hats = 0};
}

/* A hat's directions, the values of SDL's SDL_HAT_* bits in SDL2 and SDL3. */
constexpr std::int32_t input_hat_centered{0};
constexpr std::int32_t input_hat_up{1};
constexpr std::int32_t input_hat_right{2};
constexpr std::int32_t input_hat_down{4};
constexpr std::int32_t input_hat_left{8};
constexpr std::int32_t input_hat_up_right{3};
constexpr std::int32_t input_hat_down_right{6};
constexpr std::int32_t input_hat_down_left{12};
constexpr std::int32_t input_hat_up_left{9};

/* One input event of an input script, due at harness tick `tick`.  `key`
 * is a key event's code, 0 for the others; `x` and `y` a mouse move's
 * motion, `y` the wheel's steps; `button` a mouse button event's button.
 * A joystick event's `device` is its slot, `shape` an attach's shape,
 * `control` the axis, button or hat number from 0, `value` the axis value or
 * the hat's direction.
 */
struct input_event
{
	harness_tick tick;
	input_action action;
	input_key key;
	std::int32_t x{0};
	std::int32_t y{0};
	input_button button{};
	std::uint8_t device{0};
	input_device_shape shape{input_device_shape::none};
	std::uint8_t control{0};
	std::int32_t value{0};
	bool operator==(const input_event &) const = default;
};

/* Why a script was rejected, at its 1-based line `line`. */
struct input_script_error
{
	std::size_t line;
	std::string message;
};

namespace input_script_detail {

constexpr std::string_view blanks{" \t\r"};

/* The next blank-separated word of `rest`, which loses it; empty at the end. */
[[nodiscard]]
constexpr std::string_view next_word(std::string_view &rest)
{
	const auto start{rest.find_first_not_of(blanks)};
	if (start == std::string_view::npos)
	{
		rest = {};
		return {};
	}
	rest.remove_prefix(start);
	const auto end{std::min(rest.find_first_of(blanks), rest.size())};
	const auto word{rest.substr(0, end)};
	rest.remove_prefix(end);
	return word;
}

[[nodiscard]]
constexpr std::optional<harness_tick> parse_tick(const std::string_view word)
{
	std::uint32_t tick{0};
	const auto first{std::to_address(word.begin())};
	const auto end{std::to_address(word.end())};
	if (const auto [ptr, ec]{std::from_chars(first, end, tick)}; ec != std::errc{} || ptr != end)
		return std::nullopt;
	return harness_tick{tick};
}

/* The whole of `word` as a number of type T, or nothing. */
template <typename T>
[[nodiscard]]
constexpr std::optional<T> parse_whole(const std::string_view word)
{
	T value{0};
	const auto first{std::to_address(word.begin())};
	const auto end{std::to_address(word.end())};
	if (const auto [ptr, ec]{std::from_chars(first, end, value)}; ec != std::errc{} || ptr != end)
		return std::nullopt;
	return value;
}

/* A mouse_move's motion or the wheel's steps: a whole number from -32767 to
 * 32767, so the game's sums of them stay far from overflow.
 */
[[nodiscard]]
constexpr std::optional<std::int32_t> parse_amount(const std::string_view word)
{
	const auto amount{parse_whole<std::int32_t>(word)};
	if (!amount || amount.value() < -32767 || amount.value() > 32767)
		return std::nullopt;
	return amount;
}

/* A mouse button: left, middle or right, or SDL's number from 1 to 17, the
 * buttons mouse_button_handler maps.
 */
[[nodiscard]]
constexpr std::optional<input_button> parse_button(const std::string_view word)
{
	if (word == "left")
		return input_button{1};
	if (word == "middle")
		return input_button{2};
	if (word == "right")
		return input_button{3};
	const auto button{parse_whole<std::uint8_t>(word)};
	if (!button || button.value() < 1 || button.value() > 17)
		return std::nullopt;
	return input_button{button.value()};
}

/* A hat direction's name, as SDL's hat bits. */
[[nodiscard]]
constexpr std::optional<std::int32_t> parse_hat_direction(const std::string_view word)
{
	struct named_direction
	{
		std::string_view name;
		std::int32_t bits;
	};
	constexpr std::array<named_direction, 9> directions{{
	    {.name = "centered", .bits = input_hat_centered},
	    {.name = "up", .bits = input_hat_up},
	    {.name = "right", .bits = input_hat_right},
	    {.name = "down", .bits = input_hat_down},
	    {.name = "left", .bits = input_hat_left},
	    {.name = "up_right", .bits = input_hat_up_right},
	    {.name = "down_right", .bits = input_hat_down_right},
	    {.name = "down_left", .bits = input_hat_down_left},
	    {.name = "up_left", .bits = input_hat_up_left},
	}};
	const auto i{std::ranges::find(directions, word, &named_direction::name)};
	if (i == directions.end())
		return std::nullopt;
	return i->bits;
}

/* One joystick line after its action word: the event, or why it is
 * rejected.  `shapes` holds each slot's attached shape, which an attach
 * sets and a detach clears.
 */
[[nodiscard]]
inline std::expected<input_event, std::string>
parse_joystick_line(const harness_tick tick, const std::string_view action, std::string_view rest,
                    std::array<input_device_shape, input_device_count> &shapes)
{
	/* The next word as a whole number from `low` to `high`; nullopt for anything else. */
	const auto next_number{[&rest]<typename T>(const T low, const T high) -> std::optional<T> {
		const auto number{parse_whole<T>(next_word(rest))};
		if (!number || number.value() < low || number.value() > high)
			return std::nullopt;
		return number;
	}};
	const auto slot{next_number(std::uint8_t{0}, std::uint8_t{input_device_count - 1})};
	if (!slot)
		return std::unexpected{std::string{action} + " needs a slot from 0 to 7"};
	auto &shape{shapes[slot.value()]};
	input_event event{.tick = tick, .action = input_action::end, .key = {}, .device = slot.value()};
	const auto done{[&rest, &event, action]() -> std::expected<input_event, std::string> {
		if (!next_word(rest).empty())
			return std::unexpected{"too many words for " + std::string{action}};
		return event;
	}};
	if (action == "joy_attach")
	{
		const auto word{next_word(rest)};
		if (shape != input_device_shape::none)
			return std::unexpected{"slot " + std::to_string(slot.value()) + " is attached already"};
		if (word == "gamepad")
			event.shape = input_device_shape::gamepad;
		else if (word == "stick")
			event.shape = input_device_shape::stick;
		else
			return std::unexpected{std::string{"joy_attach needs a shape: gamepad or stick"}};
		event.action = input_action::joy_attach;
		shape = event.shape;
		return done();
	}
	if (shape == input_device_shape::none)
		return std::unexpected{"slot " + std::to_string(slot.value()) + " has no device attached"};
	const auto controls{input_controls_of(shape)};
	/* The number of the last of `count` controls; every shape has at least one of each it has. */
	const auto last{[](const std::uint8_t count) { return static_cast<std::uint8_t>(count - 1); }};
	if (action == "joy_detach")
	{
		event.action = input_action::joy_detach;
		shape = input_device_shape::none;
		return done();
	}
	if (action == "joy_axis")
	{
		const auto axis{next_number(std::uint8_t{0}, last(controls.axes))};
		if (!axis)
			return std::unexpected{"joy_axis needs an axis from 0 to " +
			                       std::to_string(controls.axes - 1)};
		const auto value{next_number(std::int32_t{-32768}, std::int32_t{32767})};
		if (!value)
			return std::unexpected{std::string{"joy_axis needs a value from -32768 to 32767"}};
		event.action = input_action::joy_axis;
		event.control = axis.value();
		event.value = value.value();
		return done();
	}
	if (action == "joy_button")
	{
		const auto button{next_number(std::uint8_t{0}, last(controls.buttons))};
		if (!button)
			return std::unexpected{"joy_button needs a button from 0 to " +
			                       std::to_string(controls.buttons - 1)};
		const auto state{next_word(rest)};
		if (state == "down")
			event.action = input_action::joy_button_down;
		else if (state == "up")
			event.action = input_action::joy_button_up;
		else
			return std::unexpected{std::string{"joy_button needs down or up"}};
		event.control = button.value();
		return done();
	}
	/* joy_hat */
	if (controls.hats == 0)
		return std::unexpected{"slot " + std::to_string(slot.value()) + "'s device has no hat"};
	const auto hat{next_number(std::uint8_t{0}, last(controls.hats))};
	if (!hat)
		return std::unexpected{"joy_hat needs a hat from 0 to " +
		                       std::to_string(controls.hats - 1)};
	const auto direction{parse_hat_direction(next_word(rest))};
	if (!direction)
		return std::unexpected{std::string{"joy_hat needs a direction: centered, up, right, down, "
		                                   "left, up_right, down_right, down_left or up_left"}};
	event.action = input_action::joy_hat;
	event.control = hat.value();
	event.value = direction.value();
	return done();
}

}

/* Parse an input script: one event per line,
 *
 *     <tick> down <key>             the key goes down at harness tick <tick>
 *     <tick> up <key>               the key comes up
 *     <tick> press <key>            down at <tick>, up at <tick> + 1
 *     <tick> mouse_move <dx> <dy>   relative mouse motion, whole numbers
 *     <tick> mouse_down <button>    a mouse button goes down: left, middle,
 *                                   right, or SDL's button number 1 to 17
 *     <tick> mouse_up <button>      it comes up
 *     <tick> wheel <steps>          wheel steps, positive away from the user
 *     <tick> joy_attach <slot> <shape>  a virtual joystick in slot 0 to 7:
 *                                   gamepad or stick (input_device_shape)
 *     <tick> joy_detach <slot>      it is unplugged
 *     <tick> joy_axis <slot> <axis> <value>   an axis, from 0, moves to
 *                                   a value from -32768 to 32767
 *     <tick> joy_button <slot> <button> down|up   a button, from 0
 *     <tick> joy_hat <slot> <hat> <direction>     a hat, from 0, points
 *                                   centered, up, right, down, left,
 *                                   up_right, down_right, down_left or up_left
 *     <tick> end                    the run ends
 *
 * with words separated by blanks, `#` starting a comment, blank lines
 * skipped, and ticks never going back from one line to the next.  Exactly
 * one end line ends the script, and its event comes last: a hidden run
 * without one would never end.  A joystick line names an attached slot,
 * and an axis, button or hat its shape has (input_controls_of).
 * `key_of` maps a key's name to its code, nullopt for an unknown name.
 * The events come back ordered by tick, in script order within a tick.
 */
template <typename KeyOf>
requires(std::is_invocable_r_v<std::optional<input_key>, KeyOf &, std::string_view>)
[[nodiscard]]
std::expected<std::vector<input_event>, input_script_error>
parse_input_script(std::string_view text, KeyOf &&key_of)
{
	using input_script_detail::next_word;
	std::vector<input_event> events;
	harness_tick last_tick{0};
	std::size_t line_number{0};
	/* The end line's number; 0 before it. */
	std::size_t end_line{0};
	/* Each slot's attached device, in the order of the lines. */
	std::array<input_device_shape, input_device_count> shapes{};
	const auto fail{[&line_number](std::string message) {
		return std::unexpected{input_script_error{.line = std::max(line_number, std::size_t{1}),
		                                          .message = std::move(message)}};
	}};
	while (!text.empty())
	{
		++line_number;
		const auto line_end{std::min(text.find('\n'), text.size())};
		std::string_view rest{text.substr(0, line_end)};
		text.remove_prefix(std::min(line_end + 1, text.size()));
		rest = rest.substr(0, std::min(rest.find('#'), rest.size()));
		const auto tick_word{next_word(rest)};
		if (tick_word.empty())
			continue;
		if (end_line != 0)
			return fail("nothing may follow the end line");
		const auto tick{input_script_detail::parse_tick(tick_word)};
		if (!tick)
			return fail("expected a tick from 0 to 4294967295, not \"" + std::string{tick_word} +
			            '"');
		if (tick.value() < last_tick)
			return fail("tick " + std::string{tick_word} + " is before the line above");
		last_tick = tick.value();
		const auto action{next_word(rest)};
		if (action == "end")
		{
			if (!next_word(rest).empty())
				return fail("end takes no key");
			events.push_back({.tick = tick.value(), .action = input_action::end, .key = {}});
			end_line = line_number;
			continue;
		}
		if (action == "joy_attach" || action == "joy_detach" || action == "joy_axis" ||
		    action == "joy_button" || action == "joy_hat")
		{
			auto event{
			    input_script_detail::parse_joystick_line(tick.value(), action, rest, shapes)};
			if (!event)
				return fail(std::move(event).error());
			events.push_back(event.value());
			continue;
		}
		if (action == "mouse_move")
		{
			const auto dx{input_script_detail::parse_amount(next_word(rest))};
			const auto dy{input_script_detail::parse_amount(next_word(rest))};
			if (!dx || !dy)
				return fail("mouse_move needs two whole numbers from -32767 to 32767");
			if (!next_word(rest).empty())
				return fail("mouse_move takes two numbers");
			events.push_back({.tick = tick.value(),
			                  .action = input_action::mouse_move,
			                  .key = {},
			                  .x = dx.value(),
			                  .y = dy.value(),
			                  .button = {}});
			continue;
		}
		if (action == "mouse_down" || action == "mouse_up")
		{
			const auto button{input_script_detail::parse_button(next_word(rest))};
			if (!button)
				return fail(std::string{action} +
				            " needs a button: left, middle, right or 1 to 17");
			if (!next_word(rest).empty())
				return fail("one button per line");
			events.push_back({.tick = tick.value(),
			                  .action = action == "mouse_down" ? input_action::mouse_down
			                                                   : input_action::mouse_up,
			                  .key = {},
			                  .x = 0,
			                  .y = 0,
			                  .button = button.value()});
			continue;
		}
		if (action == "wheel")
		{
			const auto steps{input_script_detail::parse_amount(next_word(rest))};
			if (!steps || steps.value() == 0)
				return fail("wheel needs a whole number of steps from -32767 to 32767, not 0");
			if (!next_word(rest).empty())
				return fail("wheel takes one number");
			events.push_back({.tick = tick.value(),
			                  .action = input_action::wheel,
			                  .key = {},
			                  .x = 0,
			                  .y = steps.value(),
			                  .button = {}});
			continue;
		}
		if (action != "down" && action != "up" && action != "press")
			return fail(
			    "expected down, up, press, mouse_move, mouse_down, mouse_up, wheel, joy_attach, "
			    "joy_detach, joy_axis, joy_button, joy_hat or end, not \"" +
			    std::string{action} + '"');
		const auto name{next_word(rest)};
		if (name.empty())
			return fail(std::string{action} + " needs a key name");
		if (!next_word(rest).empty())
			return fail("one key per line");
		const std::optional<input_key> key{key_of(name)};
		if (!key)
			return fail("unknown key \"" + std::string{name} + '"');
		if (action == "press")
		{
			const auto down_tick{std::to_underlying(tick.value())};
			if (down_tick == std::numeric_limits<std::uint32_t>::max())
				return fail("press needs a tick after its own");
			events.push_back(
			    {.tick = tick.value(), .action = input_action::key_down, .key = key.value()});
			events.push_back({.tick = harness_tick{down_tick + 1},
			                  .action = input_action::key_up,
			                  .key = key.value()});
		}
		else
			events.push_back(
			    {.tick = tick.value(),
				 .action = action == "down" ? input_action::key_down : input_action::key_up,
				 .key = key.value()});
	}
	if (end_line == 0)
		return fail("the script needs an end line, which ends the run");
	std::ranges::stable_sort(events, {}, &input_event::tick);
	if (events.back().action != input_action::end)
	{
		line_number = end_line;
		return fail("end comes before the up of a press: move it a tick later");
	}
	return events;
}

/* The name of the scene an input script at `path` drives: the file's name
 * without its directory and extension.
 */
[[nodiscard]]
inline std::string input_scene_name(std::string_view path)
{
	if (const auto slash{path.rfind('/')}; slash != std::string_view::npos)
		path.remove_prefix(slash + 1);
	return std::string{path.substr(0, path.rfind('.'))};
}

/* Hands out a parsed script's events, a harness tick at a time. */
class input_script_player
{
	std::vector<input_event> events;
	std::size_t next{0};
	harness_tick tick{0};

  public:
	explicit input_script_player(std::vector<input_event> e) : events{std::move(e)}
	{}
	/* The events due at the next harness tick, the first call's tick 0. */
	[[nodiscard]]
	std::span<const input_event> next_tick()
	{
		const auto now{tick};
		tick = harness_tick{std::to_underlying(now) + 1};
		const std::span<const input_event> rest{std::span{events}.subspan(next)};
		const auto due{static_cast<std::size_t>(
		    std::ranges::find_if(rest, [now](const input_event &e) { return e.tick > now; }) -
		    rest.begin())};
		next += due;
		return rest.first(due);
	}
};

/* The code of the key named `name` in `keys`, a table shaped like
 * key_properties (key.h), whose index is the key's code and whose
 * key_text the key's name; nullopt for an empty or unknown name.
 */
template <typename Key>
[[nodiscard]]
std::optional<input_key> input_key_named(const std::array<Key, input_key_count> &keys,
                                         const std::string_view name)
{
	if (name.empty())
		return std::nullopt;
	const auto i{std::ranges::find(keys, name, &Key::key_text)};
	if (i == keys.end())
		return std::nullopt;
	return input_key{static_cast<std::uint8_t>(i - keys.begin())};
}

/* The entry of `keys` for the key `key`; its type keeps the index in range. */
template <typename Key>
[[nodiscard]]
const Key &input_key_entry(const std::array<Key, input_key_count> &keys, const input_key key)
{
	return keys[std::to_underlying(key)];
}

}

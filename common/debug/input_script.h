/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <algorithm>
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

/* One input event of an input script, due at harness tick `tick`.  `key`
 * is the key's code, 0 for end.
 */
struct input_event
{
	harness_tick tick;
	input_action action;
	input_key key;
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

}

/* Parse an input script: one event per line,
 *
 *     <tick> down <key>     the key goes down at harness tick <tick>
 *     <tick> up <key>       the key comes up
 *     <tick> press <key>    down at <tick>, up at <tick> + 1
 *     <tick> end            the run ends
 *
 * with words separated by blanks, `#` starting a comment, blank lines
 * skipped, and ticks never going back from one line to the next.
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
	const auto fail{[&line_number](std::string message) {
		return std::unexpected{
		    input_script_error{.line = line_number, .message = std::move(message)}};
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
			continue;
		}
		if (action != "down" && action != "up" && action != "press")
			return fail("expected down, up, press or end, not \"" + std::string{action} + '"');
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
	std::ranges::stable_sort(events, {}, &input_event::tick);
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

}

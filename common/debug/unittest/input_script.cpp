/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/input_script.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth input script
#include <boost/test/unit_test.hpp>

BOOST_TEST_DONT_PRINT_LOG_VALUE(dcx::input_event)

namespace {

using dcx::harness_tick;
using dcx::input_action;
using dcx::input_event;
using dcx::input_key;

constexpr input_key key_enter{13};
constexpr input_key key_esc{27};

/* A stand-in for the game's key names (key_properties): two keys. */
std::optional<input_key> key_of(const std::string_view name)
{
	if (name == "ENTER")
		return key_enter;
	if (name == "ESC")
		return key_esc;
	return std::nullopt;
}

std::vector<input_event> parsed(const std::string_view text)
{
	auto script{dcx::parse_input_script(text, key_of)};
	BOOST_REQUIRE(script.has_value());
	return std::move(script).value();
}

/* The line a malformed script is rejected at; 0 when it is accepted. */
std::size_t error_line(const std::string_view text)
{
	const auto script{dcx::parse_input_script(text, key_of)};
	if (script.has_value())
		return 0;
	BOOST_TEST(!script.error().message.empty());
	return script.error().line;
}

}

/* Test that down, up and end lines become events at their ticks, with comments and blank
 * lines skipped.
 */
BOOST_AUTO_TEST_CASE(lines_become_events)
{
	const std::vector<input_event> want{
	    {.tick = harness_tick{5}, .action = input_action::key_down, .key = key_enter},
	    {.tick = harness_tick{7}, .action = input_action::key_up, .key = key_enter},
	    {.tick = harness_tick{90}, .action = input_action::end, .key = {}},
	};
	BOOST_TEST(parsed("# S0\n\n5 down ENTER\n\t7  up\tENTER  # let go\n90 end\n") == want);
}

/* Test that a script with CRLF line ends reads as one with LF line ends. */
BOOST_AUTO_TEST_CASE(crlf_lines_read_as_lf_lines)
{
	BOOST_TEST(parsed("# S0\r\n\r\n5 down ENTER\r\n7 up ENTER # let go\r\n90 end\r\n") ==
	           parsed("# S0\n\n5 down ENTER\n7 up ENTER # let go\n90 end\n"));
}

/* Test that press is a down at its tick and an up one tick later, ordered by tick among the
 * other lines' events.
 */
BOOST_AUTO_TEST_CASE(press_is_down_then_up_a_tick_later)
{
	const std::vector<input_event> want{
	    {.tick = harness_tick{3}, .action = input_action::key_down, .key = key_enter},
	    {.tick = harness_tick{3}, .action = input_action::key_down, .key = key_esc},
	    {.tick = harness_tick{4}, .action = input_action::key_up, .key = key_enter},
	    {.tick = harness_tick{4}, .action = input_action::key_up, .key = key_esc},
	    {.tick = harness_tick{5}, .action = input_action::end, .key = {}},
	};
	BOOST_TEST(parsed("3 press ENTER\n3 press ESC\n5 end") == want);
}

/* Test that the events due at one tick keep the order of their lines, also when presses put
 * them out of order first: many presses at one tick, which an unstable sort would mix.
 */
BOOST_AUTO_TEST_CASE(events_at_one_tick_keep_script_order)
{
	constexpr std::size_t presses{200};
	std::string text;
	std::vector<input_event> want, ups;
	for (std::size_t i{0}; i != presses; ++i)
	{
		const bool enter{i % 3 != 0};
		const auto key{enter ? key_enter : key_esc};
		text += enter ? "0 press ENTER\n" : "0 press ESC\n";
		want.push_back({.tick = harness_tick{0}, .action = input_action::key_down, .key = key});
		ups.push_back({.tick = harness_tick{1}, .action = input_action::key_up, .key = key});
	}
	text += "2 end\n";
	want.insert(want.end(), ups.begin(), ups.end());
	want.push_back({.tick = harness_tick{2}, .action = input_action::end, .key = {}});
	BOOST_TEST(parsed(text) == want);
}

/* Test that a malformed line is rejected with its 1-based line number. */
BOOST_AUTO_TEST_CASE(malformed_lines_are_rejected)
{
	BOOST_TEST(error_line("down ENTER\n1 end") == 1);
	BOOST_TEST(error_line("1 down ENTER\nx down ENTER\n2 end") == 2);
	BOOST_TEST(error_line("-1 down ENTER\n1 end") == 1);
	BOOST_TEST(error_line("1x down ENTER\n2 end") == 1);
	BOOST_TEST(error_line("4294967296 down ENTER\n1 end") == 1);
	BOOST_TEST(error_line("1\n2 end") == 1);
	BOOST_TEST(error_line("1 jump ENTER\n2 end") == 1);
	BOOST_TEST(error_line("1 down\n2 end") == 1);
	BOOST_TEST(error_line("1 down Space\n2 end") == 1);
	BOOST_TEST(error_line("1 down ENTER ESC\n2 end") == 1);
	BOOST_TEST(error_line("1 end ENTER") == 1);
	BOOST_TEST(error_line("4294967295 press ENTER") == 1);
	BOOST_TEST(error_line("4294967295 down ENTER\n4294967295 end") == 0);
}

/* Test that a line timed before the line above it is rejected: the script reads in time
 * order.
 */
BOOST_AUTO_TEST_CASE(ticks_going_back_are_rejected)
{
	BOOST_TEST(error_line("5 down ENTER\n5 up ENTER\n4 end") == 3);
}

/* Test that a script needs exactly one end line, as its last event: without one, a hidden
 * run would never end.
 */
BOOST_AUTO_TEST_CASE(script_needs_one_end_as_its_last_event)
{
	BOOST_TEST(error_line("") == 1);
	BOOST_TEST(error_line("# nothing\n\n") == 2);
	BOOST_TEST(error_line("1 down ENTER\n2 up ENTER\n") == 2);
	BOOST_TEST(error_line("1 end\n2 end") == 2);
	BOOST_TEST(error_line("1 end\n2 down ENTER") == 2);
	BOOST_TEST(error_line("1 end\n# done\n") == 0);
	BOOST_TEST(error_line("1 press ENTER\n1 end") == 2);
	BOOST_TEST(error_line("1 press ENTER\n2 end") == 0);
}

/* Test that the player hands out each tick's events once, at that tick, counting the ticks
 * itself.
 */
BOOST_AUTO_TEST_CASE(player_hands_out_each_ticks_events_once)
{
	dcx::input_script_player player{parsed("2 press ENTER\n2 down ESC\n4 end")};
	BOOST_TEST(player.next_tick().empty());
	BOOST_TEST(player.next_tick().empty());
	BOOST_TEST(player.next_tick().size() == 2u);
	const auto at_3{player.next_tick()};
	BOOST_REQUIRE(at_3.size() == 1u);
	BOOST_TEST((at_3.front().tick == harness_tick{3}));
	const auto at_4{player.next_tick()};
	BOOST_REQUIRE(at_4.size() == 1u);
	BOOST_TEST((at_4.front().action == input_action::end));
	BOOST_TEST(player.next_tick().empty());
}

/* Test that a scene is named after its script's file, without directory and extension. */
BOOST_AUTO_TEST_CASE(scene_named_after_script_file)
{
	BOOST_TEST(dcx::input_scene_name("common/debug/scenes/s1-title-menu.txt") == "s1-title-menu");
	BOOST_TEST(dcx::input_scene_name("s3") == "s3");
	BOOST_TEST(dcx::input_scene_name("a.d/s4.txt") == "s4");
	BOOST_TEST(dcx::input_scene_name("a.d/s4") == "s4");
}

namespace {

/* A stand-in for the game's key_props, with only the key's name. */
struct named_key
{
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays): the shape of key_props::key_text
	char key_text[7];
};

constexpr std::array<named_key, dcx::input_key_count> named_keys{[]() {
	std::array<named_key, dcx::input_key_count> keys{};
	keys[1] = named_key{"ESC"};
	keys[255] = named_key{"PAUSE"};
	return keys;
}()};

}

/* Test that a key is found by its name in a table of key_properties' shape, and that an
 * empty or unknown name, which unused codes have, finds none.
 */
BOOST_AUTO_TEST_CASE(key_found_by_name_in_key_table)
{
	BOOST_TEST((dcx::input_key_named(named_keys, "ESC") == input_key{1}));
	BOOST_TEST((dcx::input_key_named(named_keys, "PAUSE") == input_key{255}));
	BOOST_TEST(!dcx::input_key_named(named_keys, "Space").has_value());
	BOOST_TEST(!dcx::input_key_named(named_keys, "").has_value());
}

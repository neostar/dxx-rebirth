/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/input_script.h"

#include <cstddef>
#include <optional>
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

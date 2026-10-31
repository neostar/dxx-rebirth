/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* The input recorder (#99) at its seam: the events a run consumed go in, an input script comes
 * out, and the script's own parser (input_script.h) reads it back.  The truth is the events
 * written here as literals, never the recorder's own formatting.
 */

#include "common/debug/input_recorder.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth input recorder
#include <boost/test/unit_test.hpp>

BOOST_TEST_DONT_PRINT_LOG_VALUE(dcx::input_event)

namespace {

using dcx::harness_tick;
using dcx::input_action;
using dcx::input_button;
using dcx::input_device_shape;
using dcx::input_event;
using dcx::input_key;

using dcx::input_hat_centered;
using dcx::input_hat_down;
using dcx::input_hat_down_left;
using dcx::input_hat_down_right;
using dcx::input_hat_left;
using dcx::input_hat_right;
using dcx::input_hat_up;
using dcx::input_hat_up_left;
using dcx::input_hat_up_right;

constexpr input_key key_enter{13};
constexpr input_key key_esc{27};
/* A key whose name is the name of another, as two of the game's keys share one: the script
 * would read it back as that other key.
 */
constexpr input_key key_shared_name{200};
/* A key the game has no name for. */
constexpr input_key key_unnamed{201};

std::optional<input_key> key_of(const std::string_view name)
{
	if (name == "ENTER")
		return key_enter;
	if (name == "ESC")
		return key_esc;
	return std::nullopt;
}

std::string_view key_name(const input_key key)
{
	if (key == key_enter || key == key_shared_name)
		return "ENTER";
	if (key == key_esc)
		return "ESC";
	return {};
}

/* A recorder writing into `text`. */
dcx::input_recorder recorder_into(std::string &text)
{
	return dcx::input_recorder{[&text](const std::string_view line) {
		                           text.append(line);
		                           text.push_back('\n');
	                           },
	                           key_name, key_of};
}

std::vector<input_event> parsed(const std::string &text)
{
	auto script{dcx::parse_input_script(text, key_of)};
	BOOST_REQUIRE_MESSAGE(script.has_value(), text);
	return std::move(script).value();
}

}

/* Test that each input action, written as a script line, parses back to the event recorded. */
BOOST_AUTO_TEST_CASE(every_action_reads_back_as_recorded)
{
	const std::vector<input_event> events{
	    {.tick = harness_tick{0}, .action = input_action::key_down, .key = key_enter},
	    {.tick = harness_tick{0}, .action = input_action::key_up, .key = key_enter},
	    {.tick = harness_tick{3}, .action = input_action::mouse_move, .key = {}, .x = -7, .y = 0},
	    {.tick = harness_tick{3},
		 .action = input_action::mouse_down,
		 .key = {},
		 .x = 0,
		 .y = 0,
		 .button = input_button{3}},
	    {.tick = harness_tick{4},
		 .action = input_action::mouse_up,
		 .key = {},
		 .x = 0,
		 .y = 0,
		 .button = input_button{17}},
	    {.tick = harness_tick{9}, .action = input_action::wheel, .key = {}, .x = 0, .y = -2},
	    {.tick = harness_tick{9}, .action = input_action::key_down, .key = key_esc},
	};
	std::string text;
	auto recorder{recorder_into(text)};
	for (const auto &e : events)
		recorder.record(e);
	recorder.end(harness_tick{12});
	auto expected{events};
	expected.push_back({.tick = harness_tick{12}, .action = input_action::end, .key = {}});
	BOOST_TEST(parsed(text) == expected, boost::test_tools::per_element());
}

/* Test that each joystick action, written as a script line, parses back to the event recorded. */
BOOST_AUTO_TEST_CASE(every_joystick_action_reads_back_as_recorded)
{
	const auto joy{[](const std::uint32_t tick, const input_action action,
	                  const std::uint8_t device, const std::uint8_t control,
	                  const std::int32_t value) {
		return input_event{.tick = harness_tick{tick},
		                   .action = action,
		                   .key = {},
		                   .device = device,
		                   .control = control,
		                   .value = value};
	}};
	const auto attach{
	    [](const std::uint32_t tick, const std::uint8_t device, const input_device_shape shape) {
		    return input_event{.tick = harness_tick{tick},
			                   .action = input_action::joy_attach,
			                   .key = {},
			                   .device = device,
			                   .shape = shape};
	    }};
	std::vector<input_event> events{
	    attach(0, 0, input_device_shape::gamepad),
	    attach(0, 7, input_device_shape::stick),
	    joy(5, input_action::joy_axis, 0, 5, -32768),
	    joy(5, input_action::joy_axis, 7, 2, 32767),
	    joy(6, input_action::joy_button_down, 0, 20, 0),
	    joy(7, input_action::joy_button_up, 7, 31, 0),
	    joy(8, input_action::joy_detach, 0, 0, 0),
	    attach(9, 0, input_device_shape::stick),
	};
	for (const std::int32_t direction :
	     {input_hat_centered, input_hat_up, input_hat_right, input_hat_down, input_hat_left,
	      input_hat_up_right, input_hat_down_right, input_hat_down_left, input_hat_up_left})
		events.push_back(joy(10, input_action::joy_hat, 7, 0, direction));
	events.push_back(joy(11, input_action::joy_detach, 7, 0, 0));
	std::string text;
	auto recorder{recorder_into(text)};
	for (const auto &e : events)
		recorder.record(e);
	recorder.end(harness_tick{12});
	auto expected{events};
	expected.push_back({.tick = harness_tick{12}, .action = input_action::end, .key = {}});
	BOOST_TEST(parsed(text) == expected, boost::test_tools::per_element());
	BOOST_TEST(recorder.unrecorded() == 0u);
}

/* Test that a joystick event the script's devices cannot hold is a comment, never a line the
 * parser would reject: an input for a slot with nothing attached, a control the shape lacks, an
 * attach over an attached slot.
 */
BOOST_AUTO_TEST_CASE(a_joystick_event_no_device_holds_is_a_comment)
{
	std::string text;
	auto recorder{recorder_into(text)};
	const input_event axis_on_nothing{.tick = harness_tick{1},
	                                  .action = input_action::joy_axis,
	                                  .key = {},
	                                  .device = 2,
	                                  .control = 0,
	                                  .value = 1};
	recorder.record(axis_on_nothing);
	recorder.record({.tick = harness_tick{2},
	                 .action = input_action::joy_attach,
	                 .key = {},
	                 .device = 2,
	                 .shape = input_device_shape::stick});
	/* A stick has 3 axes, 1 hat. */
	recorder.record({.tick = harness_tick{3},
	                 .action = input_action::joy_axis,
	                 .key = {},
	                 .device = 2,
	                 .control = 3,
	                 .value = 1});
	recorder.record({.tick = harness_tick{3},
	                 .action = input_action::joy_hat,
	                 .key = {},
	                 .device = 2,
	                 .control = 1,
	                 .value = input_hat_up});
	/* A direction that is not one of the nine. */
	recorder.record({.tick = harness_tick{3},
	                 .action = input_action::joy_hat,
	                 .key = {},
	                 .device = 2,
	                 .control = 0,
	                 .value = 5});
	recorder.record({.tick = harness_tick{4},
	                 .action = input_action::joy_attach,
	                 .key = {},
	                 .device = 2,
	                 .shape = input_device_shape::gamepad});
	recorder.end(harness_tick{5});
	BOOST_TEST(recorder.unrecorded() == 5u);
	/* What was written still parses: the attach, the end. */
	BOOST_TEST(parsed(text).size() == 2u);
}

/* Test that a mouse motion of 0, 0 is written as a line, not dropped. */
BOOST_AUTO_TEST_CASE(a_motion_of_nothing_is_kept)
{
	/* The game handles a motion event of 0, 0 too: it stamps the cursor's time. */
	std::string text;
	auto recorder{recorder_into(text)};
	recorder.record(
	    {.tick = harness_tick{5}, .action = input_action::mouse_move, .key = {}, .x = 0, .y = 0});
	recorder.end(harness_tick{6});
	BOOST_TEST(parsed(text).size() == 2u);
}

/* Test that the first end is the only one written and no event after it is. */
BOOST_AUTO_TEST_CASE(only_the_first_end_counts_and_nothing_follows_it)
{
	std::string text;
	auto recorder{recorder_into(text)};
	recorder.end(harness_tick{4});
	recorder.record({.tick = harness_tick{5}, .action = input_action::key_down, .key = key_esc});
	recorder.end(harness_tick{9});
	const std::vector<input_event> expected{
	    {.tick = harness_tick{4}, .action = input_action::end, .key = {}}};
	BOOST_TEST(parsed(text) == expected, boost::test_tools::per_element());
	BOOST_TEST(recorder.ended());
}

/* Test that a key the script has no name for is written as a comment. */
BOOST_AUTO_TEST_CASE(a_key_the_script_cannot_name_back_is_a_comment)
{
	/* Recorded under its name, key_shared_name would replay as ENTER; with no name, it would
	 * not parse.  Both become comments, and the script still reads.
	 */
	std::string text;
	auto recorder{recorder_into(text)};
	recorder.record(
	    {.tick = harness_tick{1}, .action = input_action::key_down, .key = key_shared_name});
	recorder.record({.tick = harness_tick{2}, .action = input_action::key_up, .key = key_unnamed});
	recorder.end(harness_tick{3});
	const std::vector<input_event> expected{
	    {.tick = harness_tick{3}, .action = input_action::end, .key = {}}};
	BOOST_TEST(parsed(text) == expected, boost::test_tools::per_element());
	BOOST_TEST(recorder.unrecorded() == 2u);
	BOOST_TEST(text.find("# tick 1: not recorded: key 200") != std::string::npos, text);
}

/* Test that a motion beyond the script's range is written as a comment. */
BOOST_AUTO_TEST_CASE(motion_out_of_the_scripts_range_is_a_comment)
{
	std::string text;
	auto recorder{recorder_into(text)};
	recorder.record({.tick = harness_tick{1},
	                 .action = input_action::mouse_move,
	                 .key = {},
	                 .x = 32768,
	                 .y = 0});
	recorder.record(
	    {.tick = harness_tick{1}, .action = input_action::wheel, .key = {}, .x = 0, .y = -32768});
	recorder.end(harness_tick{2});
	BOOST_TEST(parsed(text).size() == 1u);
	BOOST_TEST(recorder.unrecorded() == 2u);
}

/* Test that skipped input is written as a comment naming its kind. */
BOOST_AUTO_TEST_CASE(input_the_script_cannot_hold_is_a_comment_naming_it)
{
	std::string text;
	auto recorder{recorder_into(text)};
	recorder.skip(harness_tick{7}, dcx::unrecorded_input::joystick);
	recorder.end(harness_tick{8});
	BOOST_TEST(parsed(text).size() == 1u);
	BOOST_TEST(recorder.unrecorded() == 1u);
	BOOST_TEST(text.find("# tick 7: not recorded: a joystick or gamepad event") !=
	               std::string::npos,
	           text);
}

/* Test that the summary counts the lines and the comments written. */
BOOST_AUTO_TEST_CASE(summary_counts_lines_and_comments)
{
	std::string text;
	auto recorder{recorder_into(text)};
	recorder.record({.tick = harness_tick{0}, .action = input_action::key_down, .key = key_esc});
	recorder.skip(harness_tick{1}, dcx::unrecorded_input::quit);
	recorder.end(harness_tick{2});
	BOOST_TEST(recorder.summary() ==
	           "recorded 1 input event over ticks 0-2; 1 not recorded (comments in the file)");
}

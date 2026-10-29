/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/hidden_window.h"

#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth hidden window
#include <boost/test/unit_test.hpp>

/* Test that the harness reads DXX_DBG_HIDDEN: unset shows the window, 1 hides it, and any
 * other value comes back with no answer for the caller to report.
 */
BOOST_AUTO_TEST_CASE(harness_reads_the_hidden_switch)
{
	unsetenv("DXX_DBG_HIDDEN");
	const auto unset{dcx::read_harness_hidden()};
	BOOST_TEST(unset.value == nullptr);
	BOOST_REQUIRE(unset.window.has_value());
	BOOST_TEST((unset.window.value() == dcx::harness_window::shown));

	setenv("DXX_DBG_HIDDEN", "1", 1);
	const auto on{dcx::read_harness_hidden()};
	BOOST_REQUIRE(on.window.has_value());
	BOOST_TEST((on.window.value() == dcx::harness_window::hidden));

	setenv("DXX_DBG_HIDDEN", "yes", 1);
	const auto bad{dcx::read_harness_hidden()};
	BOOST_REQUIRE(bad.value != nullptr);
	BOOST_TEST(std::string_view{bad.value} == "yes");
	BOOST_TEST(!bad.window.has_value());
	unsetenv("DXX_DBG_HIDDEN");
}

/* Test that a build that cannot hide its window treats any DXX_DBG_HIDDEN value as a
 * request it must refuse, and only an unset variable as none.
 */
BOOST_AUTO_TEST_CASE(any_value_is_a_request)
{
	BOOST_TEST(
	    !dcx::harness_hidden_requested({.value = nullptr, .window = dcx::harness_window::shown}));
	BOOST_TEST(
	    dcx::harness_hidden_requested({.value = "1", .window = dcx::harness_window::hidden}));
	BOOST_TEST(dcx::harness_hidden_requested({.value = "0", .window = std::nullopt}));
	BOOST_TEST(dcx::harness_hidden_requested({.value = "", .window = std::nullopt}));
}

/* Test that a hidden run never grabs the mouse or keyboard, nor turns on relative mouse mode,
 * whatever its DXX_DBG_HIDDEN value: it must never take the owner's real cursor.  Only a run
 * with the variable unset keeps the game's grab.
 */
BOOST_AUTO_TEST_CASE(hidden_run_never_grabs)
{
	BOOST_TEST(dcx::harness_allows_grab({.value = nullptr, .window = dcx::harness_window::shown}));
	BOOST_TEST(!dcx::harness_allows_grab({.value = "1", .window = dcx::harness_window::hidden}));
	BOOST_TEST(!dcx::harness_allows_grab({.value = "yes", .window = std::nullopt}));
}

/* Test that a run which asked to be hidden, with any DXX_DBG_HIDDEN value, reports a message on
 * stderr rather than in a message box, and that an unset variable keeps the message box.
 */
BOOST_AUTO_TEST_CASE(hidden_run_reports_on_stderr)
{
	BOOST_TEST((dcx::harness_report_for({.value = nullptr, .window = dcx::harness_window::shown}) ==
	            dcx::harness_report::message_box));
	BOOST_TEST((dcx::harness_report_for({.value = "1", .window = dcx::harness_window::hidden}) ==
	            dcx::harness_report::standard_error));
	BOOST_TEST((dcx::harness_report_for({.value = "yes", .window = std::nullopt}) ==
	            dcx::harness_report::standard_error));
}

/* Test that the report is the message's bytes, which need not end in a NUL, and a newline. */
BOOST_AUTO_TEST_CASE(report_writes_the_message_and_a_newline)
{
	std::FILE *const stream{std::tmpfile()};
	/* An explicit return: clang-analyzer does not see BOOST_REQUIRE end the case. */
	if (stream == nullptr)
	{
		BOOST_FAIL("tmpfile: no temporary file");
		return;
	}
	constexpr std::string_view message{"no such pilotX"};
	dcx::write_harness_report(stream, std::span<const char>{message}.first(message.size() - 1));
	BOOST_REQUIRE(std::fseek(stream, 0, SEEK_SET) == 0);
	std::string text;
	for (int c{std::fgetc(stream)}; c != EOF; c = std::fgetc(stream))
		text += static_cast<char>(c);
	BOOST_TEST(text == "no such pilot\n");
	// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the test owns stream; no gsl
	BOOST_TEST(std::fclose(stream) == 0);
}

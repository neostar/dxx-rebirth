/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/sdl_init.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "common/sdl/version_text.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth sdl init
#include <boost/test/unit_test.hpp>

namespace {

using dcx::sdl::app_metadata;
using dcx::sdl::hint;

const app_metadata test_metadata{
    .name = "Test", .version = "1.2.3", .identifier = "com.example.test"};

/* Records each call into a log it does not own, and answers as told. */
struct recording_ops
{
	std::vector<std::string> *log{nullptr};
	bool metadata_ok{true};
	bool hints_ok{true};
	bool init_ok{true};
	const char *last_error() const
	{
		return "metadata error text";
	}
	bool set_app_metadata(const app_metadata &m) const
	{
		log->push_back(std::string{"metadata "} + m.name);
		return metadata_ok;
	}
	void register_early_defaults() const
	{
		log->push_back("defaults");
	}
	bool set_hint(const hint &h) const
	{
		log->push_back(std::string{"hint "} + h.name + "=" + h.value);
		return hints_ok;
	}
	bool init_video() const
	{
		log->push_back("init");
		return init_ok;
	}
};

}

/* Test that start-up sets the metadata first, then every hint, then the
 * macOS default, and calls SDL_Init last.
 */
BOOST_AUTO_TEST_CASE(init_order_is_metadata_hints_defaults_init)
{
	std::vector<std::string> log;
	BOOST_TEST(dcx::sdl::init_video(test_metadata, recording_ops{&log}).initialised);
	const std::vector<std::string> expected{
	    "metadata Test",
	    "hint SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0",
	    "hint SDL_WINDOWS_CLOSE_ON_ALT_F4=0",
	    "hint SDL_MAC_PRESS_AND_HOLD=0",
	    "hint SDL_MOUSE_DPI_SCALE_CURSORS=1",
	    "defaults",
	    "init",
	};
	BOOST_TEST(log == expected, boost::test_tools::per_element());
}

/* Test that SDL_Init's failure is the result. */
BOOST_AUTO_TEST_CASE(init_result_is_the_init_result)
{
	std::vector<std::string> log;
	BOOST_TEST(
	    !dcx::sdl::init_video(test_metadata, recording_ops{&log, true, true, false}).initialised);
	log.clear();
	BOOST_TEST(
	    dcx::sdl::init_video(test_metadata, recording_ops{&log, true, true, true}).initialised);
}

/* Test that a refused metadata and refused hints are reported by name and do not stop the
 * start-up: SDL_Init is still called and its result still returned.
 */
BOOST_AUTO_TEST_CASE(refusals_are_reported_and_do_not_stop_the_start_up)
{
	std::vector<std::string> log;
	const auto report{dcx::sdl::init_video(test_metadata, recording_ops{&log, false, false, true})};
	BOOST_TEST(report.initialised);
	BOOST_REQUIRE(report.metadata_error.has_value());
	BOOST_TEST(report.metadata_error.value() == "metadata error text");
	BOOST_TEST(log.size() == 7u);
	BOOST_REQUIRE(report.refused_hints.size() == 4u);
	BOOST_TEST(std::string{report.refused_hints[0]} == "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS");
	BOOST_TEST(std::string{report.refused_hints[1]} == "SDL_WINDOWS_CLOSE_ON_ALT_F4");
	BOOST_TEST(std::string{report.refused_hints[2]} == "SDL_MAC_PRESS_AND_HOLD");
	BOOST_TEST(std::string{report.refused_hints[3]} == "SDL_MOUSE_DPI_SCALE_CURSORS");
	log.clear();
	const auto clean{dcx::sdl::init_video(test_metadata, recording_ops{&log})};
	BOOST_TEST(!clean.metadata_error.has_value());
	BOOST_TEST(clean.refused_hints.empty());
}

/* Test that real SDL3 initialises on the dummy video driver, keeps the
 * hints and the metadata, and that Quit tears it down.
 */
BOOST_AUTO_TEST_CASE(real_sdl_initialises_with_hints_and_metadata)
{
	SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
	BOOST_REQUIRE(dcx::sdl::init_video(test_metadata).initialised);
	BOOST_TEST((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == SDL_INIT_VIDEO);
	/* A null from SDL would crash std::string: fail the test first. */
	const auto text{[](const char *p) {
		BOOST_REQUIRE(p);
		return std::string{p};
	}};
	BOOST_TEST(text(SDL_GetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS)) == "0");
	BOOST_TEST(text(SDL_GetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4)) == "0");
	BOOST_TEST(text(SDL_GetHint(SDL_HINT_MAC_PRESS_AND_HOLD)) == "0");
	BOOST_TEST(text(SDL_GetHint(SDL_HINT_MOUSE_DPI_SCALE_CURSORS)) == "1");
	/* Alt+Tab stays the player's way out of the keyboard grab (sdl-input.md, Platform-only
	 * hints): the hint is unset or true, SDL's default being true.
	 */
	BOOST_TEST(SDL_GetHintBoolean(SDL_HINT_ALLOW_ALT_TAB_WHILE_GRABBED, true));
	BOOST_TEST(text(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING)) == "Test");
	BOOST_TEST(text(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING)) == "1.2.3");
	BOOST_TEST(text(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING)) ==
	           "com.example.test");
	SDL_Quit();
	BOOST_TEST((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0u);
}

/* Test that real SDL3's failure comes back as false: SDL3 returns false on
 * failure where SDL2 returned a negative number.
 */
BOOST_AUTO_TEST_CASE(real_sdl_failure_is_false)
{
	SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "no-such-driver", SDL_HINT_OVERRIDE);
	BOOST_TEST(!dcx::sdl::init_video(test_metadata).initialised);
	SDL_Quit();
}

/* Test that the version text names the library and both versions, built first. */
BOOST_AUTO_TEST_CASE(versions_text_names_the_built_and_the_loaded_version)
{
	BOOST_TEST(
	    dcx::sdl::versions_text("libSDL", SDL_VERSIONNUM(3, 4, 2), SDL_VERSIONNUM(3, 2, 20)) ==
	    "libSDL 3.4.2; loaded with libSDL 3.2.20");
}

/* Test that start-up sets no hint beyond the window hints of #40 and the input hints
 * docs/agents/sdl-input.md lists ("Platform-only hints and their defaults"): every other input hint
 * stays at SDL's default, which is where a platform-only default would otherwise go wrong unseen.
 * SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4 "0" keeps the game's own Alt+F4 and Alt+Shift+F4 bindings off
 * the window close.
 */
BOOST_AUTO_TEST_CASE(start_up_sets_only_the_listed_hints)
{
	const std::vector<std::string> allowed{
	    "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "SDL_WINDOWS_CLOSE_ON_ALT_F4", "SDL_MAC_PRESS_AND_HOLD",
	    "SDL_JOYSTICK_ENHANCED_REPORTS",    "SDL_MOUSE_DPI_SCALE_CURSORS",
	};
	for (const auto &h : dcx::sdl::pre_init_hints)
	{
		const bool listed{std::ranges::contains(allowed, std::string{h.name})};
		BOOST_TEST(listed, "unlisted hint " << h.name);
	}
}

namespace {

/* The lines of C++ source that call SDL_SetHint where SDL3 reaches them: outside every #if arm
 * that only SDL1 or SDL2 take.  A #else counts as reachable.
 */
std::vector<std::string> sdl3_hint_calls(const std::string &source)
{
	const auto only_old_sdl{[](const std::string_view t) {
		return t.contains("SDL_MAJOR_VERSION == 1") || t.contains("SDL_MAJOR_VERSION == 2") ||
		       t.contains("SDL_MAJOR_VERSION < 3");
	}};
	std::vector<bool> old_arm;
	std::vector<std::string> found;
	std::istringstream in{source};
	for (std::string line; std::getline(in, line);)
	{
		const auto first{line.find_first_not_of(" \t")};
		const std::string_view text{
		    first == std::string::npos ? std::string_view{} : std::string_view{line}.substr(first)};
		if (text.starts_with("#if"))
			old_arm.push_back(only_old_sdl(text));
		else if (text.starts_with("#elif") && !old_arm.empty())
			old_arm.back() = only_old_sdl(text);
		else if (text.starts_with("#else") && !old_arm.empty())
			old_arm.back() = false;
		else if (text.starts_with("#endif") && !old_arm.empty())
			old_arm.pop_back();
		else if (text.contains("SDL_SetHint") && !text.starts_with("/*") &&
		         !text.starts_with("*") && !text.starts_with("//") &&
		         !std::ranges::contains(old_arm, true))
			found.emplace_back(text);
	}
	return found;
}

}

/* Test that the scan sees a hint set in an SDL3 arm, an #else and the open, and passes one in an
 * SDL2-only arm, so that the scan of init.cpp below can fail.
 */
BOOST_AUTO_TEST_CASE(the_hint_scan_finds_hints_that_sdl3_reaches)
{
	const std::string source{"SDL_SetHint(A, \"1\");\n"
	                         "#if SDL_MAJOR_VERSION == 2\n"
	                         "SDL_SetHint(B, \"1\");\n"
	                         "#if DXX_X\n"
	                         "SDL_SetHint(C, \"1\");\n"
	                         "#endif\n"
	                         "#elif SDL_MAJOR_VERSION == 3\n"
	                         "SDL_SetHint(D, \"1\");\n"
	                         "#else\n"
	                         "SDL_SetHint(E, \"1\");\n"
	                         "#endif\n"
	                         "/* SDL_SetHint(F) */\n"};
	const std::vector<std::string> expected{"SDL_SetHint(A, \"1\");", "SDL_SetHint(D, \"1\");",
	                                        "SDL_SetHint(E, \"1\");"};
	BOOST_TEST(sdl3_hint_calls(source) == expected, boost::test_tools::per_element());
}

/* Test that init.cpp calls SDL_SetHint nowhere SDL3 reaches: SDL3 sets its hints through
 * pre_init_hints, which the pin above checks, and a direct call would add an input hint the pin
 * does not see.  The tests run from the repository root.
 */
BOOST_AUTO_TEST_CASE(init_cpp_sets_no_hint_directly_on_sdl3)
{
	const std::ifstream file{"similar/arch/sdl/init.cpp"};
	BOOST_REQUIRE_MESSAGE(file.is_open(), "run the tests from the repository root");
	std::ostringstream text;
	text << file.rdbuf();
	BOOST_REQUIRE(!text.str().empty());
	BOOST_TEST(sdl3_hint_calls(text.str()).empty());
}

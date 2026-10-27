/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/sdl_init.h"

#include <cstdlib>
#include <string>
#include <vector>

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
	BOOST_TEST(report.metadata_refused);
	BOOST_TEST(report.metadata_error == "metadata error text");
	BOOST_TEST(log.size() == 5u);
	BOOST_TEST(std::string{report.refused_hints[0]} == "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS");
	BOOST_TEST(std::string{report.refused_hints[1]} == "SDL_WINDOWS_CLOSE_ON_ALT_F4");
	log.clear();
	const auto clean{dcx::sdl::init_video(test_metadata, recording_ops{&log})};
	BOOST_TEST(!clean.metadata_refused);
	BOOST_TEST(clean.refused_hints[0] == nullptr);
	BOOST_TEST(clean.refused_hints[1] == nullptr);
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

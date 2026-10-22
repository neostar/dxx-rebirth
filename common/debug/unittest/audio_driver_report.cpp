/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/audio_driver_report.h"

#include <cstdlib>
#include <string>

#include "common/sdl/dxx-sdl.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth audio driver report
#include <boost/test/unit_test.hpp>

/* Test that the report names the driver SDL opened. */
BOOST_AUTO_TEST_CASE(report_names_the_driver)
{
	BOOST_TEST(dcx::audio_driver_report("coreaudio") == "DXX_DBG_HIDDEN: audio driver coreaudio");
}

/* Test that the report names no driver when SDL names none, so run.sh fails the run. */
BOOST_AUTO_TEST_CASE(report_names_no_driver_when_sdl_names_none)
{
	BOOST_TEST(dcx::audio_driver_report(nullptr) == "DXX_DBG_HIDDEN: audio driver ");
}

/* Test that a hidden run may go on only on the dummy driver, unless
 * NO_WINDOW_SOUND=1 asks for sound.
 */
BOOST_AUTO_TEST_CASE(only_the_dummy_driver_may_go_on)
{
	BOOST_TEST(dcx::audio_driver_refusal("dummy", nullptr).empty());
	BOOST_TEST(dcx::audio_driver_refusal("dummy", "0").empty());
	BOOST_TEST(dcx::audio_driver_refusal("coreaudio", "1").empty());
	BOOST_TEST(dcx::audio_driver_refusal(nullptr, "1").empty());
}

/* Test that a hidden run on another driver, or on none named, must stop,
 * the refusal naming the driver; NO_WINDOW_SOUND other than 1 changes
 * nothing.
 */
BOOST_AUTO_TEST_CASE(another_driver_or_none_must_stop)
{
	BOOST_TEST(dcx::audio_driver_refusal("coreaudio", nullptr) ==
	           "hidden run opened audio driver \"coreaudio\", not dummy: it could play sound");
	BOOST_TEST(dcx::audio_driver_refusal("disk", "0") ==
	           "hidden run opened audio driver \"disk\", not dummy: it could play sound");
	BOOST_TEST(dcx::audio_driver_refusal("dummyx", "") ==
	           "hidden run opened audio driver \"dummyx\", not dummy: it could play sound");
	BOOST_TEST(dcx::audio_driver_refusal(nullptr, nullptr) ==
	           "hidden run opened audio but SDL names no driver: it could play sound");
}

/* SDL 1.2 has no SDL_GetCurrentAudioDriver, and the game names no driver
 * there (digi_init).
 */
#if SDL_MAJOR_VERSION >= 2
/* Test that real SDL, asked for the dummy driver as no-window.sh asks
 * (SDL_AUDIODRIVER for SDL2, SDL_AUDIO_DRIVER for SDL3), opens it and the
 * report names it.
 */
BOOST_AUTO_TEST_CASE(report_names_the_dummy_driver_sdl_opened)
{
	BOOST_REQUIRE(setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
	BOOST_REQUIRE(setenv("SDL_AUDIO_DRIVER", "dummy", 1) == 0);
#if SDL_MAJOR_VERSION == 3
	BOOST_REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
#else
	BOOST_REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0);
#endif
	const std::string report{dcx::audio_driver_report(SDL_GetCurrentAudioDriver())};
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	BOOST_TEST(report == "DXX_DBG_HIDDEN: audio driver dummy");
}
#endif

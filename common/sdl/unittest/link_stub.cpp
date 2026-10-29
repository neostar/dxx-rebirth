/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/link_stub.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth link stub
#include <boost/test/unit_test.hpp>

/* Test that a stub reports itself once: the first use logs, every later use is silent. */
BOOST_AUTO_TEST_CASE(a_stub_logs_on_its_first_use_only)
{
	dcx::sdl::link_stub stub{"the SDL_mixer music", "#46"};
	BOOST_TEST(stub.first_use());
	BOOST_TEST(!stub.first_use());
	BOOST_TEST(!stub.first_use());
}

/* Test that two stubs log independently, so one stub's first use does not hide another's. */
BOOST_AUTO_TEST_CASE(stubs_log_independently)
{
	dcx::sdl::link_stub a{"the SDL_mixer music", "#46"};
	dcx::sdl::link_stub b{"gamepads", "#44"};
	BOOST_TEST(a.first_use());
	BOOST_TEST(b.first_use());
}

/* Test that the message names the stub and the ticket that replaces it. */
BOOST_AUTO_TEST_CASE(the_message_names_the_stub_and_its_ticket)
{
	const dcx::sdl::link_stub stub{"gamepads", "#44"};
	BOOST_TEST(stub.message() == "SDL3 link stub: gamepads is stubbed until #44");
}

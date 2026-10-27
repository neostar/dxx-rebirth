/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/macos_defaults.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth macos defaults
#include <boost/test/unit_test.hpp>

/* Test that registering the early defaults turns NSApplicationUpdateCycleEnabled
 * from unset to NO.
 */
BOOST_AUTO_TEST_CASE(update_cycle_default_goes_from_unset_to_no)
{
	BOOST_TEST(!dcx::sdl::update_cycle_default().has_value());
	dcx::sdl::register_early_user_defaults();
	const auto after{dcx::sdl::update_cycle_default()};
	BOOST_REQUIRE(after.has_value());
	BOOST_TEST(!after.value());
}

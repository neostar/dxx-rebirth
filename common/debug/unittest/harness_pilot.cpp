/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/harness_pilot.h"

#include <string_view>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth harness pilot
#include <boost/test/unit_test.hpp>

namespace {

bool accepted_as(const std::string_view arg, const std::string_view name)
{
	const auto pilot{dcx::harness_pilot_name(arg)};
	return pilot.has_value() && pilot.value() == name;
}

}

/* Test that a name the new-pilot menu accepts is the pilot's name, lowercased as the game
 * stores it, with or without the .plr extension.
 */
BOOST_AUTO_TEST_CASE(menu_names_are_accepted)
{
	BOOST_TEST(accepted_as("harness", "harness"));
	BOOST_TEST(accepted_as("Harness", "harness"));
	BOOST_TEST(accepted_as("harness.plr", "harness"));
	BOOST_TEST(accepted_as("HARNESS.PLR", "harness"));
	BOOST_TEST(accepted_as("a", "a"));
	BOOST_TEST(accepted_as("abcdefgh", "abcdefgh"));
	BOOST_TEST(accepted_as("abcdefgh.plr", "abcdefgh"));
	BOOST_TEST(accepted_as("a_b-9Z", "a_b-9z"));
}

/* Test that a name longer than a callsign is rejected rather than cut: the game would
 * otherwise check one file and write another, overwriting an existing pilot.
 */
BOOST_AUTO_TEST_CASE(names_longer_than_a_callsign_are_rejected)
{
	BOOST_TEST(!dcx::harness_pilot_name("abcdefghi").has_value());
	BOOST_TEST(!dcx::harness_pilot_name("harnesstest").has_value());
	BOOST_TEST(!dcx::harness_pilot_name("abcdefghijkl").has_value());
	BOOST_TEST(!dcx::harness_pilot_name("abcdefghijklmnop").has_value());
	BOOST_TEST(!dcx::harness_pilot_name("abcdefghi.plr").has_value());
}

/* Test that names the new-pilot menu cannot type are rejected: empty, a bare extension,
 * and characters outside letters, digits, _ and -.
 */
BOOST_AUTO_TEST_CASE(names_the_menu_cannot_type_are_rejected)
{
	BOOST_TEST(!dcx::harness_pilot_name("").has_value());
	BOOST_TEST(!dcx::harness_pilot_name(".plr").has_value());
	BOOST_TEST(!dcx::harness_pilot_name("a b").has_value());
	BOOST_TEST(!dcx::harness_pilot_name("../x").has_value());
	BOOST_TEST(!dcx::harness_pilot_name("a.b").has_value());
	BOOST_TEST(!dcx::harness_pilot_name("a.plr.plr").has_value());
}

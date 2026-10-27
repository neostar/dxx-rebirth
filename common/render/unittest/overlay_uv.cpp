/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/overlay_uv.h"

#include <array>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth overlay_uv
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

using uv = std::array<float, 2>;

/* Test that each rotation of an overlay texture maps a corner's texture
 * coordinates as the side's rotation turns the overlay: none keeps them,
 * a quarter turn gives (1 - v, u), a half turn (1 - u, 1 - v) and three
 * quarters (v, 1 - u).
 */
BOOST_AUTO_TEST_CASE(rotation_turns_the_overlay)
{
	const uv corner{{0.125f, 0.5f}};
	BOOST_TEST((render::overlay_uv(corner, render::uv_rotation::none) == uv{{0.125f, 0.5f}}));
	BOOST_TEST((render::overlay_uv(corner, render::uv_rotation::quarter) == uv{{0.5f, 0.125f}}));
	BOOST_TEST((render::overlay_uv(corner, render::uv_rotation::half) == uv{{0.875f, 0.5f}}));
	BOOST_TEST(
	    (render::overlay_uv(corner, render::uv_rotation::three_quarters) == uv{{0.5f, 0.875f}}));
}

/* Test that coordinates outside [0, 1], which a repeating overlay takes on
 * long walls, turn the same way.
 */
BOOST_AUTO_TEST_CASE(rotation_turns_coordinates_past_one)
{
	const uv corner{{2.5f, -1.25f}};
	BOOST_TEST((render::overlay_uv(corner, render::uv_rotation::quarter) == uv{{2.25f, 2.5f}}));
	BOOST_TEST((render::overlay_uv(corner, render::uv_rotation::half) == uv{{-1.5f, 2.25f}}));
	BOOST_TEST(
	    (render::overlay_uv(corner, render::uv_rotation::three_quarters) == uv{{-1.25f, -1.5f}}));
}

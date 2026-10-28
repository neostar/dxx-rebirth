/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The expansion of strips into triangles (#32).
 */

#include "common/render/line_quads.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "common/render/render_device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth line quads
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

namespace {

constexpr std::array<float, 4> red{{1, 0, 0, 1}};
constexpr std::array<float, 2> no_uv{{0, 0}};

/* A vertex at window pixel (x, y) of a 64 x 64 target. */
render::vertex at(const float x, const float y)
{
	return {.pos = {{x / 64, y / 64, 0}}, .rgba = red, .uv = no_uv};
}

}

/* Test that a strip's triangles keep GL's winding and put the provoking
 * vertex first: GL's (v0, v1, v2) then (v2, v1, v3), emitted as (v2, v0, v1)
 * and (v3, v2, v1).
 */
BOOST_AUTO_TEST_CASE(strip_triangles_keep_winding_with_the_provoking_vertex_first)
{
	const std::array<render::vertex, 4> v{{at(0, 0), at(1, 0), at(0, 1), at(1, 1)}};
	const auto list{render::strip_to_list(v)};
	BOOST_REQUIRE(list.size() == 6u);
	const std::array<render::vertex, 6> expected{{v[2], v[0], v[1], v[3], v[2], v[1]}};
	BOOST_TEST((std::ranges::equal(list, expected)));
	BOOST_TEST((render::strip_to_list(std::span{v}.first(2)).empty()));
}

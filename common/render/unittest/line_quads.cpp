/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The expansion of lines, points and strips into triangles (#32).  The seam
 * is the pixels the triangles cover, as a rasteriser with Metal's rule
 * (pixel centre, top-left edges) finds them: the expected sets are the ones
 * Apple's GL draws (M1 Max, macOS 26.7, probed through test-rtrace's GL
 * host), so a test fails on a pixel that moves, not on how the corners are
 * built.
 */

#include "common/render/line_quads.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include "common/render/ambient.h"
#include "common/render/render_device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth line quads
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

namespace {

constexpr render::extent target{.width = 64, .height = 64};
constexpr std::array<float, 4> red{{1, 0, 0, 1}};
constexpr std::array<float, 2> no_uv{{0, 0}};

using pixel = std::pair<int, int>;
using pixels = std::set<pixel>;

/* The expansion of a draw in the game's 2D projection on the 64 x 64
 * target: the view it reads.
 */
render::expansion_view flat_view(const float size)
{
	return {.projection = render::matrix_of(render::screen_ortho),
	        .modelview = render::matrix_of(render::modelview{render::identity{}}),
	        .viewport = target,
	        .size = size};
}

/* A vertex at window pixel (x, y) of the target. */
render::vertex at(const float x, const float y)
{
	return {.pos = {{x / 64, y / 64, 0}}, .rgba = red, .uv = no_uv};
}

/* The pixels the triangles of list cover, rows counted from the bottom: a
 * pixel is covered when its centre lies inside a triangle, and on an edge
 * when the edge is a top or left one in window coordinates with y down, as
 * Metal's rasteriser has it.
 */
pixels covered(const render::expansion_view &view, const std::vector<render::vertex> &list)
{
	struct point
	{
		double x;
		double y;
	};
	const auto window{[&](const render::vertex &v) {
		const auto c{render::detail::clip_of(view, v.pos)};
		return point{(c[0] / c[3] * 0.5 + 0.5) * view.viewport.width,
		             (1 - (c[1] / c[3] * 0.5 + 0.5)) * view.viewport.height};
	}};
	pixels out;
	for (std::size_t t{0}; t + 2 < list.size(); t += 3)
	{
		std::array<point, 3> p{{window(list[t]), window(list[t + 1]), window(list[t + 2])}};
		const double area{(p[1].x - p[0].x) * (p[2].y - p[0].y) -
		                  (p[1].y - p[0].y) * (p[2].x - p[0].x)};
		if (area == 0)
			continue;
		if (area < 0)
			std::swap(p[1], p[2]);
		for (int y{0}; y != static_cast<int>(view.viewport.height); ++y)
			for (int x{0}; x != static_cast<int>(view.viewport.width); ++x)
			{
				const point c{x + 0.5, view.viewport.height - (y + 0.5)};
				bool inside{true};
				for (std::size_t e{0}; e != 3; ++e)
				{
					const point &a{p[e]};
					const point &b{p[(e + 1) % 3]};
					const double dx{b.x - a.x}, dy{b.y - a.y};
					const double side{dx * (c.y - a.y) - dy * (c.x - a.x)};
					const bool top_or_left{(dy == 0 && dx > 0) || dy < 0};
					if (side < 0 || (side == 0 && !top_or_left))
						inside = false;
				}
				if (inside)
					out.insert({x, y});
			}
	}
	return out;
}

/* The pixels of the rectangle of columns x0 to x1 and rows y0 to y1,
 * inclusive.
 */
pixels block(const int x0, const int x1, const int y0, const int y1)
{
	pixels s;
	for (int y{y0}; y <= y1; ++y)
		for (int x{x0}; x <= x1; ++x)
			s.insert({x, y});
	return s;
}

}

/* Test that points are squares of the size's whole pixels, their pixel
 * centres on the square's left and lower-edge ties as GL's: size 1 at
 * (10, 10) is the pixel to its lower left, size 2 at (10.75, 10.25) the
 * pixels from column 10 and row 9, size 3 at (10, 10) three columns from 8.
 */
BOOST_AUTO_TEST_CASE(points_are_squares_of_whole_pixels)
{
	const auto point_pixels{[](const float size, const float x, const float y) {
		const auto view{flat_view(size)};
		const std::array<render::vertex, 1> v{{at(x, y)}};
		return covered(view, render::points_to_list(view, v));
	}};
	BOOST_TEST((point_pixels(1, 10, 10) == block(9, 9, 10, 10)));
	BOOST_TEST((point_pixels(1, 10.9f, 10.9f) == block(10, 10, 10, 10)));
	BOOST_TEST((point_pixels(2, 10.75f, 10.25f) == block(10, 11, 9, 10)));
	BOOST_TEST((point_pixels(3, 10, 10) == block(8, 10, 9, 11)));
	/* Apple's GL rounds a size of 1.5 up to 2. */
	BOOST_TEST((point_pixels(1.5f, 10.5f, 10.5f) == block(9, 10, 10, 11)));
}

/* Test that a point behind the eye or outside the clip volume draws
 * nothing, as GL clips a point by its centre.
 */
BOOST_AUTO_TEST_CASE(points_outside_the_clip_volume_draw_nothing)
{
	const auto view{flat_view(3)};
	const std::array<render::vertex, 2> off{{at(-1, 10), at(10, 70)}};
	BOOST_TEST(render::points_to_list(view, off).empty());
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

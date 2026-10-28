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
#include "common/render/rtrace/sample_trace.h"

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

/* Test that a width-1 line on a pixel boundary covers the row above it:
 * the tie of the pixel-centre rule, which GL resolves to the upper row
 * (the lower row's centre lies on the quad's lower edge).
 */
BOOST_AUTO_TEST_CASE(width_one_line_on_a_boundary_takes_the_upper_row)
{
	const auto view{flat_view(1)};
	const std::array<render::vertex, 2> v{{at(5, 10), at(20, 10)}};
	BOOST_TEST((covered(view, render::lines_to_list(view, v)) == block(5, 19, 10, 10)));
}

/* Test that GL's ties at a line's ends follow its direction: it includes
 * the pixel centre at the start and not the one at the end, whichever way
 * the line runs, though Metal's rasteriser fixes the ties by axis.  Along
 * x, right to left from 62.5 to 1.5 covers columns 62 down to 2; along y, a
 * line up from 2.5 to 12.5 covers rows 2 to 11 and one down from 12.5 to
 * 2.5 covers rows 12 down to 3 (all probed on GL).
 */
BOOST_AUTO_TEST_CASE(line_ends_follow_the_direction_of_the_line)
{
	const auto view{flat_view(1)};
	const std::array<render::vertex, 2> left{{at(62.5f, 1.5f), at(1.5f, 1.5f)}};
	BOOST_TEST((covered(view, render::lines_to_list(view, left)) == block(2, 62, 1, 1)));
	const std::array<render::vertex, 2> up{{at(58.5f, 2.5f), at(58.5f, 12.5f)}};
	BOOST_TEST((covered(view, render::lines_to_list(view, up)) == block(58, 58, 2, 11)));
	const std::array<render::vertex, 2> down{{at(58.5f, 12.5f), at(58.5f, 2.5f)}};
	BOOST_TEST((covered(view, render::lines_to_list(view, down)) == block(58, 58, 3, 12)));
}

/* Test that a width-2 line centred on a pixel centre covers the row of its
 * centre and the one above, not the one below: the quad's lower edge ties
 * with a pixel centre and its upper edge with another.
 */
BOOST_AUTO_TEST_CASE(width_two_line_on_a_pixel_centre_covers_two_rows)
{
	const auto view{flat_view(2)};
	const std::array<render::vertex, 2> v{{at(5, 10.5f), at(20, 10.5f)}};
	BOOST_TEST((covered(view, render::lines_to_list(view, v)) == block(5, 19, 10, 11)));
}

/* Test that a width-2 line's end ties do not follow its direction, as a
 * width-1 line's do: GL's wide line is a quad with Metal's ties, so a line
 * up from 2.5 to 12.5 and one down from 12.5 to 2.5 both cover rows 3 to
 * 12 (the upper end's centre in, the lower end's out), and along x, left
 * to right and right to left from 5.5 and 20.5 both cover columns 5 to 19
 * (all probed on GL at widths 2 and 3).  A slide at width 2 would move one
 * of the two directions off GL.
 */
BOOST_AUTO_TEST_CASE(width_two_line_ends_do_not_follow_the_direction)
{
	const auto view{flat_view(2)};
	const std::array<render::vertex, 2> up{{at(58.5f, 2.5f), at(58.5f, 12.5f)}};
	const std::array<render::vertex, 2> down{{at(58.5f, 12.5f), at(58.5f, 2.5f)}};
	BOOST_TEST((covered(view, render::lines_to_list(view, up)) == block(57, 58, 3, 12)));
	BOOST_TEST((covered(view, render::lines_to_list(view, down)) == block(57, 58, 3, 12)));
	const std::array<render::vertex, 2> right{{at(5.5f, 20.5f), at(20.5f, 20.5f)}};
	const std::array<render::vertex, 2> left{{at(20.5f, 20.5f), at(5.5f, 20.5f)}};
	BOOST_TEST((covered(view, render::lines_to_list(view, right)) == block(5, 19, 20, 21)));
	BOOST_TEST((covered(view, render::lines_to_list(view, left)) == block(5, 19, 20, 21)));
}

/* Test the same tie across the line: the left edge's pixel is covered and
 * the right edge's is not, so a vertical width-2 line at x = 5.5 covers
 * columns 4 and 5.
 */
BOOST_AUTO_TEST_CASE(vertical_width_two_line_covers_its_left_tie)
{
	const auto view{flat_view(2)};
	const std::array<render::vertex, 2> v{{at(5.5f, 5), at(5.5f, 20)}};
	BOOST_TEST((covered(view, render::lines_to_list(view, v)) == block(4, 5, 5, 19)));
}

/* Test that a diagonal width-1 line covers its own pixels and not the
 * neighbours: GL's line ends with the pixel before the endpoint.
 */
BOOST_AUTO_TEST_CASE(diagonal_width_one_line_covers_its_diagonal)
{
	const auto view{flat_view(1)};
	const std::array<render::vertex, 2> v{{at(5, 5), at(15, 15)}};
	pixels expected;
	for (int i{0}; i != 10; ++i)
		expected.insert({5 + i, 5 + i});
	BOOST_TEST((covered(view, render::lines_to_list(view, v)) == expected));
}

/* Test that a diagonal width-2 line repeats the one-pixel line two pixels
 * along the minor axis, as GL does (a parallelogram, not a rotated
 * rectangle of three pixels across): two pixels per row, one at each end.
 */
BOOST_AUTO_TEST_CASE(diagonal_width_two_line_is_two_pixels_per_row)
{
	const auto view{flat_view(2)};
	const std::array<render::vertex, 2> v{{at(5, 5), at(15, 15)}};
	pixels expected{{5, 5}, {14, 15}};
	for (int row{6}; row != 15; ++row)
	{
		expected.insert({row - 1, row});
		expected.insert({row, row});
	}
	BOOST_TEST((covered(view, render::lines_to_list(view, v)) == expected));
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

/* Test that lines and loops follow GL's vertex rules: an odd last vertex
 * of GL_LINES draws nothing, a loop closes on its first vertex, and fewer
 * than two vertices draw nothing.
 */
BOOST_AUTO_TEST_CASE(vertex_counts_follow_gl)
{
	const auto view{flat_view(1)};
	const std::array<render::vertex, 3> v{{at(5, 10), at(10, 10), at(15, 30)}};
	const auto pair{render::lines_to_list(view, v)};
	BOOST_TEST((covered(view, pair) == block(5, 9, 10, 10)));
	const auto loop{covered(view, render::line_loop_to_list(view, v))};
	/* The closing line from (15, 30) back to (5, 10) is steep: it reaches
	 * the loop's top row.
	 */
	BOOST_TEST((loop.contains({14, 29})));
	BOOST_TEST((render::line_loop_to_list(view, std::span{v}.first(1)).empty()));
	BOOST_TEST((render::lines_to_list(view, std::span{v}.first(1)).empty()));
}

/* Test that a line from in front of the eye to behind it is clipped, not
 * mirrored: in the 3D view a line from (0.5, 0, -1) to (0.5, 0, 1) shows
 * only the part in front of the near plane, running from the right of the
 * centre to the screen's edge; divided first, the endpoint behind the eye
 * lands at (-0.5, 0) and the line crosses the left half too.  A line wholly
 * behind the eye draws nothing.
 */
BOOST_AUTO_TEST_CASE(endpoint_behind_the_eye_is_clipped_not_mirrored)
{
	const render::expansion_view view{.projection = render::matrix_of(render::view_perspective),
	                                  .modelview =
	                                      render::matrix_of(render::modelview{render::identity{}}),
	                                  .viewport = target,
	                                  .size = 1};
	const auto vertex_at{[](const float x, const float z) {
		return render::vertex{.pos = {{x, 0, z}}, .rgba = red, .uv = no_uv};
	}};
	const std::array<render::vertex, 2> crossing{{vertex_at(0.5f, -1), vertex_at(0.5f, 1)}};
	const auto shown{covered(view, render::lines_to_list(view, crossing))};
	BOOST_TEST(shown.size() >= 15u);
	for (const auto &[x, y] : shown)
		BOOST_TEST_CONTEXT("pixel " << x << ", " << y) BOOST_TEST(x >= 48);
	const std::array<render::vertex, 2> behind{{vertex_at(0.5f, 1), vertex_at(-0.5f, 3)}};
	BOOST_TEST(render::lines_to_list(view, behind).empty());
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

/* Test that a vertex colour above 1 is clamped before the line is clipped
 * or interpolated, as GL does: a line from a dim start to an end of 4 that
 * the near plane clips halfway has no channel above 1 at any vertex,
 * clipped end or not (unclamped, the clipped end would be 2.25).
 */
BOOST_AUTO_TEST_CASE(colours_above_one_are_clamped_along_a_clipped_line)
{
	const render::expansion_view view{.projection = render::matrix_of(render::view_perspective),
	                                  .modelview =
	                                      render::matrix_of(render::modelview{render::identity{}}),
	                                  .viewport = target,
	                                  .size = 1};
	const std::array<render::vertex, 2> v{
	    {{.pos = {{0.5f, 0, -1}}, .rgba = {{0.5f, 0, 0, 1}}, .uv = no_uv},
		 {.pos = {{0.5f, 0, 1}}, .rgba = {{4, 0, 0, 1}}, .uv = no_uv}}};
	const auto list{render::lines_to_list(view, v)};
	BOOST_REQUIRE(!list.empty());
	for (const auto &p : list)
		for (const float channel : p.rgba)
			BOOST_TEST_CONTEXT("channel " << channel) BOOST_TEST((channel >= 0 && channel <= 1));
	/* The clipped end is the near plane's: halfway along in clip space
	 * between the clamped colours 0.5 and 1.
	 */
	const auto reddest{
	    std::ranges::max_element(list, {}, [](const render::vertex &p) { return p.rgba[0]; })};
	BOOST_TEST(reddest->rgba[0] > 0.5f);
}

/* Test the recipe expansion_view_of asserts: window_step divides by the
 * diagonal of P and MV, which holds for the game's projections and a
 * scale and translation, not for a rotation in the image plane.
 */
BOOST_AUTO_TEST_CASE(only_diagonal_recipes_are_expandable)
{
	BOOST_TEST(render::has_diagonal_recipe(flat_view(1)));
	auto view{flat_view(1)};
	view.projection = render::matrix_of(render::view_perspective);
	BOOST_TEST(render::has_diagonal_recipe(view));
	view.modelview[4] = 0.5f;
	BOOST_TEST(!render::has_diagonal_recipe(view));
}

/* Test that the lines trace's exact diagonals take GL's major axis (#32,
 * measured: the spokes' colours in Apple's GL).  Their float endpoints
 * leave |dx| and |dy| a few 1e-6 pixels apart, either way; on GL's grid of
 * 1/256 pixel the 135, 225 and 315 degree spokes are ties, which GL takes
 * as x-major, and the 45 degree one is a unit short of a tie in x, so it
 * is y-major.  The first two corners of the quad are one endpoint's, apart
 * along the minor axis: in y for an x-major line.
 */
BOOST_AUTO_TEST_CASE(diagonal_spokes_take_gls_major_axis)
{
	namespace rt = dcx::render::rtrace;
	const auto spokes{rt::sample_spokes(32.37f, 24.81f)};
	for (const auto [spoke, x_major] : {std::pair{std::size_t{3}, false},
	                                    {std::size_t{9}, true},
	                                    {std::size_t{15}, true},
	                                    {std::size_t{21}, true}})
	{
		const std::array<render::vertex, 2> v{{spokes[2 * spoke], spokes[2 * spoke + 1]}};
		const auto view{render::expansion_view_of(rt::line_draw(render::topology::lines, v, 1))};
		const auto list{render::lines_to_list(view, v)};
		BOOST_REQUIRE(list.size() == 6u);
		BOOST_TEST_CONTEXT("spoke " << spoke)
		{
			BOOST_TEST((list[0].pos[0] == list[1].pos[0]) == x_major);
			BOOST_TEST((list[0].pos[1] == list[1].pos[1]) != x_major);
		}
	}
}

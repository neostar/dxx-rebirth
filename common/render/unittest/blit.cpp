/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/blit.h"

#include <array>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth blit
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

namespace {

constexpr render::extent target{.width = 640, .height = 480};

}

/* Test that a canvas rectangle covers the screen space of its pixels, with
 * y growing upward from the target's bottom: the whole target is 0..1 on
 * both axes, and a rectangle at (160, 120) of 320 by 240 is the middle
 * quarter.
 */
BOOST_AUTO_TEST_CASE(canvas_rect_is_its_pixels_in_screen_space)
{
	BOOST_TEST((render::canvas_rect(0, 0, 640, 480, target) ==
	            render::screen_rect{.left = 0, .right = 1, .top = 1, .bottom = 0}));
	BOOST_TEST((render::canvas_rect(160, 120, 320, 240, target) ==
	            render::screen_rect{.left = 0.25f, .right = 0.75f, .top = 0.75f, .bottom = 0.25f}));
}

/* Test that a movie rectangle covers the same pixels as a canvas rectangle
 * does, for sizes whose quotients are exact.
 */
BOOST_AUTO_TEST_CASE(movie_rect_is_its_pixels_in_screen_space)
{
	BOOST_TEST((render::movie_rect(160, 120, 320, 240, target) ==
	            render::canvas_rect(160, 120, 320, 240, target)));
}

/* Test that a canvas rectangle divides in double and a movie rectangle in
 * float, as ogl_ubitmapm_cs and ogl_ubitblt_i did: at 640 by 480 the top
 * of row 31, 1 - 31/480, rounds to a different float in each.
 */
BOOST_AUTO_TEST_CASE(canvas_divides_in_double_movie_in_float)
{
	const float canvas_top{render::canvas_rect(0, 31, 16, 16, target).top};
	const float movie_top{render::movie_rect(0, 31, 16, 16, target).top};
	BOOST_TEST(canvas_top == 0x1.deeeeep-1f);
	BOOST_TEST(movie_top == 0x1.deeefp-1f);
	BOOST_TEST(canvas_top != movie_top);
}

/* Test that a bitmap filling its texture's used part shows the texture up
 * to the coordinates the upload set, and a bitmap smaller than that shows
 * its own share of the texture.
 */
BOOST_AUTO_TEST_CASE(whole_bitmap_shows_the_uploaded_part)
{
	const render::bitmap_window whole{.x = 0,
	                                  .y = 0,
	                                  .w = 24,
	                                  .h = 20,
	                                  .tex_w = 24,
	                                  .tex_h = 20,
	                                  .tex_tw = 32,
	                                  .tex_th = 32,
	                                  .u = 0.7f,
	                                  .v = 0.6f};
	BOOST_TEST((render::bitmap_uv(whole) ==
	            render::texture_rect{.u1 = 0, .u2 = 0.7f, .v1 = 0, .v2 = 0.6f}));
	auto part{whole};
	part.w = 16;
	part.h = 8;
	BOOST_TEST((render::bitmap_uv(part) ==
	            render::texture_rect{.u1 = 0, .u2 = 0.5f, .v1 = 0, .v2 = 0.25f}));
}

/* Test that a bitmap offset into its texture, as a sub bitmap or a glyph
 * is, shows from its offset to its far edge.
 */
BOOST_AUTO_TEST_CASE(offset_bitmap_shows_its_window)
{
	const render::bitmap_window glyph{.x = 8,
	                                  .y = 16,
	                                  .w = 8,
	                                  .h = 8,
	                                  .tex_w = 32,
	                                  .tex_h = 32,
	                                  .tex_tw = 32,
	                                  .tex_th = 32,
	                                  .u = 1,
	                                  .v = 1};
	BOOST_TEST((render::bitmap_uv(glyph) ==
	            render::texture_rect{.u1 = 0.25f, .u2 = 0.5f, .v1 = 0.5f, .v2 = 0.75f}));
}

/* Test that the four vertices run top left, top right, bottom right,
 * bottom left, each with its own colour and the texture corner of its
 * position.
 */
BOOST_AUTO_TEST_CASE(vertices_run_clockwise_from_top_left)
{
	std::array<float, 16> colors{};
	for (unsigned i{0}; i < colors.size(); ++i)
		colors[i] = static_cast<float>(i) / 16;
	const auto v{render::blit_vertices({.left = 0.1f, .right = 0.2f, .top = 0.9f, .bottom = 0.8f},
	                                   {.u1 = 0.25f, .u2 = 0.5f, .v1 = 0.125f, .v2 = 0.375f},
	                                   colors)};
	BOOST_TEST((v[0].pos == std::array<float, 3>{{0.1f, 0.9f, 0}}));
	BOOST_TEST((v[1].pos == std::array<float, 3>{{0.2f, 0.9f, 0}}));
	BOOST_TEST((v[2].pos == std::array<float, 3>{{0.2f, 0.8f, 0}}));
	BOOST_TEST((v[3].pos == std::array<float, 3>{{0.1f, 0.8f, 0}}));
	BOOST_TEST((v[0].uv == std::array<float, 2>{{0.25f, 0.125f}}));
	BOOST_TEST((v[1].uv == std::array<float, 2>{{0.5f, 0.125f}}));
	BOOST_TEST((v[2].uv == std::array<float, 2>{{0.5f, 0.375f}}));
	BOOST_TEST((v[3].uv == std::array<float, 2>{{0.25f, 0.375f}}));
	BOOST_TEST((v[0].rgba == std::array<float, 4>{{0, 1.f / 16, 2.f / 16, 3.f / 16}}));
	BOOST_TEST((v[3].rgba == std::array<float, 4>{{12.f / 16, 13.f / 16, 14.f / 16, 15.f / 16}}));
}

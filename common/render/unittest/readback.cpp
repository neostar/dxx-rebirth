/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/readback.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <tuple>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth readback
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

/* Test that flip_rows reverses the order of the rows of an image and keeps
 * the bytes within each row in order.
 */
BOOST_AUTO_TEST_CASE(flip_rows_reverses_row_order)
{
	std::array<std::uint8_t, 6> pixels{{1, 2, 3, 4, 5, 6}};
	render::flip_rows(pixels, 2);
	const std::array<std::uint8_t, 6> expected{{5, 6, 3, 4, 1, 2}};
	BOOST_TEST(pixels == expected, boost::test_tools::per_element());
}

/* Test that flip_rows leaves an image of no rows or of one row unchanged. */
BOOST_AUTO_TEST_CASE(flip_rows_keeps_empty_and_single_row_images)
{
	std::array<std::uint8_t, 0> empty{};
	render::flip_rows(empty, 3);
	std::array<std::uint8_t, 3> one_row{{7, 8, 9}};
	render::flip_rows(one_row, 3);
	const std::array<std::uint8_t, 3> expected{{7, 8, 9}};
	BOOST_TEST(one_row == expected, boost::test_tools::per_element());
}

/* Test that flip_rows rejects a row size of zero or one that does not
 * divide the image, and leaves the pixels untouched.
 */
BOOST_AUTO_TEST_CASE(flip_rows_rejects_partial_rows)
{
	std::array<std::uint8_t, 5> pixels{{1, 2, 3, 4, 5}};
	BOOST_CHECK_THROW(render::flip_rows(pixels, 2), std::invalid_argument);
	BOOST_CHECK_THROW(render::flip_rows(pixels, 0), std::invalid_argument);
	const std::array<std::uint8_t, 5> expected{{1, 2, 3, 4, 5}};
	BOOST_TEST(pixels == expected, boost::test_tools::per_element());
}

/* Test that bottom_up_y gives the row, counted from the bottom of the
 * target as glReadPixels counts it, of the bottom of a rectangle named from
 * the top left: the savegame thumbnail's 100x50 at the top of a 480-line
 * screen starts at line 430, a rectangle at the bottom at line 0, and one
 * reaching above the top (the screenshot's height rounded up to a multiple
 * of 4) still at line 0.
 */
BOOST_AUTO_TEST_CASE(bottom_up_y_counts_from_the_bottom)
{
	BOOST_TEST(render::bottom_up_y(480, {.x = 0, .y = 0, .size = {100, 50}}) == 430);
	BOOST_TEST(render::bottom_up_y(480, {.x = 0, .y = 430, .size = {100, 50}}) == 0);
	BOOST_TEST(render::bottom_up_y(1050, {.x = 0, .y = -2, .size = {1680, 1052}}) == 0);
}

/* Test that image_bytes gives the size of a tightly packed readback: the
 * savegame thumbnail, 100x50 RGBA, takes 20000 bytes; a 1024x768 RGB
 * screenshot 2359296.
 */
BOOST_AUTO_TEST_CASE(image_bytes_of_tightly_packed_rows)
{
	BOOST_TEST(render::image_bytes({100, 50}, render::pixel_format::rgba8) == 20000u);
	BOOST_TEST(render::image_bytes({1024, 768}, render::pixel_format::rgb8) == 2359296u);
}

/* Test that image_bytes rejects a size whose byte count does not fit in
 * size_t instead of wrapping to a small buffer.
 */
BOOST_AUTO_TEST_CASE(image_bytes_rejects_sizes_that_overflow)
{
	constexpr std::uint32_t side{0xffffffffu};
	BOOST_CHECK_THROW(std::ignore = render::image_bytes({side, side}, render::pixel_format::rgba8),
	                  std::length_error);
	BOOST_TEST(render::image_bytes({side, 1}, render::pixel_format::rgba8) == 0x3fffffffcu);
}

/* Test that clip_read_rect keeps a rectangle inside the target as it is. */
BOOST_AUTO_TEST_CASE(clip_read_rect_keeps_a_rectangle_inside_the_target)
{
	const auto c{render::clip_read_rect({640, 480}, {.x = 10, .y = 20, .size = {100, 50}})};
	BOOST_REQUIRE(c.has_value());
	BOOST_TEST(c.value().source_x == 10u);
	BOOST_TEST(c.value().source_y == 20u);
	BOOST_TEST(c.value().size.width == 100u);
	BOOST_TEST(c.value().size.height == 50u);
	BOOST_TEST(c.value().dest_x == 0u);
	BOOST_TEST(c.value().dest_y == 0u);
}

/* Test that clip_read_rect cuts a rectangle reaching past the target's edges
 * to the part inside, placed where it lies in the rectangle: the
 * screenshot's 1680x1052 read from y = -2 on a 1680x1050 target reads all
 * 1050 rows into the rectangle's rows 2 on, and a rectangle reaching past
 * the right and bottom edges keeps its top left part.
 */
BOOST_AUTO_TEST_CASE(clip_read_rect_cuts_a_rectangle_to_the_target)
{
	const auto top{render::clip_read_rect({1680, 1050}, {.x = 0, .y = -2, .size = {1680, 1052}})};
	BOOST_REQUIRE(top.has_value());
	BOOST_TEST(top.value().source_y == 0u);
	BOOST_TEST(top.value().dest_y == 2u);
	BOOST_TEST(top.value().size.width == 1680u);
	BOOST_TEST(top.value().size.height == 1050u);
	const auto corner{render::clip_read_rect({64, 48}, {.x = 60, .y = 40, .size = {10, 10}})};
	BOOST_REQUIRE(corner.has_value());
	BOOST_TEST(corner.value().source_x == 60u);
	BOOST_TEST(corner.value().source_y == 40u);
	BOOST_TEST(corner.value().size.width == 4u);
	BOOST_TEST(corner.value().size.height == 8u);
	BOOST_TEST(corner.value().dest_x == 0u);
	BOOST_TEST(corner.value().dest_y == 0u);
}

/* Test that clip_read_rect gives nothing for a rectangle wholly outside the
 * target or of no pixels.
 */
BOOST_AUTO_TEST_CASE(clip_read_rect_gives_nothing_outside_the_target)
{
	BOOST_TEST(!render::clip_read_rect({64, 48}, {.x = 64, .y = 0, .size = {4, 4}}).has_value());
	BOOST_TEST(!render::clip_read_rect({64, 48}, {.x = -4, .y = 0, .size = {4, 4}}).has_value());
	BOOST_TEST(!render::clip_read_rect({64, 48}, {.x = 0, .y = 48, .size = {4, 4}}).has_value());
	BOOST_TEST(!render::clip_read_rect({64, 48}, {.x = 0, .y = 0, .size = {0, 4}}).has_value());
}

/* Test that place_rows writes the clipped RGBA rows into the rectangle's
 * image at the clip's place, as RGB when asked, and zero everywhere else.
 */
BOOST_AUTO_TEST_CASE(place_rows_fills_the_outside_with_zero)
{
	/* A 2x1 clip at (1, 1) of a 3x2 rectangle. */
	const render::read_clip clip{
	    .source_x = 0, .source_y = 0, .size = {2, 1}, .dest_x = 1, .dest_y = 1};
	const std::array<std::uint8_t, 8> rows{{1, 2, 3, 4, 5, 6, 7, 8}};
	std::array<std::uint8_t, 18> rgb{};
	rgb.fill(0xaa);
	render::place_rows(rows, clip, {3, 2}, render::pixel_format::rgb8, rgb);
	const std::array<std::uint8_t, 18> expected_rgb{
	    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 5, 6, 7}};
	BOOST_TEST(rgb == expected_rgb, boost::test_tools::per_element());
	std::array<std::uint8_t, 24> rgba{};
	rgba.fill(0xaa);
	render::place_rows(rows, clip, {3, 2}, render::pixel_format::rgba8, rgba);
	const std::array<std::uint8_t, 24> expected_rgba{
	    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8}};
	BOOST_TEST(rgba == expected_rgba, boost::test_tools::per_element());
}

/* Test that place_rows rejects rows or an image of the wrong size and
 * writes nothing.
 */
BOOST_AUTO_TEST_CASE(place_rows_rejects_wrong_sizes)
{
	const render::read_clip clip{
	    .source_x = 0, .source_y = 0, .size = {2, 1}, .dest_x = 1, .dest_y = 1};
	const std::array<std::uint8_t, 7> short_rows{};
	std::array<std::uint8_t, 18> rgb{};
	rgb.fill(0xaa);
	BOOST_CHECK_THROW(render::place_rows(short_rows, clip, {3, 2}, render::pixel_format::rgb8, rgb),
	                  std::length_error);
	const std::array<std::uint8_t, 8> rows{};
	BOOST_CHECK_THROW(render::place_rows(rows, clip, {3, 2}, render::pixel_format::rgba8, rgb),
	                  std::length_error);
	BOOST_TEST(rgb[0] == 0xaa);
	/* A clip reaching past the rectangle. */
	const render::read_clip outside{
	    .source_x = 0, .source_y = 0, .size = {2, 1}, .dest_x = 2, .dest_y = 1};
	BOOST_CHECK_THROW(render::place_rows(rows, outside, {3, 2}, render::pixel_format::rgb8, rgb),
	                  std::length_error);
	BOOST_TEST(rgb[0] == 0xaa);
}

/* Test that the pixel read for ogl_ugpixel is the one-pixel rectangle one
 * row above the pixel, as GL read it: a bitmap at (10, 20), pixel (3, 4).
 */
BOOST_AUTO_TEST_CASE(ugpixel_reads_the_row_above_the_pixel)
{
	const auto r{render::ugpixel_rect(10, 20, 3, 4)};
	BOOST_TEST(r.x == 13);
	BOOST_TEST(r.y == 23);
	BOOST_TEST(r.size.width == 1u);
	BOOST_TEST(r.size.height == 1u);
}

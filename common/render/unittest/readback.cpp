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

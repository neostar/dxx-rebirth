/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/mipmaps.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "common/render/render_device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth mipmaps
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

namespace {

using bytes = std::vector<std::uint8_t>;

}

/* Test that a full chain runs down to 1x1: one level more than the base-2
 * logarithm of the longer side.
 */
BOOST_AUTO_TEST_CASE(chain_runs_down_to_one_texel)
{
	BOOST_TEST(render::mip_level_count({.width = 64, .height = 64}) == 7u);
	BOOST_TEST(render::mip_level_count({.width = 64, .height = 16}) == 7u);
	BOOST_TEST(render::mip_level_count({.width = 2, .height = 256}) == 9u);
	BOOST_TEST(render::mip_level_count({.width = 1, .height = 1}) == 1u);
	BOOST_TEST(render::mip_level_count({.width = 0, .height = 0}) == 0u);
	BOOST_TEST((render::next_mip_extent({.width = 8, .height = 2}) ==
	            render::extent{.width = 4, .height = 1}));
	BOOST_TEST((render::next_mip_extent({.width = 4, .height = 1}) ==
	            render::extent{.width = 2, .height = 1}));
}

/* Test the 2x2 box filter's rounding: four texels summing to 7 in a
 * channel give 2, (7 + 2) / 4, where a truncated mean gives 1; each
 * channel on its own.
 */
BOOST_AUTO_TEST_CASE(box_mean_rounds_half_up)
{
	const bytes base{1, 0, 255, 10, 2, 0, 255, 11, 2, 0, 254, 12, 2, 1, 254, 13};
	BOOST_TEST((render::next_mip_level({.width = 2, .height = 2}, base) == bytes{2, 0, 255, 12}),
	           boost::test_tools::per_element());
}

/* Test that a level of one row or one column halves along its other side
 * with the truncated mean of two texels: 1 and 2 give 1, 255 and 254 give
 * 254.
 */
BOOST_AUTO_TEST_CASE(line_mean_truncates)
{
	const bytes row{1, 255, 0, 3, 2, 254, 0, 4};
	BOOST_TEST((render::next_mip_level({.width = 2, .height = 1}, row) == bytes{1, 254, 0, 3}),
	           boost::test_tools::per_element());
	BOOST_TEST((render::next_mip_level({.width = 1, .height = 2}, row) == bytes{1, 254, 0, 3}),
	           boost::test_tools::per_element());
}

/* Test that a 4x2 level halves to 2x1 by blocks, row by row: the left
 * block of texels 0, 1, 4 and 5, the right one of 2, 3, 6 and 7.
 */
BOOST_AUTO_TEST_CASE(blocks_take_their_own_texels)
{
	bytes base(std::size_t{4} * 2 * 4);
	for (std::size_t t{0}; t != 8; ++t)
		base[t * 4] = static_cast<std::uint8_t>(t * 10);
	const auto next{render::next_mip_level({.width = 4, .height = 2}, base)};
	BOOST_REQUIRE(next.size() == 8u);
	/* (0 + 10 + 40 + 50 + 2) / 4 and (20 + 30 + 60 + 70 + 2) / 4. */
	BOOST_TEST(next[0] == 25u);
	BOOST_TEST(next[4] == 45u);
}

/* Test that a level that cannot be halved is refused: rows of the wrong
 * size, a 1x1 level, a side that is no power of two.
 */
BOOST_AUTO_TEST_CASE(unhalvable_level_is_refused)
{
	BOOST_CHECK_THROW((void)render::next_mip_level({.width = 2, .height = 2}, bytes(12)),
	                  std::length_error);
	BOOST_CHECK_THROW((void)render::next_mip_level({.width = 1, .height = 1}, bytes(4)),
	                  std::invalid_argument);
	BOOST_CHECK_THROW((void)render::next_mip_level({.width = 3, .height = 2}, bytes(24)),
	                  std::invalid_argument);
}

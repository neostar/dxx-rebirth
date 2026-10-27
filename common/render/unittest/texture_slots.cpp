/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/texture_slots.h"

#include <cstdint>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth texture_slots
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

namespace {

constexpr std::uint32_t pool_size{20000};

}

/* Test that a pool texture's slot is its index plus one, and that slot
 * leads back to the same index: the first and the last of the pool.
 */
BOOST_AUTO_TEST_CASE(pool_slot_leads_back_to_its_index)
{
	BOOST_TEST(render::pool_slot(0).index == 1u);
	BOOST_TEST(render::pool_slot(pool_size - 1).index == pool_size);
	BOOST_TEST((render::slot_place_of(render::pool_slot(0), pool_size) ==
	            render::slot_place{.owner = render::slot_owner::pool, .index = 0}));
	BOOST_TEST((render::slot_place_of(render::pool_slot(pool_size - 1), pool_size) ==
	            render::slot_place{.owner = render::slot_owner::pool, .index = pool_size - 1}));
}

/* Test that the movie frame's slot is the one after the pool's, and that
 * slot leads to the movie frame, not to a pool texture.
 */
BOOST_AUTO_TEST_CASE(movie_frame_slot_follows_the_pool)
{
	BOOST_TEST(render::movie_frame_slot(pool_size).index == pool_size + 1);
	BOOST_TEST((render::slot_place_of(render::movie_frame_slot(pool_size), pool_size).owner ==
	            render::slot_owner::movie_frame));
}

/* Test that slot none and the slots past the movie frame's lead nowhere,
 * so the front end draws no texture for them.
 */
BOOST_AUTO_TEST_CASE(other_slots_lead_nowhere)
{
	BOOST_TEST((render::slot_place_of(render::texture_slot::none, pool_size).owner ==
	            render::slot_owner::none));
	BOOST_TEST(
	    (render::slot_place_of({pool_size + 2}, pool_size).owner == render::slot_owner::none));
}

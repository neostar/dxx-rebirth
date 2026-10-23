/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/gl_texture_names.h"

#include <stdexcept>

#include "common/render/render_device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth gl_texture_names
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;
namespace gl = dcx::render::gl;

/* Test that a slot holds the name it was given until it is given another
 * or 0, and that each change returns the name held before.
 */
BOOST_AUTO_TEST_CASE(slot_holds_its_name)
{
	gl::texture_names names;
	const render::texture_slot a{1}, b{20001};
	BOOST_TEST(names.find(a) == 0u);
	BOOST_TEST(names.exchange(a, 7) == 0u);
	BOOST_TEST(names.exchange(b, 9) == 0u);
	BOOST_TEST(names.find(a) == 7u);
	BOOST_TEST(names.find(b) == 9u);
	BOOST_TEST(names.size() == 2u);
	BOOST_TEST(names.exchange(a, 8) == 7u);
	BOOST_TEST(names.exchange(a, 0) == 8u);
	BOOST_TEST(names.find(a) == 0u);
	BOOST_TEST(names.size() == 1u);
}

/* Test that a new adapter's names take every texture of the old one, which
 * keeps none, so a mode change on the same GL context keeps the textures.
 */
BOOST_AUTO_TEST_CASE(take_moves_every_name)
{
	gl::texture_names old_names, new_names;
	(void)old_names.exchange({1}, 7);
	(void)old_names.exchange({2}, 8);
	new_names.take(old_names);
	BOOST_TEST(new_names.find({1}) == 7u);
	BOOST_TEST(new_names.find({2}) == 8u);
	BOOST_TEST(new_names.size() == 2u);
	BOOST_TEST(old_names.size() == 0u);
	BOOST_TEST(old_names.find({1}) == 0u);
}

/* Test that taking names into names that hold some is refused, since the
 * names held would be lost without being deleted.
 */
BOOST_AUTO_TEST_CASE(take_into_held_names_is_refused)
{
	gl::texture_names a, b;
	(void)a.exchange({1}, 7);
	(void)b.exchange({2}, 8);
	BOOST_CHECK_THROW(a.take(b), std::logic_error);
	BOOST_TEST(b.find({2}) == 8u);
}

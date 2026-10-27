/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/ambient.h"

#include <array>
#include <span>

#include "common/render/render_device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth ambient
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

namespace {

const std::array<render::vertex, 3> triangle{{
    {.pos = {{0, 0, 0}}, .rgba = {{1, 0, 0, 1}}, .uv = {{0, 0}}},
    {.pos = {{1, 0, 0}}, .rgba = {{0, 1, 0, 1}}, .uv = {{0, 0}}},
    {.pos = {{0, 1, 0}}, .rgba = {{0, 0, 1, 1}}, .uv = {{0, 0}}},
}};

}

/* Test that a new ambient state and a default draw state test alpha
 * against the one named default reference.
 */
BOOST_AUTO_TEST_CASE(default_alpha_ref_is_named_default)
{
	BOOST_TEST(render::ambient{}.alpha_ref == render::default_alpha_ref);
	BOOST_TEST(render::draw_state{}.alpha_ref == render::default_alpha_ref);
	BOOST_TEST(render::default_alpha_ref == 0.02f);
}

/* Test that blending off draws with blend none, and turning it back on
 * restores the blend function set before, as glEnable(GL_BLEND) does.
 */
BOOST_AUTO_TEST_CASE(blending_off_keeps_function)
{
	render::ambient a;
	a.blend_function = render::blend::additive_a;
	a.blending = false;
	BOOST_TEST((a.state(render::wrap::clamp).blend == render::blend::none));
	a.blending = true;
	BOOST_TEST((a.state(render::wrap::clamp).blend == render::blend::additive_a));
}

/* Test that a fan carries every part of the ambient state, so the device
 * needs nothing from an earlier draw.
 */
BOOST_AUTO_TEST_CASE(fan_carries_ambient_state)
{
	render::ambient a;
	a.alpha_test = true;
	a.alpha_ref = 0.8f;
	a.depth = render::depth::test_and_write;
	a.cull = render::cull::front;
	a.viewport = {.x = 1, .y = 2, .size = {.width = 3, .height = 4}};
	a.transforms = {.projection = render::view_perspective, .modelview = render::identity{}};
	const auto call{a.fan(triangle, render::texture_slot{7}, render::wrap::repeat)};
	BOOST_TEST((call.topology == render::topology::fan));
	BOOST_TEST((call.texture == render::texture_slot{7}));
	BOOST_TEST((call.state == render::draw_state{.blend = render::blend::normal,
	                                             .depth = render::depth::test_and_write,
	                                             .cull = render::cull::front,
	                                             .alpha_ref = 0.8f,
	                                             .wrap = render::wrap::repeat,
	                                             .line_width = 1.f}));
	BOOST_TEST((call.viewport == a.viewport));
	BOOST_TEST((call.transforms == a.transforms));
	BOOST_TEST(std::ranges::equal(call.vertices, triangle));
}

/* Test that a new ambient state, which stands for GL before the first
 * ogl_start_frame (title, menus), draws with the alpha test off, and that
 * turning the test on draws with the alpha reference.
 */
BOOST_AUTO_TEST_CASE(alpha_test_off_until_turned_on)
{
	render::ambient a;
	a.alpha_ref = 0.8f;
	BOOST_TEST(a.state(render::wrap::clamp).alpha_ref == render::alpha_test_off);
	BOOST_TEST(render::alpha_test_off == 0.f);
	a.alpha_test = true;
	BOOST_TEST(a.state(render::wrap::clamp).alpha_ref == 0.8f);
}

/* Test that a draw takes the line width the game set last, as GL's line
 * width stays until changed.
 */
BOOST_AUTO_TEST_CASE(draw_takes_ambient_line_width)
{
	render::ambient a;
	BOOST_TEST(a.state(render::wrap::clamp).line_width == 1.f);
	a.line_width = 3.f;
	BOOST_TEST(a.state(render::wrap::clamp).line_width == 3.f);
}

/* Test that a call of any topology carries the ambient state, untextured,
 * as the 2D primitives draw.
 */
BOOST_AUTO_TEST_CASE(call_of_topology_carries_ambient_state)
{
	render::ambient a;
	a.blend_function = render::blend::additive_a;
	a.alpha_test = true;
	a.alpha_ref = 0.8f;
	a.depth = render::depth::test_and_write;
	a.cull = render::cull::front;
	a.line_width = 3.f;
	a.viewport = {.x = 1, .y = 2, .size = {.width = 3, .height = 4}};
	a.transforms = {.projection = render::view_perspective, .modelview = render::identity{}};
	const auto call{a.call(render::topology::lines, triangle)};
	BOOST_TEST((call.topology == render::topology::lines));
	BOOST_TEST((call.texture == render::texture_slot::none));
	BOOST_TEST((call.state == render::draw_state{.blend = render::blend::additive_a,
	                                             .depth = render::depth::test_and_write,
	                                             .cull = render::cull::front,
	                                             .alpha_ref = 0.8f,
	                                             .wrap = render::wrap::clamp,
	                                             .line_width = 3.f}));
	BOOST_TEST((call.viewport == a.viewport));
	BOOST_TEST((call.transforms == a.transforms));
	BOOST_TEST(std::ranges::equal(call.vertices, triangle));
}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/frame_setup.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

#include "common/render/device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth frame setup
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

namespace {

/* A draw as the fake device keeps it: the vertices copied. */
struct kept_draw
{
	render::draw_state state;
	render::topology topology;
	render::transforms transforms;
	std::vector<render::vertex> vertices;
};

/* What a frame asked of a device, in order: clears and draws. */
using call = std::variant<render::clear_targets, kept_draw>;

class recording_device final : public render::device
{
  public:
	std::vector<call> calls;
	recording_device() : device{render::caps{}}
	{}
	void draw(const render::draw_call &c) override
	{
		calls.emplace_back(kept_draw{.state = c.state,
		                             .topology = c.topology,
		                             .transforms = c.transforms,
		                             .vertices = {c.vertices.begin(), c.vertices.end()}});
	}
	void texture(render::texture_slot, const render::texture_image *) override
	{}
	void clear(const render::clear_targets t) override
	{
		calls.emplace_back(t);
	}
	void present() override
	{}
	void read_pixels(render::read_source, const render::read_rect &, render::pixel_format,
	                 std::span<std::uint8_t>) override
	{
		throw std::logic_error("not used");
	}
};

}

/* Test that a palette step with no positive part darkens: 2.5 times its
 * magnitude, blended with palfx_neg.
 */
BOOST_AUTO_TEST_CASE(negative_palette_step_darkens_scaled)
{
	const auto f{render::flash_of(-0.2f, 0.f, -0.4f, true)};
	BOOST_REQUIRE(f.has_value());
	BOOST_TEST((f.value().blend == render::blend::palfx_neg));
	BOOST_TEST(f.value().rgb[0] == 0.5f);
	BOOST_TEST(f.value().rgb[1] == 0.f);
	BOOST_TEST(f.value().rgb[2] == 1.f);
}

/* Test that a palette step with a positive part adds its colour as it is. */
BOOST_AUTO_TEST_CASE(positive_palette_step_adds)
{
	const auto f{render::flash_of(0.5f, -0.1f, 0.f, true)};
	BOOST_REQUIRE(f.has_value());
	BOOST_TEST((f.value().blend == render::blend::additive_c));
	BOOST_TEST(f.value().rgb[0] == 0.5f);
	BOOST_TEST(f.value().rgb[1] == -0.1f);
}

/* Test that no palette step draws no flash. */
BOOST_AUTO_TEST_CASE(inactive_palette_step_has_no_flash)
{
	BOOST_TEST(!render::flash_of(0.5f, 0.5f, 0.5f, false).has_value());
}

/* Test that a device without the stereo and copy caps refuses the calls
 * that need them.
 */
BOOST_AUTO_TEST_CASE(device_without_caps_refuses_stereo_and_copy)
{
	recording_device d;
	BOOST_CHECK_THROW(d.select_eye(render::eye::left), std::logic_error);
	BOOST_CHECK_THROW(
	    d.copy_region({.src_x = 0, .src_y = 0, .dest_x = 1, .dest_y = 1, .size = {1, 1}}, false),
	    std::logic_error);
}

/* Test that a flash draws one quad over the whole target in the 2D state
 * the frame ends with (no depth, no culling, the 2D projection), whatever
 * blend the game set, and leaves the ambient blend normal for the next draw.
 */
BOOST_AUTO_TEST_CASE(flash_draws_one_screen_quad_in_2d_state_and_restores_blend)
{
	recording_device d;
	render::ambient a;
	a.blend_function = render::blend::additive_a;
	a.blending = false;
	render::draw_palette_flash(d, a,
	                           {.blend = render::blend::additive_c, .rgb = {{0.1f, 0.2f, 0.3f}}});
	BOOST_REQUIRE_EQUAL(d.calls.size(), 1u);
	const auto &k{std::get<kept_draw>(d.calls[0])};
	BOOST_TEST((k.state.blend == render::blend::additive_c));
	BOOST_TEST((k.state.depth == render::depth::off));
	BOOST_TEST((k.state.cull == render::cull::none));
	BOOST_TEST((k.transforms == render::transforms{render::screen_ortho, render::identity{}}));
	BOOST_TEST((k.topology == render::topology::fan));
	BOOST_REQUIRE_EQUAL(k.vertices.size(), 4u);
	constexpr std::array<std::array<float, 2>, 4> corners{{{{0, 0}}, {{0, 1}}, {{1, 1}}, {{1, 0}}}};
	for (std::size_t i{0}; i != 4; ++i)
	{
		BOOST_TEST(k.vertices[i].pos[0] == corners[i][0]);
		BOOST_TEST(k.vertices[i].pos[1] == corners[i][1]);
		BOOST_TEST(k.vertices[i].pos[2] == 0.f);
		BOOST_TEST((k.vertices[i].rgba == std::array<float, 4>{{0.1f, 0.2f, 0.3f, 1.f}}),
		           boost::test_tools::per_element());
	}
	BOOST_TEST((a.blend_function == render::blend::normal));
	BOOST_TEST(a.blending);
}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The rtrace tests: draws and synthetic traces through the GL adapter on a
 * hidden window (gl_host.h), compared by the pixels they give, never by GL
 * calls.  Run them through tools/dev/rtrace-tests.sh, which supplies the
 * environment the hidden window needs.
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "common/render/readback.h"
#include "common/render/render_device.h"
#include "common/render/rtrace/gl_host.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth rtrace
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

namespace {

constexpr render::extent target{.width = 64, .height = 48};

/* A draw of vertices in a viewport of the whole target, with an ortho
 * projection of [0, 1] on both axes.
 */
render::draw_call flat_draw(const render::topology t, const std::span<const render::vertex> v)
{
	return {
	    .topology = t,
	    .vertices = v,
	    .texture = render::texture_slot::none,
	    .state = {},
	    .viewport = {.x = 0, .y = 0, .size = target},
	    .transforms =
	        {.projection =
			     render::ortho{
	                 .left = 0, .right = 1, .bottom = 0, .top = 1, .z_near = -1, .z_far = 1},
			 .modelview = render::identity{}},
	};
}

constexpr std::array<float, 4> red{{1, 0, 0, 1}};
constexpr std::array<float, 2> no_uv{{0, 0}};

bool every_pixel_is(const std::span<const std::uint8_t> rgb, const std::array<std::uint8_t, 3> p)
{
	for (std::size_t i{0}; i + 2 < rgb.size(); i += 3)
		if (rgb[i] != p[0] || rgb[i + 1] != p[1] || rgb[i + 2] != p[2])
			return false;
	return !rgb.empty();
}

/* The image being drawn on d, a device for target. */
std::vector<std::uint8_t> drawing(render::device &d)
{
	std::vector<std::uint8_t> rgb(render::image_bytes(target, render::pixel_format::rgb8));
	d.read_pixels(render::read_source::drawing, {.x = 0, .y = 0, .size = target},
	              render::pixel_format::rgb8, rgb);
	return rgb;
}

/* The part of the target from row y0 (0 its bottom, 1 its top) up, in
 * colour rgba, drawn with state s.  The call views the quad's vertices.
 */
struct quad
{
	std::array<render::vertex, 4> vertices;
	render::draw_call call;
	explicit quad(const std::array<float, 4> &rgba, const float y0 = 0,
	              const render::draw_state &s = {})
	    : vertices{{
	          {.pos = {{0, y0, 0}}, .rgba = rgba, .uv = no_uv},
	          {.pos = {{1, y0, 0}}, .rgba = rgba, .uv = no_uv},
	          {.pos = {{1, 1, 0}}, .rgba = rgba, .uv = no_uv},
	          {.pos = {{0, 1, 0}}, .rgba = rgba, .uv = no_uv},
	      }},
	      call{flat_draw(render::topology::fan, vertices)}
	{
		call.state = s;
	}
	quad(const quad &) = delete;
	quad &operator=(const quad &) = delete;
	quad(quad &&) = delete;
	quad &operator=(quad &&) = delete;
	~quad() = default;
};

}

/* Test that the hidden window's adapter draws: a draw over the whole target
 * reads back in its colour.
 */
BOOST_AUTO_TEST_CASE(hidden_window_draws)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	d.clear({.color = true, .depth = true});
	const quad q{red};
	d.draw(q.call);
	BOOST_TEST(every_pixel_is(drawing(d), {{255, 0, 0}}));
}

/* Test that the adapter on the host keeps the alpha test: an unblended draw
 * whose alpha is under its alpha reference leaves the target black.
 */
BOOST_AUTO_TEST_CASE(alpha_under_reference_is_discarded)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	d.clear({.color = true, .depth = true});
	const quad q{{{1, 0, 0, 0.01f}}, 0, {.blend = render::blend::none}};
	d.draw(q.call);
	BOOST_TEST(every_pixel_is(drawing(d), {{0, 0, 0}}));
}

/* Test that the adapter on the host tests depth LEQUAL: a draw at the depth
 * of an earlier one covers it.
 */
BOOST_AUTO_TEST_CASE(equal_depth_draw_covers_the_earlier)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	d.clear({.color = true, .depth = true});
	const quad first{red, 0, {.depth = render::depth::test_and_write}};
	const quad second{{{0, 1, 0, 1}}, 0, {.depth = render::depth::test_and_write}};
	d.draw(first.call);
	d.draw(second.call);
	BOOST_TEST(every_pixel_is(drawing(d), {{0, 255, 0}}));
}

/* Test that the adapter on the host culls front faces: a counter-clockwise
 * draw culling front faces leaves the target black.
 */
BOOST_AUTO_TEST_CASE(front_face_is_culled)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	d.clear({.color = true, .depth = true});
	const quad q{red, 0, {.cull = render::cull::front}};
	d.draw(q.call);
	BOOST_TEST(every_pixel_is(drawing(d), {{0, 0, 0}}));
}

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
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "common/render/ambient.h"
#include "common/render/blit.h"
#include "common/render/frame_setup.h"
#include "common/render/readback.h"
#include "common/render/render_device.h"
#include "common/render/replay.h"
#include "common/render/rtrace/gl_host.h"
#include "common/render/rtrace/sample_trace.h"
#include "common/render/trace.h"
#include "common/render/transform_matrix.h"

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "d_gl.h"
#include "tools/dev/upstream_end.h"
// clang-format on

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth rtrace
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;
namespace trace = dcx::render::trace;

namespace {

using bytes = std::vector<std::uint8_t>;

constexpr render::extent target{render::rtrace::sample_target};

/* The frames a replay of the trace in t gives, and how it ended. */
struct outcome
{
	std::vector<render::replayed_frame> frames;
	bool ok;
	trace::read_error error;
};

outcome replay_on_gl(const bytes &t)
{
	trace::reader r{t};
	const auto h{r.header()};
	BOOST_REQUIRE(h.has_value());
	render::rtrace::gl_host host{h.value().target};
	outcome o{.frames = {}, .ok = true, .error = {}};
	const auto done{render::replay(
	    r, host.device(), [&o](const render::replayed_frame &f) { o.frames.push_back(f); })};
	if (!done)
	{
		o.ok = false;
		o.error = done.error();
	}
	return o;
}

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

/* A width by 1 texture image of nearest-filtered opaque texels. */
struct texture_row
{
	std::vector<std::uint8_t> rgba;
	render::texture_image image;
	explicit texture_row(std::vector<std::uint8_t> texels)
	    : rgba{std::move(texels)},
	      image{{.size = {.width = static_cast<std::uint32_t>(rgba.size() / 4), .height = 1},
		         .format = render::texture_format::rgba8,
		         .filter = render::filter::nearest,
		         .anisotropic = false,
		         .has_transparency = false},
		        rgba}
	{}
};

/* The whole target in white, textured from slot with wrap, its texture
 * coordinates running from 0 to u_end left to right.  The call views the
 * vertices.
 */
struct textured_quad
{
	std::array<render::vertex, 4> vertices;
	render::draw_call call;
	textured_quad(const render::texture_slot slot, const render::wrap wrap, const float u_end)
	    : vertices{{
	          {.pos = {{0, 0, 0}}, .rgba = {{1, 1, 1, 1}}, .uv = {{0, 0}}},
	          {.pos = {{1, 0, 0}}, .rgba = {{1, 1, 1, 1}}, .uv = {{u_end, 0}}},
	          {.pos = {{1, 1, 0}}, .rgba = {{1, 1, 1, 1}}, .uv = {{u_end, 1}}},
	          {.pos = {{0, 1, 0}}, .rgba = {{1, 1, 1, 1}}, .uv = {{0, 1}}},
	      }},
	      call{flat_draw(render::topology::fan, vertices)}
	{
		call.texture = slot;
		call.state.wrap = wrap;
	}
	textured_quad(const textured_quad &) = delete;
	textured_quad &operator=(const textured_quad &) = delete;
	textured_quad(textured_quad &&) = delete;
	textured_quad &operator=(textured_quad &&) = delete;
	~textured_quad() = default;
};

/* The colour of column x of the middle row of image rgb of target. */
std::array<std::uint8_t, 3> column(const std::span<const std::uint8_t> rgb, const std::uint32_t x)
{
	const std::size_t i{(std::size_t{target.height / 2} * target.width + x) * 3};
	return {rgb[i], rgb[i + 1], rgb[i + 2]};
}

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

/* Test that a cleared frame reads back black, at the target's size and
 * under the present's frame index.
 */
BOOST_AUTO_TEST_CASE(cleared_frame_is_black)
{
	trace::writer w{target};
	w.clear({.color = true, .depth = true});
	w.present({.frame = 7});
	const auto o{replay_on_gl(w.take())};
	BOOST_TEST(o.ok);
	BOOST_REQUIRE(o.frames.size() == 1u);
	const auto &f{o.frames.front()};
	BOOST_TEST(f.frame == 7u);
	BOOST_TEST((f.size == target));
	BOOST_TEST(f.rgb.size() == std::size_t{64} * 48 * 3);
	BOOST_TEST(every_pixel_is(f.rgb, {{0, 0, 0}}));
}

/* Test that a draw covering the target shows in its frame, in its colour. */
BOOST_AUTO_TEST_CASE(draw_shows_in_its_frame)
{
	trace::writer w{target};
	w.clear({.color = true, .depth = true});
	const quad q{red};
	w.draw(q.call);
	w.present({.frame = 0});
	const auto o{replay_on_gl(w.take())};
	BOOST_TEST(o.ok);
	BOOST_REQUIRE(o.frames.size() == 1u);
	BOOST_TEST(every_pixel_is(o.frames.front().rgb, {{255, 0, 0}}));
}

/* Test that a trace replayed twice, each time on a new window, gives the
 * same frames, and that its two different frames hash differently.
 */
BOOST_AUTO_TEST_CASE(replay_twice_gives_identical_hashes)
{
	trace::writer w{target};
	w.clear({.color = true, .depth = true});
	w.draw(flat_draw(render::topology::triangles, render::rtrace::sample_shaded_triangle));
	w.present({.frame = 0});
	w.clear({.color = true, .depth = true});
	const quad q{red};
	w.draw(q.call);
	w.present({.frame = 1});
	const auto t{w.take()};
	const auto first{replay_on_gl(t)};
	const auto second{replay_on_gl(t)};
	BOOST_TEST(first.ok);
	BOOST_TEST(second.ok);
	BOOST_REQUIRE(first.frames.size() == 2u);
	BOOST_REQUIRE(second.frames.size() == 2u);
	for (std::size_t i{0}; i != 2; ++i)
	{
		BOOST_TEST(first.frames[i].hash == second.frames[i].hash);
		BOOST_TEST((first.frames[i].rgb == second.frames[i].rgb));
	}
	BOOST_TEST(first.frames[0].hash != first.frames[1].hash);
}

/* Test that a frame comes back with its top row first: a draw over the top
 * half fills the first half of the rows.
 */
BOOST_AUTO_TEST_CASE(frame_is_top_row_first)
{
	trace::writer w{target};
	w.clear({.color = true, .depth = true});
	const quad top_half{red, 0.5f};
	w.draw(top_half.call);
	w.present({.frame = 0});
	const auto o{replay_on_gl(w.take())};
	BOOST_REQUIRE(o.frames.size() == 1u);
	const std::span<const std::uint8_t> rgb{o.frames.front().rgb};
	const auto half{rgb.size() / 2};
	BOOST_TEST(every_pixel_is(rgb.first(half), {{255, 0, 0}}));
	BOOST_TEST(every_pixel_is(rgb.subspan(half), {{0, 0, 0}}));
}

/* Test that a trace ending inside a record replays the frames before it and
 * then reports it as truncated.
 */
BOOST_AUTO_TEST_CASE(malformed_trace_reports_its_error_after_its_frames)
{
	trace::writer w{target};
	w.clear({.color = true, .depth = true});
	w.present({.frame = 0});
	w.present({.frame = 1});
	auto t{w.take()};
	t.pop_back();
	const auto o{replay_on_gl(t)};
	BOOST_TEST(!o.ok);
	BOOST_TEST((o.error == trace::read_error::truncated));
	BOOST_REQUIRE(o.frames.size() == 1u);
	BOOST_TEST(o.frames.front().frame == 0u);
}

namespace {

/* The bytes of the committed trace the i-th argument after `--` names:
 * tools/dev/rtrace-tests.sh passes the sample's, the overbright
 * trace's, the flash trace's, then the walls trace's.
 */
[[nodiscard]]
bytes committed_trace(const std::size_t i)
{
	const auto &suite{boost::unit_test::framework::master_test_suite()};
	BOOST_REQUIRE_MESSAGE(suite.argc == 5,
	                      "usage: test-rtrace [BOOST OPTIONS] -- SAMPLE OVERBRIGHT FLASH WALLS");
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-container): argv holds argc pointers
	const std::span<char *const> args{suite.argv, static_cast<std::size_t>(suite.argc)};
	std::ifstream in{args[i], std::ios::binary};
	BOOST_REQUIRE(in.good());
	return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}

/* Test that the committed sample trace is the writer's: its bytes are what
 * sample_trace() writes.
 */
BOOST_AUTO_TEST_CASE(sample_trace_is_the_writers)
{
	BOOST_TEST((committed_trace(1) == render::rtrace::sample_trace()));
}

/* Test that the committed overbright trace is the writer's: its bytes are
 * what overbright_trace() writes.
 */
BOOST_AUTO_TEST_CASE(overbright_trace_is_the_writers)
{
	BOOST_TEST((committed_trace(2) == render::rtrace::overbright_trace()));
}

/* Test that the committed flash trace is the writer's: its bytes are what
 * flash_trace() writes.
 */
BOOST_AUTO_TEST_CASE(flash_trace_is_the_writers)
{
	BOOST_TEST((committed_trace(3) == render::rtrace::flash_trace()));
}

/* Test that the committed walls trace is the writer's: its bytes are what
 * walls_trace() writes.
 */
BOOST_AUTO_TEST_CASE(walls_trace_is_the_writers)
{
	BOOST_TEST((committed_trace(4) == render::rtrace::walls_trace()));
}

/* Test the depth band trace on GL (#28): between the near plane and twice
 * it, GL keeps the nearer green quad, 32 by 24 pixels, over every pixel of
 * the blue one drawn after it, and the frame hashes as pinned; test-sdlgpu's
 * depth_band_keeps_the_nearer holds SDL_GPU on Metal to the same hash.
 */
BOOST_AUTO_TEST_CASE(depth_band_keeps_the_nearer_on_gl)
{
	const auto o{replay_on_gl(render::rtrace::depth_band_trace())};
	BOOST_TEST(o.ok);
	BOOST_REQUIRE(o.frames.size() == 1u);
	const auto &rgb{o.frames.front().rgb};
	std::size_t blue{0}, green{0};
	for (std::size_t i{0}; i + 2 < rgb.size(); i += 3)
	{
		if (rgb[i] == 0 && rgb[i + 1] == 0 && rgb[i + 2] == 255)
			++blue;
		if (rgb[i] == 0 && rgb[i + 1] == 255 && rgb[i + 2] == 0)
			++green;
	}
	BOOST_TEST(blue == 0u);
	BOOST_TEST(green == std::size_t{32} * 24);
	BOOST_TEST(o.frames.front().hash == 0x053439a05ecf8825u);
}

/* Test that a textured draw samples the texture of its own slot, not the
 * one uploaded last.
 */
BOOST_AUTO_TEST_CASE(textured_draw_samples_its_slot)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	const texture_row green{{0, 255, 0, 255}};
	const texture_row blue{{0, 0, 255, 255}};
	d.texture({1}, &green.image);
	d.texture({2}, &blue.image);
	d.clear({.color = true, .depth = true});
	const textured_quad q{{1}, render::wrap::clamp, 1};
	d.draw(q.call);
	BOOST_TEST(every_pixel_is(drawing(d), {{0, 255, 0}}));
}

/* Test that a textured draw takes its wrap: texture coordinates past 1
 * repeat the texture with repeat and its edge with clamp.
 */
BOOST_AUTO_TEST_CASE(textured_draw_takes_its_wrap)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	const texture_row red_blue{{255, 0, 0, 255, 0, 0, 255, 255}};
	d.texture({1}, &red_blue.image);
	const std::array<std::uint8_t, 3> red_rgb{{255, 0, 0}}, blue_rgb{{0, 0, 255}};
	for (const auto wrap : {render::wrap::repeat, render::wrap::clamp})
	{
		d.clear({.color = true, .depth = true});
		/* u runs 0 to 2: each quarter of the target is one texel wide. */
		const textured_quad q{{1}, wrap, 2};
		d.draw(q.call);
		const auto rgb{drawing(d)};
		BOOST_TEST((column(rgb, 8) == red_rgb));
		BOOST_TEST((column(rgb, 24) == blue_rgb));
		BOOST_TEST((column(rgb, 40) == (wrap == render::wrap::repeat ? red_rgb : blue_rgb)));
	}
}

/* Test that a textured draw of a slot that holds no texture, which a
 * well-formed trace never makes, is refused rather than drawn with
 * whatever texture GL has bound.
 */
BOOST_AUTO_TEST_CASE(textured_draw_of_empty_slot_is_refused)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	const texture_row green{{0, 255, 0, 255}};
	d.texture({1}, &green.image);
	d.texture({1}, nullptr);
	const textured_quad q{{1}, render::wrap::clamp, 1};
	BOOST_CHECK_THROW(d.draw(q.call), std::logic_error);
}

/* Test that the ortho recipe's matrix (transform_matrix.h), which the
 * SDL_GPU adapter draws with, is the one glOrtho builds on the host's GL,
 * element for element: the game's 2D projection and an asymmetric one.
 */
BOOST_AUTO_TEST_CASE(ortho_matrix_is_gls)
{
	const render::rtrace::gl_host host{target};
	for (const render::ortho &o :
	     {render::ortho{.left = 0, .right = 1, .bottom = 0, .top = 1, .z_near = -1, .z_far = 1},
	      render::ortho{
	          .left = -3, .right = 5, .bottom = -2, .top = 6, .z_near = 0.5, .z_far = 100}})
	{
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(o.left, o.right, o.bottom, o.top, o.z_near, o.z_far);
		render::matrix gl{};
		glGetFloatv(GL_PROJECTION_MATRIX, gl.data());
		BOOST_TEST(render::matrix_of(o) == gl, boost::test_tools::per_element());
	}
}

/* Test that the perspective recipe's matrix is the one the GL adapter
 * builds on the host's GL (gluPerspective, then the stereo shift), element
 * for element: the 3D view's, a stereo eye's, others whose elements
 * round differently under SGI's form of gluPerspective, and one whose
 * element 14 rounds differently unless 2 near is rounded to float first.
 */
BOOST_AUTO_TEST_CASE(perspective_matrix_is_gls)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	for (const render::perspective &p :
	     {render::view_perspective,
	      render::perspective{
	          .fovy = 90, .aspect = 1, .z_near = 0.1, .z_far = 5000, .stereo_dx = -0.0375f},
	      render::perspective{.fovy = 63.5, .aspect = 4.0 / 3, .z_near = 0.37, .z_far = 1234.5},
	      render::perspective{.fovy = 45, .aspect = 1, .z_near = 1, .z_far = 100},
	      render::perspective{.fovy = 30, .aspect = 0.75, .z_near = 2, .z_far = 3000},
	      render::perspective{.fovy = 120, .aspect = 1, .z_near = 0.25, .z_far = 77.7},
	      render::perspective{.fovy = 10, .aspect = 2, .z_near = 3.3, .z_far = 9},
	      render::perspective{.fovy = 75, .aspect = 1.6, .z_near = 0.05, .z_far = 640},
	      render::perspective{.fovy = 48.342374243203913,
	                          .aspect = 1.475677203535734,
	                          .z_near = 0.0011208076448162329,
	                          .z_far = 372.49184310084053}})
	{
		auto call{render::rtrace::sample_draw(render::topology::fan, {})};
		call.transforms.projection = p;
		d.draw(call);
		render::matrix gl{};
		glGetFloatv(GL_PROJECTION_MATRIX, gl.data());
		BOOST_TEST(render::matrix_of(p) == gl, boost::test_tools::per_element());
	}
}

/* Test that the GL adapter applies the state of a draw with no vertices:
 * after a draw with a moved modelview, an empty draw with the identity
 * leaves GL's modelview matrix the identity, which the front end relies on
 * to bring GL to its ambient state for the code not yet drawing through the
 * device (ogl_sync_gl_state).
 */
BOOST_AUTO_TEST_CASE(empty_draw_applies_its_state)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	quad q{red};
	q.call.transforms.modelview =
	    render::translate_scale{.translate = {{0.25f, 0.5f, 0}}, .scale = {{2, 3, 1}}};
	d.draw(q.call);
	d.draw(flat_draw(render::topology::fan, {}));
	std::array<GLfloat, 16> m{};
	glGetFloatv(GL_MODELVIEW_MATRIX, m.data());
	const std::array<GLfloat, 16> identity{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	BOOST_TEST(m == identity, boost::test_tools::per_element());
}

/* Test that each line draw sets its own width through the state shadow,
 * with no empty draw to restore one: after a line at width 2 and a line at
 * width 1, GL draws at 1, and a second line at width 2 sets 2 again, as
 * the vertex reticle draws on each frame among lines of the frame's width.
 */
BOOST_AUTO_TEST_CASE(each_line_draw_sets_its_width)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	const std::array<render::vertex, 2> line{{
	    {.pos = {{0, 0.5f, 0}}, .rgba = red, .uv = no_uv},
	    {.pos = {{1, 0.5f, 0}}, .rgba = red, .uv = no_uv},
	}};
	auto wide{flat_draw(render::topology::lines, line)};
	wide.state.line_width = 2.f;
	auto narrow{flat_draw(render::topology::lines, line)};
	narrow.state.line_width = 1.f;
	d.draw(wide);
	d.draw(narrow);
	GLfloat width{};
	glGetFloatv(GL_LINE_WIDTH, &width);
	BOOST_TEST(width == 1.f);
	d.draw(wide);
	glGetFloatv(GL_LINE_WIDTH, &width);
	BOOST_TEST(width == 2.f);
}

/* Test that a rectangle reaching past the target's corner reads the
 * target's pixels inside it and zero outside, whatever the buffer held:
 * the readback contract both adapters keep (readback.h).
 */
BOOST_AUTO_TEST_CASE(read_past_the_corner_is_zero_outside)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	d.clear({.color = true, .depth = true});
	const quad q{red};
	d.draw(q.call);
	/* 4x4 at (62, 46): 2x2 inside, at the rectangle's top left. */
	std::array<std::uint8_t, std::size_t{4} * 4 * 3> rgb{};
	rgb.fill(0xaa);
	d.read_pixels(render::read_source::drawing, {.x = 62, .y = 46, .size = {4, 4}},
	              render::pixel_format::rgb8, rgb);
	std::array<std::uint8_t, std::size_t{4} * 4 * 3> expected{};
	for (const std::size_t pixel : {0u, 1u, 4u, 5u})
		expected[pixel * 3] = 255;
	BOOST_TEST(rgb == expected, boost::test_tools::per_element());
}

/* Test that a readback anchored at the target's bottom and reaching past
 * its top, as the PNG screenshot reads a height rounded up to 4, gives
 * zero rows above the target and then the target's rows, top row first:
 * a target red in its top half and green in its bottom half.
 */
BOOST_AUTO_TEST_CASE(read_past_the_top_has_zero_rows_first)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	d.clear({.color = true, .depth = true});
	const quad green{{{0, 1, 0, 1}}};
	const quad top_half{red, 0.5f};
	d.draw(green.call);
	d.draw(top_half.call);
	const render::extent size{.width = target.width, .height = target.height + 2};
	std::vector<std::uint8_t> rgb(render::image_bytes(size, render::pixel_format::rgb8), 0xaa);
	d.read_pixels(render::read_source::drawing, {.x = 0, .y = -2, .size = size},
	              render::pixel_format::rgb8, rgb);
	const std::span<const std::uint8_t> image{rgb};
	const std::size_t row{std::size_t{size.width} * 3};
	const std::size_t half{target.height / 2};
	BOOST_TEST(
	    std::ranges::all_of(image.first(2 * row), [](const std::uint8_t b) { return b == 0; }));
	BOOST_TEST(every_pixel_is(image.subspan(2 * row, half * row), {{255, 0, 0}}));
	BOOST_TEST(every_pixel_is(image.subspan((2 + half) * row), {{0, 255, 0}}));
}

/* Test that a rectangle wholly outside the target reads zero. */
BOOST_AUTO_TEST_CASE(read_outside_the_target_is_zero)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	d.clear({.color = true, .depth = true});
	const quad q{red};
	d.draw(q.call);
	std::array<std::uint8_t, 16> outside{};
	outside.fill(0xaa);
	d.read_pixels(render::read_source::drawing, {.x = 64, .y = 0, .size = {2, 2}},
	              render::pixel_format::rgba8, outside);
	const std::array<std::uint8_t, 16> zero{};
	BOOST_TEST(outside == zero, boost::test_tools::per_element());
}

/* Test that a rectangle reaching past the target's left edge reads, as
 * RGBA, zero left of the target and the target's pixels right of it.
 */
BOOST_AUTO_TEST_CASE(read_past_the_left_edge_is_zero_outside)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	d.clear({.color = true, .depth = true});
	const quad q{red};
	d.draw(q.call);
	/* 4x4 at (-2, 46): 2x2 inside, at the rectangle's top right. */
	std::array<std::uint8_t, std::size_t{4} * 4 * 4> rgba{};
	rgba.fill(0xaa);
	d.read_pixels(render::read_source::drawing, {.x = -2, .y = 46, .size = {4, 4}},
	              render::pixel_format::rgba8, rgba);
	std::array<std::uint8_t, std::size_t{4} * 4 * 4> expected{};
	for (const std::size_t pixel : {2u, 3u, 6u, 7u})
	{
		expected[pixel * 4] = 255;
		expected[pixel * 4 + 3] = 255;
	}
	BOOST_TEST(rgba == expected, boost::test_tools::per_element());
}

/* Test that the presented image reads zero before the first present, as
 * on the SDL_GPU adapter, whatever GL's front buffer holds: red, cleared
 * there behind the device's back.
 */
BOOST_AUTO_TEST_CASE(presented_reads_zero_before_a_present)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	glDrawBuffer(GL_FRONT);
	glClearColor(1, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0, 0, 0, 0);
	glDrawBuffer(GL_BACK);
	std::array<std::uint8_t, std::size_t{2} * 2 * 3> rgb{};
	rgb.fill(0xaa);
	d.read_pixels(render::read_source::presented, {.x = 0, .y = 0, .size = {2, 2}},
	              render::pixel_format::rgb8, rgb);
	const std::array<std::uint8_t, std::size_t{2} * 2 * 3> zero{};
	BOOST_TEST(rgb == zero, boost::test_tools::per_element());
}

/* Test that a GL draw outside the device after a moved draw is not moved:
 * a quad over the whole target moved to its right half, then the front
 * end's sync (ogl_sync_gl_state), then the left half drawn with
 * glDrawArrays as the code not yet drawing through the device draws, leave
 * the left half green and the right half red.
 */
BOOST_AUTO_TEST_CASE(direct_draw_after_moved_draw_is_not_moved)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	render::ambient a;
	a.viewport = {.x = 0, .y = 0, .size = target};
	const quad whole{red};
	auto moved{a.call(render::topology::fan, whole.vertices)};
	moved.transforms.modelview =
	    render::translate_scale{.translate = {{0.5f, 0, 0}}, .scale = {{0.5f, 1, 1}}};
	d.draw(moved);
	d.draw(a.call(render::topology::lines, {}));
	const std::array<GLfloat, 8> left_half{{0, 0, 0.5f, 0, 0.5f, 1, 0, 1}};
	glColor4f(0, 1, 0, 1);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, left_half.data());
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glDisableClientState(GL_VERTEX_ARRAY);
	const auto rgb{drawing(d)};
	BOOST_TEST((column(rgb, target.width / 4) == std::array<std::uint8_t, 3>{{0, 255, 0}}));
	BOOST_TEST((column(rgb, target.width * 3 / 4) == std::array<std::uint8_t, 3>{{255, 0, 0}}));
}

namespace {

/* How a flash reaches the picture. */
enum class flash_path : std::uint8_t
{
	none,
	/* draw_palette_flash on the device. */
	device,
	/* The GL calls ogl_do_palfx made before the render device, replayed. */
	direct_gl,
};

/* The picture after a green quad and the flash of the palette step
 * (r, g, b), by path.
 */
std::vector<std::uint8_t> flashed_picture(const flash_path path, const float r, const float g,
                                          const float b)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	d.clear({.color = true, .depth = true});
	const quad base{{{0.2f, 0.6f, 0.3f, 1}}};
	d.draw(base.call);
	if (path == flash_path::device)
	{
		render::ambient a;
		a.viewport = {.x = 0, .y = 0, .size = target};
		const auto flash{render::flash_of(r, g, b, true)};
		BOOST_REQUIRE(flash.has_value());
		render::draw_palette_flash(d, a, flash.value());
	}
	else if (path == flash_path::direct_gl)
	{
		GLfloat alast_r = r, alast_g = g, alast_b = b;
		glDisable(GL_TEXTURE_2D);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);
		glEnable(GL_BLEND);
		if (r <= 0 && g <= 0 && b <= 0)
		{
			alast_r = static_cast<GLfloat>(r * -2.5);
			alast_g = static_cast<GLfloat>(g * -2.5);
			alast_b = static_cast<GLfloat>(b * -2.5);
			glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
		}
		else
			glBlendFunc(GL_ONE, GL_ONE);
		const std::array<GLfloat, 16> color_array{{alast_r, alast_g, alast_b, 1.0, alast_r, alast_g,
		                                           alast_b, 1.0, alast_r, alast_g, alast_b, 1.0,
		                                           alast_r, alast_g, alast_b, 1.0}};
		const std::array<GLfloat, 8> vertices{{0, 0, 0, 1, 1, 1, 1, 0}};
		glVertexPointer(2, GL_FLOAT, 0, vertices.data());
		glColorPointer(4, GL_FLOAT, 0, color_array.data());
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
	}
	return drawing(d);
}

}

/* Test that a darkening palette flash (palfx_neg) gives the picture the
 * direct GL calls gave before the render device, and changes the picture.
 */
BOOST_AUTO_TEST_CASE(negative_palette_flash_matches_direct_gl)
{
	const auto device{flashed_picture(flash_path::device, -0.1f, -0.2f, 0.f)};
	BOOST_TEST(device == flashed_picture(flash_path::direct_gl, -0.1f, -0.2f, 0.f),
	           boost::test_tools::per_element());
	BOOST_TEST(device != flashed_picture(flash_path::none, 0, 0, 0));
}

/* Test that an adding palette flash with a negative component (additive_c,
 * which GL clamps to 0) gives the picture the direct GL calls gave, and
 * changes the picture.
 */
BOOST_AUTO_TEST_CASE(mixed_sign_palette_flash_matches_direct_gl)
{
	const auto device{flashed_picture(flash_path::device, 0.3f, -0.1f, 0.2f)};
	BOOST_TEST(device == flashed_picture(flash_path::direct_gl, 0.3f, -0.1f, 0.2f),
	           boost::test_tools::per_element());
	BOOST_TEST(device != flashed_picture(flash_path::none, 0, 0, 0));
}

/* Test that copy_region copies the rectangle of the target to another
 * place: the top half, red, copied over the bottom half, which was cleared,
 * leaves every pixel red.  Copying between eyes needs a stereo context, which
 * this one is not: it throws, as does selecting an eye.
 */
BOOST_AUTO_TEST_CASE(copy_region_copies_a_rectangle_and_guards_its_caps)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	BOOST_TEST(d.caps().copy_region);
	BOOST_TEST(!d.caps().quad_buffer_stereo);
	d.clear({.color = true, .depth = true});
	const quad top_half{red, 0.5f};
	d.draw(top_half.call);
	const std::int32_t half{static_cast<std::int32_t>(target.height / 2)};
	const render::copy_rect up_to_down{
	    .src_x = 0,
	    .src_y = half,
	    .dest_x = 0,
	    .dest_y = 0,
	    .size = {.width = target.width, .height = target.height / 2}};
	d.copy_region(up_to_down, false);
	BOOST_TEST(every_pixel_is(drawing(d), {{255, 0, 0}}));
	BOOST_CHECK_THROW(d.copy_region(up_to_down, true), std::logic_error);
	BOOST_CHECK_THROW(d.select_eye(render::eye::right), std::logic_error);
}

namespace {

/* The colour of pixel (x, y) of image rgb of target, y from the top. */
std::array<std::uint8_t, 3> pixel_at(const std::span<const std::uint8_t> rgb, const std::uint32_t x,
                                     const std::uint32_t y)
{
	const std::size_t i{(std::size_t{y} * target.width + x) * 3};
	return {rgb[i], rgb[i + 1], rgb[i + 2]};
}

constexpr std::array<std::uint8_t, 3> red_rgb{{255, 0, 0}}, green_rgb{{0, 255, 0}},
    blue_rgb{{0, 0, 255}}, black_rgb{{0, 0, 0}};

/* A texture image of tw by th texels, the way the front end pads a bitmap
 * of w by h: the top left w by h texels from texel(x, y), the rest green.
 */
template <typename F>
struct padded_image
{
	std::vector<std::uint8_t> rgba;
	render::texture_image image;
	padded_image(const std::uint32_t tw, const std::uint32_t th, const std::uint32_t w,
	             const std::uint32_t h, F texel)
	    : rgba{make(tw, th, w, h, texel)}, image{{.size = {.width = tw, .height = th},
		                                          .format = render::texture_format::rgba8,
		                                          .filter = render::filter::nearest,
		                                          .anisotropic = false,
		                                          .has_transparency = false},
		                                         rgba}
	{}
	static std::vector<std::uint8_t> make(const std::uint32_t tw, const std::uint32_t th,
	                                      const std::uint32_t w, const std::uint32_t h, F texel)
	{
		std::vector<std::uint8_t> v;
		for (std::uint32_t y{0}; y != th; ++y)
			for (std::uint32_t x{0}; x != tw; ++x)
			{
				const auto c{x < w && y < h ? texel(x, y) : green_rgb};
				v.insert(v.end(), {c[0], c[1], c[2], 255});
			}
		return v;
	}
};

}

/* Test that a movie frame, uploaded to a slot and drawn through the GL
 * adapter with movie_rect, blits into the rectangle it is given and shows
 * its own texels there: a 6 by 4 frame in an 8 by 4 texture, its
 * left three columns red and its right three blue, drawn into 24 by 16
 * pixels at (16, 8) of the 64 by 48 target, fills the rectangle in red then
 * blue, shows none of the texture's green padding and leaves the rest of
 * the target black.
 */
BOOST_AUTO_TEST_CASE(movie_frame_blits_into_its_rectangle)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	/* Any slot draws the same: 20001 is the number ogl_texture_slot gives
	 * the movie frame today, but nothing here reads ogl_texture_list, and
	 * rtrace's gl_host resolves the slot by its own name for it.
	 */
	constexpr render::texture_slot movie_slot{20001};
	const padded_image frame{8, 4, 6, 4, [](const std::uint32_t x, const std::uint32_t) {
		                         return x < 3 ? red_rgb : blue_rgb;
	                         }};
	d.texture(movie_slot, &frame.image);
	d.clear({.color = true, .depth = true});
	render::ambient a;
	a.viewport = {.x = 0, .y = 0, .size = target};
	const auto vertices{render::blit_vertices(render::movie_rect(16, 8, 24, 16, target),
	                                          {.u1 = 0, .u2 = 0.75f, .v1 = 0, .v2 = 1},
	                                          {{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}})};
	d.draw(a.fan(vertices, movie_slot));
	const auto rgb{drawing(d)};
	for (std::uint32_t y{8}; y != 24; ++y)
	{
		BOOST_TEST((pixel_at(rgb, 16, y) == red_rgb));
		BOOST_TEST((pixel_at(rgb, 27, y) == red_rgb));
		BOOST_TEST((pixel_at(rgb, 28, y) == blue_rgb));
		BOOST_TEST((pixel_at(rgb, 39, y) == blue_rgb));
	}
	BOOST_TEST((pixel_at(rgb, 15, 16) == black_rgb));
	BOOST_TEST((pixel_at(rgb, 40, 16) == black_rgb));
	BOOST_TEST((pixel_at(rgb, 20, 7) == black_rgb));
	BOOST_TEST((pixel_at(rgb, 20, 24) == black_rgb));
}

/* Test that a glyph, a bitmap offset into its parent's texture, shows its
 * own window of the texture, tinted by its vertex colours: the top right
 * 4 by 4 texels of an 8 by 8 texture, which are blue, the rest red, drawn
 * with a half grey tint give blue of half the texel's, and nothing beside.
 */
BOOST_AUTO_TEST_CASE(glyph_blits_its_window_of_the_texture)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	const padded_image font{8, 8, 8, 8, [](const std::uint32_t x, const std::uint32_t y) {
		                        return x >= 4 && y < 4 ? blue_rgb : red_rgb;
	                        }};
	d.texture({3}, &font.image);
	d.clear({.color = true, .depth = true});
	render::ambient a;
	a.viewport = {.x = 0, .y = 0, .size = target};
	const auto uv{render::bitmap_uv({.x = 4,
	                                 .y = 0,
	                                 .w = 4,
	                                 .h = 4,
	                                 .tex_w = 8,
	                                 .tex_h = 8,
	                                 .tex_tw = 8,
	                                 .tex_th = 8,
	                                 .u = 1,
	                                 .v = 1})};
	const auto vertices{render::blit_vertices(
	    render::canvas_rect(8, 8, 8, 8, target), uv,
	    {{0.5f, 0.5f, 0.5f, 1, 0.5f, 0.5f, 0.5f, 1, 0.5f, 0.5f, 0.5f, 1, 0.5f, 0.5f, 0.5f, 1}})};
	d.draw(a.fan(vertices, {3}));
	const auto rgb{drawing(d)};
	for (const std::uint32_t y : {8u, 15u})
		for (const std::uint32_t x : {8u, 15u})
			BOOST_TEST((pixel_at(rgb, x, y) == std::array<std::uint8_t, 3>{{0, 0, 128}}));
	BOOST_TEST((pixel_at(rgb, 16, 12) == black_rgb));
	BOOST_TEST((pixel_at(rgb, 7, 12) == black_rgb));
}

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

#include "common/render/moved_draw.h"
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

/* Test that the committed sample trace is the writer's: its bytes are what
 * sample_trace() writes.  tools/dev/rtrace-tests.sh passes its path after
 * `--`.
 */
BOOST_AUTO_TEST_CASE(sample_trace_is_the_writers)
{
	const auto &suite{boost::unit_test::framework::master_test_suite()};
	BOOST_REQUIRE_MESSAGE(suite.argc == 2, "usage: test-rtrace [BOOST OPTIONS] -- SAMPLE");
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-container): argv holds argc pointers
	const std::span<char *const> args{suite.argv, static_cast<std::size_t>(suite.argc)};
	std::ifstream in{args[1], std::ios::binary};
	BOOST_REQUIRE(in.good());
	const bytes committed{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
	BOOST_TEST((committed == render::rtrace::sample_trace()));
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

/* Test that the GL adapter applies the state of a draw with no vertices:
 * after a draw with a moved modelview, an empty draw with the identity
 * leaves GL's modelview matrix the identity, which the front end relies on
 * to restore the matrix the code not yet drawing through the device
 * expects (ogl_draw_moved).
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

/* Test that an empty line draw restores the line width through the state
 * shadow: after a line at width 2, an empty line draw at width 1 sets 1
 * itself, and a second line at width 2 sets 2 again, as the vertex reticle
 * draws on each frame after restoring the width the code not yet drawing
 * through the device expects.
 */
BOOST_AUTO_TEST_CASE(empty_line_draw_restores_line_width)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	const std::array<render::vertex, 2> line{{
	    {.pos = {{0, 0.5f, 0}}, .rgba = red, .uv = no_uv},
	    {.pos = {{1, 0.5f, 0}}, .rgba = red, .uv = no_uv},
	}};
	auto wide{flat_draw(render::topology::lines, line)};
	wide.state.line_width = 2.f;
	auto restore{flat_draw(render::topology::lines, {})};
	restore.state.line_width = 1.f;
	d.draw(wide);
	d.draw(restore);
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
 * a quad over the whole target moved to its right half, then the left half
 * drawn with glDrawArrays as the code not yet drawing through the device
 * draws, leave the left half green and the right half red.
 */
BOOST_AUTO_TEST_CASE(direct_draw_after_moved_draw_is_not_moved)
{
	render::rtrace::gl_host host{target};
	auto &d{host.device()};
	render::ambient a;
	a.viewport = {.x = 0, .y = 0, .size = target};
	const quad whole{red};
	render::draw_moved(d, a, render::topology::fan, whole.vertices,
	                   {.translate = {{0.5f, 0, 0}}, .scale = {{0.5f, 1, 1}}}, a.line_width);
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

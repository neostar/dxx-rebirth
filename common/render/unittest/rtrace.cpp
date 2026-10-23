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
#include <vector>

#include "common/render/readback.h"
#include "common/render/render_device.h"
#include "common/render/replay.h"
#include "common/render/rtrace/gl_host.h"
#include "common/render/trace.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth rtrace
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;
namespace trace = dcx::render::trace;

namespace {

using bytes = std::vector<std::uint8_t>;

constexpr render::extent target{.width = 64, .height = 48};

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

/* A shaded triangle, whose edges and colours GL rasterizes unevenly. */
constexpr std::array<render::vertex, 3> shaded_triangle{{
    {.pos = {{0.1f, 0.1f, 0}}, .rgba = {{1, 0, 0, 1}}, .uv = no_uv},
    {.pos = {{0.9f, 0.3f, 0}}, .rgba = {{0, 1, 0, 1}}, .uv = no_uv},
    {.pos = {{0.4f, 0.95f, 0}}, .rgba = {{0, 0, 1, 1}}, .uv = no_uv},
}};

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

/* The committed sample trace (common/render/rtrace/synthetic/sample.rtr):
 * the shaded triangle, then the whole target in yellow.
 */
bytes sample_trace()
{
	trace::writer w{target};
	w.clear({.color = true, .depth = true});
	w.draw(flat_draw(render::topology::triangles, shaded_triangle));
	w.present({.frame = 0});
	w.clear({.color = true, .depth = true});
	const quad yellow{{{1, 1, 0, 1}}};
	w.draw(yellow.call);
	w.present({.frame = 1});
	return w.take();
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
	w.draw(flat_draw(render::topology::triangles, shaded_triangle));
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
	BOOST_TEST((committed == sample_trace()));
}

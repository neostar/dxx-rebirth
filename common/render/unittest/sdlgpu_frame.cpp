/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The SDL_GPU adapter's frame logic through the GPU command seam's
 * counting fake (research #71, Design): what a frame asks of SDL, counted
 * and compared by value, without a GPU.
 */

#include "common/render/sdlgpu_frame.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

#include "common/render/ambient.h"
#include "common/render/device.h"
#include "common/render/render_device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth sdlgpu frame
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;
namespace sdlgpu = dcx::render::sdlgpu;

namespace {

constexpr render::extent target{.width = 64, .height = 48};

/* A draw of one call, as the fake saw it. */
struct seen_draw
{
	std::uint32_t first;
	std::uint32_t count;
	sdlgpu::pipeline_key key;
	sdlgpu::gpu_viewport viewport;
	sdlgpu::vertex_uniforms vertex;
	sdlgpu::fragment_uniforms fragment;
};

/* The counting fake of the GPU command seam: counts every call and keeps
 * what is bound as SDL does, so each draw is seen with the state it ran
 * under.  A pass forgets the pipeline, viewport, vertex buffer and texture
 * bound in the last one; uniforms belong to the command buffer and stay
 * (SDL_gpu.h).  A draw without all four bound fails the test.
 */
struct counting_commands final : sdlgpu::commands
{
	std::vector<render::vertex> uploaded;
	unsigned uploads{0};
	std::vector<sdlgpu::pass_ops> passes;
	unsigned ends{0};
	unsigned pipeline_binds{0};
	unsigned viewport_sets{0};
	unsigned vertex_buffer_binds{0};
	unsigned untextured_binds{0};
	unsigned vertex_pushes{0};
	unsigned fragment_pushes{0};
	std::vector<seen_draw> draws;
	bool in_pass{false};
	std::optional<sdlgpu::pipeline_key> key;
	std::optional<sdlgpu::gpu_viewport> viewport;
	bool vertex_buffer_bound{false};
	bool texture_bound{false};
	sdlgpu::vertex_uniforms vertex{};
	sdlgpu::fragment_uniforms fragment{};

	void upload_vertices(const std::span<const render::vertex> v) override
	{
		BOOST_TEST(!in_pass);
		++uploads;
		uploaded.assign(v.begin(), v.end());
	}
	void begin_pass(const sdlgpu::pass_ops &ops) override
	{
		BOOST_TEST(!in_pass);
		in_pass = true;
		passes.push_back(ops);
		key.reset();
		viewport.reset();
		vertex_buffer_bound = false;
		texture_bound = false;
	}
	void end_pass() override
	{
		BOOST_TEST(in_pass);
		in_pass = false;
		++ends;
	}
	void bind_pipeline(const sdlgpu::pipeline_key &k) override
	{
		BOOST_TEST(in_pass);
		++pipeline_binds;
		key = k;
	}
	void set_viewport(const sdlgpu::gpu_viewport &v) override
	{
		BOOST_TEST(in_pass);
		++viewport_sets;
		viewport = v;
	}
	void bind_vertex_buffer() override
	{
		BOOST_TEST(in_pass);
		++vertex_buffer_binds;
		vertex_buffer_bound = true;
	}
	void bind_untextured() override
	{
		BOOST_TEST(in_pass);
		++untextured_binds;
		texture_bound = true;
	}
	void push_vertex_uniforms(const sdlgpu::vertex_uniforms &u) override
	{
		++vertex_pushes;
		vertex = u;
	}
	void push_fragment_uniforms(const sdlgpu::fragment_uniforms &u) override
	{
		++fragment_pushes;
		fragment = u;
	}
	void draw(const std::uint32_t first, const std::uint32_t count) override
	{
		BOOST_TEST(in_pass);
		BOOST_TEST(key.has_value());
		BOOST_TEST(viewport.has_value());
		BOOST_TEST(vertex_buffer_bound);
		BOOST_TEST(texture_bound);
		draws.push_back({.first = first,
		                 .count = count,
		                 .key = key.value_or(sdlgpu::pipeline_key{}),
		                 .viewport = viewport.value_or(sdlgpu::gpu_viewport{}),
		                 .vertex = vertex,
		                 .fragment = fragment});
	}
};

constexpr std::array<float, 2> no_uv{{0, 0}};

/* An untextured quad over the whole target in the game's 2D projection,
 * drawn with state s.  The call views the quad's vertices.
 */
struct quad
{
	std::array<render::vertex, 4> vertices;
	render::draw_call call;
	explicit quad(const render::draw_state &s = {}, const float red = 1)
	    : vertices{{
	          {.pos = {{0, 0, 0}}, .rgba = {{red, 0, 0, 1}}, .uv = no_uv},
	          {.pos = {{1, 0, 0}}, .rgba = {{red, 0, 0, 1}}, .uv = no_uv},
	          {.pos = {{1, 1, 0}}, .rgba = {{red, 0, 0, 1}}, .uv = no_uv},
	          {.pos = {{0, 1, 0}}, .rgba = {{red, 0, 0, 1}}, .uv = no_uv},
	      }},
	      call{.topology = render::topology::fan,
		       .vertices = vertices,
		       .texture = render::texture_slot::none,
		       .state = s,
		       .viewport = {.x = 0, .y = 0, .size = target},
		       .transforms = {.projection = render::screen_ortho, .modelview = render::identity{}}}
	{}
	quad(const quad &) = delete;
	quad &operator=(const quad &) = delete;
	quad(quad &&) = delete;
	quad &operator=(quad &&) = delete;
	~quad() = default;
};

constexpr render::clear_targets both{.color = true, .depth = true};
constexpr render::clear_targets none{.color = false, .depth = false};

}

/* Test that every pipeline key has its own place in the pipeline set and
 * that key_at gives the key back.
 */
BOOST_AUTO_TEST_CASE(pipeline_keys_fill_the_set_once)
{
	std::set<std::size_t> seen;
	for (const auto b : {render::blend::normal, render::blend::additive_a,
	                     render::blend::additive_c, render::blend::palfx_neg, render::blend::none})
		for (const auto z : {render::depth::off, render::depth::test_and_write})
			for (const auto c : {render::cull::none, render::cull::front})
			{
				const sdlgpu::pipeline_key k{.blend = b, .depth = z, .cull = c};
				const auto i{sdlgpu::index_of(k)};
				BOOST_TEST(i < sdlgpu::pipeline_set_size);
				BOOST_TEST((sdlgpu::key_at(i) == k));
				seen.insert(i);
			}
	BOOST_TEST(seen.size() == 20u);
	BOOST_CHECK_THROW((void)sdlgpu::key_at(20), std::out_of_range);
}

/* Test that every sampler key, a filter, a wrap and whether the texture
 * asks for anisotropy, has its own place in the sampler set and that
 * sampler_key_at gives the key back.
 */
BOOST_AUTO_TEST_CASE(sampler_keys_fill_the_set_once)
{
	std::set<std::size_t> seen;
	for (const auto f : {render::filter::nearest, render::filter::trilinear})
		for (const auto w : {render::wrap::clamp, render::wrap::repeat})
			for (const bool a : {false, true})
			{
				const sdlgpu::sampler_key k{.filter = f, .wrap = w, .anisotropic = a};
				const auto i{sdlgpu::index_of(k)};
				BOOST_TEST(i < sdlgpu::sampler_set_size);
				BOOST_TEST((sdlgpu::sampler_key_at(i) == k));
				seen.insert(i);
			}
	BOOST_TEST(seen.size() == 8u);
	BOOST_CHECK_THROW((void)sdlgpu::sampler_key_at(8), std::out_of_range);
	BOOST_CHECK_THROW(
	    (void)sdlgpu::index_of(sdlgpu::sampler_key{.filter = static_cast<render::filter>(2),
		                                           .wrap = render::wrap::clamp,
		                                           .anisotropic = false}),
	    std::out_of_range);
}

/* Test that the pipeline cache is keyed by the draw state's blend, depth
 * and cull: states differing in alpha reference, wrap or line width share
 * a pipeline, and a state differing in blend does not.
 */
BOOST_AUTO_TEST_CASE(pipeline_key_is_blend_depth_cull)
{
	const render::draw_state a{};
	const render::draw_state b{.alpha_ref = 0.5f, .wrap = render::wrap::repeat, .line_width = 3};
	const render::draw_state c{.blend = render::blend::additive_a};
	BOOST_TEST((sdlgpu::key_of(a) == sdlgpu::key_of(b)));
	BOOST_TEST(sdlgpu::index_of(sdlgpu::key_of(a)) != sdlgpu::index_of(sdlgpu::key_of(c)));
}

/* Test that three equal-state quads cost one upload, one pass and one bind
 * of each kind, and draw as triangle lists one after another in the one
 * vertex buffer.
 */
BOOST_AUTO_TEST_CASE(equal_state_draws_bind_once)
{
	sdlgpu::frame f{target};
	const quad q;
	f.clear(both);
	for (int i{0}; i != 3; ++i)
		f.draw(q.call);
	counting_commands c;
	BOOST_TEST(f.encode(c, none));
	BOOST_TEST(c.uploads == 1u);
	BOOST_TEST(c.uploaded.size() == 18u);
	BOOST_REQUIRE(c.passes.size() == 1u);
	BOOST_TEST((c.passes.front() == sdlgpu::pass_ops{.clear_color = true, .clear_depth = true}));
	BOOST_TEST(c.ends == 1u);
	BOOST_TEST(c.pipeline_binds == 1u);
	BOOST_TEST(c.viewport_sets == 1u);
	BOOST_TEST(c.vertex_buffer_binds == 1u);
	BOOST_TEST(c.untextured_binds == 1u);
	BOOST_TEST(c.vertex_pushes == 1u);
	BOOST_TEST(c.fragment_pushes == 1u);
	BOOST_REQUIRE(c.draws.size() == 3u);
	for (std::uint32_t i{0}; i != 3; ++i)
	{
		BOOST_TEST(c.draws[i].first == 6 * i);
		BOOST_TEST(c.draws[i].count == 6u);
	}
}

/* Test that a draw runs with the uniforms and viewport of its own call:
 * the game's 2D ortho and the identity, its alpha reference untextured,
 * and its viewport with the origin moved to the top left.
 */
BOOST_AUTO_TEST_CASE(draw_runs_with_its_own_uniforms)
{
	sdlgpu::frame f{target};
	quad q{{.alpha_ref = 0.25f}};
	q.call.viewport = {.x = 10, .y = 5, .size = {.width = 20, .height = 10}};
	f.draw(q.call);
	counting_commands c;
	BOOST_TEST(f.encode(c, none));
	BOOST_REQUIRE(c.draws.size() == 1u);
	const auto &d{c.draws.front()};
	const render::matrix ortho{{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1}};
	const render::matrix identity{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	BOOST_TEST(d.vertex.projection == ortho, boost::test_tools::per_element());
	BOOST_TEST(d.vertex.modelview == identity, boost::test_tools::per_element());
	BOOST_TEST(
	    (d.fragment == sdlgpu::fragment_uniforms{.alpha_ref = 0.25f, .use_texture = 0, .pad = {}}));
	/* 48 - (5 + 10) from the top. */
	BOOST_TEST(
	    (d.viewport == sdlgpu::gpu_viewport{
	                       .x = 10, .y = 33, .w = 20, .h = 10, .min_depth = 0.5f, .max_depth = 1}));
	/* Nothing cleared and nothing undefined: the pass loads both. */
	BOOST_REQUIRE(c.passes.size() == 1u);
	BOOST_TEST((c.passes.front() == sdlgpu::pass_ops{.clear_color = false, .clear_depth = false}));
}

/* Test that the bind cache calls SDL only on change: a new blend binds a
 * pipeline, a new alpha reference pushes the fragment uniforms alone, and
 * a draw that changes nothing binds nothing.
 */
BOOST_AUTO_TEST_CASE(bind_cache_calls_only_on_change)
{
	sdlgpu::frame f{target};
	const quad plain;
	const quad additive{{.blend = render::blend::additive_c}};
	const quad discarding{{.blend = render::blend::additive_c, .alpha_ref = 0.5f}};
	f.draw(plain.call);
	f.draw(additive.call);
	f.draw(discarding.call);
	f.draw(discarding.call);
	counting_commands c;
	BOOST_TEST(f.encode(c, none));
	BOOST_REQUIRE(c.draws.size() == 4u);
	BOOST_TEST(c.pipeline_binds == 2u);
	BOOST_TEST(c.fragment_pushes == 2u);
	BOOST_TEST(c.vertex_pushes == 1u);
	BOOST_TEST(c.viewport_sets == 1u);
	BOOST_TEST((c.draws[1].key == sdlgpu::pipeline_key{.blend = render::blend::additive_c}));
	BOOST_TEST(c.draws[3].fragment.alpha_ref == 0.5f);
}

/* Test that a depth clear between draws ends the pass and begins one that
 * loads colour and clears depth, with the pipeline, viewport, vertex buffer
 * and texture bound again, since SDL forgets them at a pass's end, and the
 * uniforms not pushed again, since they stay with the command buffer.
 */
BOOST_AUTO_TEST_CASE(mid_frame_depth_clear_splits_the_pass)
{
	sdlgpu::frame f{target};
	const quad q;
	f.clear(both);
	f.draw(q.call);
	f.clear({.color = false, .depth = true});
	f.draw(q.call);
	counting_commands c;
	BOOST_TEST(f.encode(c, none));
	BOOST_REQUIRE(c.passes.size() == 2u);
	BOOST_TEST((c.passes[1] == sdlgpu::pass_ops{.clear_color = false, .clear_depth = true}));
	BOOST_TEST(c.ends == 2u);
	BOOST_TEST(c.pipeline_binds == 2u);
	BOOST_TEST(c.vertex_buffer_binds == 2u);
	BOOST_TEST(c.untextured_binds == 2u);
	BOOST_TEST(c.viewport_sets == 2u);
	BOOST_TEST(c.vertex_pushes == 1u);
	BOOST_TEST(c.fragment_pushes == 1u);
	BOOST_TEST(c.uploads == 1u);
	BOOST_TEST(c.draws.size() == 2u);
}

/* Test that a frame of clears alone encodes one clearing pass and no
 * upload, that targets nothing has drawn to are cleared without a clear,
 * and that an empty frame on defined targets encodes nothing.
 */
BOOST_AUTO_TEST_CASE(clears_and_undefined_targets)
{
	{
		sdlgpu::frame f{target};
		f.clear({.color = true, .depth = false});
		counting_commands c;
		BOOST_TEST(f.encode(c, none));
		BOOST_TEST(c.uploads == 0u);
		BOOST_REQUIRE(c.passes.size() == 1u);
		BOOST_TEST(
		    (c.passes.front() == sdlgpu::pass_ops{.clear_color = true, .clear_depth = false}));
		BOOST_TEST(c.draws.empty());
		/* No vertex buffer exists to bind before a frame with vertices. */
		BOOST_TEST(c.vertex_buffer_binds == 0u);
		BOOST_TEST(c.untextured_binds == 0u);
	}
	{
		sdlgpu::frame f{target};
		counting_commands c;
		BOOST_TEST(f.encode(c, {.color = false, .depth = true}));
		BOOST_REQUIRE(c.passes.size() == 1u);
		BOOST_TEST(
		    (c.passes.front() == sdlgpu::pass_ops{.clear_color = false, .clear_depth = true}));
	}
	{
		sdlgpu::frame f{target};
		counting_commands c;
		BOOST_TEST(!f.encode(c, none));
		BOOST_TEST(c.passes.empty());
	}
}

/* Test that encoding empties the frame: a second encode adds nothing. */
BOOST_AUTO_TEST_CASE(encode_empties_the_frame)
{
	sdlgpu::frame f{target};
	const quad q;
	f.draw(q.call);
	counting_commands first;
	BOOST_TEST(f.encode(first, none));
	counting_commands second;
	BOOST_TEST(!f.encode(second, none));
	BOOST_TEST(second.draws.empty());
	BOOST_TEST(second.uploads == 0u);
}

/* Test that a fan of fewer than three vertices draws nothing and that what
 * later tickets bring is refused, not drawn wrong: a texture, a perspective
 * projection, lines, line loops, points and strips.
 */
BOOST_AUTO_TEST_CASE(unsupported_draws_are_refused)
{
	sdlgpu::frame f{target};
	quad q;
	q.call.vertices = std::span{q.vertices}.first(2);
	f.draw(q.call);
	counting_commands c;
	BOOST_TEST(!f.encode(c, none));
	quad textured;
	textured.call.texture = {1};
	BOOST_CHECK_THROW(f.draw(textured.call), std::logic_error);
	quad perspective;
	perspective.call.transforms.projection = render::view_perspective;
	BOOST_CHECK_THROW(f.draw(perspective.call), std::logic_error);
	for (const auto t : {render::topology::lines, render::topology::line_loop,
	                     render::topology::points, render::topology::strip})
	{
		quad other;
		other.call.topology = t;
		BOOST_CHECK_THROW(f.draw(other.call), std::logic_error);
	}
}

/* Test that the upload plan starts every reservation at a multiple of 512
 * bytes and that a buffer grows to the next power of two from 64 KiB,
 * keeping a capacity that fits.
 */
BOOST_AUTO_TEST_CASE(upload_plan_packs_at_512_bytes)
{
	sdlgpu::upload_plan p;
	BOOST_TEST(p.reserve(100) == 0u);
	BOOST_TEST(p.reserve(1) == 512u);
	BOOST_TEST(p.reserve(1024) == 1024u);
	BOOST_TEST(p.reserve(0) == 2048u);
	BOOST_TEST(p.size() == 2048u);
	BOOST_TEST(sdlgpu::grown_capacity(0, 1) == 65536u);
	BOOST_TEST(sdlgpu::grown_capacity(65536, 65537) == 131072u);
	BOOST_TEST(sdlgpu::grown_capacity(131072, 100) == 131072u);
}

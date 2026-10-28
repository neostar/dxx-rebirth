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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "common/render/ambient.h"
#include "common/render/device.h"
#include "common/render/render_device.h"
#include "shaders/generated/tex_modulate_frag.h"
#include "shaders/generated/tex_opaque_frag.h"

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
	render::texture_slot slot;
	sdlgpu::sampler_key sampler;
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
	/* The last upload's plan, with each texture's rows copied. */
	struct seen_rows
	{
		render::texture_slot slot;
		render::extent size;
		std::size_t offset;
		std::vector<std::uint8_t> rgba;
	};
	std::size_t upload_size{0};
	std::size_t vertex_offset{0};
	std::vector<seen_rows> texture_rows;
	unsigned texture_binds{0};
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
	/* The texture bound, none for the white one, and its sampler. */
	render::texture_slot bound_slot{render::texture_slot::none};
	sdlgpu::sampler_key bound_sampler{};
	sdlgpu::vertex_uniforms vertex{};
	sdlgpu::fragment_uniforms fragment{};

	void upload(const sdlgpu::frame_uploads &u) override
	{
		BOOST_TEST(!in_pass);
		BOOST_TEST(passes.empty());
		++uploads;
		upload_size = u.size;
		vertex_offset = u.vertex_offset;
		uploaded.assign(u.vertices.begin(), u.vertices.end());
		texture_rows.clear();
		for (const auto &t : u.textures)
			texture_rows.push_back({.slot = t.slot,
			                        .size = t.size,
			                        .offset = t.offset,
			                        .rgba = {t.rgba.begin(), t.rgba.end()}});
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
		bound_slot = render::texture_slot::none;
	}
	void bind_texture(const render::texture_slot slot, const sdlgpu::sampler_key &k) override
	{
		BOOST_TEST(in_pass);
		++texture_binds;
		texture_bound = true;
		bound_slot = slot;
		bound_sampler = k;
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
		                 .fragment = fragment,
		                 .slot = bound_slot,
		                 .sampler = bound_sampler});
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

/* Test that every pipeline key, with or without the alpha test, has its own
 * place in the pipeline set and that key_at gives the key back.
 */
BOOST_AUTO_TEST_CASE(pipeline_keys_fill_the_set_once)
{
	std::set<std::size_t> seen;
	for (const auto b : {render::blend::normal, render::blend::additive_a,
	                     render::blend::additive_c, render::blend::palfx_neg, render::blend::none})
		for (const auto z : {render::depth::off, render::depth::test_and_write})
			for (const auto c : {render::cull::none, render::cull::front})
				for (const bool t : {false, true})
				{
					const sdlgpu::pipeline_key k{
					    .blend = b, .depth = z, .cull = c, .alpha_test = t};
					const auto i{sdlgpu::index_of(k)};
					BOOST_TEST(i < sdlgpu::pipeline_set_size);
					BOOST_TEST((sdlgpu::key_at(i) == k));
					seen.insert(i);
				}
	BOOST_TEST(seen.size() == 40u);
	BOOST_CHECK_THROW((void)sdlgpu::key_at(40), std::out_of_range);
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
 * and cull and by whether the shader discards (frame::draw decides that):
 * states differing in alpha reference, wrap or line width share a
 * pipeline, and a state differing in blend does not.
 */
BOOST_AUTO_TEST_CASE(pipeline_key_is_blend_depth_cull)
{
	const render::draw_state a{};
	const render::draw_state b{.alpha_ref = 0.5f, .wrap = render::wrap::repeat, .line_width = 3};
	const render::draw_state c{.blend = render::blend::additive_a};
	BOOST_TEST((sdlgpu::key_of(a, false) == sdlgpu::key_of(b, false)));
	BOOST_TEST(sdlgpu::index_of(sdlgpu::key_of(a, false)) !=
	           sdlgpu::index_of(sdlgpu::key_of(c, false)));
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

/* Test that a perspective draw runs with the 3D view's matrix as GL builds
 * it (gluPerspective(90, 1, 0.1, 5000), read back from Apple's GL) and its
 * own modelview, apart, as the vertex shader multiplies them.
 */
BOOST_AUTO_TEST_CASE(perspective_draw_runs_with_its_matrix)
{
	sdlgpu::frame f{target};
	quad q{{.depth = render::depth::test_and_write, .cull = render::cull::front}};
	q.call.transforms = {
	    .projection = render::view_perspective,
	    .modelview = render::translate_scale{.translate = {{1, 2, -3}}, .scale = {{1, 1, -1}}}};
	f.draw(q.call);
	counting_commands c;
	BOOST_TEST(f.encode(c, none));
	BOOST_REQUIRE(c.draws.size() == 1u);
	const auto &d{c.draws.front()};
	const render::matrix gl{
	    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -0x1.00029ep+0f, -1, 0, 0, -0x1.999bb2p-3f, 0}};
	const render::matrix moved{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 1, 2, -3, 1}};
	BOOST_TEST(d.vertex.projection == gl, boost::test_tools::per_element());
	BOOST_TEST(d.vertex.modelview == moved, boost::test_tools::per_element());
	BOOST_TEST((d.key == sdlgpu::pipeline_key{.blend = render::blend::normal,
	                                          .depth = render::depth::test_and_write,
	                                          .cull = render::cull::front,
	                                          .alpha_test = true}));
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
 * later tickets bring is refused, not drawn wrong: lines, line loops,
 * points and strips.
 */
BOOST_AUTO_TEST_CASE(unsupported_draws_are_refused)
{
	sdlgpu::frame f{target};
	quad q;
	q.call.vertices = std::span{q.vertices}.first(2);
	f.draw(q.call);
	counting_commands c;
	BOOST_TEST(!f.encode(c, none));
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

namespace {

/* An image of size with pixels rgba, which it views. */
[[nodiscard]]
render::texture_image image_of(const render::extent size, const std::span<const std::uint8_t> rgba,
                               const render::filter filter = render::filter::nearest,
                               const bool anisotropic = false,
                               const render::texture_format format = render::texture_format::rgba8,
                               const bool has_transparency = false)
{
	return {{.size = size,
	         .format = format,
	         .filter = filter,
	         .anisotropic = anisotropic,
	         .has_transparency = has_transparency},
	        rgba};
}

constexpr std::array<std::uint8_t, 16> two_by_two{
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};

}

/* Test that a textured draw binds its slot's texture with the sampler of
 * the texture's filter and anisotropy and the draw's wrap, and samples it
 * (use_texture 1); that a second draw of the same texture and wrap binds
 * nothing, another wrap binds again, and an untextured draw binds the white
 * texture and does not sample.
 */
BOOST_AUTO_TEST_CASE(textured_draw_binds_its_texture_and_sampler)
{
	sdlgpu::frame f{target};
	const auto image{image_of({2, 2}, two_by_two, render::filter::trilinear, true)};
	f.texture({1}, &image);
	quad repeat{{.wrap = render::wrap::repeat}};
	repeat.call.texture = {1};
	quad clamp{{.wrap = render::wrap::clamp}};
	clamp.call.texture = {1};
	const quad untextured;
	f.draw(repeat.call);
	f.draw(repeat.call);
	f.draw(clamp.call);
	f.draw(untextured.call);
	counting_commands c;
	BOOST_TEST(f.encode(c, none));
	BOOST_REQUIRE(c.draws.size() == 4u);
	BOOST_TEST(c.texture_binds == 2u);
	BOOST_TEST(c.untextured_binds == 1u);
	const sdlgpu::sampler_key repeating{
	    .filter = render::filter::trilinear, .wrap = render::wrap::repeat, .anisotropic = true};
	BOOST_TEST((c.draws[0].slot == render::texture_slot{1}));
	BOOST_TEST((c.draws[0].sampler == repeating));
	BOOST_TEST(c.draws[0].fragment.use_texture == 1u);
	BOOST_TEST((c.draws[2].sampler == sdlgpu::sampler_key{.filter = render::filter::trilinear,
	                                                      .wrap = render::wrap::clamp,
	                                                      .anisotropic = true}));
	BOOST_TEST((c.draws[3].slot == render::texture_slot::none));
	BOOST_TEST(c.draws[3].fragment.use_texture == 0u);
}

/* Test that one plan per frame covers the vertices and every texture's
 * rows: one upload, the vertices at offset 0 and each texture at the next
 * multiple of 512 bytes, its rows tight; an RGB texture uploads with alpha
 * 255, as GL_RGB8 samples it; a slot given twice uploads its last image
 * once.
 */
BOOST_AUTO_TEST_CASE(one_plan_covers_vertices_and_texture_rows)
{
	sdlgpu::frame f{target};
	const auto first{image_of({2, 2}, two_by_two)};
	f.texture({1}, &first);
	std::array<std::uint8_t, 64> rgb_pixels{};
	rgb_pixels.fill(7);
	const auto rgb{
	    image_of({4, 4}, rgb_pixels, render::filter::nearest, false, render::texture_format::rgb8)};
	f.texture({2}, &rgb);
	std::array<std::uint8_t, 16> again{};
	again.fill(9);
	const auto second{image_of({2, 2}, again)};
	f.texture({1}, &second);
	const quad q;
	f.draw(q.call);
	counting_commands c;
	BOOST_TEST(f.encode(c, none));
	BOOST_TEST(c.uploads == 1u);
	BOOST_TEST(c.vertex_offset == 0u);
	BOOST_TEST(c.uploaded.size() == 6u);
	BOOST_REQUIRE(c.texture_rows.size() == 2u);
	const auto &a{c.texture_rows[0]};
	const auto &b{c.texture_rows[1]};
	BOOST_TEST((a.slot == render::texture_slot{2}));
	BOOST_TEST(a.offset == 512u);
	BOOST_TEST((a.size == render::extent{4, 4}));
	std::array<std::uint8_t, 64> opaque{};
	for (std::size_t i{0}; i != opaque.size(); ++i)
		opaque[i] = i % 4 == 3 ? 255 : 7;
	BOOST_TEST(a.rgba == opaque, boost::test_tools::per_element());
	BOOST_TEST((b.slot == render::texture_slot{1}));
	BOOST_TEST(b.offset == 1024u);
	BOOST_TEST(b.rgba == again, boost::test_tools::per_element());
	BOOST_TEST(c.upload_size == 1024u + 16u);
}

/* Test that a frame of a texture alone uploads it with no pass, and that
 * the upload is not repeated by the next encode.
 */
BOOST_AUTO_TEST_CASE(texture_alone_uploads_without_a_pass)
{
	sdlgpu::frame f{target};
	const auto image{image_of({2, 2}, two_by_two)};
	f.texture({3}, &image);
	counting_commands c;
	(void)f.encode(c, none);
	BOOST_TEST(c.uploads == 1u);
	BOOST_TEST(c.uploaded.empty());
	BOOST_REQUIRE(c.texture_rows.size() == 1u);
	BOOST_TEST(c.texture_rows[0].offset == 0u);
	BOOST_TEST(c.passes.empty());
	counting_commands next;
	BOOST_TEST(!f.encode(next, none));
	BOOST_TEST(next.uploads == 0u);
}

/* Test that the frame tells whether one of its draws samples a slot, which
 * the device flushes before it changes that slot's texture, and forgets it
 * once encoded.
 */
BOOST_AUTO_TEST_CASE(frame_tells_which_slots_its_draws_sample)
{
	sdlgpu::frame f{target};
	const auto image{image_of({2, 2}, two_by_two)};
	f.texture({1}, &image);
	f.texture({2}, &image);
	quad q;
	q.call.texture = {1};
	f.draw(q.call);
	BOOST_TEST(f.samples({1}));
	BOOST_TEST(!f.samples({2}));
	counting_commands c;
	BOOST_TEST(f.encode(c, none));
	BOOST_TEST(!f.samples({1}));
}

/* Test that a textured draw of a slot that holds no texture, never given
 * or released, is refused, as the GL replay refuses it.
 */
BOOST_AUTO_TEST_CASE(textured_draw_of_empty_slot_is_refused)
{
	sdlgpu::frame f{target};
	quad q;
	q.call.texture = {1};
	BOOST_CHECK_THROW(f.draw(q.call), std::invalid_argument);
	const auto image{image_of({2, 2}, two_by_two)};
	f.texture({1}, &image);
	f.texture({1}, nullptr);
	BOOST_CHECK_THROW(f.draw(q.call), std::invalid_argument);
	counting_commands c;
	BOOST_TEST(!f.encode(c, none));
	BOOST_TEST(c.uploads == 0u);
}

/* Test that the shader discards only where the alpha test can change a
 * pixel (#29): a draw of a texture with transparent pixels runs the
 * discarding pipeline at a reference above 0 (the front end's 0.02 and
 * 0.8), a draw of an opaque texture or at reference 0 (alpha_test_off)
 * does not, and an opaque draw whose vertex alpha is below the reference
 * does, as GL discards it.  The reference reaches the shader either way.
 */
BOOST_AUTO_TEST_CASE(alpha_test_pipeline_only_where_it_can_discard)
{
	sdlgpu::frame f{target};
	const auto clear_img{image_of({2, 2}, two_by_two, render::filter::nearest, false,
	                              render::texture_format::rgba8, true)};
	const auto solid_img{image_of({2, 2}, two_by_two)};
	f.texture({1}, &clear_img);
	f.texture({2}, &solid_img);
	const auto with{[](const float ref, const render::texture_slot slot, const float alpha) {
		auto q{std::make_unique<quad>(render::draw_state{.alpha_ref = ref})};
		for (auto &v : q->vertices)
			v.rgba[3] = alpha;
		q->call.texture = slot;
		return q;
	}};
	std::vector<std::unique_ptr<quad>> quads;
	/* 0: transparent texture at 0.02; 1: at 0.8; 2: alpha test off. */
	quads.push_back(with(0.02f, {1}, 1));
	quads.push_back(with(0.8f, {1}, 1));
	quads.push_back(with(render::alpha_test_off, {1}, 1));
	/* 3: opaque texture; 4: opaque texture, vertex alpha under the reference;
	 * 5: untextured, opaque; 6: untextured, alpha under the reference. */
	quads.push_back(with(0.8f, {2}, 1));
	quads.push_back(with(0.8f, {2}, 0.5f));
	quads.push_back(with(0.8f, render::texture_slot::none, 1));
	quads.push_back(with(0.02f, render::texture_slot::none, 0.01f));
	/* 7: opaque texture, depth tested: the discarding shader, until the
	 * depth band is decided (needs_alpha_test). */
	quads.push_back(with(0.8f, {2}, 1));
	quads.back()->call.state.depth = render::depth::test_and_write;
	for (const auto &q : quads)
		f.draw(q->call);
	counting_commands c;
	BOOST_TEST(f.encode(c, none));
	BOOST_REQUIRE(c.draws.size() == quads.size());
	const std::array<bool, 8> expected{{true, true, false, false, true, false, true, true}};
	for (std::size_t i{0}; i != expected.size(); ++i)
		BOOST_TEST(c.draws[i].key.alpha_test == expected[i], "draw " << i);
	BOOST_TEST(c.draws[1].fragment.alpha_ref == 0.8f);
	BOOST_TEST(c.draws[3].fragment.alpha_ref == 0.8f);
}

namespace {

/* Whether the MSL source in code names the MSL discard, which only a shader
 * that can discard a fragment holds.
 */
[[nodiscard]]
bool discards(const std::span<const std::uint8_t> msl)
{
	constexpr std::string_view discard{"discard_fragment"};
	return std::search(msl.begin(), msl.end(), discard.begin(), discard.end(),
	                   [](const std::uint8_t a, const char b) {
		                   return a == static_cast<std::uint8_t>(b);
	                   }) != msl.end();
}

}

/* Test that the shader of a pipeline with the alpha test discards and the
 * shader of one without it does not (#29): the committed MSL of
 * tex_modulate and of tex_opaque.
 */
BOOST_AUTO_TEST_CASE(only_the_alpha_test_shader_discards)
{
	BOOST_TEST(discards(dcx::shaders::tex_modulate_frag::msl));
	BOOST_TEST(!discards(dcx::shaders::tex_opaque_frag::msl));
}

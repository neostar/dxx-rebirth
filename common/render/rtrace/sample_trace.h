/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The writers of the committed synthetic traces in
 * common/render/rtrace/synthetic/: sample.rtr, the first, the shaded
 * triangle, then the whole target in yellow, each in its own frame; and
 * overbright.rtr, textured quads with vertex colours above 1 (#27).
 * test-rtrace checks each committed file is its writer's output;
 * tools/dev/rtrace-tests.sh pins GL's frames of each, and test-sdlgpu
 * replays the sample on SDL_GPU (R3).
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "common/render/render_device.h"
#include "common/render/trace.h"

namespace dcx::render::rtrace {

/* The sample's target. */
inline constexpr extent sample_target{.width = 64, .height = 48};

/* A draw of vertices over the whole sample target, untextured, with the
 * default draw state and an ortho projection of [0, 1] on both axes.  The
 * call views the vertices.
 */
[[nodiscard]]
inline draw_call sample_draw(const topology t, const std::span<const vertex> v)
{
	return {
	    .topology = t,
	    .vertices = v,
	    .texture = texture_slot::none,
	    .state = {},
	    .viewport = {.x = 0, .y = 0, .size = sample_target},
	    .transforms =
	        {.projection =
			     ortho{.left = 0, .right = 1, .bottom = 0, .top = 1, .z_near = -1, .z_far = 1},
			 .modelview = identity{}},
	};
}

/* A shaded triangle, whose edges and colours rasterize unevenly. */
inline constexpr std::array<vertex, 3> sample_shaded_triangle{{
    {.pos = {{0.1f, 0.1f, 0}}, .rgba = {{1, 0, 0, 1}}, .uv = {{0, 0}}},
    {.pos = {{0.9f, 0.3f, 0}}, .rgba = {{0, 1, 0, 1}}, .uv = {{0, 0}}},
    {.pos = {{0.4f, 0.95f, 0}}, .rgba = {{0, 0, 1, 1}}, .uv = {{0, 0}}},
}};

/* The sample trace's bytes. */
[[nodiscard]]
inline std::vector<std::uint8_t> sample_trace()
{
	trace::writer w{sample_target};
	w.clear({.color = true, .depth = true});
	w.draw(sample_draw(topology::triangles, sample_shaded_triangle));
	w.present({.frame = 0});
	w.clear({.color = true, .depth = true});
	constexpr std::array<float, 4> yellow{{1, 1, 0, 1}};
	const std::array<vertex, 4> quad{{
	    {.pos = {{0, 0, 0}}, .rgba = yellow, .uv = {{0, 0}}},
	    {.pos = {{1, 0, 0}}, .rgba = yellow, .uv = {{0, 0}}},
	    {.pos = {{1, 1, 0}}, .rgba = yellow, .uv = {{0, 0}}},
	    {.pos = {{0, 1, 0}}, .rgba = yellow, .uv = {{0, 0}}},
	}};
	w.draw(sample_draw(topology::fan, quad));
	w.present({.frame = 1});
	return w.take();
}

/* A 4x4 nearest-filtered texture image of format, and the pixels it views:
 * each texel a different colour, some of them translucent.
 */
struct sample_texture
{
	std::array<std::uint8_t, 64> rgba;
	texture_image image;
	explicit sample_texture(const texture_format format)
	    : rgba{[] {
		      std::array<std::uint8_t, 64> p{};
		      for (std::size_t i{0}; i != 16; ++i)
		      {
			      p[i * 4] = static_cast<std::uint8_t>(i * 16);
			      p[i * 4 + 1] = static_cast<std::uint8_t>(255 - i * 16);
			      p[i * 4 + 2] = static_cast<std::uint8_t>(i % 3 * 120);
			      p[i * 4 + 3] = static_cast<std::uint8_t>(i % 2 != 0 ? 255 : 128);
		      }
		      return p;
	      }()},
	      image{{.size = {4, 4},
		         .format = format,
		         .filter = filter::nearest,
		         .anisotropic = false,
		         .has_transparency = true},
		        rgba}
	{}
	sample_texture(const sample_texture &) = delete;
	sample_texture &operator=(const sample_texture &) = delete;
	sample_texture(sample_texture &&) = delete;
	sample_texture &operator=(sample_texture &&) = delete;
	~sample_texture() = default;
};

/* A quad over the whole sample target, its corners counter-clockwise from
 * the bottom left in colours rgba, its texture coordinates from 0 to uv_max
 * on both axes.
 */
[[nodiscard]]
inline std::array<vertex, 4> sample_quad(const std::array<std::array<float, 4>, 4> &rgba,
                                         const float uv_max)
{
	return {{
	    {.pos = {{0, 0, 0}}, .rgba = rgba[0], .uv = {{0, 0}}},
	    {.pos = {{1, 0, 0}}, .rgba = rgba[1], .uv = {{uv_max, 0}}},
	    {.pos = {{1, 1, 0}}, .rgba = rgba[2], .uv = {{uv_max, uv_max}}},
	    {.pos = {{0, 1, 0}}, .rgba = rgba[3], .uv = {{0, uv_max}}},
	}};
}

/* The overbright trace's bytes (#27): vertex colours above 1, which GL
 * clamps per vertex before it interpolates them (spike T9, vclamp_off).
 * Frame 0, a quad of the RGBA texture repeated twice over the target,
 * modulated by corner colours up to 3 and an alpha of 2, blended.  Frame 1,
 * the shaded triangle in colours up to 4 over black, untextured, then a
 * quad of the RGB texture, its translucent texels opaque, clamped from 0
 * to 1.25 under corner colours up to 2.5 at half alpha.
 */
[[nodiscard]]
inline std::vector<std::uint8_t> overbright_trace()
{
	trace::writer w{sample_target};
	const sample_texture rgba{texture_format::rgba8};
	const sample_texture rgb{texture_format::rgb8};
	w.texture({1}, &rgba.image);
	w.texture({2}, &rgb.image);
	w.clear({.color = true, .depth = true});
	const auto repeated{
	    sample_quad({{{{3, 0, 0, 1}}, {{0, 3, 0, 1}}, {{0, 0, 3, 1}}, {{2, 2, 2, 2}}}}, 2)};
	draw_call textured{sample_draw(topology::fan, repeated)};
	textured.texture = {1};
	textured.state.wrap = wrap::repeat;
	w.draw(textured);
	w.present({.frame = 0});
	w.clear({.color = true, .depth = true});
	std::array<vertex, 3> bright{sample_shaded_triangle};
	bright[0].rgba = {{4, 0.5f, 0, 1}};
	bright[1].rgba = {{0, 4, 0.5f, 1}};
	bright[2].rgba = {{0.5f, 0, 4, 1}};
	w.draw(sample_draw(topology::triangles, bright));
	const auto clamped{sample_quad({{{{2.5f, 1, 0, 0.5f}},
	                                 {{0, 2.5f, 1, 0.5f}},
	                                 {{1, 0, 2.5f, 0.5f}},
	                                 {{2.5f, 2.5f, 0, 0.5f}}}},
	                               1.25f)};
	draw_call opaque{sample_draw(topology::fan, clamped)};
	opaque.texture = {2};
	w.draw(opaque);
	w.present({.frame = 1});
	return w.take();
}

}

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
 * overbright.rtr, textured quads with vertex colours above 1 (#27); and
 * flash.rtr, the blend modes and the red and negative palette flashes (#30);
 * and walls.rtr, lit walls in perspective, depth-tested and culled (#28).
 * test-rtrace checks each committed file is its writer's output;
 * test-sdlgpu and tools/dev/rtrace-tests.sh replay them on SDL_GPU (R3).
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

/* A quad over the whole sample target in one colour. */
[[nodiscard]]
inline std::array<vertex, 4> sample_flat_quad(const std::array<float, 4> &rgba)
{
	return sample_quad({{rgba, rgba, rgba, rgba}}, 0);
}

/* Draw the flash trace's scene: an opaque grey over the target, and the
 * shaded triangle over it, so that a blend shows against both.
 */
inline void flash_scene(trace::writer &w)
{
	w.clear({.color = true, .depth = true});
	const auto grey{sample_flat_quad({{0.5f, 0.4f, 0.3f, 1}})};
	w.draw(sample_draw(topology::fan, grey));
	w.draw(sample_draw(topology::triangles, sample_shaded_triangle));
}

/* A full-target quad of rgba drawn with blend b over the scene. */
inline void flash_quad(trace::writer &w, const blend b, const std::array<float, 4> &rgba)
{
	const auto q{sample_flat_quad(rgba)};
	draw_call c{sample_draw(topology::fan, q)};
	c.state.blend = b;
	w.draw(c);
}

/* The flash trace's bytes (#30): the game's palette flashes over a scene
 * (ogl_do_palfx), and the other blend modes.  Frame 0, a weapon-hit flash
 * of mixed sign, additive_c of (0.6, -0.1, -0.2), whose negative
 * components the colour target clamps to 0 before blending (a flat colour,
 * so this pins the blend factors, not the vertex shader's clamp, which the
 * walls trace's frame 2 pins) and whose red the sum clamps to 1; frame 1, the
 * negative flash, palfx_neg of a brightness of (-19, -10, 0) out of 63
 * scaled by 2.5 as ogl_do_palfx does, (0.754, 0.397, 0); frame 2, a
 * translucent blue quad over the scene under normal blending, a green one
 * under additive_a and a grey one under additive_c, each at alpha 0.25.
 */
[[nodiscard]]
inline std::vector<std::uint8_t> flash_trace()
{
	trace::writer w{sample_target};
	flash_scene(w);
	flash_quad(w, blend::additive_c, {{0.6f, -0.1f, -0.2f, 1}});
	w.present({.frame = 0});
	flash_scene(w);
	flash_quad(w, blend::palfx_neg, {{0.754f, 0.397f, 0, 1}});
	w.present({.frame = 1});
	flash_scene(w);
	flash_quad(w, blend::normal, {{0, 0, 1, 0.25f}});
	flash_quad(w, blend::additive_a, {{0, 1, 0, 0.25f}});
	flash_quad(w, blend::additive_c, {{0.2f, 0.2f, 0.2f, 0.25f}});
	w.present({.frame = 2});
	return w.take();
}

/* A draw of a quad in the 3D view (frame_setup.h): gluPerspective(90, 1,
 * 0.1, 5000), the identity modelview, eye-space vertices, depth tested
 * and written, front faces culled, as the game draws walls.  The call
 * views the vertices.
 */
[[nodiscard]]
inline draw_call wall_draw(const std::span<const vertex> v)
{
	draw_call c{sample_draw(topology::fan, v)};
	c.state.depth = depth::test_and_write;
	c.state.cull = cull::front;
	c.transforms.projection =
	    perspective{.fovy = 90, .aspect = 1, .z_near = 0.1, .z_far = 5000, .stereo_dx = 0};
	return c;
}

/* A quad of four eye-space corners, each with its vertex colour, its
 * texture coordinates from 0 to uv_max.  Corners clockwise on screen face
 * away from GL's counter-clockwise front, so the game's front culling keeps
 * them.
 */
[[nodiscard]]
inline std::array<vertex, 4> wall_quad(const std::array<std::array<float, 3>, 4> &pos,
                                       const std::array<std::array<float, 4>, 4> &rgba,
                                       const float uv_max)
{
	return {{
	    {.pos = pos[0], .rgba = rgba[0], .uv = {{0, 0}}},
	    {.pos = pos[1], .rgba = rgba[1], .uv = {{0, uv_max}}},
	    {.pos = pos[2], .rgba = rgba[2], .uv = {{uv_max, uv_max}}},
	    {.pos = pos[3], .rgba = rgba[3], .uv = {{uv_max, 0}}},
	}};
}

/* One colour at every corner. */
[[nodiscard]]
inline std::array<std::array<float, 4>, 4> flat(const std::array<float, 4> &rgba)
{
	return {{rgba, rgba, rgba, rgba}};
}

/* The walls trace's bytes (#28).  Frame 0, a lit corridor from 0.5 to 4
 * deep: floor, ceiling and side walls of the repeated RGBA texture under
 * vertex light from 1.6 near to 0.3 far, all wound clockwise on screen; a
 * red quad wound counter-clockwise in front of them, which front culling
 * drops (the winding check); a green quad at 2 deep, then the end wall at 4
 * deep drawn after it, which the depth test keeps behind it.  Frame 1, the
 * near plane: over a far grey wall, a floor left of the centre just below
 * the eye from 0.02 deep (in front of the near plane, 0.1, and on screen
 * there) to 3, a red quad at 0.15 deep, nearer than twice the
 * near plane, and a yellow one at 0.25, farther: GL clips the floor at the
 * near plane and keeps both quads; with depth clip instead of the clip
 * distances the red quad and the floor nearer than 0.2 vanish (ADR 2's
 * depth pairing rule).  Frame 2, mixed-sign vertex light: a wall with one
 * corner of (-1, 0, 0) and three of (1, 1, 1), which GL clamps per vertex
 * before it interpolates, untextured.
 */
[[nodiscard]]
inline std::vector<std::uint8_t> walls_trace()
{
	trace::writer w{sample_target};
	const sample_texture rgba{texture_format::rgba8};
	w.texture({1}, &rgba.image);
	constexpr float n{-0.5f}, f{-4};
	constexpr std::array<float, 4> bright{{1.6f, 1.4f, 1.2f, 1}}, dim{{0.3f, 0.35f, 0.4f, 1}};
	const auto lit{[&](const std::array<std::array<float, 3>, 4> &pos) {
		/* Corners at the near end bright, at the far end dim. */
		std::array<std::array<float, 4>, 4> c{};
		for (std::size_t i{0}; i != 4; ++i)
			c[i] = pos[i][2] == n ? bright : dim;
		return wall_quad(pos, c, 2);
	}};
	const std::array<std::array<vertex, 4>, 4> corridor{{
	    lit({{{{-1, -1, n}}, {{-1, -1, f}}, {{1, -1, f}}, {{1, -1, n}}}}),
	    lit({{{{-1, 1, n}}, {{1, 1, n}}, {{1, 1, f}}, {{-1, 1, f}}}}),
	    lit({{{{-1, -1, n}}, {{-1, 1, n}}, {{-1, 1, f}}, {{-1, -1, f}}}}),
	    lit({{{{1, -1, n}}, {{1, -1, f}}, {{1, 1, f}}, {{1, 1, n}}}}),
	}};
	w.clear({.color = true, .depth = true});
	for (const auto &q : corridor)
	{
		draw_call c{wall_draw(q)};
		c.texture = {1};
		c.state.wrap = wrap::repeat;
		w.draw(c);
	}
	const auto back_face{wall_quad(
	    {{{{-0.3f, -0.3f, -1}}, {{0.3f, -0.3f, -1}}, {{0.3f, 0.3f, -1}}, {{-0.3f, 0.3f, -1}}}},
	    flat({{1, 0, 0, 1}}), 0)};
	w.draw(wall_draw(back_face));
	const auto middle{wall_quad(
	    {{{{-0.4f, -0.6f, -2}}, {{-0.4f, 0.2f, -2}}, {{0.5f, 0.2f, -2}}, {{0.5f, -0.6f, -2}}}},
	    {{{{0, 1, 0, 1}}, {{0, 0.8f, 0.2f, 1}}, {{0.2f, 1, 0, 1}}, {{0, 0.6f, 0, 1}}}}, 0)};
	w.draw(wall_draw(middle));
	const auto end{wall_quad({{{{-1, -1, f}}, {{-1, 1, f}}, {{1, 1, f}}, {{1, -1, f}}}},
	                         {{{{0.2f, 0.2f, 0.6f, 1}},
	                           {{0.6f, 0.2f, 0.2f, 1}},
	                           {{0.2f, 0.6f, 0.2f, 1}},
	                           {{0.5f, 0.5f, 0.5f, 1}}}},
	                         0)};
	w.draw(wall_draw(end));
	w.present({.frame = 0});
	w.clear({.color = true, .depth = true});
	const auto far_wall{wall_quad({{{{-4, -4, -4}}, {{-4, 4, -4}}, {{4, 4, -4}}, {{4, -4, -4}}}},
	                              flat({{0.5f, 0.5f, 0.5f, 1}}), 0)};
	w.draw(wall_draw(far_wall));
	const auto floor{wall_quad({{{{-0.3f, -0.01f, -0.02f}},
	                             {{-0.3f, -0.01f, -3}},
	                             {{-0.005f, -0.01f, -3}},
	                             {{-0.005f, -0.01f, -0.02f}}}},
	                           {{{{0, 0, 1, 1}}, {{0, 1, 1, 1}}, {{0, 1, 1, 1}}, {{0, 0, 1, 1}}}},
	                           0)};
	w.draw(wall_draw(floor));
	const auto near_red{wall_quad({{{{-0.05f, -0.03f, -0.15f}},
	                                {{-0.05f, 0.05f, -0.15f}},
	                                {{0.03f, 0.05f, -0.15f}},
	                                {{0.03f, -0.03f, -0.15f}}}},
	                              flat({{1, 0, 0, 1}}), 0)};
	w.draw(wall_draw(near_red));
	const auto yellow{wall_quad({{{{0.08f, -0.05f, -0.25f}},
	                              {{0.08f, 0.1f, -0.25f}},
	                              {{0.2f, 0.1f, -0.25f}},
	                              {{0.2f, -0.05f, -0.25f}}}},
	                            flat({{1, 1, 0, 1}}), 0)};
	w.draw(wall_draw(yellow));
	w.present({.frame = 1});
	w.clear({.color = true, .depth = true});
	const auto mixed{
	    wall_quad({{{{-1, -1, -1.5f}}, {{-1, 1, -1.5f}}, {{1, 1, -1.5f}}, {{1, -1, -1.5f}}}},
		          {{{{-1, 0, 0, 1}}, {{1, 1, 1, 1}}, {{1, 1, 1, 1}}, {{1, 1, 1, 1}}}}, 0)};
	w.draw(wall_draw(mixed));
	w.present({.frame = 2});
	return w.take();
}

}

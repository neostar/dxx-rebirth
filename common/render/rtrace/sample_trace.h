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
 * and walls.rtr, lit walls in perspective, depth-tested and culled (#28);
 * and alpha.rtr, the alpha test at the thresholds 0.02 and 0.8 (#29); and
 * lines.rtr, lines, points and strips (#32); and oversize.rtr, viewports
 * reaching past the target (#18); and fonts.rtr, the font formats' ramps
 * and text (#33).
 * test-rtrace checks each committed file is its writer's output;
 * test-sdlgpu and tools/dev/rtrace-tests.sh replay them on SDL_GPU (R3).
 */

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "common/render/ambient.h"
#include "common/render/blit.h"
#include "common/render/palette_expand.h"
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

/* A 4x4 nearest-filtered RGBA texture whose texels are never black and
 * whose alphas are alpha_ramp (its texel i, row by row from v = 0), the
 * alphas 0.02 * 255 = 5.1 and 0.8 * 255 = 204 fall between: no texel ties a
 * threshold, so no driver's rounding decides one.  With transparent false
 * every alpha is 255 and has_transparency says so, as the game's textures
 * without transparent pixels do.
 */
inline constexpr std::array<std::uint8_t, 16> alpha_ramp{
    {0, 3, 5, 6, 10, 60, 128, 200, 203, 205, 210, 230, 255, 1, 4, 100}};

struct alpha_texture
{
	std::array<std::uint8_t, 64> rgba;
	texture_image image;
	explicit alpha_texture(const bool transparent)
	    : rgba{[transparent] {
		      std::array<std::uint8_t, 64> p{};
		      for (std::size_t i{0}; i != 16; ++i)
		      {
			      p[i * 4] = static_cast<std::uint8_t>(80 + i * 11);
			      p[i * 4 + 1] = static_cast<std::uint8_t>(250 - i * 13);
			      p[i * 4 + 2] = static_cast<std::uint8_t>(60 + i % 4 * 50);
			      p[i * 4 + 3] = transparent ? alpha_ramp[i] : std::uint8_t{255};
		      }
		      return p;
	      }()},
	      image{{.size = {4, 4},
		         .format = texture_format::rgba8,
		         .filter = filter::nearest,
		         .anisotropic = false,
		         .has_transparency = transparent},
		        rgba}
	{}
	alpha_texture(const alpha_texture &) = delete;
	alpha_texture &operator=(const alpha_texture &) = delete;
	alpha_texture(alpha_texture &&) = delete;
	alpha_texture &operator=(alpha_texture &&) = delete;
	~alpha_texture() = default;
};

/* A draw of the alpha texture in slot over the whole target, blending off
 * (a discarded texel shows the clear colour), with the alpha reference ref.
 */
[[nodiscard]]
inline draw_call alpha_draw(const std::span<const vertex> v, const texture_slot slot,
                            const float ref)
{
	draw_call c{sample_draw(topology::fan, v)};
	c.texture = slot;
	c.state.blend = blend::none;
	c.state.alpha_ref = ref;
	return c;
}

/* The alpha trace's bytes (#29): the alpha test, GL_GEQUAL (a fragment
 * passes when its alpha is at least the reference), at the front end's two
 * thresholds.  Frames 0 and 1, the 4x4 alpha texture over the target at
 * 0.02 (5 of 16 texels discarded) and 0.8 (12 of 16); frame 2, the same at
 * alpha_test_off, which discards none; frame 3, left half, a texture without
 * transparent pixels at 0.8, which discards none, right half, an untextured
 * quad of vertex alpha 0.5 at 0.8, which GL discards whole; frame 4, in the
 * 3D view over a far grey wall, a nearer wall of the alpha texture at 0.8:
 * its discarded texels write no depth and show the far wall; frame 5, the
 * reference 1 against the one texel of alpha exactly 1, which passes: a
 * fragment whose alpha equals the reference is kept.
 */
[[nodiscard]]
inline std::vector<std::uint8_t> alpha_trace()
{
	trace::writer w{sample_target};
	const alpha_texture clear{true}, solid{false};
	w.texture({1}, &clear.image);
	w.texture({2}, &solid.image);
	const auto white{sample_quad(flat({{1, 1, 1, 1}}), 1)};
	const std::array<float, 3> refs{{default_alpha_ref, 0.8f, alpha_test_off}};
	for (std::size_t i{0}; i != refs.size(); ++i)
	{
		w.clear({.color = true, .depth = true});
		w.draw(alpha_draw(white, {1}, refs[i]));
		w.present({.frame = static_cast<std::uint32_t>(i)});
	}
	w.clear({.color = true, .depth = true});
	draw_call left{alpha_draw(white, {2}, 0.8f)};
	left.viewport = {.x = 0, .y = 0, .size = {.width = 32, .height = 48}};
	w.draw(left);
	const auto half{sample_flat_quad({{1, 1, 1, 0.5f}})};
	draw_call right{alpha_draw(half, texture_slot::none, 0.8f)};
	right.viewport = {.x = 32, .y = 0, .size = {.width = 32, .height = 48}};
	w.draw(right);
	w.present({.frame = 3});
	w.clear({.color = true, .depth = true});
	const auto far_wall{wall_quad({{{{-4, -4, -4}}, {{-4, 4, -4}}, {{4, 4, -4}}, {{4, -4, -4}}}},
	                              flat({{0.5f, 0.5f, 0.5f, 1}}), 0)};
	w.draw(wall_draw(far_wall));
	const auto near_wall{wall_quad({{{{-1, -1, -2}}, {{-1, 1, -2}}, {{1, 1, -2}}, {{1, -1, -2}}}},
	                               flat({{1, 1, 1, 1}}), 1)};
	draw_call c{wall_draw(near_wall)};
	c.texture = {1};
	c.state.blend = blend::none;
	c.state.alpha_ref = 0.8f;
	w.draw(c);
	w.present({.frame = 4});
	/* Frame 5: a reference of 1 against the texel of alpha 255, a tie:
	 * GL_GEQUAL passes it, and discards the other 15. */
	w.clear({.color = true, .depth = true});
	w.draw(alpha_draw(white, {1}, 1));
	w.present({.frame = 5});
	return w.take();
}

/* A vertex at window pixel (x, y) of the sample target, in the ortho
 * projection of sample_draw.
 */
[[nodiscard]]
inline vertex sample_pixel(const float x, const float y, const std::array<float, 4> &rgba)
{
	return {.pos = {{x / static_cast<float>(sample_target.width),
	                 y / static_cast<float>(sample_target.height), 0}},
	        .rgba = rgba,
	        .uv = {{0, 0}}};
}

/* A draw of lines or points of width in the sample's 2D projection,
 * blended.  The call views the vertices.
 */
[[nodiscard]]
inline draw_call line_draw(const topology t, const std::span<const vertex> v, const float width)
{
	draw_call c{sample_draw(t, v)};
	c.state.line_width = width;
	return c;
}

/* The spokes of the lines trace's first two frames: lines from a ring
 * around (cx, cy) to a larger one every 15 degrees, with endpoints off the
 * pixel grid and colours that change along each line (the automap's lines,
 * gr_line at linedotscale 1, the spike's 136 open pixels), then the exact
 * diagonals and axis-aligned lines of each quarter-pixel offset.
 */
[[nodiscard]]
inline std::vector<vertex> sample_spokes(const float cx, const float cy)
{
	std::vector<vertex> v;
	for (std::size_t i{0}; i != 24; ++i)
	{
		const double a{static_cast<double>(i) * 15 * 3.14159265358979323846 / 180};
		const float inner{5.f + static_cast<float>(i % 4) * 0.25f};
		const float t{static_cast<float>(i) / 24};
		v.push_back(sample_pixel(cx + inner * static_cast<float>(std::cos(a)),
		                         cy + inner * static_cast<float>(std::sin(a)), {{1, t, 0.2f, 1}}));
		v.push_back(sample_pixel(cx + 14 * static_cast<float>(std::cos(a)),
		                         cy + 14 * static_cast<float>(std::sin(a)), {{0.2f, 1, t, 0.6f}}));
	}
	return v;
}

/* The lines trace's bytes (#32).  Frames 0 and 1, the same scene at
 * widths 1 and 2: spokes (sample_spokes), a diagonal, a horizontal and a
 * vertical at each quarter-pixel offset, and a flat-coloured polyline
 * through the corners of the target.  Frame 2, a 16-sided line loop, a
 * triangle strip (the reticle's), and points of 1, 2, 3 and 4 pixels at
 * off-grid positions.  Frame 3, 3D lines (g3_draw_line: width 1, depth
 * tested and written, no culling) across a wall at 2 deep, in front of it
 * and behind it, which the depth test hides.  Frame 4, lines reaching past
 * the eye, in the 3D view: one from in front of it to behind it at width 1,
 * one at width 2, one wholly behind it (GL draws nothing), and one that
 * crosses the near plane with its far end on screen: GL clips each in clip
 * space, where a divide first would mirror the endpoint behind the eye
 * through the centre of the screen (ADR 2's pixel-centre rule).
 */
[[nodiscard]]
inline std::vector<std::uint8_t> lines_trace()
{
	trace::writer w{sample_target};
	for (const float width : {1.f, 2.f})
	{
		w.clear({.color = true, .depth = true});
		auto spokes{sample_spokes(32.37f, 24.81f)};
		w.draw(line_draw(topology::lines, spokes, width));
		std::vector<vertex> grid;
		for (std::size_t k{0}; k != 4; ++k)
		{
			const float o{static_cast<float>(k) * 0.25f};
			const float c{0.3f + 0.2f * static_cast<float>(k)};
			const std::array<float, 4> col{{c, 1, 1 - c, 1}};
			const float x{2.f + static_cast<float>(k) * 3};
			grid.push_back(sample_pixel(x + o, 2 + o, col));
			grid.push_back(sample_pixel(x + 10 + o, 12 + o, col));
			grid.push_back(sample_pixel(x + o, 14 + 3 * static_cast<float>(k) + o, col));
			grid.push_back(sample_pixel(x + 12 + o, 14 + 3 * static_cast<float>(k) + o, col));
			grid.push_back(sample_pixel(54 + 2 * static_cast<float>(k) + o, 2 + o, col));
			grid.push_back(sample_pixel(54 + 2 * static_cast<float>(k) + o, 12 + o, col));
		}
		w.draw(line_draw(topology::lines, grid, width));
		const std::array<vertex, 4> corners{
		    {sample_pixel(1.5f, 45.5f, {{1, 1, 0, 1}}), sample_pixel(62.5f, 45.5f, {{1, 1, 0, 1}}),
			 sample_pixel(62.5f, 1.5f, {{1, 1, 0, 1}}), sample_pixel(1.5f, 1.5f, {{1, 1, 0, 1}})}};
		w.draw(line_draw(topology::lines, corners, width));
		w.present({.frame = width < 2 ? 0u : 1u});
	}
	w.clear({.color = true, .depth = true});
	std::vector<vertex> loop;
	for (std::size_t i{0}; i != 16; ++i)
	{
		const double a{static_cast<double>(i) * 3.14159265358979323846 / 8};
		loop.push_back(sample_pixel(16.3f + 10.3f * static_cast<float>(std::cos(a)),
		                            30.7f + 10.3f * static_cast<float>(std::sin(a)),
		                            {{0, 1, 0, 0.8f}}));
	}
	w.draw(line_draw(topology::line_loop, loop, 2));
	const std::array<vertex, 4> strip{
	    {sample_pixel(34.5f, 22.2f, {{1, 0, 0, 1}}), sample_pixel(34.5f, 30.9f, {{1, 0, 0, 1}}),
		 sample_pixel(45.8f, 22.2f, {{0, 0, 1, 1}}), sample_pixel(45.8f, 30.9f, {{0, 0, 1, 1}})}};
	w.draw(line_draw(topology::strip, strip, 1));
	for (std::size_t size{1}; size != 5; ++size)
	{
		const std::array<vertex, 2> dots{{
		    sample_pixel(10.3f + 12 * static_cast<float>(size), 8.6f, {{1, 0.5f, 0, 1}}),
		    sample_pixel(10.8f + 12 * static_cast<float>(size), 12.1f, {{0, 1, 1, 1}}),
		}};
		w.draw(line_draw(topology::points, dots, static_cast<float>(size)));
	}
	w.present({.frame = 2});
	w.clear({.color = true, .depth = true});
	const auto wall{wall_quad({{{{-1, -1, -2}}, {{-1, 1, -2}}, {{1, 1, -2}}, {{1, -1, -2}}}},
	                          flat({{0, 0, 0, 1}}), 0)};
	w.draw(wall_draw(wall));
	std::vector<vertex> deep;
	for (std::size_t i{0}; i != 6; ++i)
	{
		const float k{static_cast<float>(i)};
		const float z0{-1.5f - 0.1f * k}, z1{-2.5f + 0.05f * k};
		deep.push_back({.pos = {{-0.9f + 0.03f * k, -0.95f + 0.1f * k, z0}},
		                .rgba = {{1, 0.9f, 0.1f * k, 1}},
		                .uv = {{0, 0}}});
		deep.push_back({.pos = {{0.9f - 0.02f * k, -0.25f - 0.08f * k, z1}},
		                .rgba = {{1, 0.9f, 0.1f * k, 1}},
		                .uv = {{0, 0}}});
	}
	draw_call lines3d{wall_draw(deep)};
	lines3d.topology = topology::lines;
	lines3d.state.cull = cull::none;
	lines3d.state.line_width = 1;
	w.draw(lines3d);
	w.present({.frame = 3});
	w.clear({.color = true, .depth = true});
	const auto eye_line{[](const float x0, const float y0, const float z0, const float x1,
	                       const float y1, const float z1, const std::array<float, 4> &rgba) {
		return std::array<vertex, 2>{{{.pos = {{x0, y0, z0}}, .rgba = rgba, .uv = {{0, 0}}},
		                              {.pos = {{x1, y1, z1}}, .rgba = rgba, .uv = {{0, 0}}}}};
	}};
	const auto draw_eye_line{[&w](const std::array<vertex, 2> &v, const float width) {
		draw_call c{wall_draw(v)};
		c.topology = topology::lines;
		c.state.depth = depth::off;
		c.state.cull = cull::none;
		c.state.line_width = width;
		w.draw(c);
	}};
	draw_eye_line(eye_line(-0.5f, -0.2f, -1, 0.4f, 0.3f, 1, {{1, 0, 0, 1}}), 1);
	draw_eye_line(eye_line(0.5f, -0.3f, -1.5f, -0.2f, 0.6f, 2, {{0, 1, 0, 1}}), 2);
	draw_eye_line(eye_line(0.3f, 0.2f, 1, -0.4f, -0.1f, 3, {{0, 0, 1, 1}}), 1);
	draw_eye_line(eye_line(0.8f, 0.7f, -3, 0.1f, 0.2f, -0.05f, {{1, 1, 0, 1}}), 1);
	w.present({.frame = 4});
	return w.take();
}

/* The depth band trace's bytes (#28), not committed: one frame of the 3D
 * view in which a green quad at 0.12 deep, then a smaller blue one behind
 * it at 0.18, drawn last, both lie between the near plane (0.1) and
 * 2 near far / (far + near), about 0.2, where gluPerspective's z_ndc is
 * negative.  GL keeps the nearer green quad over all of the blue one.  A
 * transform model that clamped z_ndc < 0 to the viewport's minimum depth
 * would tie them, and the depth test, LESS_OR_EQUAL, would show the blue
 * quad drawn last (ADR 2's depth pairing rule).
 */
[[nodiscard]]
inline std::vector<std::uint8_t> depth_band_trace()
{
	trace::writer w{sample_target};
	w.clear({.color = true, .depth = true});
	const auto green{wall_quad({{{{-0.06f, -0.06f, -0.12f}},
	                             {{-0.06f, 0.06f, -0.12f}},
	                             {{0.06f, 0.06f, -0.12f}},
	                             {{0.06f, -0.06f, -0.12f}}}},
	                           flat({{0, 1, 0, 1}}), 0)};
	w.draw(wall_draw(green));
	const auto blue{wall_quad({{{{-0.045f, -0.045f, -0.18f}},
	                            {{-0.045f, 0.045f, -0.18f}},
	                            {{0.045f, 0.045f, -0.18f}},
	                            {{0.045f, -0.045f, -0.18f}}}},
	                          flat({{0, 0, 1, 1}}), 0)};
	w.draw(wall_draw(blue));
	w.present({.frame = 0});
	return w.take();
}

/* The oversize trace's bytes (#18): GL viewports reaching past the target,
 * which SDL_GPU's debug mode asserts on and the SDL_GPU device clamps
 * (fit_viewport), keeping each pixel inside where GL puts it.  Frame 0, a
 * quad in four corner colours, its viewport (32, -24, 64x48) past the
 * right and bottom edges.  Frame 1, the same quad, its viewport
 * (-32, 24, 64x48) past the left and top edges, then a quad whose viewport
 * (64, 0, 16x16) holds no pixel of the target, which draws nothing.
 */
[[nodiscard]]
inline std::vector<std::uint8_t> oversize_trace()
{
	trace::writer w{sample_target};
	const auto quad{
	    sample_quad({{{{1, 0, 0, 1}}, {{0, 1, 0, 1}}, {{0, 0, 1, 1}}, {{1, 1, 1, 1}}}}, 0)};
	const auto in_viewport{[&quad](const int x, const int y, const extent size) {
		auto call{sample_draw(topology::fan, quad)};
		call.viewport = {.x = x, .y = y, .size = size};
		return call;
	}};
	w.clear({.color = true, .depth = true});
	w.draw(in_viewport(32, -24, sample_target));
	w.present({.frame = 0});
	w.clear({.color = true, .depth = true});
	w.draw(in_viewport(-32, 24, sample_target));
	w.draw(in_viewport(64, 0, {.width = 16, .height = 16}));
	w.present({.frame = 1});
	return w.take();
}

/* The texels of the ramp and font textures. */
inline constexpr std::size_t ramp_texels{std::size_t{8} * 8};
inline constexpr std::size_t font_texels{std::size_t{16} * 8};

/* The formats the ramp trace measures, in frame order (two frames each). */
inline constexpr std::array<texture_format, 4> ramp_formats{
    {texture_format::rgba8, texture_format::rgba2, texture_format::luminance4_alpha4,
	 texture_format::intensity4}};

/* The 8x8 texture of the ramp trace: 64 levels i * 4, the palette's six
 * bits scaled to eight (palette_expand.h).  With ramps_alpha false, red
 * runs up with the texel, green down and blue scrambles, alpha 255; with
 * it true, the colour is white and alpha runs up.
 */
struct ramp_texture
{
	std::vector<std::uint8_t> rgba;
	texture_image image;
	ramp_texture(const texture_format format, const bool ramps_alpha)
	    : rgba{[ramps_alpha] {
		      std::vector<std::uint8_t> p(ramp_texels * 4);
		      const auto level{
		          [](const std::size_t v) { return static_cast<std::uint8_t>(v * 4); }};
		      for (std::size_t i{0}; i != 64; ++i)
		      {
			      const std::uint8_t ramp{level(i)};
			      const std::uint8_t opaque{255};
			      p[i * 4] = ramps_alpha ? opaque : ramp;
			      p[i * 4 + 1] = ramps_alpha ? opaque : level(63 - i);
			      p[i * 4 + 2] = ramps_alpha ? opaque : level(i * 7 % 64);
			      p[i * 4 + 3] = ramps_alpha ? ramp : opaque;
		      }
		      return p;
	      }()},
	      image{{.size = {8, 8},
		         .format = format,
		         .filter = filter::nearest,
		         .anisotropic = false,
		         .has_transparency = ramps_alpha},
		        rgba}
	{}
	ramp_texture(const ramp_texture &) = delete;
	ramp_texture &operator=(const ramp_texture &) = delete;
	ramp_texture(ramp_texture &&) = delete;
	ramp_texture &operator=(ramp_texture &&) = delete;
	~ramp_texture() = default;
};

/* The font atlas of the text frames: 16 by 8 palette indices, glyphs of
 * index 1 (a palette colour for rgba2) on 255, the transparent one, with a
 * run of 254, the super-transparent one, in the third glyph.
 */
inline constexpr std::array<std::uint8_t, font_texels> font_indices{[] {
	std::array<std::uint8_t, font_texels> p{};
	for (std::size_t i{0}; i != p.size(); ++i)
	{
		const std::size_t x{i % 16}, y{i / 16};
		const bool ink{(x % 8 == y) || (x % 8 + y == 7) || x % 8 == 0};
		p[i] = ink ? std::uint8_t{1} : std::uint8_t{255};
		if (x >= 10 && x < 13 && y == 4)
			p[i] = 254;
	}
	return p;
}()};

/* The palette the font atlas is expanded with: index 1 is (40, 20, 60) of
 * 63, every other entry black.
 */
struct font_palette_entry
{
	std::uint8_t r, g, b;
};

struct font_palette
{
	[[nodiscard]]
	font_palette_entry operator[](const std::size_t i) const
	{
		return i == 1 ? font_palette_entry{.r = 40, .g = 20, .b = 60}
		              : font_palette_entry{.r = 0, .g = 0, .b = 0};
	}
};

/* The font atlas as a texture image of format, expanded from the palette
 * indices as the front end does (palette_expand.h).
 */
struct font_texture
{
	std::vector<std::uint8_t> rgba;
	texture_image image;
	explicit font_texture(const texture_format format)
	    : rgba(font_texels * 4), image{{.size = {16, 8},
		                                .format = format,
		                                .filter = filter::nearest,
		                                .anisotropic = false,
		                                .has_transparency = true},
		                               rgba}
	{
		expand_palette(font_palette{},
		               {.indices = font_indices,
		                .stride = 16,
		                .x = 0,
		                .y = 0,
		                .size = {16, 8},
		                .transparent = true,
		                .super_transparent = format == texture_format::luminance4_alpha4},
		               format, {16, 8}, rgba);
	}
	font_texture(const font_texture &) = delete;
	font_texture &operator=(const font_texture &) = delete;
	font_texture(font_texture &&) = delete;
	font_texture &operator=(font_texture &&) = delete;
	~font_texture() = default;
};

/* The fonts trace's bytes (#33): what the three font formats keep, and
 * text in them.  Frames 0 to 7, for each of ramp_formats (rgba8 as the
 * reference, rgba2, luminance4_alpha4, intensity4), the colour ramp of
 * ramp_texture over the target unblended, then the alpha ramp blended
 * over black; texel (x, y) fills the 8 by 6 pixels at column x * 8, row
 * y * 6 from the bottom.  Frames 8 to 10, text: the glyphs of a 16 by 8
 * atlas (font_indices), expanded as rgba2, luminance4_alpha4 and
 * intensity4, blitted as the front end blits a string (ambient state,
 * blit_vertices, bitmap_uv) in three vertex colours, one of them
 * translucent, over a grey quad.
 */
[[nodiscard]]
inline std::vector<std::uint8_t> fonts_trace()
{
	trace::writer w{sample_target};
	const auto whole{sample_quad(flat({{1, 1, 1, 1}}), 1)};
	std::uint32_t frame{0};
	std::uint32_t slot{1};
	for (const auto f : ramp_formats)
		for (const bool alpha : {false, true})
		{
			const ramp_texture t{f, alpha};
			w.texture({slot}, &t.image);
			w.clear({.color = true, .depth = true});
			draw_call c{sample_draw(topology::fan, whole)};
			c.texture = {slot};
			c.state.blend = alpha ? blend::normal : blend::none;
			c.state.alpha_ref = alpha_test_off;
			w.draw(c);
			w.present({.frame = frame++});
			++slot;
		}
	const auto grey{sample_flat_quad({{0.3f, 0.3f, 0.3f, 1}})};
	for (const auto f :
	     {texture_format::rgba2, texture_format::luminance4_alpha4, texture_format::intensity4})
	{
		const font_texture t{f};
		w.texture({slot}, &t.image);
		w.clear({.color = true, .depth = true});
		w.draw(sample_draw(topology::fan, grey));
		ambient a;
		a.viewport = {.x = 0, .y = 0, .size = sample_target};
		const std::array<std::array<float, 16>, 3> tints{{
		    {{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}},
		    {{1, 0.2f, 0.2f, 1, 1, 0.2f, 0.2f, 1, 0.2f, 1, 0.2f, 1, 0.2f, 1, 0.2f, 1}},
		    {{0.2f, 0.4f, 1, 0.6f, 0.2f, 0.4f, 1, 0.6f, 0.2f, 0.4f, 1, 0.6f, 0.2f, 0.4f, 1, 0.6f}},
		}};
		for (std::size_t g{0}; g != 3; ++g)
		{
			const int gx{g == 2 ? 8 : 0}, gy{g == 1 ? 4 : 0};
			const auto uv{bitmap_uv({.x = gx,
			                         .y = gy,
			                         .w = 8,
			                         .h = 4,
			                         .tex_w = 16,
			                         .tex_h = 8,
			                         .tex_tw = 16,
			                         .tex_th = 8,
			                         .u = 1,
			                         .v = 1})};
			const auto v{
			    blit_vertices(canvas_rect(4 + static_cast<int>(g) * 20,
				                          10 + static_cast<int>(g) * 8, 16, 8, sample_target),
				              uv, tints[g])};
			w.draw(a.fan(v, {slot}));
		}
		w.present({.frame = frame++});
		++slot;
	}
	return w.take();
}

}

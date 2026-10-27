/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The vertices of the front end's 2D blits (HUD and menu bitmaps, font
 * glyphs, movie frames): a screen rectangle of the 0..1 ortho projection
 * (screen_ortho) and the part of a texture it shows.  Each function does
 * the arithmetic ogl_ubitmapm_cs and ogl_ubitblt_i did before the render
 * device, in the same types, so the pixels stay those of GL (R1).
 */

#pragma once

#include <array>
#include <cstddef>

#include "common/render/render_device.h"

namespace dcx::render {

/* A rectangle in the 0..1 screen space: left and right on x, top and
 * bottom on y, which grows upward.
 */
struct screen_rect
{
	float left;
	float right;
	float top;
	float bottom;
	constexpr bool operator==(const screen_rect &) const = default;
};

/* The part of a texture a blit shows, in texture coordinates. */
struct texture_rect
{
	float u1;
	float u2;
	float v1;
	float v2;
	constexpr bool operator==(const texture_rect &) const = default;
};

/* The pixel rectangle at (x, y) of size w by h from the target's top left,
 * as a screen rectangle: ogl_ubitmapm_cs divided in double.
 */
[[nodiscard]]
constexpr screen_rect canvas_rect(const int x, const int y, const int w, const int h,
                                  const extent target)
{
	const double tw{static_cast<double>(target.width)};
	const double th{static_cast<double>(target.height)};
	return {
	    .left = static_cast<float>(x / tw),
	    .right = static_cast<float>((w + x) / tw),
	    .top = static_cast<float>(1.0 - y / th),
	    .bottom = static_cast<float>(1.0 - (h + y) / th),
	};
}

/* The same for a movie frame, which ogl_ubitblt_i divided in float. */
[[nodiscard]]
constexpr screen_rect movie_rect(const unsigned x, const unsigned y, const unsigned w,
                                 const unsigned h, const extent target)
{
	const float tw{static_cast<float>(target.width)};
	const float th{static_cast<float>(target.height)};
	const float left{static_cast<float>(x) / tw};
	const float width{static_cast<float>(w) / tw};
	const float top{static_cast<float>(1.0 - static_cast<double>(static_cast<float>(y) / th))};
	const float height{static_cast<float>(h) / th};
	return {.left = left, .right = left + width, .top = top, .bottom = top - height};
}

/* A bitmap's place in its texture: the bitmap's offset and size (x, y,
 * w, h; a sub bitmap, as a font glyph, is a window on its parent's
 * texture), the size of the part of the texture the upload filled (tex_w,
 * tex_h), the size of the texture (tex_tw, tex_th, powers of two) and the
 * texture coordinates of that part's far corner (u, v): the ogl_texture's
 * w and h, which ogl_init_texture sets, and tw, th, u and v, which
 * ogl_loadtexture sets.
 */
struct bitmap_window
{
	int x;
	int y;
	int w;
	int h;
	unsigned tex_w;
	unsigned tex_h;
	unsigned tex_tw;
	unsigned tex_th;
	float u;
	float v;
};

/* The texture coordinates of the bitmap, as ogl_ubitmapm_cs took them. */
[[nodiscard]]
constexpr texture_rect bitmap_uv(const bitmap_window &b)
{
	texture_rect t{};
	const float tw{static_cast<float>(b.tex_tw)};
	const float th{static_cast<float>(b.tex_th)};
	if (b.x == 0)
	{
		t.u1 = 0;
		t.u2 = (b.w == static_cast<int>(b.tex_w)) ? b.u : static_cast<float>(b.w + b.x) / tw;
	}
	else
	{
		t.u1 = static_cast<float>(b.x) / tw;
		t.u2 = static_cast<float>(b.w + b.x) / tw;
	}
	if (b.y == 0)
	{
		t.v1 = 0;
		t.v2 = (b.h == static_cast<int>(b.tex_h)) ? b.v : static_cast<float>(b.h + b.y) / th;
	}
	else
	{
		t.v1 = static_cast<float>(b.y) / th;
		t.v2 = static_cast<float>(b.h + b.y) / th;
	}
	return t;
}

/* The four corners of a blit as a triangle fan, top left first, clockwise,
 * the colour of each from colors (four RGBA values in a row).
 */
[[nodiscard]]
constexpr std::array<vertex, 4> blit_vertices(const screen_rect &r, const texture_rect &t,
                                              const std::array<float, 16> &colors)
{
	const auto corner{
	    [&](const std::size_t i, const float x, const float y, const float u, const float v) {
		    return vertex{
		        .pos = {{x, y, 0}},
		        .rgba = {{colors[i * 4], colors[i * 4 + 1], colors[i * 4 + 2], colors[i * 4 + 3]}},
		        .uv = {{u, v}},
		    };
	    }};
	return {{
	    corner(0, r.left, r.top, t.u1, t.v1),
	    corner(1, r.right, r.top, t.u2, t.v1),
	    corner(2, r.right, r.bottom, t.u2, t.v2),
	    corner(3, r.left, r.bottom, t.u1, t.v2),
	}};
}

}

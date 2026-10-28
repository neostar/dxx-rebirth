/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Points and strips as triangle lists (lines follow), for an adapter
 * whose API has no wide lines or sized points (SDL_GPU: D3D12 has neither,
 * #32; ADR 2's pixel-centre rule).  The expansion happens in clip space on
 * the transform model the triangles use, clip = P * (MV * v) with no
 * divide: a segment is clipped there against GL's six planes, and each
 * corner of its quad is the endpoint moved in the image plane, by a
 * number of window pixels that is a multiple of w / viewport in clip
 * units.  P and MV map that step back to model space, so the vertex shader
 * transforms the corners with the draw's own matrices and the pixels fall
 * where GL's do.  Dividing by w before the clip would mirror an endpoint
 * behind the eye to the front of it (test-sdlgpu's
 * dividing_before_the_clip_mirrors_an_endpoint_behind_the_eye).
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "common/render/render_device.h"
#include "common/render/transform_matrix.h"

namespace dcx::render {

/* What an expansion reads of a draw call: its matrices, the viewport's size
 * in pixels, and the width of its lines or the size of its points.
 */
struct expansion_view
{
	matrix projection;
	matrix modelview;
	extent viewport;
	float size;
};

[[nodiscard]]
inline expansion_view expansion_view_of(const draw_call &call)
{
	return {.projection = matrix_of(call.transforms.projection),
	        .modelview = matrix_of(call.transforms.modelview),
	        .viewport = call.viewport.size,
	        .size = call.state.line_width};
}

namespace detail {

using clip_coords = std::array<double, 4>;

/* The clip coordinates of p, as pos_color_uv.vert.hlsl computes them. */
[[nodiscard]]
inline clip_coords clip_of(const expansion_view &view, const std::array<float, 3> &p)
{
	const auto times{[](const matrix &m, const std::array<float, 4> &v) {
		std::array<float, 4> r{};
		for (std::size_t i{0}; i != 4; ++i)
			r[i] = m[i] * v[0] + m[4 + i] * v[1] + m[8 + i] * v[2] + m[12 + i] * v[3];
		return r;
	}};
	const auto eye{times(view.modelview, {{p[0], p[1], p[2], 1.f}})};
	const auto c{times(view.projection, eye)};
	return {c[0], c[1], c[2], c[3]};
}

/* GL's clip volume as six distances, each at least 0 inside. */
[[nodiscard]]
constexpr std::array<double, 6> clip_distances(const clip_coords &c)
{
	return {{c[3] + c[0], c[3] - c[0], c[3] + c[1], c[3] - c[1], c[3] + c[2], c[3] - c[2]}};
}

/* GL's width of a line or size of a point in whole pixels, at least 1:
 * Apple's GL draws a size of 1.5 as 2 and 2.5 as 3 (#32).
 */
[[nodiscard]]
inline double whole_pixels(const float size)
{
	return std::max(1.0, std::floor(static_cast<double>(size) + 0.5));
}

/* The model-space step that moves a point of clip coordinate w by pixels
 * window pixels along x (axis 0) or y (axis 1), parallel to the image
 * plane: pixels * 2 / viewport in NDC, times w in clip space, and back
 * through P (at constant depth a clip step is P's diagonal times an eye
 * step) and MV (a scale and translation).  Zero for an axis a matrix
 * flattens.
 */
[[nodiscard]]
inline std::array<float, 3> window_step(const expansion_view &view, const double w,
                                        const std::size_t axis, const double pixels)
{
	const double extent_px{
	    static_cast<double>(axis == 0 ? view.viewport.width : view.viewport.height)};
	const double scale{static_cast<double>(view.projection[axis * 5]) *
	                   static_cast<double>(view.modelview[axis * 5])};
	std::array<float, 3> s{};
	if (scale != 0 && extent_px != 0)
		s[axis] = static_cast<float>(pixels * 2 * w / (extent_px * scale));
	return s;
}

/* A corner: v moved by sign times step. */
[[nodiscard]]
constexpr vertex offset_vertex(const vertex &v, const std::array<float, 3> &step, const float sign)
{
	vertex r{v};
	for (std::size_t i{0}; i != 3; ++i)
		r.pos[i] += sign * step[i];
	return r;
}

inline void append_quad(std::vector<vertex> &out, const vertex &c0, const vertex &c1,
                        const vertex &c2, const vertex &c3)
{
	out.insert(out.end(), {c0, c1, c2, c0, c2, c3});
}

inline void append_point(std::vector<vertex> &out, const expansion_view &view, const vertex &v)
{
	const auto c{clip_of(view, v.pos)};
	for (const double d : clip_distances(c))
		if (d < 0)
			return;
	if (!(c[3] > 0))
		return;
	const double pixels{whole_pixels(view.size)};
	const auto sx{window_step(view, c[3], 0, pixels)};
	const auto sy{window_step(view, c[3], 1, pixels)};
	const auto at{[&](const float x, const float y) {
		vertex r{v};
		for (std::size_t i{0}; i != 3; ++i)
			r.pos[i] += x * sx[i] + y * sy[i];
		return r;
	}};
	append_quad(out, at(-0.5f, -0.5f), at(0.5f, -0.5f), at(0.5f, 0.5f), at(-0.5f, 0.5f));
}

}

/* GL_POINTS: a square of the size's whole pixels, centred on each. */
[[nodiscard]]
inline std::vector<vertex> points_to_list(const expansion_view &view,
                                          const std::span<const vertex> v)
{
	std::vector<vertex> list;
	for (const auto &p : v)
		detail::append_point(list, view, p);
	return list;
}

/* GL_TRIANGLE_STRIP as a triangle list.  Triangle i is (vi, vi+1, vi+2)
 * for even i and (vi+1, vi, vi+2) for odd i, which keeps the winding; each
 * is emitted rotated, as fan_to_list does, to put vi+2, GL's provoking
 * vertex, first.  Fewer than three vertices draw nothing.
 */
[[nodiscard]]
inline std::vector<vertex> strip_to_list(const std::span<const vertex> strip)
{
	std::vector<vertex> list;
	if (strip.size() < 3)
		return list;
	list.reserve(3 * (strip.size() - 2));
	for (std::size_t i{0}; i + 2 < strip.size(); ++i)
	{
		list.push_back(strip[i + 2]);
		list.push_back(strip[i % 2 == 0 ? i : i + 1]);
		list.push_back(strip[i % 2 == 0 ? i + 1 : i]);
	}
	return list;
}

}

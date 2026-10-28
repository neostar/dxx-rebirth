/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Lines, line loops, points and strips as triangle lists, for an adapter
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
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
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

/* Whether the x and y rows of P and MV do not mix x with y, which
 * window_step relies on (the matrices are column-major: m[1] is row 1 of
 * column 0, m[4] and m[8] row 0 of columns 1 and 2, m[9] row 1 of column 2).
 */
[[nodiscard]]
constexpr bool has_diagonal_recipe(const expansion_view &view)
{
	for (const matrix *m : {&view.projection, &view.modelview})
		if ((*m)[1] != 0 || (*m)[4] != 0 || (*m)[8] != 0 || (*m)[9] != 0)
			return false;
	return true;
}

[[nodiscard]]
inline expansion_view expansion_view_of(const draw_call &call)
{
	const expansion_view view{.projection = matrix_of(call.transforms.projection),
	                          .modelview = matrix_of(call.transforms.modelview),
	                          .viewport = call.viewport.size,
	                          .size = call.state.line_width};
	assert(has_diagonal_recipe(view));
	return view;
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
	return {{c[0], c[1], c[2], c[3]}};
}

/* GL's clip volume as six distances, each at least 0 inside. */
[[nodiscard]]
constexpr std::array<double, 6> clip_distances(const clip_coords &c)
{
	return {{c[3] + c[0], c[3] - c[0], c[3] + c[1], c[3] - c[1], c[3] + c[2], c[3] - c[2]}};
}

/* GL clamps a vertex colour to [0, 1] before it interpolates or clips it. */
[[nodiscard]]
constexpr std::array<float, 4> saturated(const std::array<float, 4> &c)
{
	std::array<float, 4> r{};
	for (std::size_t i{0}; i != 4; ++i)
		r[i] = std::clamp(c[i], 0.f, 1.f);
	return r;
}

/* The vertex a fraction t along the segment from a to b; a at 0, b at 1,
 * exactly.  Every vertex has the colour of the clamped ends.
 */
[[nodiscard]]
inline vertex lerp_vertex(const vertex &a, const vertex &b, const double t)
{
	const auto ca{saturated(a.rgba)}, cb{saturated(b.rgba)};
	if (t <= 0 || t >= 1)
	{
		vertex end{t <= 0 ? a : b};
		end.rgba = t <= 0 ? ca : cb;
		return end;
	}
	const auto mix{
	    [t](const float x, const float y) { return static_cast<float>(x + (y - x) * t); }};
	vertex r{};
	for (std::size_t i{0}; i != 3; ++i)
		r.pos[i] = mix(a.pos[i], b.pos[i]);
	for (std::size_t i{0}; i != 4; ++i)
		r.rgba[i] = mix(ca[i], cb[i]);
	for (std::size_t i{0}; i != 2; ++i)
		r.uv[i] = mix(a.uv[i], b.uv[i]);
	return r;
}

/* A vertex of the expansion with its clip coordinates. */
struct clipped
{
	vertex v;
	clip_coords c;
};

/* The part of the segment a to b inside GL's clip volume, clipped in clip
 * space before any divide (Cyrus-Beck over the six planes); none when no
 * part is inside.
 */
[[nodiscard]]
inline std::optional<std::array<clipped, 2>> clip_segment(const expansion_view &view,
                                                          const vertex &a, const vertex &b)
{
	const auto ca{clip_of(view, a.pos)}, cb{clip_of(view, b.pos)};
	const auto da{clip_distances(ca)}, db{clip_distances(cb)};
	double t0{0}, t1{1};
	for (std::size_t i{0}; i != da.size(); ++i)
	{
		if (da[i] < 0 && db[i] < 0)
			return std::nullopt;
		if (da[i] < 0)
			t0 = std::max(t0, da[i] / (da[i] - db[i]));
		else if (db[i] < 0)
			t1 = std::min(t1, da[i] / (da[i] - db[i]));
	}
	if (t0 > t1)
		return std::nullopt;
	const auto at{[&](const double t) {
		return clip_coords{{ca[0] + (cb[0] - ca[0]) * t, ca[1] + (cb[1] - ca[1]) * t,
		                    ca[2] + (cb[2] - ca[2]) * t, ca[3] + (cb[3] - ca[3]) * t}};
	}};
	const clipped first{.v = lerp_vertex(a, b, t0), .c = t0 <= 0 ? ca : at(t0)};
	const clipped second{.v = lerp_vertex(a, b, t1), .c = t1 >= 1 ? cb : at(t1)};
	return std::array<clipped, 2>{{first, second}};
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
 * plane: pixels * 2 / viewport in NDC, times w in clip space, divided by
 * the diagonal entries of P and MV.  There is no inverse: this holds for
 * diagonal recipes only (a scale and translation, an ortho or symmetric
 * perspective projection), which expansion_view_of asserts.  Zero for an
 * axis a matrix flattens.
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
	const std::array<vertex, 6> q{{c0, c1, c2, c0, c2, c3}};
	out.insert(out.end(), q.begin(), q.end());
}

/* Whether the line from window point (x0, y0) to (x1, y1) is x-major.  GL
 * (Apple's, measured on the lines trace, #32) takes x-major for a tie, and
 * ties are common: the endpoints of an exact diagonal are float, and carry
 * rounding of 4e-6 pixel at 64 px (a float's ulp there), which puts |dx|
 * and |dy| that far apart either way.  GL's vertices lie on the
 * rasteriser's grid of 1/256 pixel, where that rounding is gone, but each
 * endpoint's x and y round on their own: a diagonal whose endpoints round
 * to different extents (the trace's 45 degree spoke, 1493 against 1494
 * units) is not a tie, and GL takes y-major there.  So the endpoints are
 * rounded to that grid before the extents are compared.
 */
[[nodiscard]]
inline bool x_major(const double x0, const double y0, const double x1, const double y1)
{
	constexpr double grid{256};
	const auto snapped{[](const double v) { return std::round(v * grid); }};
	return std::abs(snapped(x1) - snapped(x0)) >= std::abs(snapped(y1) - snapped(y0));
}

inline void append_line(std::vector<vertex> &out, const expansion_view &view, const vertex &a,
                        const vertex &b)
{
	const auto seg{clip_segment(view, a, b)};
	if (!seg)
		return;
	const auto &[p, q]{seg.value()};
	if (!(p.c[3] > 0) || !(q.c[3] > 0))
		return;
	/* GL's wide line, non-antialiased, repeats the one-pixel line width
	 * times along the minor axis (the spike's push_wide_line, the GL
	 * specification's section 3.4.2, and Apple's GL, measured): a
	 * parallelogram whose ends are the endpoints' columns (x-major) or rows.
	 */
	const double dx{(q.c[0] / q.c[3] - p.c[0] / p.c[3]) * view.viewport.width / 2};
	const double dy{(q.c[1] / q.c[3] - p.c[1] / p.c[3]) * view.viewport.height / 2};
	if (dx == 0 && dy == 0)
		return;
	const auto window{[&](const clipped &e, const std::size_t axis, const double extent) {
		return (e.c[axis] / e.c[3] * 0.5 + 0.5) * extent;
	}};
	const double w{static_cast<double>(view.viewport.width)};
	const double h{static_cast<double>(view.viewport.height)};
	const std::size_t minor{
	    x_major(window(p, 0, w), window(p, 1, h), window(q, 0, w), window(q, 1, h)) ? 1u : 0u};
	const double width{whole_pixels(view.size)};
	const auto sa{window_step(view, p.c[3], minor, width)};
	const auto sb{window_step(view, q.c[3], minor, width)};
	/* A width-1 line GL draws by the diamond-exit rule: it includes a pixel
	 * centre at the start of the line and not at its end, whichever way
	 * it runs, where Metal's rasteriser includes a centre on a quad's left
	 * edge and on its upper edge, not on its right or lower one.  Sliding
	 * the line a subpixel along itself, away from the end that GL
	 * includes, moves the ties to GL's and leaves the line where it is.  A
	 * wider line GL draws as a quad, with Metal's ties (measured, #32).
	 */
	const bool along_x{minor == 1};
	const double toward{width > 1 ? 0.0 : along_x ? (dx < 0 ? 1.0 : 0.0) : (dy > 0 ? -1.0 : 0.0)};
	constexpr double subpixel{1.0 / 256};
	const double slide_x{along_x ? toward * subpixel : toward * subpixel * dx / dy};
	const double slide_y{along_x ? toward * subpixel * dy / dx : toward * subpixel};
	const auto slide{[&](const clipped &end, const vertex &v) {
		const auto sx{window_step(view, end.c[3], 0, slide_x)};
		const auto sy{window_step(view, end.c[3], 1, slide_y)};
		vertex r{v};
		for (std::size_t i{0}; i != 3; ++i)
			r.pos[i] += sx[i] + sy[i];
		return r;
	}};
	const vertex pa{slide(p, p.v)}, qa{slide(q, q.v)};
	append_quad(out, offset_vertex(pa, sa, -0.5f), offset_vertex(pa, sa, 0.5f),
	            offset_vertex(qa, sb, 0.5f), offset_vertex(qa, sb, -0.5f));
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

/* GL_LINES as a triangle list: two triangles per pair of vertices, in
 * order; a last odd vertex draws nothing.
 */
[[nodiscard]]
inline std::vector<vertex> lines_to_list(const expansion_view &view,
                                         const std::span<const vertex> v)
{
	std::vector<vertex> list;
	for (std::size_t i{0}; i + 1 < v.size(); i += 2)
		detail::append_line(list, view, v[i], v[i + 1]);
	return list;
}

/* GL_LINE_LOOP: a line from each vertex to the next, and from the last to
 * the first; fewer than two vertices draw nothing.
 */
[[nodiscard]]
inline std::vector<vertex> line_loop_to_list(const expansion_view &view,
                                             const std::span<const vertex> v)
{
	std::vector<vertex> list;
	if (v.size() < 2)
		return list;
	for (std::size_t i{0}; i != v.size(); ++i)
		detail::append_line(list, view, v[i], v[(i + 1) % v.size()]);
	return list;
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

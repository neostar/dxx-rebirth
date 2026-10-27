/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The matrices of the transform recipes (render_device.h) for an adapter
 * without GL's matrix stack: each as GL builds it after glLoadIdentity,
 * column-major, as glGetFloatv reads it and the SDL_GPU vertex shader takes
 * it (shaders/hlsl/pos_color_uv.vert.hlsl).
 */

#pragma once

#include <array>
#include <cmath>
#include <numbers>
#include <variant>

#include "common/render/render_device.h"

namespace dcx::render {

/* A 4x4 matrix, column-major: element (row r, column c) at 4 * c + r. */
using matrix = std::array<float, 16>;

/* glOrtho's matrix (the GL specification's), computed in double, as glOrtho
 * takes its arguments, and rounded to float once per element.
 */
[[nodiscard]]
constexpr matrix matrix_of(const ortho &o)
{
	const double width{o.right - o.left};
	const double height{o.top - o.bottom};
	const double depth{o.z_far - o.z_near};
	const auto f{[](const double v) { return static_cast<float>(v); }};
	matrix m{};
	m[0] = f(2 / width);
	m[5] = f(2 / height);
	m[10] = f(-2 / depth);
	m[12] = f(-(o.right + o.left) / width);
	m[13] = f(-(o.top + o.bottom) / height);
	/* -0 when near and far are opposite: stored as GL stores it, +0. */
	m[14] = f(-(o.z_far + o.z_near) / depth) + 0.f;
	m[15] = 1;
	return m;
}

/* gluPerspective's matrix as Apple's GL builds it, fitted element for
 * element to glGetFloatv on an M1 Max (test-rtrace's
 * perspective_matrix_is_gls holds it there): GLU's glFrustum form, ymax =
 * z_near * tan(fovy * pi / 360) and xmax = ymax * aspect in double, then
 * each reciprocal of the frustum's width, height and depth, and 2 near,
 * rounded to float before it multiplies (elements 0, 5 and 14 take that
 * 2 near).  SGI's form, the cotangent and one division
 * per element in double, is one ULP off in elements 0, 5 and 10, and a
 * ULP in depth makes visible ties (ADR 2).  A non-zero stereo_dx is then
 * added to element 8 in float, as stereo_shift does to the matrix it reads
 * back.
 */
[[nodiscard]]
inline matrix matrix_of(const perspective &p)
{
	const auto f{[](const double v) { return static_cast<float>(v); }};
	const double ymax{p.z_near * std::tan(p.fovy * std::numbers::pi / 360)};
	const double xmax{ymax * p.aspect};
	const double inv_width{f(1 / static_cast<double>(f(2 * xmax)))};
	const double inv_height{f(1 / static_cast<double>(f(2 * ymax)))};
	const double inv_depth{f(1 / static_cast<double>(f(p.z_far - p.z_near)))};
	const double two_near{f(2 * p.z_near)};
	matrix m{};
	m[0] = f(two_near * inv_width);
	m[5] = f(two_near * inv_height);
	m[10] = f(-(p.z_far + p.z_near) * inv_depth);
	m[11] = -1;
	m[14] = f(-two_near * p.z_far * inv_depth);
	if (p.stereo_dx != 0)
		m[8] += p.stereo_dx;
	return m;
}

/* The matrix of either projection recipe. */
[[nodiscard]]
inline matrix matrix_of(const projection &p)
{
	return std::visit([](const auto &r) { return matrix_of(r); }, p);
}

/* glLoadIdentity alone, or then glTranslatef and glScalef: T * S. */
[[nodiscard]]
constexpr matrix matrix_of(const modelview &mv)
{
	matrix m{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	if (const auto *const ts{std::get_if<translate_scale>(&mv)})
	{
		m[0] = ts->scale[0];
		m[5] = ts->scale[1];
		m[10] = ts->scale[2];
		m[12] = ts->translate[0];
		m[13] = ts->translate[1];
		m[14] = ts->translate[2];
	}
	return m;
}

}

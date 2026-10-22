/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The GL calls of the GL adapter's draw state (ADR 2), without a GL type:
 * they are issued through an ops object, the GL adapter's real one or a
 * test's model of GL, so what they do is tested without a GL context.
 * Transform recipes are replayed with the GL calls that build them today,
 * never as matrices, so the matrices keep their bits (R1).
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

#include "common/render/render_device.h"

namespace dcx::render::gl {

/* The glEnable capabilities a draw state sets. */
enum class capability : std::uint8_t
{
	blend,
	alpha_test,
	depth_test,
	cull_face,
};

/* The glBlendFunc factors the game uses. */
enum class factor : std::uint8_t
{
	zero,
	one,
	src_alpha,
	one_minus_src_alpha,
	one_minus_src_color,
};

struct blend_factors
{
	factor src;
	factor dst;
	constexpr bool operator==(const blend_factors &) const = default;
};

enum class matrix : std::uint8_t
{
	projection,
	modelview,
};

/* The glBlendFunc factors of b; none for blend::none, which turns blending
 * off instead.
 */
[[nodiscard]]
constexpr std::optional<blend_factors> blend_function(const blend b)
{
	switch (b)
	{
	case blend::normal:
		return blend_factors{.src = factor::src_alpha, .dst = factor::one_minus_src_alpha};
	case blend::additive_a:
		return blend_factors{.src = factor::src_alpha, .dst = factor::one};
	case blend::additive_c:
		return blend_factors{.src = factor::one, .dst = factor::one};
	case blend::palfx_neg:
		return blend_factors{.src = factor::zero, .dst = factor::one_minus_src_color};
	case blend::none:
		break;
	}
	return std::nullopt;
}

/* Whether a draw of t rasterizes with the line width. */
[[nodiscard]]
constexpr bool uses_line_width(const topology t)
{
	return t == topology::lines || t == topology::line_loop || t == topology::points;
}

/* The GL calls the shadow issues, one member each:
 * enable(capability, bool)       glEnable or glDisable
 * blend_func(blend_factors)      glBlendFunc
 * alpha_func_gequal(float)       glAlphaFunc(GL_GEQUAL, ref)
 * depth_func_lequal()            glDepthFunc(GL_LEQUAL)
 * cull_face_front()              glCullFace(GL_FRONT)
 * viewport(viewport)             glViewport
 * matrix_mode(matrix)            glMatrixMode
 * load_identity()                glLoadIdentity
 * perspective(perspective)       gluPerspective(fovy, aspect, near, far)
 * stereo_shift(float)            the projection's element 8 += dx
 * ortho(ortho)                   glOrtho
 * translate(array<float, 3>)     glTranslatef
 * scale(array<float, 3>)         glScalef
 * line_width(float)              glLineWidth
 */
template <typename T>
concept ops = requires(T &gl, const blend_factors &f, const viewport &v, const perspective &p,
                       const ortho &o, const std::array<float, 3> &a) {
	gl.enable(capability::blend, true);
	gl.blend_func(f);
	gl.alpha_func_gequal(0.f);
	gl.depth_func_lequal();
	gl.cull_face_front();
	gl.viewport(v);
	gl.matrix_mode(matrix::projection);
	gl.load_identity();
	gl.perspective(p);
	gl.stereo_shift(0.f);
	gl.ortho(o);
	gl.translate(a);
	gl.scale(a);
	gl.line_width(0.f);
};

/* Replays a projection recipe as ogl_start_frame and ogl_end_frame build it,
 * and leaves the modelview matrix current, as the game expects.
 */
template <ops Gl>
void replay(Gl &gl, const projection &p)
{
	gl.matrix_mode(matrix::projection);
	gl.load_identity();
	if (const auto *const persp{std::get_if<perspective>(&p)})
	{
		gl.perspective(*persp);
		if (persp->stereo_dx != 0)
			gl.stereo_shift(persp->stereo_dx);
	}
	else if (const auto *const orth{std::get_if<ortho>(&p)})
		gl.ortho(*orth);
	gl.matrix_mode(matrix::modelview);
}

/* Replays a modelview recipe; the modelview matrix is current. */
template <ops Gl>
void replay(Gl &gl, const modelview &m)
{
	gl.load_identity();
	if (const auto *const ts{std::get_if<translate_scale>(&m)})
	{
		gl.translate(ts->translate);
		gl.scale(ts->scale);
	}
}

}

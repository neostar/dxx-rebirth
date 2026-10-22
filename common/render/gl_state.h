/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The GL adapter's state shadow (ADR 2): turns each self-contained draw
 * call back into the few GL state calls that differ from the last draw's.
 * It holds no GL type: it issues the calls through an ops object, the GL
 * adapter's real one or a test's recorder, so what it emits is tested
 * without a GL context.  Transform recipes are replayed with the GL calls
 * that build them today, never as matrices, so the matrices keep their
 * bits (R1).
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

/* What the adapter last set, each part unknown (empty) until it sets it:
 * the first draw sets everything.  Code not yet drawing through the device
 * changes GL behind the shadow's back, so the GL state of a draw is right
 * while two things hold:
 * (a) the front end's ambient state, which every draw call is built from,
 *     equals GL's blend, alpha reference, depth test, culling, viewport and
 *     transforms at each draw: a part changed behind the shadow's back then
 *     costs a redundant call, never a missing one;
 * (b) code outside the device leaves the alpha test on, glDepthFunc LEQUAL,
 *     glCullFace FRONT and the modelview matrix current, which the shadow
 *     sets once and assumes; ogl_start_frame does so before every 3D draw
 *     today.
 */
class state_shadow
{
	std::optional<bool> blend_on_;
	std::optional<blend_factors> blend_func_;
	std::optional<bool> alpha_test_on_;
	std::optional<float> alpha_ref_;
	std::optional<bool> depth_on_;
	bool depth_func_set_{false};
	std::optional<bool> cull_on_;
	bool cull_face_set_{false};
	std::optional<float> line_width_;
	std::optional<render::viewport> viewport_;
	std::optional<render::projection> projection_;
	std::optional<render::modelview> modelview_;

	template <ops Gl>
	static void enable(Gl &gl, std::optional<bool> &shadow, const capability c, const bool on)
	{
		if (shadow != on)
		{
			gl.enable(c, on);
			shadow = on;
		}
	}

  public:
	/* Issues the GL calls that bring GL from the last applied call's state
	 * to call's: draw state, viewport and transforms.
	 */
	template <ops Gl>
	void apply(Gl &gl, const draw_call &call)
	{
		const draw_state &s{call.state};
		if (const auto f{blend_function(s.blend)})
		{
			enable(gl, blend_on_, capability::blend, true);
			if (blend_func_ != f)
			{
				gl.blend_func(*f);
				blend_func_ = f;
			}
		}
		else
			enable(gl, blend_on_, capability::blend, false);
		enable(gl, alpha_test_on_, capability::alpha_test, true);
		if (alpha_ref_ != s.alpha_ref)
		{
			gl.alpha_func_gequal(s.alpha_ref);
			alpha_ref_ = s.alpha_ref;
		}
		const bool depth_on{s.depth == depth::test_and_write};
		enable(gl, depth_on_, capability::depth_test, depth_on);
		if (depth_on && !depth_func_set_)
		{
			gl.depth_func_lequal();
			depth_func_set_ = true;
		}
		const bool cull_on{s.cull == cull::front};
		enable(gl, cull_on_, capability::cull_face, cull_on);
		if (cull_on && !cull_face_set_)
		{
			gl.cull_face_front();
			cull_face_set_ = true;
		}
		if (uses_line_width(call.topology) && line_width_ != s.line_width)
		{
			gl.line_width(s.line_width);
			line_width_ = s.line_width;
		}
		if (viewport_ != call.viewport)
		{
			gl.viewport(call.viewport);
			viewport_ = call.viewport;
		}
		if (projection_ != call.transforms.projection)
		{
			replay(gl, call.transforms.projection);
			projection_ = call.transforms.projection;
		}
		if (modelview_ != call.transforms.modelview)
		{
			replay(gl, call.transforms.modelview);
			modelview_ = call.transforms.modelview;
		}
	}
};

}

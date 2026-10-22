/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/gl_state.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <variant>
#include <vector>

#include "common/render/ambient.h"
#include "common/render/render_device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth gl_state
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;
namespace gl = dcx::render::gl;

namespace {

/* A 4x4 matrix in GL's column-major order. */
using mat4 = std::array<double, 16>;

constexpr mat4 identity_matrix{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};

mat4 multiply(const mat4 &a, const mat4 &b)
{
	mat4 r{};
	for (std::size_t col{0}; col < 4; ++col)
		for (std::size_t row{0}; row < 4; ++row)
		{
			double sum{0};
			for (std::size_t k{0}; k < 4; ++k)
				sum += a.at(k * 4 + row) * b.at(col * 4 + k);
			r.at(col * 4 + row) = sum;
		}
	return r;
}

/* The matrix gluPerspective multiplies by. */
mat4 perspective_matrix(const render::perspective &p)
{
	const double f{1 / std::tan(p.fovy * std::numbers::pi / 360)};
	const double depth{p.z_near - p.z_far};
	return {{f / p.aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (p.z_far + p.z_near) / depth, -1, 0, 0,
	         2 * p.z_far * p.z_near / depth, 0}};
}

/* The matrix glOrtho multiplies by. */
mat4 ortho_matrix(const render::ortho &o)
{
	return {{2 / (o.right - o.left), 0, 0, 0, 0, 2 / (o.top - o.bottom), 0, 0, 0, 0,
	         -2 / (o.z_far - o.z_near), 0, -(o.right + o.left) / (o.right - o.left),
	         -(o.top + o.bottom) / (o.top - o.bottom), -(o.z_far + o.z_near) / (o.z_far - o.z_near),
	         1}};
}

/* A model of the GL state the shadow's calls change, starting from GL's
 * defaults, so tests check the state a draw leaves, not the calls that
 * made it.  It counts the calls, which is what the shadow saves.
 */
struct gl_model
{
	std::array<bool, 4> enabled{};
	gl::blend_factors blend{.src = gl::factor::one, .dst = gl::factor::zero};
	float alpha_ref{0};
	bool depth_lequal{false};
	bool cull_front{false};
	float width{1};
	render::viewport view{.x = 0, .y = 0, .size = {.width = 0, .height = 0}};
	gl::matrix mode{gl::matrix::modelview};
	mat4 projection{identity_matrix};
	mat4 modelview{identity_matrix};
	unsigned calls{0};

	mat4 &current()
	{
		return mode == gl::matrix::projection ? projection : modelview;
	}
	[[nodiscard]]
	bool is_enabled(const gl::capability c) const
	{
		return enabled.at(static_cast<std::size_t>(c));
	}
	void enable(const gl::capability c, const bool on)
	{
		++calls;
		enabled.at(static_cast<std::size_t>(c)) = on;
	}
	void blend_func(const gl::blend_factors &f)
	{
		++calls;
		blend = f;
	}
	void alpha_func_gequal(const float ref)
	{
		++calls;
		alpha_ref = ref;
	}
	void depth_func_lequal()
	{
		++calls;
		depth_lequal = true;
	}
	void cull_face_front()
	{
		++calls;
		cull_front = true;
	}
	void viewport(const render::viewport &v)
	{
		++calls;
		view = v;
	}
	void matrix_mode(const gl::matrix m)
	{
		++calls;
		mode = m;
	}
	void load_identity()
	{
		++calls;
		current() = identity_matrix;
	}
	void perspective(const render::perspective &p)
	{
		++calls;
		current() = multiply(current(), perspective_matrix(p));
	}
	/* The projection's element 8 += dx, whichever matrix is current. */
	void stereo_shift(const float dx)
	{
		++calls;
		projection[8] += dx;
	}
	void ortho(const render::ortho &o)
	{
		++calls;
		current() = multiply(current(), ortho_matrix(o));
	}
	void translate(const std::array<float, 3> &a)
	{
		++calls;
		current() =
		    multiply(current(), {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, a[0], a[1], a[2], 1}});
	}
	void scale(const std::array<float, 3> &a)
	{
		++calls;
		current() =
		    multiply(current(), {{a[0], 0, 0, 0, 0, a[1], 0, 0, 0, 0, a[2], 0, 0, 0, 0, 1}});
	}
	void line_width(const float w)
	{
		++calls;
		width = w;
	}
};

static_assert(gl::ops<gl_model>);

/* The projection matrix p's recipe stands for: the game's gluPerspective
 * with the stereo shift of element 8, or its glOrtho.
 */
mat4 expected_projection(const render::projection &p)
{
	if (const auto *const persp{std::get_if<render::perspective>(&p)})
	{
		auto m{perspective_matrix(*persp)};
		m[8] += persp->stereo_dx;
		return m;
	}
	return ortho_matrix(std::get<render::ortho>(p));
}

/* The modelview matrix m's recipe stands for: a translation of points
 * scaled first.
 */
mat4 expected_modelview(const render::modelview &m)
{
	if (const auto *const ts{std::get_if<render::translate_scale>(&m)})
	{
		const auto &t{ts->translate};
		const auto &s{ts->scale};
		return {{s[0], 0, 0, 0, 0, s[1], 0, 0, 0, 0, s[2], 0, t[0], t[1], t[2], 1}};
	}
	return identity_matrix;
}

}

/* Test that each blend selects the GL blend function the game sets for it
 * today, and that none has no function.
 */
BOOST_AUTO_TEST_CASE(blend_function_matches_game)
{
	using f = gl::factor;
	BOOST_TEST((gl::blend_function(render::blend::normal) ==
	            gl::blend_factors{.src = f::src_alpha, .dst = f::one_minus_src_alpha}));
	BOOST_TEST((gl::blend_function(render::blend::additive_a) ==
	            gl::blend_factors{.src = f::src_alpha, .dst = f::one}));
	BOOST_TEST((gl::blend_function(render::blend::additive_c) ==
	            gl::blend_factors{.src = f::one, .dst = f::one}));
	BOOST_TEST((gl::blend_function(render::blend::palfx_neg) ==
	            gl::blend_factors{.src = f::zero, .dst = f::one_minus_src_color}));
	BOOST_TEST(!gl::blend_function(render::blend::none).has_value());
}

/* Test that replaying a recipe leaves the matrices it stands for and the
 * modelview matrix current, as the game's own calls do.
 */
BOOST_AUTO_TEST_CASE(replay_leaves_recipe_matrices)
{
	const render::perspective eye{
	    .fovy = 90, .aspect = 1, .z_near = 0.1, .z_far = 5000, .stereo_dx = 0.25f};
	const render::translate_scale moved{.translate = {{0.25f, 0.5f, 0}}, .scale = {{2, 3, 1}}};
	for (const render::projection &p :
	     {render::projection{render::view_perspective}, render::projection{eye},
	      render::projection{render::screen_ortho}})
	{
		gl_model g;
		gl::replay(g, p);
		BOOST_TEST(g.projection == expected_projection(p), boost::test_tools::per_element());
		BOOST_TEST((g.mode == gl::matrix::modelview));
	}
	gl_model g;
	gl::replay(g, render::modelview{moved});
	BOOST_TEST(g.modelview == expected_modelview(moved), boost::test_tools::per_element());
	gl::replay(g, render::modelview{render::identity{}});
	BOOST_TEST(g.modelview == identity_matrix, boost::test_tools::per_element());
}

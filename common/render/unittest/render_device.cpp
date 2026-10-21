/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/render_device.h"

#include <cstdint>
#include <unordered_map>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth render_device
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

/* Test that two draw states with the same fields compare equal. */
BOOST_AUTO_TEST_CASE(draw_state_equal_when_fields_equal)
{
	const render::draw_state a{
	    .blend = render::blend::additive_c,
	    .depth = render::depth::test_and_write,
	    .cull = render::cull::front,
	    .alpha_ref = 0.8f,
	    .wrap = render::wrap::repeat,
	    .line_width = 2.f,
	};
	const render::draw_state b{a};
	BOOST_TEST((a == b));
	BOOST_TEST((render::draw_state{} == render::draw_state{}));
}

/* Test that changing any one field makes two draw states unequal. */
BOOST_AUTO_TEST_CASE(draw_state_unequal_when_one_field_differs)
{
	const render::draw_state base{};
	auto blend{base};
	blend.blend = render::blend::palfx_neg;
	auto depth{base};
	depth.depth = render::depth::test_and_write;
	auto cull{base};
	cull.cull = render::cull::front;
	auto alpha_ref{base};
	alpha_ref.alpha_ref = 0.8f;
	auto wrap{base};
	wrap.wrap = render::wrap::repeat;
	auto line_width{base};
	line_width.line_width = 2.f;
	BOOST_TEST((base != blend));
	BOOST_TEST((base != depth));
	BOOST_TEST((base != cull));
	BOOST_TEST((base != alpha_ref));
	BOOST_TEST((base != wrap));
	BOOST_TEST((base != line_width));
}

/* Test that the draw-state hash is the 64-bit FNV-1a of the state's
 * canonical bytes (blend, depth, cull, alpha_ref, wrap, line_width; enums as
 * one byte, floats as little-endian IEEE singles), so it is the same on every
 * platform and in every run.  The expected values come from an independent
 * Python implementation.
 */
BOOST_AUTO_TEST_CASE(draw_state_hash_matches_reference_values)
{
	BOOST_TEST(render::hash(render::draw_state{}) == 0x32c842eadf2ddd20u);
	BOOST_TEST(render::hash(render::draw_state{
	               .blend = render::blend::additive_c,
	               .depth = render::depth::test_and_write,
	               .cull = render::cull::front,
	               .alpha_ref = 0.8f,
	               .wrap = render::wrap::repeat,
	               .line_width = 2.f,
	           }) == 0x7ad40cecb7a65cd6u);
}

/* Test that negative and positive zero, which compare equal, hash equal. */
BOOST_AUTO_TEST_CASE(draw_state_hash_equal_for_signed_zeros)
{
	const render::draw_state positive{.alpha_ref = 0.f};
	const render::draw_state negative{.alpha_ref = -0.f};
	BOOST_TEST((positive == negative));
	BOOST_TEST(render::hash(negative) == 0x52b4c1da1b9eecf8u);
	BOOST_TEST(render::hash(positive) == render::hash(negative));
}

/* Test that a draw state works as an unordered container key, as a pipeline
 * cache uses it.
 */
BOOST_AUTO_TEST_CASE(draw_state_is_unordered_key)
{
	std::unordered_map<render::draw_state, int> pipelines;
	pipelines[render::draw_state{}] = 1;
	pipelines[render::draw_state{.blend = render::blend::additive_a}] = 2;
	pipelines[render::draw_state{}] = 3;
	BOOST_TEST(pipelines.size() == 2u);
	BOOST_TEST(pipelines.at(render::draw_state{}) == 3);
}

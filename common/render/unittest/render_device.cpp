/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/render_device.h"

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

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

namespace {

/* Vertex n of a test fan: every field distinct, colours outside [0, 1]
 * included, since vertex colours are not clamped.
 */
constexpr render::vertex fan_vertex(const float n)
{
	return {
	    .pos = {{n, n + 0.25f, -n}},
	    .rgba = {{n * 0.5f, -n, 1.f + n, 2.f}},
	    .uv = {{n * 0.125f, 1.f - n}},
	};
}

constexpr std::array<render::vertex, 5> pentagon{{
    fan_vertex(0),
    fan_vertex(1),
    fan_vertex(2),
    fan_vertex(3),
    fan_vertex(4),
}};

}

/* Test that a fan of n vertices becomes n - 2 triangles, triangle i as
 * (vi+2, v0, vi+1): GL's triangle (v0, vi+1, vi+2) rotated so that its
 * provoking (last) vertex comes first, each vertex copied unchanged.
 */
BOOST_AUTO_TEST_CASE(fan_to_list_splits_pentagon)
{
	const std::vector<render::vertex> expected{
	    {.pos = {{2, 2.25f, -2}}, .rgba = {{1, -2, 3, 2}}, .uv = {{0.25f, -1}}},
	    {.pos = {{0, 0.25f, 0}}, .rgba = {{0, 0, 1, 2}}, .uv = {{0, 1}}},
	    {.pos = {{1, 1.25f, -1}}, .rgba = {{0.5f, -1, 2, 2}}, .uv = {{0.125f, 0}}},
	    {.pos = {{3, 3.25f, -3}}, .rgba = {{1.5f, -3, 4, 2}}, .uv = {{0.375f, -2}}},
	    {.pos = {{0, 0.25f, 0}}, .rgba = {{0, 0, 1, 2}}, .uv = {{0, 1}}},
	    {.pos = {{2, 2.25f, -2}}, .rgba = {{1, -2, 3, 2}}, .uv = {{0.25f, -1}}},
	    {.pos = {{4, 4.25f, -4}}, .rgba = {{2, -4, 5, 2}}, .uv = {{0.5f, -3}}},
	    {.pos = {{0, 0.25f, 0}}, .rgba = {{0, 0, 1, 2}}, .uv = {{0, 1}}},
	    {.pos = {{3, 3.25f, -3}}, .rgba = {{1.5f, -3, 4, 2}}, .uv = {{0.375f, -2}}},
	};
	BOOST_TEST((render::fan_to_list(pentagon) == expected));
}

/* Test that a fan of one triangle is that triangle, provoking vertex first. */
BOOST_AUTO_TEST_CASE(fan_to_list_rotates_single_triangle)
{
	const std::vector<render::vertex> expected{
	    {.pos = {{2, 2.25f, -2}}, .rgba = {{1, -2, 3, 2}}, .uv = {{0.25f, -1}}},
	    {.pos = {{0, 0.25f, 0}}, .rgba = {{0, 0, 1, 2}}, .uv = {{0, 1}}},
	    {.pos = {{1, 1.25f, -1}}, .rgba = {{0.5f, -1, 2, 2}}, .uv = {{0.125f, 0}}},
	};
	BOOST_TEST((render::fan_to_list(std::span(pentagon).first(3)) == expected));
}

/* Test that a fan of fewer than three vertices, which GL draws as nothing,
 * becomes an empty list.
 */
BOOST_AUTO_TEST_CASE(fan_to_list_drops_degenerate_fan)
{
	BOOST_TEST(render::fan_to_list(std::span(pentagon).first(2)).empty());
	BOOST_TEST(render::fan_to_list(std::span(pentagon).first(0)).empty());
}

/* Test that transform recipes compare by their GL call arguments: the same
 * arguments are equal, and changing any one makes them unequal.
 */
BOOST_AUTO_TEST_CASE(transform_recipes_equal_by_arguments)
{
	const render::perspective game{.fovy = 90.0, .aspect = 1.0, .z_near = 0.1, .z_far = 5000.0};
	BOOST_TEST((game == render::perspective{90.0, 1.0, 0.1, 5000.0}));
	BOOST_TEST((game != render::perspective{90.0, 1.0, 0.1, 5000.0, 0.5f}));
	BOOST_TEST((game != render::perspective{60.0, 1.0, 0.1, 5000.0}));
	BOOST_TEST((game != render::perspective{90.0, 4.0 / 3.0, 0.1, 5000.0}));
	BOOST_TEST((game != render::perspective{90.0, 1.0, 1.0, 5000.0}));
	BOOST_TEST((game != render::perspective{90.0, 1.0, 0.1, 1000.0}));
	const render::ortho unit{
	    .left = 0, .right = 1, .bottom = 0, .top = 1, .z_near = -1, .z_far = 1};
	BOOST_TEST((unit == render::ortho{0, 1, 0, 1, -1, 1}));
	BOOST_TEST((unit != render::ortho{-1, 1, 0, 1, -1, 1}));
	BOOST_TEST((unit != render::ortho{0, 2, 0, 1, -1, 1}));
	BOOST_TEST((unit != render::ortho{0, 1, -1, 1, -1, 1}));
	BOOST_TEST((unit != render::ortho{0, 1, 0, 2, -1, 1}));
	BOOST_TEST((unit != render::ortho{0, 1, 0, 1, 0, 1}));
	BOOST_TEST((unit != render::ortho{0, 1, 0, 1, -1, 2}));
	const render::translate_scale ts{.translate = {{0.5f, 0.25f, 0}}, .scale = {{2, 3, 1}}};
	BOOST_TEST((ts == render::translate_scale{{{0.5f, 0.25f, 0}}, {{2, 3, 1}}}));
	BOOST_TEST((ts != render::translate_scale{{{0.5f, 0.25f, 1}}, {{2, 3, 1}}}));
	BOOST_TEST((ts != render::translate_scale{{{0.5f, 0.25f, 0}}, {{2, 3, 4}}}));
}

/* Test that recipes of different kinds are unequal even where their matrices
 * would be the same: a recipe is a GL call sequence, never a matrix.
 */
BOOST_AUTO_TEST_CASE(transform_recipes_of_different_kinds_unequal)
{
	const render::modelview none{render::identity{}};
	const render::modelview unit_scale{render::translate_scale{{{0, 0, 0}}, {{1, 1, 1}}}};
	BOOST_TEST((none == render::modelview{render::identity{}}));
	BOOST_TEST((none != unit_scale));
	const render::projection gl_ortho{render::ortho{0, 1, 0, 1, -1, 1}};
	const render::projection gl_perspective{render::perspective{90.0, 1.0, 0.1, 5000.0}};
	BOOST_TEST((gl_ortho != gl_perspective));
	const render::transforms a{.projection = gl_ortho, .modelview = none};
	BOOST_TEST((a == render::transforms{gl_ortho, none}));
	BOOST_TEST((a != render::transforms{gl_perspective, none}));
	BOOST_TEST((a != render::transforms{gl_ortho, unit_scale}));
}

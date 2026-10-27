/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The writer of the committed sample trace
 * (common/render/rtrace/synthetic/sample.rtr), the first synthetic trace:
 * the shaded triangle, then the whole target in yellow, each in its own
 * frame.  test-rtrace checks the committed file is its output; test-sdlgpu
 * replays it on SDL_GPU (R3).
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

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

}

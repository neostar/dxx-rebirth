/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The front end's ambient state (ADR 2): the blend, alpha reference, depth
 * test, culling, viewport and transforms the game set last, as GL kept them
 * before the render device.  Game code keeps calling gr_settransblend and
 * the other setters; the front end records each change here and builds
 * every draw call from it, so the device itself carries no state.
 */

#pragma once

#include <span>

#include "common/render/render_device.h"

namespace dcx::render {

/* The projection the game draws 2D with: glOrtho(0, 1, 0, 1, -1, 1). */
inline constexpr ortho screen_ortho{
    .left = 0, .right = 1, .bottom = 0, .top = 1, .z_near = -1, .z_far = 1};

/* The projection of the 3D view: gluPerspective(90, 1, 0.1, 5000). */
inline constexpr perspective view_perspective{
    .fovy = 90, .aspect = 1, .z_near = 0.1, .z_far = 5000};

struct ambient
{
	/* The blend function, kept while blending is off so that turning it
	 * back on restores it, as glEnable(GL_BLEND) does.
	 */
	render::blend blend_function{render::blend::normal};
	bool blending{true};
	float alpha_ref{default_alpha_ref};
	/* GL_ALPHA_TEST: off until the first 3D frame (begin_3d_frame, from
	 * ogl_start_frame) turns it on (the title and menus draw without it);
	 * only a new GL context (ogl_create_device) turns it off again.
	 */
	bool alpha_test{false};
	/* glLineWidth: 1 in a new GL context, linedotscale from the first
	 * ogl_start_frame.
	 */
	float line_width{1.f};
	render::depth depth{render::depth::off};
	render::cull cull{render::cull::none};
	render::viewport viewport{.x = 0, .y = 0, .size = {.width = 0, .height = 0}};
	render::transforms transforms{.projection = screen_ortho, .modelview = identity{}};
	/* The draw state of a draw with wrap: blend none while blending is
	 * off, alpha_test_off while the alpha test is.
	 */
	[[nodiscard]]
	constexpr draw_state state(const render::wrap wrap) const
	{
		return draw_state{
		    .blend = blending ? blend_function : render::blend::none,
		    .depth = depth,
		    .cull = cull,
		    .alpha_ref = alpha_test ? alpha_ref : alpha_test_off,
		    .wrap = wrap,
		    .line_width = line_width,
		};
	}
	/* An untextured draw of vertices as topology t in the ambient state, as
	 * the 2D primitives draw.
	 */
	[[nodiscard]]
	constexpr draw_call call(const topology t, const std::span<const vertex> vertices) const
	{
		return draw_call{
		    .topology = t,
		    .vertices = vertices,
		    .texture = texture_slot::none,
		    .state = state(render::wrap::clamp),
		    .viewport = viewport,
		    .transforms = transforms,
		};
	}
	/* A triangle fan of vertices with texture, in the ambient state; an
	 * untextured fan clamps, as wrap does not apply to it.
	 */
	[[nodiscard]]
	constexpr draw_call fan(const std::span<const vertex> vertices, const texture_slot texture,
	                        const render::wrap wrap = render::wrap::clamp) const
	{
		return draw_call{
		    .topology = topology::fan,
		    .vertices = vertices,
		    .texture = texture,
		    .state = state(wrap),
		    .viewport = viewport,
		    .transforms = transforms,
		};
	}
};

}

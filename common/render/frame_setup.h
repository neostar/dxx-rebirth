/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The frame lifecycle through the render device (ADR 2): the ambient state
 * of a 3D frame and of the 2D drawing after it, the depth clear that starts
 * a 3D frame, and the palette flash drawn before a present.  None of it
 * calls GL: the GL adapter sets GL on the next draw (gl_state.h).
 */

#pragma once

#include <array>
#include <optional>

#include "common/render/ambient.h"
#include "common/render/device.h"
#include "common/render/render_device.h"

namespace dcx::render {

/* Starts a 3D frame (ogl_start_frame): the state its draws use, then a depth
 * clear of the whole target.  The rear view starts a second frame in the
 * middle of the first, so its depth clear is a clear of depth alone, with
 * the colour kept.  line_width is the width of one line pixel.
 */
inline void begin_3d_frame(device &d, ambient &a, const float line_width)
{
	a.blend_function = blend::normal;
	a.blending = true;
	a.alpha_ref = default_alpha_ref;
	a.alpha_test = true;
	a.line_width = line_width;
	a.depth = depth::test_and_write;
	a.cull = cull::front;
	a.transforms = {view_perspective, identity{}};
	d.clear({.color = false, .depth = true});
}

/* Ends a 3D frame (ogl_end_frame): the 2D state the menus and the HUD draw
 * with.
 */
inline void end_3d_frame(ambient &a)
{
	a.cull = cull::none;
	a.depth = depth::off;
	a.transforms = {screen_ortho, identity{}};
}

/* The palette flash of one frame: the blend it draws a screen-sized quad
 * with, and that quad's colour.
 */
struct palette_flash
{
	render::blend blend;
	std::array<float, 3> rgb;
};

/* The flash for the colour (r, g, b) the palette steps by, in [-1, 1]; none
 * while active is false.  A colour with no positive part darkens
 * (palfx_neg), scaled by 2.5 to match D1 and D2 on GL and made positive to
 * have an effect; any other colour adds (additive_c).
 */
[[nodiscard]]
inline std::optional<palette_flash> flash_of(const float r, const float g, const float b,
                                             const bool active)
{
	if (!active)
		return std::nullopt;
	if (r <= 0 && g <= 0 && b <= 0)
		return palette_flash{.blend = blend::palfx_neg,
		                     .rgb = {{static_cast<float>(r * -2.5), static_cast<float>(g * -2.5),
		                              static_cast<float>(b * -2.5)}}};
	return palette_flash{.blend = blend::additive_c, .rgb = {{r, g, b}}};
}

/* Draws f over the whole target in the ambient state with its blend, then
 * leaves blending on and normal, as the next frame expects.
 */
inline void draw_palette_flash(device &d, ambient &a, const palette_flash &f)
{
	const std::array<float, 4> rgba{{f.rgb[0], f.rgb[1], f.rgb[2], 1.f}};
	const std::array<vertex, 4> corners{{
	    {.pos = {{0, 0, 0}}, .rgba = rgba, .uv = {{0, 0}}},
	    {.pos = {{0, 1, 0}}, .rgba = rgba, .uv = {{0, 0}}},
	    {.pos = {{1, 1, 0}}, .rgba = rgba, .uv = {{0, 0}}},
	    {.pos = {{1, 0, 0}}, .rgba = rgba, .uv = {{0, 0}}},
	}};
	auto call{a.call(topology::fan, corners)};
	call.state.blend = f.blend;
	d.draw(call);
	a.blend_function = blend::normal;
	a.blending = true;
}

}

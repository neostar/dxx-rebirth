/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * A draw moved by a modelview recipe, as glPushMatrix, glTranslatef,
 * glScalef and glPopMatrix moved the game's circles, disks, vertex
 * reticle and spheres before the render device (ogl_draw_moved).
 */

#pragma once

#include <span>

#include "common/render/ambient.h"
#include "common/render/device.h"
#include "common/render/render_device.h"

namespace dcx::render {

/* Draws vertices as t on d in the ambient state a, moved by move, with
 * line_width for lines.  The front end's code not yet drawing through the
 * device reads GL's modelview matrix, which glPopMatrix restored before;
 * an empty draw in a's state then restores a's modelview through the GL
 * adapter's state shadow (gl_device.h), so the shadow knows the matrix GL
 * holds.  The empty draw can go once no such code remains (#16).
 */
inline void draw_moved(device &d, const ambient &a, const topology t,
                       const std::span<const vertex> vertices, const translate_scale &move,
                       const float line_width)
{
	auto call{a.call(t, vertices)};
	call.transforms.modelview = move;
	call.state.line_width = line_width;
	d.draw(call);
	d.draw(a.call(topology::fan, {}));
}

}

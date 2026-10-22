/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The GL adapter of the render device (ADR 2): draws each self-contained
 * draw call with the GL calls the front end made before the device, so the
 * GL picture stays bit-identical (R1).  It owns the GL state shadow
 * (gl_state.h), which turns stateless draws back into the few GL state
 * calls that changed.  Only OpenGL builds compile it.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include "common/render/device.h"
#include "common/render/gl_state.h"

namespace dcx::render {

/* What the adapter takes from the front end's GL code, which stays in the
 * upstream files until every draw goes through the device (#16).
 */
struct gl_front_end
{
	/* Shows the back buffer. */
	std::function<void()> swap;
	/* The rows of the target, which the automap's stereo view changes while
	 * it draws.
	 */
	std::function<std::uint32_t()> target_height;
	/* Turns GL_TEXTURE_2D off through the front end's OGL_DISABLE shadow,
	 * which the code not yet drawing through the device reads.
	 */
	std::function<void()> untextured;
	/* Multiplies the current matrix by the perspective's: gluPerspective,
	 * or the front end's replacement for it on GLES.
	 */
	std::function<void(const render::perspective &)> build_perspective;
};

/* The state shadow keeps each draw's GL state right while two things hold
 * (gl_state.h): the front end's ambient state equals GL's blend, alpha
 * reference, depth test, culling, viewport and transforms at each draw;
 * and code outside the device leaves the alpha test on, glDepthFunc
 * LEQUAL, glCullFace FRONT and the modelview matrix current, which
 * ogl_start_frame sets before every 3D draw today.
 */
class gl_device final : public device
{
	gl::state_shadow shadow_;
	gl_front_end front_end_;

  public:
	/* A device for the current GL context.  The front end makes a new one
	 * whenever it sets up a GL context or its state, so the shadow starts
	 * empty and the first draw sets everything.
	 */
	gl_device(const render::caps &c, gl_front_end front_end);
	/* Untextured draws only: textures go through the device from #11, and
	 * a textured draw throws std::logic_error until then.
	 */
	void draw(const draw_call &call) override;
	/* Throws std::logic_error: textures go through the device from #11. */
	void texture(texture_slot slot, const texture_image *image) override;
	void clear(clear_targets targets) override;
	void present() override;
	void read_pixels(read_source source, const read_rect &rect, pixel_format format,
	                 std::span<std::uint8_t> pixels) override;
};

}

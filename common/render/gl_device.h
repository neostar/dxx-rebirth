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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

#include "common/render/device.h"
#include "common/render/gl_errors.h"
#include "common/render/gl_state.h"
#include "common/render/gl_texture_names.h"

namespace dcx::render {

/* What the adapter takes from the front end's GL code, which stays in the
 * upstream files until every draw goes through the device (#16).
 */
struct gl_front_end
{
	/* Shows the back buffer. */
	std::function<void()> swap;
	/* The size of the target, which the automap's stereo view changes
	 * while it draws.
	 */
	std::function<render::extent()> target;
	/* Turns GL_TEXTURE_2D off through the front end's OGL_DISABLE shadow,
	 * which the code not yet drawing through the device reads.
	 */
	std::function<void()> untextured;
	/* Binds the texture of a slot with a wrap and turns GL_TEXTURE_2D on,
	 * through the front end's shadows of both (OGL_ENABLE and the
	 * texture's wrap state), which the code not yet drawing through the
	 * device reads.  That code binds textures behind the state shadow's
	 * back, so the adapter calls this on every textured draw.
	 */
	std::function<void(texture_slot, render::wrap)> textured;
	/* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, 0 without the extension: the
	 * anisotropy an anisotropic texture gets above 1.
	 */
	float max_anisotropy;
};

/* What a GL context holds when the front end makes a device for it. */
struct gl_context_state
{
	/* GL_STEREO. */
	bool stereo_buffers;
	/* GL_ALPHA_TEST: off in a new context. */
	bool alpha_test;
	/* GL_LINE_WIDTH: 1 in a new context. */
	float line_width;
};

/* The depth of each part of the default framebuffer, for the texture
 * statistics (-DbgRenderStats).
 */
struct gl_framebuffer_bits
{
	int index;
	int red;
	int green;
	int blue;
	int alpha;
	/* 1 for a single-buffered framebuffer, 2 for a double-buffered one. */
	int buffers;
	int depth;
};

/* The strings a GL context names itself with (empty when it gives none). */
struct gl_context_names
{
	std::string vendor;
	std::string renderer;
	std::string version;
};

/* The state shadow keeps each draw's GL state right while two things hold
 * (gl_state.h): the front end's ambient state equals GL's blend, alpha
 * reference, depth test, culling, viewport and transforms at each draw;
 * and code outside the device leaves the alpha test on, glDepthFunc
 * LEQUAL, glCullFace FRONT and the modelview matrix current, which
 * ogl_start_frame sets before every 3D draw today.  The bound texture, its
 * wrap and GL_TEXTURE_2D are outside the state shadow: every textured draw
 * binds its texture, and every draw sets GL_TEXTURE_2D and the wrap through
 * the front end (gl_front_end::textured and untextured), whose own shadows
 * of those two, OGL_ENABLE's of GL_TEXTURE_2D and each texture's wrap
 * state, the code outside the device reads.
 */
class gl_device final : public device
{
	gl::state_shadow shadow_;
	gl_front_end front_end_;
	/* The GL texture name of each slot that holds an image.  A device made
	 * for the same GL context takes them (take_textures); the front end
	 * releases every slot before it closes the context.  The destructor
	 * deletes nothing, since it may run after the context is gone.
	 */
	gl::texture_names names_;
	/* Whether present ran: until then the presented image reads zero. */
	bool presented_{false};

  public:
	/* A device for the current GL context.  The front end makes a new one
	 * whenever it sets up a GL context or its state, so the shadow starts
	 * empty and the first draw sets everything.
	 */
	gl_device(const render::caps &c, gl_front_end front_end);
	/* Draws call.  A call with no vertices draws nothing but still sets its
	 * GL state through the state shadow and runs the front end's textured or
	 * untextured hook: the front end restores the GL state the code not yet
	 * drawing through the device expects that way (ogl_draw_moved, the
	 * vertex reticle's line width).
	 */
	void draw(const draw_call &call) override;
	/* Makes a new GL texture of image for slot, deleting the slot's old one,
	 * with the GL calls ogl_loadtexture made, and leaves it bound as they
	 * did.  The pixels go up as GL_RGBA in the image's internal format
	 * (GL_RGBA on GLES, where the two must match).  A null image deletes
	 * the slot's texture; texture_slot::none holds none and throws
	 * std::invalid_argument.
	 */
	void texture(texture_slot slot, const texture_image *image) override;
	/* The GL texture name of slot, 0 when it holds none: the front end binds
	 * it for the code not yet drawing through the device.
	 */
	[[nodiscard]]
	std::uint32_t gl_name(texture_slot slot) const;
	/* Takes the textures of from, a device of the same GL context that
	 * this one replaces, as upstream keeps them across an SDL2 mode change;
	 * from is left with none.  Throws std::logic_error when this device
	 * holds textures already.
	 */
	void take_textures(gl_device &from);
	/* How many slots hold a texture: 0 once the front end released them
	 * all.
	 */
	[[nodiscard]]
	std::size_t texture_count() const;
	/* The GL state of the current context, which the front end records in its
	 * ambient state: a context kept across a mode change keeps what it had.
	 */
	[[nodiscard]]
	static gl_context_state read_context();
	/* The vendor, renderer and version strings of the current context, for
	 * the driver workarounds.
	 */
	[[nodiscard]]
	static gl_context_names context_names();
	/* The bits of the default framebuffer of the current context. */
	[[nodiscard]]
	static gl_framebuffer_bits framebuffer_bits();
	/* Takes the GL errors pending (gl_errors.h), which the game never
	 * checks for.
	 */
	[[nodiscard]]
	static drained_gl_errors<unsigned> drain_errors();
	void clear(clear_targets targets) override;
	void present() override;
	/* Reads rect of the target: zero outside it (readback.h).  Throws
	 * std::length_error on a buffer of the wrong size.
	 */
	void read_pixels(read_source source, const read_rect &rect, pixel_format format,
	                 std::span<std::uint8_t> pixels) override;
	/* glDrawBuffer(GL_BACK_LEFT or GL_BACK_RIGHT), as the stereo frames did. */
	void select_eye(eye e) override;
	/* glWindowPos2i and glCopyPixels, after glReadBuffer(GL_BACK_LEFT) and
	 * glDrawBuffer(GL_BACK_RIGHT) when from_left_eye.
	 */
	void copy_region(const copy_rect &rect, bool from_left_eye) override;
};

}

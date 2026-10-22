/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The GL adapter (gl_device.h).  Every GL call here is one the front end
 * made before the render device, with the same arguments.
 */

#include "common/render/gl_device.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <utility>

#include "common/render/device.h"
#include "common/render/gl_state.h"
#include "common/render/readback.h"
#include "common/render/render_device.h"

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "d_gl.h"
#include "tools/dev/upstream_end.h"
// clang-format on

namespace dcx::render {

namespace {

[[nodiscard]]
constexpr GLenum gl_capability(const gl::capability c)
{
	switch (c)
	{
	case gl::capability::blend:
		return GL_BLEND;
	case gl::capability::alpha_test:
		return GL_ALPHA_TEST;
	case gl::capability::depth_test:
		return GL_DEPTH_TEST;
	case gl::capability::cull_face:
		break;
	}
	return GL_CULL_FACE;
}

[[nodiscard]]
constexpr GLenum gl_factor(const gl::factor f)
{
	switch (f)
	{
	case gl::factor::zero:
		return GL_ZERO;
	case gl::factor::one:
		return GL_ONE;
	case gl::factor::src_alpha:
		return GL_SRC_ALPHA;
	case gl::factor::one_minus_src_alpha:
		return GL_ONE_MINUS_SRC_ALPHA;
	case gl::factor::one_minus_src_color:
		break;
	}
	return GL_ONE_MINUS_SRC_COLOR;
}

[[nodiscard]]
constexpr GLenum gl_mode(const topology t)
{
	switch (t)
	{
	case topology::fan:
		return GL_TRIANGLE_FAN;
	case topology::strip:
		return GL_TRIANGLE_STRIP;
	case topology::triangles:
		return GL_TRIANGLES;
	case topology::lines:
		return GL_LINES;
	case topology::line_loop:
		return GL_LINE_LOOP;
	case topology::points:
		break;
	}
	return GL_POINTS;
}

/* The state shadow's GL calls (gl::ops), each as the front end made it. */
struct gl_calls
{
	const std::function<void(const render::perspective &)> &build_perspective;
	static void enable(const gl::capability c, const bool on)
	{
		if (on)
			glEnable(gl_capability(c));
		else
			glDisable(gl_capability(c));
	}
	static void blend_func(const gl::blend_factors &f)
	{
		glBlendFunc(gl_factor(f.src), gl_factor(f.dst));
	}
	static void alpha_func_gequal(const float ref)
	{
		glAlphaFunc(GL_GEQUAL, ref);
	}
	static void depth_func_lequal()
	{
		glDepthFunc(GL_LEQUAL);
	}
	static void cull_face_front()
	{
		glCullFace(GL_FRONT);
	}
	static void viewport(const render::viewport &v)
	{
		glViewport(v.x, v.y, static_cast<GLsizei>(v.size.width),
		           static_cast<GLsizei>(v.size.height));
	}
	static void matrix_mode(const gl::matrix m)
	{
		glMatrixMode(m == gl::matrix::projection ? GL_PROJECTION : GL_MODELVIEW);
	}
	static void load_identity()
	{
		glLoadIdentity();
	}
	void perspective(const render::perspective &p) const
	{
		build_perspective(p);
	}
	/* As ogl_stereo_frame shifts the projection for an eye.  It selects the
	 * projection matrix itself: GLES's replacement for gluPerspective leaves
	 * the modelview matrix current.
	 */
	static void stereo_shift(const float dx)
	{
		glMatrixMode(GL_PROJECTION);
		std::array<GLfloat, 16> m{};
		glGetFloatv(GL_PROJECTION_MATRIX, m.data());
		m[8] += dx;
		glLoadMatrixf(m.data());
	}
	static void ortho(const render::ortho &o)
	{
#if DXX_USE_OGLES
		glOrthof(static_cast<GLfloat>(o.left), static_cast<GLfloat>(o.right),
		         static_cast<GLfloat>(o.bottom), static_cast<GLfloat>(o.top),
		         static_cast<GLfloat>(o.z_near), static_cast<GLfloat>(o.z_far));
#else
		glOrtho(o.left, o.right, o.bottom, o.top, o.z_near, o.z_far);
#endif
	}
	static void translate(const std::array<float, 3> &a)
	{
		glTranslatef(a[0], a[1], a[2]);
	}
	static void scale(const std::array<float, 3> &a)
	{
		glScalef(a[0], a[1], a[2]);
	}
	static void line_width(const float w)
	{
		glLineWidth(w);
	}
};

static_assert(gl::ops<gl_calls>);

}

gl_device::gl_device(const render::caps &c, gl_front_end front_end)
    : device{c}, front_end_{std::move(front_end)}
{}

void gl_device::draw(const draw_call &call)
{
	if (call.texture != texture_slot::none)
		throw std::logic_error("gl_device::draw: textures go through the device from #11");
	gl_calls gl{.build_perspective = front_end_.build_perspective};
	shadow_.apply(gl, call);
	front_end_.untextured();
	if (call.vertices.empty())
		return;
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	constexpr GLsizei stride{sizeof(vertex)};
	glVertexPointer(3, GL_FLOAT, stride, call.vertices.front().pos.data());
	glColorPointer(4, GL_FLOAT, stride, call.vertices.front().rgba.data());
	glDrawArrays(gl_mode(call.topology), 0, static_cast<GLsizei>(call.vertices.size()));
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}

void gl_device::texture(texture_slot, const texture_image *)
{
	throw std::logic_error("gl_device::texture: textures go through the device from #11");
}

void gl_device::clear(const clear_targets targets)
{
	GLbitfield bits{0};
	if (targets.color)
		bits |= GLbitfield{GL_COLOR_BUFFER_BIT};
	if (targets.depth)
		bits |= GLbitfield{GL_DEPTH_BUFFER_BIT};
	if (bits != 0)
		glClear(bits);
}

void gl_device::present()
{
	front_end_.swap();
}

void gl_device::read_pixels(const read_source source, const read_rect &rect,
                            const pixel_format format, const std::span<std::uint8_t> pixels)
{
	if (pixels.size() != image_bytes(rect.size, format))
		throw std::length_error("read_pixels: buffer size does not match the rectangle");
	if (pixels.empty())
		return;
	GLint pack_alignment{};
	glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
#if !DXX_USE_OGLES
	GLint read_buffer{};
	glGetIntegerv(GL_READ_BUFFER, &read_buffer);
	GLint buffer{GL_FRONT};
	if (source == read_source::drawing)
		glGetIntegerv(GL_DRAW_BUFFER, &buffer);
	glReadBuffer(static_cast<GLenum>(buffer));
#else
	(void)source;
#endif
	glReadPixels(rect.x, bottom_up_y(front_end_.target_height(), rect),
	             static_cast<GLsizei>(rect.size.width), static_cast<GLsizei>(rect.size.height),
	             format == pixel_format::rgba8 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
#if !DXX_USE_OGLES
	glReadBuffer(static_cast<GLenum>(read_buffer));
#endif
	glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment);
	flip_rows(pixels, image_bytes({.width = rect.size.width, .height = 1}, format));
}

}

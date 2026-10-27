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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "common/render/device.h"
#include "common/render/gl_state.h"
#include "common/render/readback.h"
#include "common/render/render_device.h"

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "d_gl.h"
#include "ogl_extensions.h"
#include "tools/dev/upstream_end.h"
#if !DXX_USE_OGLES && !(defined(__APPLE__) && defined(__MACH__))
/* gluBuild2DMipmaps: d_gl.h includes GLU only on macOS. */
#include <GL/glu.h>
#endif
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

/* The internal format ogl_init_texture chose for f.  GLES takes only the
 * format of the pixels, GL_RGBA, for which an RGB texture samples the same,
 * its alpha being 255.
 */
[[nodiscard]]
constexpr GLint gl_internal_format(const texture_format f)
{
#if DXX_USE_OGLES
	(void)f;
	return GL_RGBA;
#else
	switch (f)
	{
	case texture_format::rgba8:
		return GL_RGBA8;
	case texture_format::rgb8:
		return GL_RGB8;
	case texture_format::intensity4:
		return GL_INTENSITY4;
	case texture_format::luminance4_alpha4:
		return GL_LUMINANCE4_ALPHA4;
	case texture_format::rgba2:
		break;
	}
	return GL_RGBA2;
#endif
}

/* Uploads image to the bound texture with ogl_loadtexture's filters and
 * mipmaps.
 */
void upload(const texture_image &image, const float max_anisotropy)
{
	const texture_desc &d{image.desc()};
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	const bool anisotropic{d.anisotropic && max_anisotropy > 1.0f};
	GLint mag_filter{GL_LINEAR};
	GLint min_filter{GL_LINEAR_MIPMAP_LINEAR};
	bool mipmaps{true};
	if (d.filter == filter::nearest)
	{
		mag_filter = GL_NEAREST;
		/* Anisotropy looks better on mipmaps. */
		min_filter = anisotropic ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST;
		mipmaps = anisotropic;
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
	if (anisotropic)
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_anisotropy);
	const auto width{static_cast<GLsizei>(d.size.width)};
	const auto height{static_cast<GLsizei>(d.size.height)};
	const GLint internal_format{gl_internal_format(d.format)};
#if DXX_USE_OGLES
	/* GLES 1.1 makes the mipmaps itself when asked. */
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, mipmaps ? GL_TRUE : GL_FALSE);
#else
	if (mipmaps)
	{
		gluBuild2DMipmaps(GL_TEXTURE_2D, internal_format, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
		                  image.rgba().data());
		return;
	}
#endif
	glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
	             image.rgba().data());
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
	static void point_size(const float w)
	{
		glPointSize(w);
	}
};

static_assert(gl::ops<gl_calls>);

}

gl_device::gl_device(const render::caps &c, gl_front_end front_end)
    : device{c}, front_end_{std::move(front_end)}
{}

void gl_device::draw(const draw_call &call)
{
	/* The state first, also for a call with no vertices (gl_device.h). */
	gl_calls gl{.build_perspective = front_end_.build_perspective};
	shadow_.apply(gl, call);
	const bool textured{call.texture != texture_slot::none};
	if (textured)
		front_end_.textured(call.texture, call.state.wrap);
	else
		front_end_.untextured();
	if (call.vertices.empty())
		return;
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	constexpr GLsizei stride{sizeof(vertex)};
	glVertexPointer(3, GL_FLOAT, stride, call.vertices.front().pos.data());
	glColorPointer(4, GL_FLOAT, stride, call.vertices.front().rgba.data());
	if (textured)
	{
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, stride, call.vertices.front().uv.data());
	}
	glDrawArrays(gl_mode(call.topology), 0, static_cast<GLsizei>(call.vertices.size()));
	if (textured)
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}

void gl_device::texture(const texture_slot slot, const texture_image *const image)
{
	if (slot == texture_slot::none)
		throw std::invalid_argument("gl_device::texture: texture_slot::none holds no texture");
	if (const GLuint old{names_.exchange(slot, 0)}; old != 0)
		glDeleteTextures(1, &old);
	if (!image)
		return;
	GLuint name{0};
	glGenTextures(1, &name);
	(void)names_.exchange(slot, name);
	glBindTexture(GL_TEXTURE_2D, name);
	upload(*image, front_end_.max_anisotropy);
}

std::uint32_t gl_device::gl_name(const texture_slot slot) const
{
	return names_.find(slot);
}

void gl_device::take_textures(gl_device &from)
{
	names_.take(from.names_);
}

std::size_t gl_device::texture_count() const
{
	return names_.size();
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
	presented_ = true;
}

void gl_device::read_pixels(const read_source source, const read_rect &rect,
                            const pixel_format format, const std::span<std::uint8_t> pixels)
{
	if (pixels.size() != image_bytes(rect.size, format))
		throw std::length_error("read_pixels: buffer size does not match the rectangle");
	const render::extent target{front_end_.target()};
	const auto clip{clip_read_rect(target, rect)};
	if (!clip || (source == read_source::presented && !presented_))
	{
		std::ranges::fill(pixels, std::uint8_t{0});
		return;
	}
	const auto &c{clip.value()};
	/* The clip's pixels as RGBA, which every GL reads back, for place_rows. */
	std::vector<std::uint8_t> rows(image_bytes(c.size, pixel_format::rgba8));
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
	const read_rect inside{.x = static_cast<std::int32_t>(c.source_x),
	                       .y = static_cast<std::int32_t>(c.source_y),
	                       .size = c.size};
	glReadPixels(inside.x, bottom_up_y(target.height, inside), static_cast<GLsizei>(c.size.width),
	             static_cast<GLsizei>(c.size.height), GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
#if !DXX_USE_OGLES
	glReadBuffer(static_cast<GLenum>(read_buffer));
#endif
	glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment);
	flip_rows(rows, image_bytes({.width = c.size.width, .height = 1}, pixel_format::rgba8));
	place_rows(rows, c, rect.size, format, pixels);
}

}

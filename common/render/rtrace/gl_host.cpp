/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/rtrace/gl_host.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "common/render/caps.h"
#include "common/render/gl_device.h"
#include "common/render/render_device.h"
#include "common/sdl/dxx-sdl.h"
#include "common/sdl/sdl_deleter.h"

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "d_gl.h"
#include "ogl_extensions.h"
#include "tools/dev/upstream_end.h"
// clang-format on
/* d_gl.h includes GLU on macOS only. */
#if !(defined(__APPLE__) && defined(__MACH__))
#include <GL/glu.h>
#endif

namespace dcx::render::rtrace {

#if SDL_MAJOR_VERSION >= 2
namespace {

[[noreturn]]
void sdl_failed(const char *what)
{
	throw std::runtime_error(std::string{what} + ": " + SDL_GetError());
}

[[nodiscard]]
int window_side(const std::uint32_t side)
{
	if (side > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
		throw std::runtime_error("rtrace: the trace's target is too large for a window");
	return static_cast<int>(side);
}

/* SDL's video subsystem, initialised for the guard's life. */
struct video_subsystem
{
	video_subsystem()
	{
#if SDL_MAJOR_VERSION == 2
		if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
#elif SDL_MAJOR_VERSION == 3
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
#endif
			sdl_failed("SDL_InitSubSystem");
	}
	~video_subsystem()
	{
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}
	video_subsystem(const video_subsystem &) = delete;
	video_subsystem &operator=(const video_subsystem &) = delete;
	video_subsystem(video_subsystem &&) = delete;
	video_subsystem &operator=(video_subsystem &&) = delete;
};

/* Asks for the default framebuffer the game's window has: 8 bits a colour
 * channel, a 24-bit depth buffer, double buffered.
 */
void set_gl_attributes()
{
	for (const auto [attribute, value] :
	     {std::pair{SDL_GL_RED_SIZE, 8}, std::pair{SDL_GL_GREEN_SIZE, 8},
	      std::pair{SDL_GL_BLUE_SIZE, 8}, std::pair{SDL_GL_DEPTH_SIZE, 24},
	      std::pair{SDL_GL_DOUBLEBUFFER, 1}})
	{
#if SDL_MAJOR_VERSION == 2
		if (SDL_GL_SetAttribute(attribute, value) != 0)
#elif SDL_MAJOR_VERSION == 3
		if (!SDL_GL_SetAttribute(attribute, value))
#endif
			sdl_failed("SDL_GL_SetAttribute");
	}
}

#if SDL_MAJOR_VERSION == 2
using gl_context = sdl::sdl_unique_ptr<std::remove_pointer_t<SDL_GLContext>, SDL_GL_DeleteContext>;
#elif SDL_MAJOR_VERSION == 3
using gl_context = sdl::sdl_unique_ptr<std::remove_pointer_t<SDL_GLContext>, SDL_GL_DestroyContext>;
#endif

[[nodiscard]]
render::extent drawable_size(SDL_Window *const window)
{
	int width{0}, height{0};
#if SDL_MAJOR_VERSION == 2
	SDL_GL_GetDrawableSize(window, &width, &height);
#elif SDL_MAJOR_VERSION == 3
	if (!SDL_GetWindowSizeInPixels(window, &width, &height))
		sdl_failed("SDL_GetWindowSizeInPixels");
#endif
	return {.width = static_cast<std::uint32_t>(width),
	        .height = static_cast<std::uint32_t>(height)};
}

}

/* SDL's video subsystem, a hidden window of the target's size and its
 * current GL context, released in reverse order.  Throws
 * std::runtime_error when any of them cannot be had or the window's
 * drawable is not the target's size.
 */
struct gl_host::sdl_window
{
	video_subsystem video;
	sdl::sdl_unique_ptr<SDL_Window, SDL_DestroyWindow> window;
	gl_context context;
	explicit sdl_window(const extent target)
	    : window{(set_gl_attributes(), create_window(target))},
	      context{SDL_GL_CreateContext(window.get())}
	{
		if (!context)
			sdl_failed("SDL_GL_CreateContext");
		if (const auto size{drawable_size(window.get())}; size != target)
			throw std::runtime_error("rtrace: the window's drawable is " +
			                         std::to_string(size.width) + "x" +
			                         std::to_string(size.height) + ", not the trace's target");
	}
	[[nodiscard]]
	static SDL_Window *create_window(const extent target)
	{
		const int width{window_side(target.width)};
		const int height{window_side(target.height)};
#if SDL_MAJOR_VERSION == 2
		auto *const w{SDL_CreateWindow("rtrace", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		                               width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN)};
#elif SDL_MAJOR_VERSION == 3
		auto *const w{
		    SDL_CreateWindow("rtrace", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN)};
#endif
		if (w == nullptr)
			sdl_failed("SDL_CreateWindow");
		return w;
	}
	void swap() const
	{
		SDL_GL_SwapWindow(window.get());
	}
};

namespace {

[[nodiscard]]
bool has_gl_extension(const char *name)
{
	return SDL_GL_ExtensionSupported(name);
}

}
#else
/* SDL 1.2 has no hidden windows: a host cannot be made, and the build
 * compiles this only so that the rtrace sources build and lint in every GL
 * configuration (tools/dev/rtrace-tests.sh skips SDL 1.2 builds).
 */
struct gl_host::sdl_window
{
	[[noreturn]]
	explicit sdl_window(extent)
	{
		throw std::runtime_error("rtrace: needs SDL 2 or 3; SDL 1.2 has no hidden windows");
	}
	void swap() const
	{}
};

namespace {

[[nodiscard]]
bool has_gl_extension(const char *)
{
	return false;
}

}
#endif

namespace {

[[nodiscard]]
render::caps context_caps()
{
	GLfloat max_anisotropy{0};
	if (has_gl_extension("GL_EXT_texture_filter_anisotropic"))
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_anisotropy);
	return render::gl_caps(max_anisotropy);
}

}

gl_host::gl_host(const extent target)
    : window_{std::make_unique<sdl_window>(target)},
      device_{context_caps(),
	          {
                  .swap = [w = window_.get()] { w->swap(); },
                  .target_height = [h = target.height] { return h; },
                  .untextured = [] { glDisable(GL_TEXTURE_2D); },
                  .build_perspective =
                      [](const render::perspective &p) {
	                      gluPerspective(p.fovy, p.aspect, p.z_near, p.z_far);
                      },
              }}
{}

gl_host::~gl_host() = default;

}

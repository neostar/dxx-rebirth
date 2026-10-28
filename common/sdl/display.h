/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "common/sdl/dxx-sdl.h"
#include "common/sdl/sdl_deleter.h"
#include "common/sdl/window_state.h"

#if SDL_MAJOR_VERSION == 3

namespace dcx::sdl {

/* The platform module's window (docs/agents/sdl-window.md, Design): the
 * renderer is chosen before the window exists (section 2), the window is
 * made for it, and for GL the swap interval is set only once the context
 * is current, since SDL_GL_SetSwapInterval fails without one.
 */

enum class renderer : std::uint8_t
{
	gl,
	sdl_gpu,
};

struct display_request
{
	const char *title{""};
	/* The windowed size, in window coordinates. */
	pixel_size size{};
	renderer preferred{renderer::gl};
	/* SDL_GPU's driver name, or nullptr for SDL's choice. */
	const char *gpu_driver{nullptr};
	/* Borderless desktop fullscreen (section 3). */
	bool fullscreen{false};
	bool borderless{false};
	/* SDL_WINDOW_HIDDEN and nothing else: the rest of a hidden run comes
	 * from the environment (section 7).
	 */
	bool hidden{false};
	bool vsync{false};
};

/* What open_display built.  The members are declared in creation order, so
 * they are destroyed in reverse: the GL context, the window's claim, the
 * window, the GPU device.
 */
template <typename Ops>
struct display
{
	typename Ops::gpu_device_ptr gpu;
	typename Ops::window_ptr window;
	typename Ops::claim_ptr claim;
	typename Ops::gl_context_ptr gl;
	renderer chosen{renderer::gl};
	/* Why SDL_GPU was asked for and GL taken (#52), for the log. */
	std::optional<std::string> fallback_reason;
	/* Why no window was made: open_display failed. */
	std::optional<std::string> error;
	/* SDL refused the swap interval: the game runs on, so log it. */
	std::optional<std::string> swap_interval_error;
	window_state state{false};
	[[nodiscard]]
	explicit operator bool() const
	{
		return !error;
	}
};

[[nodiscard]]
constexpr SDL_WindowFlags window_flags(const display_request &r, const renderer chosen)
{
	SDL_WindowFlags flags{chosen == renderer::gl ? SDL_WINDOW_OPENGL : SDL_WindowFlags{0}};
	if (r.fullscreen)
		flags |= SDL_WINDOW_FULLSCREEN;
	if (r.borderless)
		flags |= SDL_WINDOW_BORDERLESS;
	if (r.hidden)
		flags |= SDL_WINDOW_HIDDEN;
	return flags;
}

template <typename Ops>
[[nodiscard]]
display<Ops> open_display(const display_request &r, Ops ops)
{
	display<Ops> d;
	d.state = window_state{r.fullscreen};
	if (r.preferred == renderer::sdl_gpu)
	{
		d.gpu = ops.create_gpu_device(r.gpu_driver);
		if (d.gpu)
			d.chosen = renderer::sdl_gpu;
		else
			d.fallback_reason =
			    std::string{"no SDL_GPU device ("} + ops.last_error() + "), using OpenGL";
	}
	d.window = ops.create_window(r.title, r.size, window_flags(r, d.chosen));
	if (!d.window)
	{
		d.error = std::string{"cannot create the window: "} + ops.last_error();
		return d;
	}
	if (d.chosen == renderer::sdl_gpu)
	{
		d.claim = ops.claim_window(d.gpu.get(), d.window.get());
		if (!d.claim)
			d.error = std::string{"cannot claim the window for SDL_GPU: "} + ops.last_error();
	}
	else
	{
		d.gl = ops.create_gl_context(d.window.get());
		if (!d.gl || !ops.make_current(d.window.get(), d.gl.get()))
			d.error = std::string{"cannot create the GL context: "} + ops.last_error();
		else if (!ops.set_swap_interval(r.vsync ? 1 : 0))
			d.swap_interval_error = ops.last_error();
	}
	if (!d.error)
		/* Fullscreen is asynchronous; at startup the game waits for it
		 * so that the first mode is the fullscreen size (section 3).
		 */
		ops.sync_window(d.window.get());
	return d;
}

/* SDL's side of open_display. */
struct sdl_display_ops
{
	using gpu_device_ptr = sdl_unique_ptr<SDL_GPUDevice, SDL_DestroyGPUDevice>;
	using window_ptr = sdl_unique_ptr<SDL_Window, SDL_DestroyWindow>;
	using gl_context_ptr = sdl_unique_ptr<SDL_GLContextState, SDL_GL_DestroyContext>;
	/* A window claimed by an SDL_GPU device; destroying it releases the
	 * window from the device.
	 */
	class claim_ptr
	{
		SDL_GPUDevice *device{nullptr};
		SDL_Window *window{nullptr};

	  public:
		claim_ptr() = default;
		claim_ptr(SDL_GPUDevice *const d, SDL_Window *const w) : device{d}, window{w}
		{}
		claim_ptr(claim_ptr &&o) noexcept
		    : device{std::exchange(o.device, nullptr)}, window{std::exchange(o.window, nullptr)}
		{}
		claim_ptr &operator=(claim_ptr &&o) noexcept
		{
			std::swap(device, o.device);
			std::swap(window, o.window);
			return *this;
		}
		claim_ptr(const claim_ptr &) = delete;
		claim_ptr &operator=(const claim_ptr &) = delete;
		~claim_ptr()
		{
			if (device)
				SDL_ReleaseWindowFromGPUDevice(device, window);
		}
		explicit operator bool() const
		{
			return device != nullptr;
		}
	};

	/* Debug mode on, as GPU validation needs (ADR 5); #50, which gives the
	 * game this device, decides whether release builds keep it.
	 */
	static gpu_device_ptr create_gpu_device(const char *const driver)
	{
		return gpu_device_ptr{SDL_CreateGPUDevice(
		    SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, true,
		    driver)};
	}
	static window_ptr create_window(const char *const title, const pixel_size size,
	                                const SDL_WindowFlags flags)
	{
		return window_ptr{SDL_CreateWindow(title, size.width, size.height, flags)};
	}
	static claim_ptr claim_window(SDL_GPUDevice *const device, SDL_Window *const window)
	{
		if (!SDL_ClaimWindowForGPUDevice(device, window))
			return {};
		return {device, window};
	}
	static gl_context_ptr create_gl_context(SDL_Window *const window)
	{
		return gl_context_ptr{SDL_GL_CreateContext(window)};
	}
	static bool make_current(SDL_Window *const window, SDL_GLContextState *const context)
	{
		return SDL_GL_MakeCurrent(window, context);
	}
	static bool set_swap_interval(const int interval)
	{
		return SDL_GL_SetSwapInterval(interval);
	}
	static void sync_window(SDL_Window *const window)
	{
		SDL_SyncWindow(window);
	}
	static const char *last_error()
	{
		return SDL_GetError();
	}
};

/* The window event an SDL event is, if the game acts on it. */
[[nodiscard]]
constexpr std::optional<window_event> window_event_of(const SDL_Event &e)
{
	switch (e.type)
	{
	case SDL_EVENT_WINDOW_SHOWN:
		return window_event::shown;
	case SDL_EVENT_WINDOW_HIDDEN:
		return window_event::hidden;
	case SDL_EVENT_WINDOW_EXPOSED:
		return window_event::exposed;
	case SDL_EVENT_WINDOW_OCCLUDED:
		return window_event::occluded;
	case SDL_EVENT_WINDOW_MINIMIZED:
		return window_event::minimized;
	case SDL_EVENT_WINDOW_RESTORED:
		return window_event::restored;
	case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		return window_event::enter_fullscreen;
	case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
		return window_event::leave_fullscreen;
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		return window_event::pixel_size_changed;
	default:
		return std::nullopt;
	}
}

}

#endif

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

}

#endif

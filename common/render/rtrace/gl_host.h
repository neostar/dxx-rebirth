/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * rtrace's GL context and adapter.  The context comes from a hidden SDL
 * window (SDL_WINDOW_HIDDEN) and nothing else; where it can draw comes
 * from the environment (docs/agents/sdl-window.md, section 7): on macOS
 * cocoa with SDL_MAC_BACKGROUND_APP=1 from tools/dev/no-window.sh, on
 * Linux SDL_VIDEO_DRIVER=offscreen.  Tests and the tool take the same path.
 */

#pragma once

#include <memory>

#include "common/render/gl_device.h"
#include "common/render/render_device.h"

namespace dcx::render::rtrace {

/* A hidden window of the target's size with a current GL context, and the
 * GL adapter drawing to it.  Nothing else draws to the context, so the
 * adapter's state shadow sets all the GL state it relies on at its first
 * draw (gl_state.h).  Not copyable or movable: the adapter's front end
 * refers to the window.
 */
class gl_host
{
	struct sdl_window;
	std::unique_ptr<sdl_window> window_;
	render::gl_device device_;

  public:
	/* Throws std::runtime_error, with SDL's message, when SDL's video, the
	 * window or the context cannot be had.
	 */
	explicit gl_host(extent target);
	~gl_host();
	gl_host(const gl_host &) = delete;
	gl_host &operator=(const gl_host &) = delete;
	gl_host(gl_host &&) = delete;
	gl_host &operator=(gl_host &&) = delete;
	[[nodiscard]]
	render::gl_device &device()
	{
		return device_;
	}
};

}

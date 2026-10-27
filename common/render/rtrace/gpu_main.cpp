/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * rtrace-gpu: rtrace (main.cpp) through the headless SDL_GPU adapter
 * instead of GL, so that a trace's frame hash lines compare across the two
 * renderers (R3).  The same arguments, output and exit status as rtrace.
 * SDL3 only, and a GPU program (ADR 5): run it through
 * tools/dev/no-window.sh tools/dev/gpu-validate.sh; it creates its device
 * with SDL's GPU debug mode on.
 */

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>

#include "common/render/device.h"
#include "common/render/render_device.h"
#include "common/render/rtrace/replay_file.h"
#include "common/render/sdlgpu_device.h"
#include "common/sdl/dxx-sdl.h"

namespace {

namespace render = dcx::render;

/* SDL's video subsystem, which SDL_CreateGPUDevice needs (sdlgpu_device.h),
 * initialised for the guard's life.
 */
struct video_subsystem
{
	video_subsystem()
	{
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
			throw std::runtime_error(SDL_GetError());
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

}

int main(const int argc, char **const argv)
{
	/* Video init on macOS makes the process a foreground app; this tool
	 * stays in the background (as test-sdlgpu does).
	 */
	if (!SDL_SetHintWithPriority(SDL_HINT_MAC_BACKGROUND_APP, "1", SDL_HINT_OVERRIDE))
	{
		std::cerr << "rtrace-gpu: SDL_SetHintWithPriority: " << SDL_GetError() << '\n';
		return EXIT_FAILURE;
	}
	return render::rtrace::run(
	    argc, argv, "rtrace-gpu",
	    [](const render::extent target, const std::function<void(render::device &)> &use) {
		    const video_subsystem video;
		    const auto d{render::sdlgpu::device::create(
		        {.target = target, .debug_mode = true, .driver = {}, .clear_rgba = {}})};
		    use(*d);
	    });
}

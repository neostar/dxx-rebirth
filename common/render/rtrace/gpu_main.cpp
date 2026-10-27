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
 * with SDL's GPU debug mode on.  On Windows, with DXX_D3D12_GPU_VALIDATION
 * set to 1, it also turns on the D3D12 debug layer's GPU-based validation
 * before it creates its device, which SDL 3.4.18 never does (#49).
 */

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "common/render/device.h"
#include "common/render/render_device.h"
#include "common/render/rtrace/replay_file.h"
#include "common/render/sdlgpu_device.h"
#include "common/sdl/dxx-sdl.h"

#ifdef _WIN32
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <memory>
#endif

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

#ifdef _WIN32

struct com_release
{
	void operator()(IUnknown *const p) const
	{
		p->Release();
	}
};

/* Turns on GPU-based validation for the D3D12 devices this process creates
 * from now on. Microsoft: "GPU-Based Validation can only be enabled/disabled
 * prior to creating a device. By default, GPU-Based Validation is disabled."
 * (ID3D12Debug1::SetEnableGPUBasedValidation). Returns the debug interface,
 * which the caller keeps, or null with the reason in why.
 *
 * Inference, which the run must confirm: SDL's own D3D12 device, created
 * later in this process, inherits the setting. Microsoft: "Once enabled, GBV
 * will remain enabled until the Direct3D 12 device is released", and SDL in
 * debug mode gets its own ID3D12Debug and calls EnableDebugLayer before
 * D3D12CreateDevice, so the setting should hold for its device. Only the
 * run's log (the validation messages GBV adds, or none) can tell.
 *
 * d3d12.dll is linked (-ld3d12), not loaded by name: GetProcAddress would
 * need a cast from FARPROC.
 */
[[nodiscard]]
std::unique_ptr<ID3D12Debug1, com_release> enable_gpu_based_validation(const char *&why)
{
	ID3D12Debug *base{};
	if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&base))))
	{
		why = "D3D12GetDebugInterface failed (Graphics Tools missing?)";
		return {};
	}
	const std::unique_ptr<ID3D12Debug, com_release> base_owner{base};
	ID3D12Debug1 *debug1{};
	if (FAILED(base->QueryInterface(IID_PPV_ARGS(&debug1))))
	{
		why = "no ID3D12Debug1";
		return {};
	}
	std::unique_ptr<ID3D12Debug1, com_release> debug{debug1};
	debug->SetEnableGPUBasedValidation(TRUE);
	return debug;
}

/* Honours DXX_D3D12_GPU_VALIDATION=1, which the d3d12 check sets: enables
 * GPU-based validation, kept in keep for the process's life, and logs one
 * line saying it is on, or why it could not be (false then, and the run
 * fails). Without the variable it does nothing.
 */
[[nodiscard]]
bool request_gpu_based_validation(std::unique_ptr<ID3D12Debug1, com_release> &keep)
{
	const char *const want{
	    SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "DXX_D3D12_GPU_VALIDATION")};
	if (!want || std::string_view{want} != "1")
		return true;
	const char *why{};
	keep = enable_gpu_based_validation(why);
	if (!keep)
	{
		std::cout << "rtrace-gpu: D3D12 GPU-based validation could not be enabled: " << why << '\n'
		          << std::flush;
		return false;
	}
	std::cout << "rtrace-gpu: D3D12 GPU-based validation on\n" << std::flush;
	return true;
}

#endif

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
#ifdef _WIN32
	std::unique_ptr<ID3D12Debug1, com_release> gpu_based_validation;
	if (!request_gpu_based_validation(gpu_based_validation))
		return EXIT_FAILURE;
#endif
	return render::rtrace::run(
	    argc, argv, "rtrace-gpu",
	    [](const render::extent target, const std::function<void(render::device &)> &use) {
		    const video_subsystem video;
		    const auto d{render::sdlgpu::device::create(
		        {.target = target, .debug_mode = true, .driver = {}, .clear_rgba = {}})};
		    use(*d);
	    });
}

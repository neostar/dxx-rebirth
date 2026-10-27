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
 * before it creates its device, which SDL 3.4.18 never does (#49), and
 * confirms from SDL's device that the debug layer started with it.
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

#ifdef _WIN32
#include <algorithm>
#include <cctype>
#include <cstring>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
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
 * Microsoft: "This method enables or disables GPU-Based Validation (GBV)
 * before creating a device with the debug layer enabled."
 * (ID3D12Debug1::SetEnableGPUBasedValidation). It returns nothing, so
 * whether SDL's device, created later in this process, has it on is
 * confirmed from that device: confirm_gpu_based_validation.
 *
 * d3d12.dll and dxgi.dll are linked (-ld3d12 -ldxgi), not loaded by
 * name: GetProcAddress would need a cast from FARPROC.
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

/* A D3D12 or DXGI call's failure, as an exception run() reports. */
void check(const HRESULT hr, const char *const what)
{
	if (FAILED(hr))
		throw std::runtime_error(std::string{what} + " failed");
}

/* Copies of the variable-length structs the info queue hands out: an
 * element count that holds bytes bytes, so the copy is aligned for T.
 */
template <typename T>
std::vector<T> holding(const SIZE_T bytes)
{
	return std::vector<T>((bytes + sizeof(T) - 1) / sizeof(T) + 1);
}

/* Whether the debug layer's startup options text says GPU-based validation
 * is on. The text's exact form is learned on the first real run, so this
 * reads it loosely: case, spaces, '-' and '_' ignored, the words "GPU based
 * validation" followed (after ':' or '=') by "enabled", "on" or "true".
 */
[[nodiscard]]
bool says_gpu_based_validation_on(const std::string_view text)
{
	std::string folded;
	for (const char c : text)
		if (c != ' ' && c != '-' && c != '_' && c != '\t')
			folded += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	constexpr std::string_view key{"gpubasedvalidation"};
	const auto at{folded.find(key)};
	if (at == std::string::npos)
		return false;
	std::string_view rest{folded};
	rest.remove_prefix(at + key.size());
	while (!rest.empty() && (rest.front() == ':' || rest.front() == '='))
		rest.remove_prefix(1);
	rest = rest.substr(0, rest.find_first_of(",;()\r\n"));
	return rest == "enabled" || rest == "on" || rest == "true";
}

/* Confirms that SDL's D3D12 device, just created, has GPU-based validation
 * on, from the device itself: Microsoft (D3D12CreateDevice): "Direct3D 12
 * devices are singletons per adapter. If a Direct3D 12 device already
 * exists in the current process for a given adapter, then a subsequent
 * call to D3D12CreateDevice returns the existing device." So creating a
 * device on WARP (IDXGIFactory4::EnumWarpAdapter; the run requires SDL to
 * have picked WARP) returns SDL's. That it is SDL's and not a new one: its
 * info queue carries the storage filter SDL pushes in debug mode (deny
 * INFO). The evidence is the debug layer's
 * D3D12_MESSAGE_ID_CREATEDEVICE_DEBUG_LAYER_STARTUP_OPTIONS message, stored
 * at device creation, before SDL's filter. (ID3D12DebugDevice1::
 * GetDebugParameter would only read GBV's settings, not whether it is on,
 * and MinGW-w64 11's d3d12sdklayers.h has no ID3D12DebugDevice1.)
 *
 * Prints `rtrace-gpu: D3D12 debug layer startup options: TEXT` and
 * `rtrace-gpu: D3D12 GPU-based validation confirmed on SDL's device`, or
 * throws (the run exits 1) when the evidence is missing or says off. Every
 * reference it takes is released on return, before SDL destroys its device.
 */
void confirm_gpu_based_validation()
{
	IDXGIFactory4 *factory_p{};
	check(CreateDXGIFactory1(IID_PPV_ARGS(&factory_p)), "CreateDXGIFactory1");
	const std::unique_ptr<IDXGIFactory4, com_release> factory{factory_p};
	IDXGIAdapter *adapter_p{};
	check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter_p)), "IDXGIFactory4::EnumWarpAdapter");
	const std::unique_ptr<IDXGIAdapter, com_release> adapter{adapter_p};
	ID3D12Device *device_p{};
	check(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_p)),
	      "D3D12CreateDevice on WARP");
	const std::unique_ptr<ID3D12Device, com_release> device{device_p};
	ID3D12InfoQueue *queue_p{};
	check(device->QueryInterface(IID_PPV_ARGS(&queue_p)),
	      "ID3D12Device::QueryInterface(ID3D12InfoQueue)");
	const std::unique_ptr<ID3D12InfoQueue, com_release> queue{queue_p};

	SIZE_T bytes{};
	check(queue->GetStorageFilter(nullptr, &bytes), "ID3D12InfoQueue::GetStorageFilter");
	auto filter{holding<D3D12_INFO_QUEUE_FILTER>(bytes)};
	check(queue->GetStorageFilter(filter.data(), &bytes), "ID3D12InfoQueue::GetStorageFilter");
	const auto &deny{filter.front().DenyList};
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-container): its own list
	const std::span<const D3D12_MESSAGE_SEVERITY> severities{deny.pSeverityList,
	                                                         deny.NumSeverities};
	if (std::ranges::find(severities, D3D12_MESSAGE_SEVERITY_INFO) == severities.end())
		throw std::runtime_error{
		    "the WARP device is not SDL's: its info queue lacks SDL's storage filter"};

	const UINT64 count{queue->GetNumStoredMessagesAllowedByRetrievalFilter()};
	for (UINT64 i{}; i != count; ++i)
	{
		SIZE_T length{};
		check(queue->GetMessage(i, nullptr, &length), "ID3D12InfoQueue::GetMessage");
		auto message{holding<D3D12_MESSAGE>(length)};
		check(queue->GetMessage(i, message.data(), &length), "ID3D12InfoQueue::GetMessage");
		const D3D12_MESSAGE &m{message.front()};
		if (m.ID != D3D12_MESSAGE_ID_CREATEDEVICE_DEBUG_LAYER_STARTUP_OPTIONS)
			continue;
		std::string text{m.pDescription, strnlen(m.pDescription, m.DescriptionByteLength)};
		std::ranges::replace(text, '\r', ' ');
		std::ranges::replace(text, '\n', ' ');
		std::cout << "rtrace-gpu: D3D12 debug layer startup options: " << text << '\n'
		          << std::flush;
		if (!says_gpu_based_validation_on(text))
			throw std::runtime_error{"D3D12 GPU-based validation not confirmed: the startup "
			                         "options do not say it is on"};
		std::cout << "rtrace-gpu: D3D12 GPU-based validation confirmed on SDL's device\n"
		          << std::flush;
		return;
	}
	throw std::runtime_error{
	    "D3D12 GPU-based validation not confirmed: no debug layer startup options among " +
	    std::to_string(count) + " stored messages"};
}

/* Honours DXX_D3D12_GPU_VALIDATION=1, which the d3d12 check sets: enables
 * GPU-based validation, kept in keep for the process's life, and logs one
 * line saying it is requested, or why it could not be (false then, and the
 * run fails). Requested is not confirmed: confirm_gpu_based_validation
 * checks SDL's device once it exists. Without the variable it does nothing.
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
	std::cout << "rtrace-gpu: D3D12 GPU-based validation requested\n" << std::flush;
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
	/* Run once SDL has created its device, before the trace uses it. */
	const auto after_create{[confirm = gpu_based_validation != nullptr] {
		if (confirm)
			confirm_gpu_based_validation();
	}};
#else
	const auto after_create{[] {}};
#endif
	return render::rtrace::run(
	    argc, argv, "rtrace-gpu",
	    [&after_create](const render::extent target,
		                const std::function<void(render::device &)> &use) {
		    const video_subsystem video;
		    const auto d{render::sdlgpu::device::create(
		        {.target = target, .debug_mode = true, .driver = {}, .clear_rgba = {}})};
		    after_create();
		    use(*d);
	    });
}

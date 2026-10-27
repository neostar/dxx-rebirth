/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The SDL_GPU calls that hand out raw memory, wrapped into typed views
 * (ADR 5: casts from void * live in sdl_wrap* files only), and SDL's
 * printf-style logging.  SDL3 only.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "common/sdl/dxx-sdl.h"

#if SDL_MAJOR_VERSION == 3
namespace dcx::sdl {

/* Maps transfer buffer b, of size bytes, for reading: its bytes, or an
 * empty span when SDL fails (SDL_GetError says why).  The caller unmaps it.
 */
[[nodiscard]]
inline std::span<const std::uint8_t>
map_for_reading(SDL_GPUDevice *const d, SDL_GPUTransferBuffer *const b, const std::size_t size)
{
	const auto *const p{static_cast<const std::uint8_t *>(SDL_MapGPUTransferBuffer(d, b, false))};
	if (p == nullptr)
		return {};
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-container): SDL maps all size bytes
	return {p, size};
}

/* Maps transfer buffer b, of size bytes, for writing, cycling it when
 * cycle is true: its bytes, or an empty span when SDL fails (SDL_GetError
 * says why).  The caller unmaps it.
 */
[[nodiscard]]
inline std::span<std::byte> map_for_writing(SDL_GPUDevice *const d, SDL_GPUTransferBuffer *const b,
                                            const std::size_t size, const bool cycle)
{
	auto *const p{static_cast<std::byte *>(SDL_MapGPUTransferBuffer(d, b, cycle))};
	if (p == nullptr)
		return {};
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-container): SDL maps all size bytes
	return {p, size};
}

/* Logs SDL's last error after what failed, as an error of SDL's GPU
 * category, which gpu-validate.sh fails a run on.
 */
inline void log_gpu_error(const char *const what)
{
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-format-attr-call): constant format
	SDL_LogError(SDL_LOG_CATEGORY_GPU, "sdlgpu: %s: %s", what, SDL_GetError());
}

}
#endif

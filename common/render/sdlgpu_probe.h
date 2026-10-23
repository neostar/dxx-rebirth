/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The SDL_GPU adapter's capability probe (docs/agents/sdl-gpu-backends.md,
 * area 5): what a device supports, asked once at creation through the
 * queries a device answers, so that no part of the adapter branches on a
 * backend's name.  Free of SDL: the device fills format_queries from
 * SDL_GPUTextureSupportsFormat and SDL_GPUTextureSupportsSampleCount, tests
 * from a list.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <functional>
#include <stdexcept>

#include "common/render/caps.h"

namespace dcx::render::sdlgpu {

enum class depth_format : std::uint8_t
{
	d32_float,
	d24_unorm,
};

/* What the probe asks a device: whether it can make a depth target of a
 * format, and a colour target (R8G8B8A8_UNORM) of a sample count.
 */
struct format_queries
{
	std::function<bool(depth_format)> depth_supported;
	std::function<bool(std::uint32_t samples)> samples_supported;
};

/* SDL_GPU accepts at most this anisotropy everywhere: Vulkan guarantees 16
 * where anisotropy is supported, and SDL has no limit query.
 */
inline constexpr float max_anisotropy_limit{16.f};

/* What the probe found: the depth format of the offscreen target, the
 * colour target's sample counts (bit n set: 2^n samples, n < 4), the
 * anisotropy samplers ask for, and the common caps (ADR 2).
 */
struct device_caps
{
	sdlgpu::depth_format depth;
	std::uint8_t sample_counts;
	float max_anisotropy;
	render::caps common;
	[[nodiscard]]
	constexpr bool supports_samples(const std::uint32_t samples) const
	{
		for (unsigned n{0}; n < 4; ++n)
			if (samples == 1u << n)
				return (unsigned{sample_counts} >> n & 1u) != 0;
		return false;
	}
};

/* Why a device cannot serve the adapter.  d24_only: the game draws on
 * 32-bit float depth, and D24_UNORM is unproven until M5 measures it, so the
 * game falls back to GL with this reason logged.
 */
enum class probe_failure : std::uint8_t
{
	d24_only,
	no_depth_format,
};

[[nodiscard]]
constexpr const char *to_string(const probe_failure f)
{
	switch (f)
	{
	case probe_failure::d24_only:
		return "the device has D24_UNORM depth only; D32_FLOAT is required until M5";
	case probe_failure::no_depth_format:
		return "the device has neither D32_FLOAT nor D24_UNORM depth";
	}
	return "unknown probe failure";
}

/* Probes a device once: D32_FLOAT depth, the sample counts 1, 2, 4 and 8,
 * and the requested anisotropy clamped to [1, max_anisotropy_limit], NaN
 * as 1 (none).  Throws std::invalid_argument when a query is missing.
 */
[[nodiscard]]
inline std::expected<device_caps, probe_failure> probe_caps(const format_queries &q,
                                                            const float requested_anisotropy)
{
	if (!q.depth_supported || !q.samples_supported)
		throw std::invalid_argument("probe_caps: format_queries needs both of its queries");
	if (!q.depth_supported(depth_format::d32_float))
		return std::unexpected(q.depth_supported(depth_format::d24_unorm)
		                           ? probe_failure::d24_only
								   : probe_failure::no_depth_format);
	std::uint8_t counts{0};
	for (unsigned n{0}; n < 4; ++n)
		if (q.samples_supported(1u << n))
			counts = static_cast<std::uint8_t>(counts | 1u << n);
	const float anisotropy{std::isnan(requested_anisotropy)
	                           ? 1.f
							   : std::clamp(requested_anisotropy, 1.f, max_anisotropy_limit)};
	return device_caps{.depth = depth_format::d32_float,
	                   .sample_counts = counts,
	                   .max_anisotropy = anisotropy,
	                   .common = {.anisotropic = anisotropy > 1.f}};
}

}

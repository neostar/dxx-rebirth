/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Mipmaps as GL's front end gets them (#34): gluBuild2DMipmaps halves each
 * level on the CPU with GLU's box filter, so an adapter that builds the same
 * levels from the same RGBA samples a minified texture as GL does.  It
 * holds no GL or SDL_GPU type.
 */

#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "common/render/render_device.h"

namespace dcx::render {

/* How many levels a full chain of size has, down to 1x1: one more than
 * the base-2 logarithm of its longer side.  0 for an empty size.
 */
[[nodiscard]]
constexpr std::uint32_t mip_level_count(const extent size)
{
	const std::uint32_t longer{std::max(size.width, size.height)};
	return longer == 0 ? 0u : static_cast<std::uint32_t>(std::bit_width(longer));
}

/* The size of the level after size: each side halved, but never below 1. */
[[nodiscard]]
constexpr extent next_mip_extent(const extent size)
{
	return {.width = std::max(size.width / 2u, 1u), .height = std::max(size.height / 2u, 1u)};
}

/* The level after the one of size whose RGBA rows rgba holds, as GLU's
 * halveImage_ubyte makes it: each texel the mean of the 2x2 block it
 * replaces, rounded half up ((a + b + c + d + 2) / 4); once one side is 1,
 * the mean of the two texels it replaces along the other, truncated
 * ((a + b) / 2, halve1Dimage_ubyte).  Throws std::length_error when rgba
 * does not hold size, std::invalid_argument for a 1x1 level, which has no
 * next one, or a side that is no power of two.
 */
[[nodiscard]]
inline std::vector<std::uint8_t> next_mip_level(const extent size,
                                                const std::span<const std::uint8_t> rgba)
{
	constexpr std::size_t channels{4};
	if (rgba.size() != std::size_t{size.width} * size.height * channels)
		throw std::length_error("mipmaps: rows that do not match their size");
	if (!std::has_single_bit(size.width) || !std::has_single_bit(size.height))
		throw std::invalid_argument("mipmaps: a side that is no power of two");
	if (size.width == 1 && size.height == 1)
		throw std::invalid_argument("mipmaps: a 1x1 level has no next one");
	const extent next{next_mip_extent(size)};
	std::vector<std::uint8_t> out(std::size_t{next.width} * next.height * channels);
	const std::size_t row{std::size_t{size.width} * channels};
	/* The steps from a texel to the one beside it and below it that
	 * share its next texel: none along a side of 1.
	 */
	const std::size_t across{size.width == 1 ? 0 : channels};
	const std::size_t down{size.height == 1 ? 0 : row};
	const bool box{across != 0 && down != 0};
	std::size_t o{0};
	for (std::uint32_t y{0}; y != next.height; ++y)
		for (std::uint32_t x{0}; x != next.width; ++x)
		{
			const std::size_t i{(size.width == 1 ? 0 : std::size_t{x} * 2 * channels) +
			                    (size.height == 1 ? 0 : std::size_t{y} * 2 * row)};
			for (std::size_t c{0}; c != channels; ++c, ++o)
			{
				const unsigned a{rgba[i + c]}, b{rgba[i + across + down + c]};
				out[o] = static_cast<std::uint8_t>(box ? (a + b + unsigned{rgba[i + across + c]} +
				                                          unsigned{rgba[i + down + c]} + 2u) /
				                                             4u
				                                       : (a + b) / 2u);
			}
		}
	return out;
}

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The pixel readback contract (ADR 2, `read_pixels`): a rectangle of the
 * target, named from its top left, comes back with its top row first.
 * These pure helpers turn GL's bottom-up readback into that order.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

#include "common/render/render_device.h"

namespace dcx::render {

/* A rectangle of the target in pixels, named from the top left: y counts
 * rows down from the top row.  It may reach past the target's edges, where
 * the pixels read are undefined.
 */
struct read_rect
{
	std::int32_t x;
	std::int32_t y;
	render::extent size;
	constexpr bool operator==(const read_rect &) const = default;
};

/* The pixel layout a readback writes: 8 bits a channel, rows tightly
 * packed.
 */
enum class pixel_format : std::uint8_t
{
	rgb8,
	rgba8,
};

/* Which image a readback reads: the one last presented (a screenshot), or
 * the one being drawn (the savegame thumbnail, read before it is shown).
 */
enum class read_source : std::uint8_t
{
	presented,
	drawing,
};

/* The bytes a readback of an image of this size and format fills. */
[[nodiscard]]
constexpr std::size_t image_bytes(const render::extent size, const pixel_format format)
{
	const std::size_t channels{format == pixel_format::rgba8 ? 4u : 3u};
	return std::size_t{size.width} * size.height * channels;
}

/* The row of the bottom of rect, counted up from the bottom of a target
 * target_height rows high, as glReadPixels takes it.
 */
[[nodiscard]]
constexpr std::int32_t bottom_up_y(const std::uint32_t target_height, const read_rect &rect)
{
	return static_cast<std::int32_t>(std::int64_t{target_height} - rect.y - rect.size.height);
}

/* Reverses the order of the rows of an image of row_bytes-byte rows, in
 * place, keeping the bytes within each row in order: a bottom-up image
 * becomes top-down and back.  Throws std::invalid_argument, changing
 * nothing, when row_bytes is 0 or does not divide the image.
 */
inline void flip_rows(const std::span<std::uint8_t> pixels, const std::size_t row_bytes)
{
	if (row_bytes == 0 || pixels.size() % row_bytes != 0)
		throw std::invalid_argument("flip_rows: the image is not a whole number of rows");
	const std::size_t rows{pixels.size() / row_bytes};
	if (rows < 2)
		return;
	for (std::size_t top{0}, bottom{rows - 1}; top < bottom; ++top, --bottom)
		std::ranges::swap_ranges(pixels.subspan(top * row_bytes, row_bytes),
		                         pixels.subspan(bottom * row_bytes, row_bytes));
}

}

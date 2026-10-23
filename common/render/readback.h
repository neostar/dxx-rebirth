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
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

#include "common/render/render_device.h"

namespace dcx::render {

/* A rectangle of the target in pixels, named from the top left: y counts
 * rows down from the top row.  It may reach past the target's edges, where
 * the pixels read are zero (the SDL_GPU adapter; the GL adapter leaves them
 * undefined until #80 brings it to clip_read_rect too).
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

[[nodiscard]]
constexpr std::size_t channels(const pixel_format format)
{
	return format == pixel_format::rgba8 ? 4u : 3u;
}

/* The bytes a readback of an image of this size and format fills.  Throws
 * std::length_error when they do not fit in a size_t.
 */
[[nodiscard]]
constexpr std::size_t image_bytes(const render::extent size, const pixel_format format)
{
	constexpr auto max{std::numeric_limits<std::size_t>::max()};
	const std::size_t row{std::size_t{size.width} * channels(format)};
	if (size.height != 0 && row > max / size.height)
		throw std::length_error("image_bytes: the image does not fit in memory");
	return row * size.height;
}

/* The part of a read_rect inside the target: size pixels from (source_x,
 * source_y) of the target, which land at (dest_x, dest_y) of the
 * rectangle's image.  All named from the top left.
 */
struct read_clip
{
	std::uint32_t source_x;
	std::uint32_t source_y;
	render::extent size;
	std::uint32_t dest_x;
	std::uint32_t dest_y;
	constexpr bool operator==(const read_clip &) const = default;
};

namespace detail {

/* The clip of the span [start, start + length) to [0, limit) along one
 * axis: the source start, the destination start and the length.
 */
struct axis_clip
{
	std::uint32_t source;
	std::uint32_t dest;
	std::uint32_t length;
};

[[nodiscard]]
constexpr std::optional<axis_clip> clip_axis(const std::int32_t start, const std::uint32_t length,
                                             const std::uint32_t limit)
{
	const std::int64_t begin{std::max<std::int64_t>(start, 0)};
	const std::int64_t end{std::min<std::int64_t>(std::int64_t{start} + length, limit)};
	if (begin >= end)
		return std::nullopt;
	return axis_clip{.source = static_cast<std::uint32_t>(begin),
	                 .dest = static_cast<std::uint32_t>(begin - start),
	                 .length = static_cast<std::uint32_t>(end - begin)};
}

}

/* The part of rect inside a target of size target, or nothing when no pixel
 * of rect is inside it.
 */
[[nodiscard]]
constexpr std::optional<read_clip> clip_read_rect(const render::extent target,
                                                  const read_rect &rect)
{
	const auto x{detail::clip_axis(rect.x, rect.size.width, target.width)};
	const auto y{detail::clip_axis(rect.y, rect.size.height, target.height)};
	if (!x || !y)
		return std::nullopt;
	const auto &cx{x.value()};
	const auto &cy{y.value()};
	return read_clip{.source_x = cx.source,
	                 .source_y = cy.source,
	                 .size = {.width = cx.length, .height = cy.length},
	                 .dest_x = cx.dest,
	                 .dest_y = cy.dest};
}

/* Writes rows, the clip's pixels as tightly packed RGBA8 rows, top row
 * first, into image, the readback of a rectangle of size rect_size in
 * format, at the clip's place, and zero into every other byte.  Throws
 * std::length_error, writing nothing, when rows or image is not the size
 * of its image or the clip reaches past the rectangle.
 */
inline void place_rows(const std::span<const std::uint8_t> rows, const read_clip &clip,
                       const render::extent rect_size, const pixel_format format,
                       const std::span<std::uint8_t> image)
{
	if (rows.size() != image_bytes(clip.size, pixel_format::rgba8) ||
	    image.size() != image_bytes(rect_size, format) ||
	    std::uint64_t{clip.dest_x} + clip.size.width > rect_size.width ||
	    std::uint64_t{clip.dest_y} + clip.size.height > rect_size.height)
		throw std::length_error("place_rows: the rows do not fit the rectangle's image");
	std::ranges::fill(image, std::uint8_t{0});
	const std::size_t n{channels(format)};
	const std::size_t image_row{std::size_t{rect_size.width} * n};
	for (std::size_t y{0}; y < clip.size.height; ++y)
		for (std::size_t x{0}; x < clip.size.width; ++x)
		{
			const auto from{rows.subspan((y * clip.size.width + x) * 4, n)};
			const auto to{image.subspan((y + clip.dest_y) * image_row + (x + clip.dest_x) * n, n)};
			std::ranges::copy(from, to.begin());
		}
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

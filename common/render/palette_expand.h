/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Palette expansion (#11): turns a game bitmap's palette indices into the
 * RGBA pixels of a texture image (render_device.h), padded to the texture's
 * power-of-two size, as the GL front end filled its texture buffer.  It
 * holds no GL or SDL_GPU type, so every adapter uploads the same pixels.
 */

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

#include "common/render/render_device.h"

namespace dcx::render {

/* The palette index a bitmap with transparency draws as transparent. */
inline constexpr std::uint8_t transparent_index{255};
/* The palette index a bitmap with super-transparency draws as transparent
 * white, which the game recolours (cloaked robots, see-through walls).
 */
inline constexpr std::uint8_t super_transparent_index{254};
/* The index expand_index takes for the padding outside the bitmap: no
 * palette index, so always transparent.
 */
inline constexpr unsigned padding_index{256};

/* A palette entry as the game keeps it: six bits per channel. */
template <typename P>
concept palette = requires(const P &p, std::size_t i) {
	{ p[i].r } -> std::convertible_to<std::uint8_t>;
	{ p[i].g } -> std::convertible_to<std::uint8_t>;
	{ p[i].b } -> std::convertible_to<std::uint8_t>;
};

/* The indices of a bitmap: size.width by size.height indices starting at
 * column x, row y of a buffer whose rows are stride indices long.
 */
struct indexed_bitmap
{
	std::span<const std::uint8_t> indices;
	std::uint32_t stride;
	std::uint32_t x;
	std::uint32_t y;
	render::extent size;
	/* Whether index 255 is transparent and 254 super-transparent. */
	bool transparent;
	bool super_transparent;
};

/* What a pixel of a texture image is made of before its format applies. */
struct texel
{
	std::uint8_t r, g, b, a;
};

/* The texel an index stands for, as each format samples it: rgba8 and
 * rgba2 the palette colour, six bits scaled to eight, or transparent black
 * or super-transparent white; rgb8 the colour, always opaque;
 * intensity4 white or transparent black in all four channels;
 * luminance4_alpha4 white with the alpha of the index.  index is
 * padding_index for the padding outside the bitmap.
 */
template <palette Palette>
[[nodiscard]]
texel expand_index(const Palette &pal, const indexed_bitmap &bitmap, const texture_format format,
                   const unsigned index)
{
	const bool super{index == super_transparent_index && bitmap.super_transparent};
	const bool clear{(index == transparent_index && bitmap.transparent) || index == padding_index};
	switch (format)
	{
	case texture_format::rgba8:
	case texture_format::rgba2:
		break;
	case texture_format::rgb8:
		if (super)
			throw std::domain_error("expand_palette: super-transparent pixel without alpha");
		if (clear)
			return {.r = 0, .g = 0, .b = 0, .a = 255};
		break;
	case texture_format::intensity4:
		if (super)
			throw std::domain_error("expand_palette: super-transparent pixel without alpha");
		if (clear)
			return {.r = 0, .g = 0, .b = 0, .a = 0};
		return {.r = 255, .g = 255, .b = 255, .a = 255};
	case texture_format::luminance4_alpha4:
		if (super)
			return {.r = 255, .g = 255, .b = 255, .a = 0};
		if (clear)
			return {.r = 0, .g = 0, .b = 0, .a = 0};
		return {.r = 255, .g = 255, .b = 255, .a = 255};
	}
	if (super)
		return {.r = 255, .g = 255, .b = 255, .a = 0};
	if (clear)
		return {.r = 0, .g = 0, .b = 0, .a = 0};
	const auto &c{pal[index]};
	return {.r = static_cast<std::uint8_t>(c.r * 4),
	        .g = static_cast<std::uint8_t>(c.g * 4),
	        .b = static_cast<std::uint8_t>(c.b * 4),
	        .a = 255};
}

/* Fills rgba, a texture image of padded's size, from bitmap in format, row
 * by row from the top, each index as expand_index makes it.  Outside the
 * bitmap, the column right of it repeats its last column and the row below
 * it repeats its last row, so filtering finds no dark border; the rest is
 * padding.  Those two edges read the indices as the game always did: as
 * if the bitmap's rows were size.width long and began at the start of
 * indices, which is right for every bitmap but a part of a wider one.
 * Throws std::length_error when rgba is not padded's size, std::out_of_range
 * when an index lies outside bitmap.indices, and std::domain_error for a
 * super-transparent pixel in a format without colour and alpha.
 */
template <palette Palette>
void expand_palette(const Palette &pal, const indexed_bitmap &bitmap, const texture_format format,
                    const extent padded, const std::span<std::uint8_t> rgba)
{
	if (std::uint64_t{padded.width} * padded.height * 4 != rgba.size())
		throw std::length_error("expand_palette: pixel count does not match size");
	const auto at{[&bitmap](const std::uint64_t i) -> unsigned {
		if (i >= bitmap.indices.size())
			throw std::out_of_range("expand_palette: index outside the bitmap");
		return bitmap.indices[static_cast<std::size_t>(i)];
	}};
	const std::uint32_t width{bitmap.size.width};
	const std::uint32_t height{bitmap.size.height};
	std::size_t out{0};
	for (std::uint32_t y{0}; y < padded.height; ++y)
		for (std::uint32_t x{0}; x < padded.width; ++x)
		{
			unsigned index{padding_index};
			if (x < width && y < height)
				index = at(std::uint64_t{bitmap.y + y} * bitmap.stride + bitmap.x + x);
			else if (x == width && y < height)
				index = at(std::uint64_t{width} * (y + 1) - 1);
			else if (y == height && x < width)
				index = at(std::uint64_t{width} * (height - 1) + x);
			const texel t{expand_index(pal, bitmap, format, index)};
			rgba[out++] = t.r;
			rgba[out++] = t.g;
			rgba[out++] = t.b;
			rgba[out++] = t.a;
		}
}

/* Bleeds colour into the transparent pixels of rgba, an image width pixels
 * wide, against the dark edges filtering draws around transparency: each
 * transparent pixel takes the colour, not the alpha, of its first opaque
 * neighbour, looking left, right, above, below, below left, below right,
 * above left and above right.  As the game always did, the image is walked
 * as one row of pixels: left and right of an edge pixel are in the rows
 * beside it; the first and last pixels are skipped; a pixel looks above
 * only from index width + 1 on and below only up to index n - width - 2,
 * for n pixels.  Throws std::length_error when rgba is not whole pixels.
 */
inline void bleed_edges(const std::span<std::uint8_t> rgba, const std::uint32_t width)
{
	if (rgba.size() % 4 != 0)
		throw std::length_error("bleed_edges: not whole pixels");
	const std::size_t n{rgba.size() / 4};
	if (n < 2)
		return;
	const auto opaque{[rgba](const std::size_t i) { return rgba[i * 4 + 3] != 0; }};
	/* Copies the colour of pixel from to pixel to, if from is opaque. */
	const auto take{[rgba, opaque](const std::size_t to, const std::size_t from) {
		if (!opaque(from))
			return false;
		rgba[to * 4] = rgba[from * 4];
		rgba[to * 4 + 1] = rgba[from * 4 + 1];
		rgba[to * 4 + 2] = rgba[from * 4 + 2];
		return true;
	}};
	const std::size_t w{width};
	for (std::size_t i{1}; i + 1 < n; ++i)
	{
		if (opaque(i))
			continue;
		const bool has_above{i >= w + 1};
		const bool has_below{i + w + 1 < n};
		if (take(i, i - 1) || take(i, i + 1))
			continue;
		if (has_above && take(i, i - w))
			continue;
		if (has_below && (take(i, i + w) || take(i, i + w - 1) || take(i, i + w + 1)))
			continue;
		if (has_above)
			(void)(take(i, i - w - 1) || take(i, i - w + 1));
	}
}

/* Whether any pixel of rgba has an alpha below 255 (texture_desc). */
[[nodiscard]]
inline bool has_transparency(const std::span<const std::uint8_t> rgba)
{
	for (std::size_t i{3}; i < rgba.size(); i += 4)
		if (rgba[i] != 255)
			return true;
	return false;
}

}

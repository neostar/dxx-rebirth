/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/palette_expand.h"

#include <array>
#include <cstdint>
#include <ostream>
#include <vector>

#include "common/render/render_device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth palette_expand
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

namespace {

struct rgb
{
	std::uint8_t r, g, b;
};

/* Entry i is (i % 64, 63 - i % 64, 1): every index a distinct colour. */
const auto test_palette{[] {
	std::array<rgb, 256> p{};
	for (unsigned i{0}; i < p.size(); ++i)
		p.at(i) = {.r = static_cast<std::uint8_t>(i % 64),
		           .g = static_cast<std::uint8_t>(63 - i % 64),
		           .b = 1};
	return p;
}()};

/* One RGBA pixel, printable for Boost.Test. */
struct pixel
{
	std::uint8_t r, g, b, a;
	constexpr bool operator==(const pixel &) const = default;
};

std::ostream &operator<<(std::ostream &os, const pixel &p)
{
	return os << '(' << unsigned{p.r} << ", " << unsigned{p.g} << ", " << unsigned{p.b} << ", "
	          << unsigned{p.a} << ')';
}

/* The pixels of an RGBA image. */
std::vector<pixel> pixels_of_image(const std::vector<std::uint8_t> &rgba)
{
	std::vector<pixel> pixels;
	for (std::size_t i{0}; i < rgba.size(); i += 4)
		pixels.push_back({rgba.at(i), rgba.at(i + 1), rgba.at(i + 2), rgba.at(i + 3)});
	return pixels;
}

/* Expands indices, a width by height bitmap, into a padded image. */
std::vector<pixel> expand(const std::vector<std::uint8_t> &indices, const render::extent size,
                          const render::extent padded,
                          const render::texture_format format = render::texture_format::rgba8,
                          const bool transparent = true, const bool super_transparent = true)
{
	std::vector<std::uint8_t> rgba(std::size_t{padded.width} * padded.height * 4, 0xaa);
	render::expand_palette(test_palette,
	                       {.indices = indices,
	                        .stride = size.width,
	                        .x = 0,
	                        .y = 0,
	                        .size = size,
	                        .transparent = transparent,
	                        .super_transparent = super_transparent},
	                       format, padded, rgba);
	std::vector<pixel> pixels;
	for (std::size_t i{0}; i < rgba.size(); i += 4)
		pixels.push_back({rgba.at(i), rgba.at(i + 1), rgba.at(i + 2), rgba.at(i + 3)});
	return pixels;
}

}

/* Test that an opaque index becomes its palette colour scaled from six
 * bits to eight, fully opaque.
 */
BOOST_AUTO_TEST_CASE(opaque_index_takes_palette_colour)
{
	const auto pixels{expand({10, 63}, {.width = 2, .height = 1}, {.width = 2, .height = 1})};
	BOOST_TEST((pixels.at(0) == pixel{40, 212, 4, 255}));
	BOOST_TEST((pixels.at(1) == pixel{252, 0, 4, 255}));
}

/* Test that index 255 is transparent black in a bitmap with transparency
 * and an ordinary colour in one without.
 */
BOOST_AUTO_TEST_CASE(transparent_index_only_with_transparency)
{
	const render::extent one{.width = 1, .height = 1};
	BOOST_TEST((expand({255}, one, one).at(0) == pixel{0, 0, 0, 0}));
	BOOST_TEST((expand({255}, one, one, render::texture_format::rgba8, false, false).at(0) ==
	            pixel{252, 0, 4, 255}));
}

/* Test that index 254 is transparent white in a bitmap with
 * super-transparency, so the game can recolour it, and an ordinary colour
 * in one without; transparency alone does not make it transparent.
 */
BOOST_AUTO_TEST_CASE(super_transparent_index_is_clear_white)
{
	const render::extent one{.width = 1, .height = 1};
	BOOST_TEST((expand({254}, one, one).at(0) == pixel{255, 255, 255, 0}));
	BOOST_TEST((expand({254}, one, one, render::texture_format::rgba8, true, false).at(0) ==
	            pixel{248, 4, 4, 255}));
}

/* Test that a 2x2 bitmap padded to 4x4 repeats its last column to the
 * right and its last row below, and leaves the rest transparent.
 */
BOOST_AUTO_TEST_CASE(padding_repeats_edges_then_clears)
{
	const auto p{expand({1, 2, 3, 4}, {.width = 2, .height = 2}, {.width = 4, .height = 4})};
	const pixel c1{4, 248, 4, 255}, c2{8, 244, 4, 255}, c3{12, 240, 4, 255}, c4{16, 236, 4, 255},
	    clear{0, 0, 0, 0};
	const std::vector<pixel> expected{c1, c2, c2,    clear, c3,    c4,    c4,    clear,
	                                  c3, c4, clear, clear, clear, clear, clear, clear};
	BOOST_TEST(p == expected, boost::test_tools::per_element());
}

/* Test that a part of a wider bitmap starts at its origin and steps rows
 * by the stride.
 */
BOOST_AUTO_TEST_CASE(part_of_wider_bitmap_steps_by_stride)
{
	const std::vector<std::uint8_t> indices{0, 1, 2, 3, 4, 5, 6, 7, 8};
	std::vector<std::uint8_t> rgba(std::size_t{2} * 2 * 4);
	render::expand_palette(test_palette,
	                       {.indices = indices,
	                        .stride = 3,
	                        .x = 1,
	                        .y = 1,
	                        .size = {.width = 2, .height = 2},
	                        .transparent = false,
	                        .super_transparent = false},
	                       render::texture_format::rgba8, {.width = 2, .height = 2}, rgba);
	const std::vector<std::uint8_t> expected{16, 236, 4, 255, 20, 232, 4, 255,
	                                         28, 224, 4, 255, 32, 220, 4, 255};
	BOOST_TEST(rgba == expected, boost::test_tools::per_element());
}

/* Test that the edge padding of a part of a wider bitmap reads the
 * indices from their start, as if the part's rows were its width long, as
 * the game always did, while the part itself starts at its origin.
 */
BOOST_AUTO_TEST_CASE(edge_padding_reads_from_buffer_start)
{
	const std::vector<std::uint8_t> indices{0, 1, 2, 3, 4, 5, 6, 7, 8};
	std::vector<std::uint8_t> rgba(std::size_t{4} * 4 * 4);
	render::expand_palette(test_palette,
	                       {.indices = indices,
	                        .stride = 3,
	                        .x = 1,
	                        .y = 1,
	                        .size = {.width = 2, .height = 2},
	                        .transparent = false,
	                        .super_transparent = false},
	                       render::texture_format::rgba8, {.width = 4, .height = 4}, rgba);
	const pixel c1{4, 248, 4, 255}, c2{8, 244, 4, 255}, c3{12, 240, 4, 255}, c4{16, 236, 4, 255},
	    c5{20, 232, 4, 255}, c7{28, 224, 4, 255}, c8{32, 220, 4, 255}, clear{0, 0, 0, 0};
	const std::vector<pixel> expected{c4, c5, c1,    clear, c7,    c8,    c3,    clear,
	                                  c2, c3, clear, clear, clear, clear, clear, clear};
	BOOST_TEST(pixels_of_image(rgba) == expected, boost::test_tools::per_element());
}

/* Test that the padding of an rgb8 image is opaque black, as an RGB
 * texture samples it.
 */
BOOST_AUTO_TEST_CASE(rgb8_padding_is_opaque_black)
{
	const std::vector<pixel> expected{
	    {40, 212, 4, 255}, {40, 212, 4, 255}, {40, 212, 4, 255}, {0, 0, 0, 255}};
	BOOST_TEST(expand({10}, {.width = 1, .height = 1}, {.width = 2, .height = 2},
	                  render::texture_format::rgb8, false, false) == expected,
	           boost::test_tools::per_element());
}

/* Test that each format keeps what it samples: rgb8 colour, always
 * opaque; intensity4 and luminance4_alpha4 white where the bitmap is
 * opaque, whatever its colour.
 */
BOOST_AUTO_TEST_CASE(formats_without_colour_or_alpha)
{
	const render::extent size{.width = 3, .height = 1}, padded{.width = 4, .height = 1};
	using f = render::texture_format;
	const pixel white{255, 255, 255, 255}, clear{0, 0, 0, 0};
	const std::vector<pixel> rgb{{40, 212, 4, 255}, {0, 0, 0, 255}, {0, 0, 0, 255}, {0, 0, 0, 255}};
	BOOST_TEST(expand({10, 255, 255}, size, padded, f::rgb8) == rgb,
	           boost::test_tools::per_element());
	const std::vector<pixel> intensity{white, clear, clear, clear};
	BOOST_TEST(expand({10, 255, 255}, size, padded, f::intensity4) == intensity,
	           boost::test_tools::per_element());
	const std::vector<pixel> luminance_alpha{white, clear, {255, 255, 255, 0}, {255, 255, 255, 0}};
	BOOST_TEST(expand({10, 255, 254}, size, padded, f::luminance4_alpha4) == luminance_alpha,
	           boost::test_tools::per_element());
	BOOST_TEST(expand({10, 255, 254}, size, padded, f::rgba2).at(2) == (pixel{255, 255, 255, 0}));
}

/* Test that a super-transparent pixel in a format without alpha, which the
 * game never makes, is refused rather than drawn opaque.
 */
BOOST_AUTO_TEST_CASE(super_transparent_needs_alpha)
{
	const render::extent one{.width = 1, .height = 1};
	BOOST_CHECK_THROW(expand({254}, one, one, render::texture_format::rgb8), std::domain_error);
	BOOST_CHECK_THROW(expand({254}, one, one, render::texture_format::intensity4),
	                  std::domain_error);
}

/* Test that an image buffer of the wrong size and indices too short for
 * the bitmap are refused, not written or read past.
 */
BOOST_AUTO_TEST_CASE(sizes_are_checked)
{
	std::vector<std::uint8_t> rgba(4);
	const std::vector<std::uint8_t> indices{1};
	const auto fill{[&](const render::extent size, const render::extent padded) {
		render::expand_palette(test_palette,
		                       {.indices = indices,
		                        .stride = size.width,
		                        .x = 0,
		                        .y = 0,
		                        .size = size,
		                        .transparent = false,
		                        .super_transparent = false},
		                       render::texture_format::rgba8, padded, rgba);
	}};
	BOOST_CHECK_THROW(fill({.width = 1, .height = 1}, {.width = 2, .height = 1}),
	                  std::length_error);
	rgba.resize(8);
	BOOST_CHECK_THROW(fill({.width = 2, .height = 1}, {.width = 2, .height = 1}),
	                  std::out_of_range);
}

namespace {

/* The image of pixels as bytes, and back. */
std::vector<std::uint8_t> bytes(const std::vector<pixel> &pixels)
{
	std::vector<std::uint8_t> b;
	for (const auto &p : pixels)
		b.insert(b.end(), {p.r, p.g, p.b, p.a});
	return b;
}

std::vector<pixel> pixels_of(const std::vector<std::uint8_t> &b)
{
	std::vector<pixel> p;
	for (std::size_t i{0}; i < b.size(); i += 4)
		p.push_back({b.at(i), b.at(i + 1), b.at(i + 2), b.at(i + 3)});
	return p;
}

std::vector<pixel> bleed(const std::vector<pixel> &image, const std::uint32_t width)
{
	auto b{bytes(image)};
	render::bleed_edges(b, width);
	return pixels_of(b);
}

constexpr pixel clear{0, 0, 0, 0};
constexpr pixel red{255, 0, 0, 255};
constexpr pixel green{0, 255, 0, 255};
constexpr pixel blue{0, 0, 255, 255};

}

/* Test that a transparent pixel takes the colour of an opaque neighbour,
 * diagonal ones too, and stays transparent; one with no opaque neighbour
 * keeps black.
 */
BOOST_AUTO_TEST_CASE(bleed_takes_neighbour_colour)
{
	const std::vector<pixel> image{red, clear, clear, clear, clear, clear, clear, clear, clear};
	const std::vector<pixel> expected{red,   {255, 0, 0, 0}, clear, clear, {255, 0, 0, 0},
	                                  clear, clear,          clear, clear};
	BOOST_TEST(bleed(image, 3) == expected, boost::test_tools::per_element());
}

/* Test the order of the neighbours: left before right before above before
 * below, then the diagonals below before those above.
 */
BOOST_AUTO_TEST_CASE(bleed_prefers_left_right_above_below)
{
	/* The centre of a 3x3 image, with opaque neighbours on the given sides. */
	const auto centre{[](const std::vector<pixel> &image) { return bleed(image, 3).at(4); }};
	BOOST_TEST(centre({clear, blue, clear, red, clear, green, clear, blue, clear}) ==
	           (pixel{255, 0, 0, 0}));
	BOOST_TEST(centre({clear, blue, clear, clear, clear, green, clear, red, clear}) ==
	           (pixel{0, 255, 0, 0}));
	BOOST_TEST(centre({clear, clear, clear, clear, clear, clear, red, green, blue}) ==
	           (pixel{0, 255, 0, 0}));
	BOOST_TEST(centre({clear, clear, clear, clear, clear, clear, red, clear, blue}) ==
	           (pixel{255, 0, 0, 0}));
	BOOST_TEST(centre({clear, blue, clear, clear, clear, clear, clear, red, clear}) ==
	           (pixel{0, 0, 255, 0}));
	BOOST_TEST(centre({green, clear, blue, clear, clear, clear, clear, red, clear}) ==
	           (pixel{255, 0, 0, 0}));
	BOOST_TEST(centre({green, clear, blue, clear, clear, clear, red, clear, clear}) ==
	           (pixel{255, 0, 0, 0}));
	BOOST_TEST(centre({green, clear, blue, clear, clear, clear, clear, clear, red}) ==
	           (pixel{255, 0, 0, 0}));
	BOOST_TEST(centre({green, clear, blue, clear, clear, clear, clear, clear, clear}) ==
	           (pixel{0, 255, 0, 0}));
	BOOST_TEST(centre({clear, clear, blue, clear, clear, clear, clear, clear, clear}) ==
	           (pixel{0, 0, 255, 0}));
}

/* Test that the image is walked as one row, as the game did: the last
 * pixel of a row takes the colour of the first of the next, the first
 * pixel of the second row does not look above, and the first and last
 * pixels of the image are left alone.
 */
BOOST_AUTO_TEST_CASE(bleed_walks_rows_as_one)
{
	const std::vector<pixel> image{clear, clear, clear, green, clear, clear, clear, red, clear};
	const std::vector<pixel> expected{clear,          {0, 255, 0, 0}, {0, 255, 0, 0}, green,
	                                  {0, 255, 0, 0}, {0, 255, 0, 0}, {255, 0, 0, 0}, red,
	                                  clear};
	BOOST_TEST(bleed(image, 3) == expected, boost::test_tools::per_element());
	const std::vector<pixel> ends{clear, red, red, clear};
	BOOST_TEST(bleed(ends, 4) == ends, boost::test_tools::per_element());
}

/* Test that an image has transparency when any pixel's alpha is below
 * 255, a super-transparent one included, and not when all are opaque.
 */
BOOST_AUTO_TEST_CASE(transparency_is_any_alpha_below_opaque)
{
	BOOST_TEST(!render::has_transparency(bytes({red, green, blue})));
	BOOST_TEST(render::has_transparency(bytes({red, {255, 255, 255, 0}})));
	BOOST_TEST(render::has_transparency(bytes({{1, 2, 3, 254}, blue})));
}

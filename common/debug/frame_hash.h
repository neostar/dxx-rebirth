/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace dcx {

/* The hash of one presented frame's pixels: 64-bit FNV-1a, which needs no
 * library and is fast enough to run on every frame of a demo.  It is a
 * change detector, not a checksum against tampering.
 */
[[nodiscard]]
constexpr std::uint64_t frame_hash(const std::span<const std::byte> pixels)
{
	constexpr std::uint64_t offset_basis{0xcbf29ce484222325u};
	constexpr std::uint64_t prime{0x100000001b3u};
	std::uint64_t hash{offset_basis};
	for (const std::byte b : pixels)
	{
		hash ^= static_cast<std::uint64_t>(b);
		hash *= prime;
	}
	return hash;
}

/* The size of a presented frame, in pixels. */
struct frame_size
{
	std::uint16_t width;
	std::uint16_t height;
	/* The bytes of its RGB readback, packed with no row padding. */
	[[nodiscard]]
	constexpr std::size_t rgb_bytes() const
	{
		constexpr std::size_t rgb{3};
		return std::size_t{width} * height * rgb;
	}
};

/* A frame's number within its section of the frame hash log, from 0. */
enum class frame_index : std::uint32_t
{
};

/* One line of the frame hash log:
 * "<frame> <width>x<height> <hash as 16 hex digits>\n".
 */
[[nodiscard]]
inline std::string frame_hash_line(const frame_index frame, const frame_size size,
                                   const std::uint64_t hash)
{
	std::string line{std::to_string(static_cast<std::uint32_t>(frame))};
	line += ' ';
	line += std::to_string(size.width);
	line += 'x';
	line += std::to_string(size.height);
	line += ' ';
	constexpr std::string_view hex_digits{"0123456789abcdef"};
	constexpr unsigned hex_digit_count{16};
	for (unsigned i{0}; i != hex_digit_count; ++i)
		line += hex_digits[(hash >> (4u * (hex_digit_count - 1u - i))) & 0xfu];
	line += '\n';
	return line;
}

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/frame_hash.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

#include "common/debug/frame_hash_harness.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth frame hash
#include <boost/test/unit_test.hpp>

namespace {

std::span<const std::byte> bytes_of(const std::string_view s)
{
	return std::as_bytes(std::span{s.data(), s.size()});
}

}

/* Test that the frame hash is 64-bit FNV-1a, checked against the published test vectors
 * (Fowler, Noll, Vo; www.isthe.com/chongo/src/fnv/test_fnv.c).
 */
BOOST_AUTO_TEST_CASE(hash_matches_published_fnv1a_vectors)
{
	BOOST_TEST(dcx::frame_hash(bytes_of("")) == 0xcbf29ce484222325u);
	BOOST_TEST(dcx::frame_hash(bytes_of("a")) == 0xaf63dc4c8601ec8cu);
	BOOST_TEST(dcx::frame_hash(bytes_of("foobar")) == 0x85944171f73967e8u);
}

/* Test that one changed pixel byte changes the hash. */
BOOST_AUTO_TEST_CASE(hash_sees_one_changed_byte)
{
	std::array<std::byte, 12> pixels{};
	const auto before{dcx::frame_hash(pixels)};
	pixels[7] = std::byte{1};
	BOOST_TEST(dcx::frame_hash(pixels) != before);
}

/* Test that a log line gives the frame number, the size and the hash as 16 hex digits. */
BOOST_AUTO_TEST_CASE(log_line_format)
{
	const auto line{dcx::frame_hash_line(dcx::frame_index{42}, {.width = 640, .height = 480},
	                                     0xaf63dc4c8601ec8cu)};
	BOOST_TEST(line == "42 640x480 af63dc4c8601ec8c\n");
	const auto small{dcx::frame_hash_line(dcx::frame_index{0}, {.width = 1, .height = 1}, 0x1u)};
	BOOST_TEST(small == "0 1x1 0000000000000001\n");
}

/* Test that a frame's RGB readback takes three bytes per pixel, with no row padding. */
BOOST_AUTO_TEST_CASE(rgb_size)
{
	BOOST_TEST(
	    (dcx::frame_size{.width = 640, .height = 480}.rgb_bytes() == std::size_t{640} * 480u * 3u));
	BOOST_TEST((dcx::frame_size{.width = 65535, .height = 65535}.rgb_bytes() ==
	            std::size_t{65535} * 65535u * 3u));
}

/* Test that the harness reads DXX_DBG_FRAMEHASH as the log path, unset meaning no log. */
BOOST_AUTO_TEST_CASE(harness_reads_the_log_path)
{
	unsetenv("DXX_DBG_FRAMEHASH");
	BOOST_TEST(dcx::read_harness_frame_hash_path() == nullptr);
	setenv("DXX_DBG_FRAMEHASH", "/x/frames.log", 1);
	const auto *const log{dcx::read_harness_frame_hash_path()};
	BOOST_REQUIRE(log != nullptr);
	BOOST_TEST(std::string_view{log} == "/x/frames.log");
	unsetenv("DXX_DBG_FRAMEHASH");
}

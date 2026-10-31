/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* The PhysicsFS the game links is built from the 3.2.0 release with upstream's
 * fix for CVE-2024-58392 applied (tools/dev/toolchain.pins, ticket #87): these
 * tests fail against the unpatched release.  AddressSanitizer cannot see the
 * read, which happens inside the library it did not compile, so each name is
 * followed by a letter after its terminator, and a parse that skips the
 * terminator shows in the result.
 */

#include <array>
#include <physfs.h>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth physfs utf8
#include <boost/test/unit_test.hpp>

namespace {

/* A six-octet UTF-8 sequence (a lead byte of 0xFC and five continuation
 * bytes, illegal since RFC 3629), which PhysicsFS parses and discards.
 * `second` is the first continuation byte, so two names can differ.
 */
constexpr std::array<char, 8> six_octet_name(const unsigned char second, const char after)
{
	return {{static_cast<char>(0xFC), static_cast<char>(second), static_cast<char>(0x80),
	         static_cast<char>(0x80), static_cast<char>(0x80), static_cast<char>(0x80), '\0',
	         after}};
}

}

/* Test that the terminator of a name ending in a six-octet sequence is the
 * end of the name, also when the byte after it is a character: two such names
 * are equal, as the discarded sequences are, and not decided by that byte.
 */
BOOST_AUTO_TEST_CASE(six_octet_sequence_does_not_skip_the_terminator)
{
	const auto a{six_octet_name(0x80, 'A')};
	const auto b{six_octet_name(0x81, '\0')};
	BOOST_TEST(PHYSFS_utf8stricmp(a.data(), b.data()) == 0);
	BOOST_TEST(PHYSFS_utf8stricmp(b.data(), a.data()) == 0);
}

/* Test that a name with a six-octet sequence before more characters still
 * compares by them, so the fix does not drop what follows.
 */
BOOST_AUTO_TEST_CASE(six_octet_sequence_in_the_middle_compares_what_follows)
{
	auto a{six_octet_name(0x80, 'A')};
	auto b{six_octet_name(0x80, 'B')};
	/* The letters replace the terminators, so the names end after them. */
	a[6] = 'A';
	b[6] = 'B';
	a[7] = b[7] = '\0';
	BOOST_TEST(PHYSFS_utf8stricmp(a.data(), b.data()) != 0);
	b[6] = 'a';
	BOOST_TEST(PHYSFS_utf8stricmp(a.data(), b.data()) == 0);
}

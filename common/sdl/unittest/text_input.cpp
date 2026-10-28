/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/text_input.h"

#include <array>
#include <string>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth text input
#include <boost/test/unit_test.hpp>

namespace {

/* Counts the starts and stops it is asked for, and refuses to start when told. */
struct counting_ops
{
	int *starts{nullptr};
	int *stops{nullptr};
	bool accept{true};
	bool start() const
	{
		++*starts;
		return accept;
	}
	void stop() const
	{
		++*stops;
	}
};

/* The characters a buffer holds, up to its first zero. */
std::string text_of(const std::array<unsigned char, 8> &buffer)
{
	std::string s;
	for (const auto c : buffer)
	{
		if (c == 0)
			break;
		s.push_back(static_cast<char>(c));
	}
	return s;
}

}

/* Test that opening twice starts once and closing twice stops once. */
BOOST_AUTO_TEST_CASE(open_and_close_are_idempotent)
{
	int starts{0};
	int stops{0};
	dcx::sdl::text_input_gate gate{counting_ops{&starts, &stops}};
	BOOST_TEST(!gate.active());
	gate.close();
	BOOST_TEST(stops == 0);
	gate.open();
	gate.open();
	BOOST_TEST(gate.active());
	BOOST_TEST(starts == 1);
	gate.close();
	gate.close();
	BOOST_TEST(!gate.active());
	BOOST_TEST(stops == 1);
}

/* Test that a start SDL refuses leaves the gate closed, so key events keep filling the buffer,
 * and that the next open tries again.
 */
BOOST_AUTO_TEST_CASE(a_refused_start_leaves_the_gate_closed)
{
	int starts{0};
	int stops{0};
	dcx::sdl::text_input_gate gate{counting_ops{&starts, &stops, false}};
	gate.open();
	BOOST_TEST(!gate.active());
	gate.open();
	BOOST_TEST(starts == 2);
	gate.close();
	BOOST_TEST(stops == 0);
}

/* Test that ASCII text, shifted characters included, lands in the buffer in order. */
BOOST_AUTO_TEST_CASE(ascii_text_is_appended_in_order)
{
	std::array<unsigned char, 8> buffer{};
	dcx::sdl::append_game_text(buffer, "Ab!");
	BOOST_TEST(text_of(buffer) == "Ab!");
	dcx::sdl::append_game_text(buffer, "~");
	BOOST_TEST(text_of(buffer) == "Ab!~");
}

/* Test that Latin-1 code points arrive as one byte and that anything the fonts lack, controls and
 * code points above 254, is dropped without losing the characters around it.
 */
BOOST_AUTO_TEST_CASE(latin1_is_one_byte_and_the_rest_is_dropped)
{
	std::array<unsigned char, 8> buffer{};
	dcx::sdl::append_game_text(buffer, "a\xc3\xa9"
	                                   "b\xe2\x82\xac"
	                                   "c\xf0\x9f\x98\x80"
	                                   "d\t");
	BOOST_TEST(buffer[0] == 'a');
	BOOST_TEST(buffer[1] == 0xe9);
	BOOST_TEST(buffer[2] == 'b');
	BOOST_TEST(buffer[3] == 'c');
	BOOST_TEST(buffer[4] == 'd');
	BOOST_TEST(buffer[5] == 0);
}

/* Test that a truncated sequence, a stray continuation byte and a full buffer lose only what they
 * must.
 */
BOOST_AUTO_TEST_CASE(malformed_text_and_a_full_buffer_are_survived)
{
	std::array<unsigned char, 8> buffer{};
	dcx::sdl::append_game_text(buffer, "x\x80y\xc3");
	BOOST_TEST(text_of(buffer) == "xy");
	dcx::sdl::append_game_text(buffer, "123456789");
	BOOST_TEST(text_of(buffer) == "xy123456");
}

/* Test that malformed UTF-8 is dropped, never read as a character: a bad continuation byte, an
 * overlong encoding of '/' and a five-byte lead (0xf8, which the old decoder took for a four-byte
 * one) each lose only their own bytes.
 */
BOOST_AUTO_TEST_CASE(malformed_utf8_is_never_read_as_a_character)
{
	std::array<unsigned char, 8> buffer{};
	dcx::sdl::append_game_text(buffer, "a\xc3(b");
	BOOST_TEST(text_of(buffer) == "a(b");
	buffer = {};
	dcx::sdl::append_game_text(buffer, "a\xc0\xaf"
	                                   "b");
	BOOST_TEST(text_of(buffer) == "ab");
	buffer = {};
	dcx::sdl::append_game_text(buffer, "a\xf8\x80\x80\xa1\x80"
	                                   "b");
	BOOST_TEST(text_of(buffer) == "ab");
}

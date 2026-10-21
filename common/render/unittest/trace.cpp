/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/trace.h"

#include <cstdint>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth trace
#include <boost/test/unit_test.hpp>

namespace trace = dcx::render::trace;

namespace {

using bytes = std::vector<std::uint8_t>;

/* The header of a version 1 trace. */
const bytes header{'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n', 1, 0, 0, 0};

bytes operator+(bytes a, const bytes &b)
{
	a.insert(a.end(), b.begin(), b.end());
	return a;
}

}

/* Test that a new writer holds the header alone. */
BOOST_AUTO_TEST_CASE(empty_trace_is_header)
{
	trace::writer w;
	const auto out{w.take()};
	BOOST_TEST(out == header);
}

/* Test that clears and presents are written as their documented bytes, the
 * frame boundary among them.
 */
BOOST_AUTO_TEST_CASE(clear_and_present_bytes)
{
	trace::writer w;
	w.clear({.color = false, .depth = true});
	w.clear({.color = true, .depth = false});
	w.present();
	const auto out{w.take()};
	/* kind 1 clear (flags: bit 0 colour, bit 1 depth), kind 2 present. */
	BOOST_TEST((out == header + bytes{1, 1, 0, 0, 0, 2, 1, 1, 0, 0, 0, 1, 2, 0, 0, 0, 0}));
}

/* Test that take() hands over the bytes so far and the writer goes on
 * after them.
 */
BOOST_AUTO_TEST_CASE(take_hands_over_pieces)
{
	trace::writer w;
	w.present();
	const auto first{w.take()};
	w.clear({.color = true, .depth = true});
	const auto second{w.take()};
	BOOST_TEST((first == header + bytes{2, 0, 0, 0, 0}));
	BOOST_TEST(second == (bytes{1, 1, 0, 0, 0, 3}));
}

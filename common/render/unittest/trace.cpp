/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/trace.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
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

/* Test that the reader gives back the records the writer was given, in
 * order, and an empty trace as no records.
 */
BOOST_AUTO_TEST_CASE(clear_and_present_round_trip)
{
	trace::writer w;
	const auto empty{trace::read_all(w.take())};
	BOOST_REQUIRE(empty.has_value());
	BOOST_TEST(empty.value().empty());
	w.clear({.color = false, .depth = true});
	w.clear({.color = true, .depth = true});
	w.present();
	const auto records{trace::read_all(header + w.take())};
	BOOST_REQUIRE(records.has_value());
	const std::vector<trace::record> expected{
	    trace::clear{.color = false, .depth = true},
	    trace::clear{.color = true, .depth = true},
	    trace::present{},
	};
	BOOST_TEST((records.value() == expected));
}

namespace {

/* One malformed trace and the error it must be rejected with. */
struct malformed
{
	std::string_view name;
	bytes trace;
	trace::read_error error;
};

/* The malformed corpus: synthetic traces, each broken in one way. */
const std::vector<malformed> corpus{
    {"empty file", {}, trace::read_error::truncated},
    {"magic cut short", {'D', 'X', 'X'}, trace::read_error::truncated},
    {"header cut short",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n', 1},
	 trace::read_error::truncated},
    {"wrong magic",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\n', '\n', 1, 0, 0, 0},
	 trace::read_error::bad_magic},
    {"version 2",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n', 2, 0, 0, 0},
	 trace::read_error::unsupported_version},
    {"version big-endian",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n', 0, 1, 0, 0},
	 trace::read_error::unsupported_version},
    {"reserved not zero",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n', 1, 0, 1, 0},
	 trace::read_error::unsupported_version},
    {"record head cut short", header + bytes{2, 0, 0}, trace::read_error::truncated},
    {"payload past the end", header + bytes{1, 1, 0, 0, 0}, trace::read_error::truncated},
    {"length near 4 GiB", header + bytes{1, 0xff, 0xff, 0xff, 0xff, 3},
	 trace::read_error::truncated},
    {"unknown kind 0", header + bytes{0, 0, 0, 0, 0}, trace::read_error::unknown_kind},
    {"unknown kind 255", header + bytes{255, 0, 0, 0, 0}, trace::read_error::unknown_kind},
    {"clear without flags", header + bytes{1, 0, 0, 0, 0}, trace::read_error::bad_length},
    {"clear with a spare byte", header + bytes{1, 2, 0, 0, 0, 3, 0}, trace::read_error::bad_length},
    {"present with a payload", header + bytes{2, 1, 0, 0, 0, 0}, trace::read_error::bad_length},
    {"clear flag bit 2", header + bytes{1, 1, 0, 0, 0, 4}, trace::read_error::bad_value},
    {"good record, then garbage", header + bytes{2, 0, 0, 0, 0, 9}, trace::read_error::truncated},
};

}

/* Test that each trace of the malformed corpus is rejected with its error,
 * and that the reader keeps that error once found.
 */
BOOST_AUTO_TEST_CASE(malformed_corpus_rejected)
{
	for (const auto &m : corpus)
	{
		BOOST_TEST_CONTEXT(m.name)
		{
			const auto r{trace::read_all(m.trace)};
			BOOST_REQUIRE(!r.has_value());
			BOOST_TEST((r.error() == m.error));
			trace::reader rd{m.trace};
			auto next{rd.next()};
			while (next.has_value() && next.value().has_value())
				next = rd.next();
			const auto again{rd.next()};
			BOOST_REQUIRE(!again.has_value());
			BOOST_TEST((again.error() == m.error));
		}
	}
}

/* Test that every proper prefix of a good trace is rejected as truncated,
 * and every prefix that ends at a record boundary reads.
 */
BOOST_AUTO_TEST_CASE(every_cut_is_truncated_or_whole)
{
	trace::writer w;
	w.clear({.color = true, .depth = true});
	w.present();
	const auto good{w.take()};
	const std::vector<std::size_t> boundaries{header.size(), header.size() + 6, good.size()};
	for (std::size_t n{0}; n <= good.size(); ++n)
	{
		BOOST_TEST_CONTEXT("first " << n << " bytes")
		{
			const auto r{trace::read_all(std::span{good}.first(n))};
			if (std::ranges::find(boundaries, n) != boundaries.end())
				BOOST_TEST(r.has_value());
			else
				BOOST_TEST((!r.has_value() && r.error() == trace::read_error::truncated));
		}
	}
}

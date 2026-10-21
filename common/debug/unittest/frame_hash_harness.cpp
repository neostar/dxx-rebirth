/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/frame_hash_harness.h"

#include <array>
#include <cstddef>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth frame hash harness
#include <boost/test/unit_test.hpp>

namespace {

constexpr unsigned no_error{0};

/* A glGetError that returns `errors` in turn, then no_error; it counts its calls. */
template <std::size_t N>
struct queued_errors
{
	std::array<unsigned, N> errors;
	std::size_t calls{0};
	unsigned operator()()
	{
		return calls < N ? errors[calls++] : (++calls, no_error);
	}
};

}

/* Test that nothing pending drains nothing, with one call. */
BOOST_AUTO_TEST_CASE(no_pending_error_drains_nothing)
{
	queued_errors<0> get_error{};
	const auto drained{dcx::drain_gl_errors(get_error, no_error)};
	BOOST_TEST(drained.count == 0u);
	BOOST_TEST(get_error.calls == 1u);
}

/* Test that every pending error is drained, the first is kept, and the drain stops at the
 * first no_error, so the readback's own error is the next one returned.
 */
BOOST_AUTO_TEST_CASE(pending_errors_are_drained)
{
	queued_errors<3> get_error{.errors = {{0x502u, 0x500u, 0x505u}}};
	const auto drained{dcx::drain_gl_errors(get_error, no_error)};
	BOOST_TEST(drained.count == 3u);
	BOOST_TEST(drained.first == 0x502u);
	BOOST_TEST(get_error.calls == 4u);
}

/* Test that a glGetError that never returns no_error, as after a lost context, ends the
 * drain after max_drained_gl_errors errors.
 */
BOOST_AUTO_TEST_CASE(endless_errors_end_the_drain)
{
	unsigned calls{0};
	const auto drained{dcx::drain_gl_errors(
	    [&calls]() {
		    ++calls;
		    return 0x507u;
	    },
	    no_error)};
	BOOST_TEST(drained.count == dcx::max_drained_gl_errors);
	BOOST_TEST(calls == dcx::max_drained_gl_errors);
}

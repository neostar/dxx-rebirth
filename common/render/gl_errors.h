/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Taking the GL errors pending in a context, which the game never checks
 * with glGetError: the GL adapter (gl_device.h) and the debug harness'
 * frame hash readback use it.
 */

#pragma once

#include <concepts>
#include <type_traits>

namespace dcx {

/* The most GL errors drain_gl_errors takes: a lost context may report an
 * error on every call.
 */
constexpr unsigned max_drained_gl_errors{64};

/* The GL errors drain_gl_errors took: how many, and the first of them. */
template <typename Error>
struct drained_gl_errors
{
	unsigned count;
	Error first;
};

/* Take the GL errors pending before the frame hash readback, which the
 * game never checks with glGetError, so that the readback's error check
 * sees only its own: call get_error until it returns no_error, at most
 * max_drained_gl_errors times.
 */
template <typename GetError, typename Error = std::invoke_result_t<GetError &>>
requires(std::equality_comparable<Error>)
[[nodiscard]]
drained_gl_errors<Error> drain_gl_errors(GetError &&get_error,
                                         const std::type_identity_t<Error> no_error)
{
	drained_gl_errors<Error> drained{.count = 0, .first = no_error};
	while (drained.count != max_drained_gl_errors)
	{
		const Error e{get_error()};
		if (e == no_error)
			break;
		if (drained.count++ == 0)
			drained.first = e;
	}
	return drained;
}

}

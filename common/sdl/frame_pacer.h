/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstdint>

namespace dcx {

/* One second in fix units (16.16, maths.h's F1_0), spelt out so that this
 * header includes no upstream header.
 */
inline constexpr std::int64_t fix_one{65536};
inline constexpr std::uint64_t ns_per_second{1000000000};

/* Convert nanoseconds since SDL started to fix seconds.  The whole seconds
 * convert first, so the product does not overflow after 78 hours as
 * ns * 65536 would.
 */
[[nodiscard]]
constexpr std::int64_t fix_from_ns(const std::uint64_t ns)
{
	const auto seconds{static_cast<std::int64_t>(ns / ns_per_second)};
	const auto rest{static_cast<std::int64_t>(ns % ns_per_second)};
	return seconds * fix_one + rest * fix_one / static_cast<std::int64_t>(ns_per_second);
}

/* The reverse: a span in fix seconds as nanoseconds; a negative span is
 * zero.
 */
[[nodiscard]]
constexpr std::uint64_t ns_from_fix(const std::int64_t fix)
{
	if (fix <= 0)
		return 0;
	return static_cast<std::uint64_t>(fix / fix_one) * ns_per_second +
	       static_cast<std::uint64_t>(fix % fix_one) * ns_per_second /
	           static_cast<std::uint64_t>(fix_one);
}

}

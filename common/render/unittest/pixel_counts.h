/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * What test-rtrace (on GL) and test-sdlgpu (on Metal) both count in a
 * replayed frame, so that one definition holds both to the same numbers.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dcx::render::unittest {

/* How many pixels of a frame's top-first RGB image are mostly channel c (0
 * red, 1 green, 2 blue): that channel above both others by more than 10
 * levels.  The overlay trace's solid passes are one colour scaled by
 * vertex light, which lies between 0.3 and 1.6.
 */
[[nodiscard]]
inline std::size_t pixels_mostly(const std::span<const std::uint8_t> rgb, const std::size_t c)
{
	std::size_t n{0};
	for (std::size_t i{0}; i + 2 < rgb.size(); i += 3)
	{
		const int v{rgb[i + c]};
		const int a{rgb[i + (c + 1) % 3]};
		const int b{rgb[i + (c + 2) % 3]};
		if (v > a + 10 && v > b + 10)
			++n;
	}
	return n;
}

}

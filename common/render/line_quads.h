/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Strips as triangle lists, for an adapter whose API has no triangle
 * strips with GL's provoking vertex (SDL_GPU, #32); lines and points
 * follow in this header.
 */

#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "common/render/render_device.h"

namespace dcx::render {

/* GL_TRIANGLE_STRIP as a triangle list.  Triangle i is (vi, vi+1, vi+2)
 * for even i and (vi+1, vi, vi+2) for odd i, which keeps the winding; each
 * is emitted rotated, as fan_to_list does, to put vi+2, GL's provoking
 * vertex, first.  Fewer than three vertices draw nothing.
 */
[[nodiscard]]
inline std::vector<vertex> strip_to_list(const std::span<const vertex> strip)
{
	std::vector<vertex> list;
	if (strip.size() < 3)
		return list;
	list.reserve(3 * (strip.size() - 2));
	for (std::size_t i{0}; i + 2 < strip.size(); ++i)
	{
		list.push_back(strip[i + 2]);
		list.push_back(strip[i % 2 == 0 ? i : i + 1]);
		list.push_back(strip[i % 2 == 0 ? i + 1 : i]);
	}
	return list;
}

}

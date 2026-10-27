/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The texture coordinates of a wall's overlay texture, which a side turns
 * in quarter turns (texture2_rotation_low in segment.h), so the front end
 * fills the overlay's render::vertex values from the side's own
 * coordinates.
 */

#pragma once

#include <array>
#include <cstdint>

namespace dcx::render {

/* The overlay's turn, with texture2_rotation_low's values: Normal, _1, _2
 * and _3.
 */
enum class uv_rotation : std::uint8_t
{
	none,
	quarter,
	half,
	three_quarters,
};

/* The overlay coordinates of a corner whose own coordinates are uv.  1 - u
 * is taken in double and rounded to float, as _g3_draw_tmap_2 did before
 * the render device.
 */
[[nodiscard]]
constexpr std::array<float, 2> overlay_uv(const std::array<float, 2> &uv, const uv_rotation r)
{
	const float u{uv[0]};
	const float v{uv[1]};
	const auto flip{[](const float f) { return static_cast<float>(1.0 - double{f}); }};
	switch (r)
	{
	case uv_rotation::quarter:
		return {{flip(v), u}};
	case uv_rotation::half:
		return {{flip(u), flip(v)}};
	case uv_rotation::three_quarters:
		return {{v, flip(u)}};
	case uv_rotation::none:
		break;
	}
	return uv;
}

}

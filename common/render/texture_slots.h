/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * How the front end numbers its textures as render device slots: slot 0
 * is none, slots 1 to the pool's size the textures of its pool
 * (ogl_texture_list) in order, and the slot after those the movie frame
 * ogl_ubitblt_i keeps outside the pool while it draws it.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "common/render/render_device.h"

namespace dcx::render {

/* What a slot leads to. */
enum class slot_owner : std::uint8_t
{
	none,
	pool,
	movie_frame,
};

/* A slot's texture: its owner and, for the pool, the index in it. */
struct slot_place
{
	slot_owner owner;
	std::size_t index;
	constexpr bool operator==(const slot_place &) const = default;
};

/* The slot of the pool's texture at index. */
[[nodiscard]]
constexpr texture_slot pool_slot(const std::size_t index)
{
	return {static_cast<std::uint32_t>(index + 1)};
}

/* The movie frame's slot, after a pool of pool_size textures. */
[[nodiscard]]
constexpr texture_slot movie_frame_slot(const std::size_t pool_size)
{
	return {static_cast<std::uint32_t>(pool_size + 1)};
}

/* The texture slot leads to, in a pool of pool_size textures. */
[[nodiscard]]
constexpr slot_place slot_place_of(const texture_slot slot, const std::size_t pool_size)
{
	if (slot.index >= 1 && slot.index <= pool_size)
		return {.owner = slot_owner::pool, .index = slot.index - 1};
	if (slot == movie_frame_slot(pool_size))
		return {.owner = slot_owner::movie_frame, .index = 0};
	return {.owner = slot_owner::none, .index = 0};
}

}

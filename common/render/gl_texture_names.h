/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The GL adapter's texture names (gl_device.h): which GL texture each
 * texture slot holds.  It holds no GL type, so what it keeps is tested
 * without a GL context; the adapter makes and deletes the names.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "common/render/render_device.h"

namespace dcx::render::gl {

class texture_names
{
	std::unordered_map<std::uint32_t, std::uint32_t> names_;

  public:
	/* The name slot holds, 0 when it holds none. */
	[[nodiscard]]
	std::uint32_t find(const texture_slot slot) const
	{
		const auto it{names_.find(slot.index)};
		return it == names_.end() ? 0 : it->second;
	}
	/* Gives slot name, or takes its name away for 0, and returns the name
	 * it held before, 0 when none.
	 */
	std::uint32_t exchange(const texture_slot slot, const std::uint32_t name)
	{
		const std::uint32_t old{find(slot)};
		if (name == 0)
			names_.erase(slot.index);
		else
			names_.insert_or_assign(slot.index, name);
		return old;
	}
	/* How many slots hold a name. */
	[[nodiscard]]
	std::size_t size() const
	{
		return names_.size();
	}
	/* Takes every name of from, which is left with none: a new adapter for
	 * the same GL context keeps the textures of the one it replaces.
	 * Throws std::logic_error when this already holds names.
	 */
	void take(texture_names &from)
	{
		if (!names_.empty())
			throw std::logic_error("texture_names::take: names would be lost");
		names_ = std::move(from.names_);
		from.names_.clear();
	}
};

}

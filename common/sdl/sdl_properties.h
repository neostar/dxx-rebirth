/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

/* SDL3 only: SDL_PropertiesID has no SDL2 counterpart, so this is not in the
 * version-neutral sdl_deleter.h.
 */
#include "common/sdl/dxx-sdl.h"

namespace dcx::sdl {

/* Owns an SDL properties group; get() is 0 when SDL could not create one. */
class sdl_properties
{
  public:
	sdl_properties() : id_{SDL_CreateProperties()}
	{}
	~sdl_properties()
	{
		if (id_ != 0)
			SDL_DestroyProperties(id_);
	}
	sdl_properties(const sdl_properties &) = delete;
	sdl_properties &operator=(const sdl_properties &) = delete;
	sdl_properties(sdl_properties &&) = delete;
	sdl_properties &operator=(sdl_properties &&) = delete;
	[[nodiscard]]
	SDL_PropertiesID get() const
	{
		return id_;
	}

  private:
	SDL_PropertiesID id_;
};

}

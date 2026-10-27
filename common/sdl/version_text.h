/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <string>

#include "common/sdl/dxx-sdl.h"

#if SDL_MAJOR_VERSION == 3

namespace dcx::sdl {

/* The tail of the verbose start-up line for one SDL3 library, "NAME a.b.c;
 * loaded with NAME x.y.z": the version it was built with and the one loaded,
 * both SDL3's packed SDL_VERSIONNUM values (SDL_VERSION and SDL_GetVersion,
 * SDL_IMAGE_VERSION and IMG_Version, and so on).  The caller prints it:
 * this header includes no upstream header, so it has no con_printf.
 */
[[nodiscard]]
inline std::string version_number(const int packed)
{
	return std::to_string(SDL_VERSIONNUM_MAJOR(packed)) + '.' +
	       std::to_string(SDL_VERSIONNUM_MINOR(packed)) + '.' +
	       std::to_string(SDL_VERSIONNUM_MICRO(packed));
}

[[nodiscard]]
inline std::string versions_text(const std::string &name, const int compiled, const int linked)
{
	return name + ' ' + version_number(compiled) + "; loaded with " + name + ' ' +
	       version_number(linked);
}

}

#endif

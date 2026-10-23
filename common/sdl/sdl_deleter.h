/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <memory>

namespace dcx::sdl {

/* A std::unique_ptr deleter that hands the pointer to F, the SDL function
 * that releases it (SDL_DestroyWindow, SDL_GL_DeleteContext, ...).
 * unique_ptr calls it only for a non-null pointer.
 */
template <auto F>
struct sdl_deleter
{
	template <typename T>
	void operator()(T *const p) const
	{
		F(p);
	}
};

/* An owner of an SDL object of type T that F releases. */
template <typename T, auto F>
using sdl_unique_ptr = std::unique_ptr<T, sdl_deleter<F>>;

}

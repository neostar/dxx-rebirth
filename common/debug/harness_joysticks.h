/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include "common/debug/hidden_window.h"
#include "common/sdl/dxx-sdl.h"

#if SDL_MAJOR_VERSION >= 2

namespace dcx {

/* Before SDL_Init: in a run that asked to be hidden, set SDL's real
 * joystick backends to "0" (harness_joystick_hints_off), so the run opens
 * only the virtual joysticks its input script attaches.  The override
 * priority wins over the environment, which SDL reads hints from too, so no
 * variable turns a backend back on.  Debug harness builds only; SDL 1.2 has
 * no hints and no hidden runs.  The hints are the harness's, not the
 * game's, so they stay out of sdl_init.h's pre_init_hints.
 */
inline void harness_turn_off_real_joysticks()
{
	for (const char *const name : harness_joystick_hints_off(read_harness_hidden()))
		SDL_SetHintWithPriority(name, "0", SDL_HINT_OVERRIDE);
}

}

#endif

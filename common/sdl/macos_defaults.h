/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <optional>

namespace dcx::sdl {

#ifdef __APPLE__
/* Register NSApplicationUpdateCycleEnabled = NO as a default of this
 * process, as SDL 3.4.x's 6c2bedd5cb does in +registerEarlyUserDefaults
 * (docs/agents/sdl-window.md, SDL version): with the macOS 26 SDK, input
 * polling is held to the display's cadence unless the application opts out,
 * and the key only takes effect when set before NSApp exists, so before
 * SDL_Init.  Drop it when the SDL pin has that change.  The registration is
 * volatile: it writes no preferences file.
 * Defined in common/sdl/macos_defaults.mm.
 */
void register_early_user_defaults();

/* NSApplicationUpdateCycleEnabled as NSUserDefaults reads it, or nothing
 * when no default is set.
 */
[[nodiscard]]
std::optional<bool> update_cycle_default();
#else
inline void register_early_user_defaults()
{}
#endif

}

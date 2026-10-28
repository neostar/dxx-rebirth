/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * An SDL assertion handler for programs that must never show UI (#18).
 * SDL's default handler shows a modal dialog and waits (the #29 spike's
 * rtrace-gpu hung on "Viewport size exceeds current render target
 * dimensions"); SDL_ASSERT=abort calls abort(), which leaves a crash
 * report.  This handler prints the assertion and ends the process with
 * status 1 instead.
 */

#pragma once

#include "common/sdl/dxx-sdl.h"

namespace dcx::sdl {

/* Installs the handler: a failed SDL assertion prints
 * `SDL assertion failed: CONDITION (FILE:LINE)` to stderr and ends the
 * process with _Exit(1): no dialog, no abort, no destructors that could
 * fault on a device in mid-frame.
 */
void exit_on_sdl_assertion();

}

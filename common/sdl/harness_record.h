/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The debug harness's input recorder at the SDL seam (#99): event.cpp hands
 * each SDL event the game consumes to harness_record_input, which turns it
 * into an input script event, the inverse of the events harness_push_input
 * pushes.  Recording needs SDL 2 or SDL 3.  harness_record.cpp, linked into
 * dbg_harness builds alone, defines these.
 */

#pragma once

#include <expected>
#include <optional>

#include "common/debug/input_recorder.h"
#include "common/debug/input_script.h"
#include "common/sdl/dxx-sdl.h"
#if SDL_MAJOR_VERSION >= 2
#include "common/sdl/mouse_input.h"
#endif

namespace dcx {

/* Start the next harness tick of the run's recording (DXX_DBG_RECORD_INPUT):
 * count it, pace it to the wall clock when no script plays, and end the
 * run there when DXX_DBG_RECORD_INPUT_TICKS says so.  Nothing without the
 * variable.
 */
void harness_record_next_tick();

/* End the run at harness tick `tick`, as a script's end line does: every
 * frame hash line is already flushed, and whatever screen is up, the run
 * ends; a recording ends there too.
 */
[[noreturn]]
void harness_end_run(harness_tick tick);

#if SDL_MAJOR_VERSION >= 2
/* What the recorder carries from one event to the next.  SDL3: the
 * motion's fractions, carried as the game's mouse carries them, and the
 * character the last key down types when a replay pushes it
 * (harness_push_text).
 */
struct harness_record_state
{
#if SDL_MAJOR_VERSION == 3
	sdl::motion_accumulator motion;
	SDL_Keycode typed{};
#endif
};

/* The script event of an SDL event the game is about to consume at
 * harness tick `tick`: nullopt for an event that is no input or that the
 * game drops, unrecorded_input for input a script cannot hold.
 */
std::expected<std::optional<input_event>, unrecorded_input>
harness_recorded_event(const SDL_Event &event, harness_tick tick, harness_record_state &state);

/* Record `event`, which the game is about to consume, at the harness tick
 * under way; nothing without DXX_DBG_RECORD_INPUT.
 */
void harness_record_input(const SDL_Event &event);
#endif

}

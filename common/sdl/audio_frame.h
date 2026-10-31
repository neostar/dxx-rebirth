/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include "common/sdl/dxx-sdl.h"

#if SDL_MAJOR_VERSION == 3
namespace dcx::sdl {

/* What the event loop (event.cpp) hands the SDL3 build's audio, which
 * registers it while its device is open (digi_mixer.cpp) and clears it at
 * close: commit_frame at the start of every event_process, so a frame's
 * effects reach the mixer in menus and briefings too, where
 * digi_sync_sounds() does not run.  A hook, so that the programs linking
 * event.cpp without the audio (its tests) need none.
 */
struct audio_frame_hooks
{
	void (*commit_frame)(){nullptr};
};

inline audio_frame_hooks audio_hooks{};

inline void audio_commit_frame()
{
	if (audio_hooks.commit_frame)
		audio_hooks.commit_frame();
}

/* Commits at the end of its scope, whichever way digi_sync_sounds()
 * leaves: the frame's track changes reach the mixer in one batch.
 */
struct audio_frame_commit
{
	audio_frame_commit() = default;
	audio_frame_commit(const audio_frame_commit &) = delete;
	audio_frame_commit &operator=(const audio_frame_commit &) = delete;
	audio_frame_commit(audio_frame_commit &&) = delete;
	audio_frame_commit &operator=(audio_frame_commit &&) = delete;
	~audio_frame_commit()
	{
		audio_commit_frame();
	}
};

}
#endif

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <utility>

namespace dcx::sdl {

/* The game's finished-song hook of the SDL3 build (the D2 title song's
 * credits chain, the jukebox's next song), run on the game thread once per
 * frame from what the music track reported, never from the mixing thread
 * (docs/agents/sdl-audio.md area 4, rule 3).  A song's end runs the hook
 * once.  A song that could not start runs none and drops it, as the SDL2
 * build runs none when a song fails to load: mix_play_file returns 0 there
 * and the caller's chain stops.  Running it instead would let the jukebox
 * retry the next song every frame while its files fail.
 */
class music_hook
{
  public:
	using function = void (*)();
	/* The hook for the song mix_play_file just started. */
	void arm(const function f)
	{
		hook_ = f;
	}
	void disarm()
	{
		hook_ = nullptr;
	}
	[[nodiscard]]
	bool armed() const
	{
		return hook_ != nullptr;
	}
	/* After the frame's commit: ended, the song ended by itself; failed, it
	 * could not start.  The hook may start the next song, which arms its own.
	 */
	void frame(const bool ended, const bool failed)
	{
		if (failed)
			hook_ = nullptr;
		else if (ended)
			if (const auto f{std::exchange(hook_, nullptr)})
				f();
	}

  private:
	function hook_{nullptr};
};

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/music_hook.h"

#include "common/sdl/audio_gains.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth music hook
#include <boost/test/unit_test.hpp>

/* The seam: the hook dispatch and the volume kernel the SDL3 build's music
 * (digi_mixer_music.cpp) runs, without a mixer: what the music track
 * reported goes in, the hook's runs come out.
 */
namespace {

/* The calls of each hook the tests arm, reset per test. */
unsigned finished_calls;
unsigned chained_calls;
/* The hook chain_next arms, as the game's one music hook. */
dcx::sdl::music_hook chained;

void finished()
{
	++finished_calls;
}

/* A hook that starts the next song, as play_credits_track does. */
void chain_next()
{
	++chained_calls;
	chained.arm(&finished);
}

}

/* Test that a song's end runs its hook once, and a frame without an end
 * runs none.
 */
BOOST_AUTO_TEST_CASE(an_end_runs_the_hook_once)
{
	finished_calls = 0;
	dcx::sdl::music_hook hook;
	hook.arm(&finished);
	hook.frame(false, false);
	BOOST_TEST(finished_calls == 0u);
	hook.frame(true, false);
	BOOST_TEST(finished_calls == 1u);
	BOOST_TEST(!hook.armed());
	hook.frame(true, false);
	BOOST_TEST(finished_calls == 1u);
}

/* Test that a hook which starts the next song leaves that song's hook armed,
 * so a chain (title, then credits) goes on.
 */
BOOST_AUTO_TEST_CASE(a_hook_may_arm_the_next_song)
{
	finished_calls = 0;
	chained_calls = 0;
	chained.arm(&chain_next);
	chained.frame(true, false);
	BOOST_TEST(chained_calls == 1u);
	BOOST_TEST(chained.armed());
	chained.frame(true, false);
	BOOST_TEST(finished_calls == 1u);
}

/* Test that a song that could not start runs no hook and drops it, as a
 * failed load in the SDL2 build runs none.
 */
BOOST_AUTO_TEST_CASE(a_failed_start_drops_the_hook)
{
	finished_calls = 0;
	dcx::sdl::music_hook hook;
	hook.arm(&finished);
	hook.frame(false, true);
	BOOST_TEST(finished_calls == 0u);
	BOOST_TEST(!hook.armed());
	hook.frame(true, false);
	BOOST_TEST(finished_calls == 0u);
}

/* Test that the music volume follows the SDL2 build's
 * Mix_VolumeMusic(vol * MIX_MAX_VOLUME / 8) over 128 at every menu step, and
 * clamps above them as SDL2_mixer does.
 */
BOOST_AUTO_TEST_CASE(music_volume_follows_the_sdl2_volume)
{
	constexpr int mix_max_volume{128};
	/* The SDL2 build's vol *= MIX_MAX_VOLUME / 8, in integers. */
	constexpr int sdl2_step{mix_max_volume / 8};
	for (int vol{0}; vol <= 8; ++vol)
	{
		const int sdl2_volume{vol * sdl2_step};
		BOOST_TEST(dcx::sdl::music_volume_gain(vol) ==
		           static_cast<float>(sdl2_volume) / static_cast<float>(mix_max_volume));
	}
	BOOST_TEST(dcx::sdl::music_volume_gain(9) == 1.0f);
}

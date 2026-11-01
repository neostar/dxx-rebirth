/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * Header file for music playback through SDL_mixer
 *
 *  -- MD2211 (2006-04-24)
 */

#pragma once

#ifdef __cplusplus
namespace dcx {

int mix_play_music(const char *, int);
int mix_play_file(const char *, int, void (*)());
void mix_set_music_volume(int);
void mix_stop_music();
void mix_pause_music();
void mix_resume_music();
void mix_pause_resume_music();
void mix_free_music();

}
#if defined(SDL_MAJOR_VERSION) && SDL_MAJOR_VERSION == 3
namespace dcx::sdl {
class effect_mixer;
}
namespace dcx {

/* The SDL3 build's mixer, whose music track plays the songs, or nullptr
 * while audio is closed (digi_mixer.cpp).
 */
sdl::effect_mixer *mix_effect_mixer();
/* At open: logs the SDL3_mixer decoders and notes whether one plays MIDI. */
void mix_music_init();
/* Once per frame, after e's commit: runs the finished-song hook on the game
 * thread when a song ended.
 */
void mix_music_frame(sdl::effect_mixer &e);

}
#endif
#endif

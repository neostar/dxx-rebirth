/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * This is an alternate backend for the music system.
 * It uses SDL_mixer to provide a more reliable playback,
 * and allow processing of multiple audio formats.
 *
 *  -- MD2211 (2006-04-24)
 */

#include <span>
#include <SDL.h>
#include <SDL_mixer.h>
#if SDL_MAJOR_VERSION == 3
#include <array>
#include <string>
#include <utility>
#include <vector>
#include "args.h"
#include "console.h"
#include "digi_mixer_music.h"
#include "hmp.h"
#include "physfsx.h"
#include "strutil.h"
#include "common/sdl/audio_gains.h"
#include "common/sdl/effect_mixer.h"
#include "common/sdl/music_hook.h"
#include "common/sdl/music_source.h"

/* The music on SDL3_mixer (#46, docs/agents/sdl-audio.md area 6): a song,
 * built-in (HMP, which the game turns into MIDI), MIDI, Ogg Vorbis, FLAC or
 * MP3, plays on the music track of the effect mixer (digi_mixer.cpp), so it
 * mixes with the effects and ducks under the briefing voice.  MIDI
 * plays through FluidSynth with the SoundFont -soundfont names; SDL3_mixer
 * reads no environment variable for it.  The end of a song reaches the
 * game's hook on the game thread, from mix_music_frame, not from the mixing
 * thread.  ADLMIDI is not ported yet: an SDL3 build has none.
 */
namespace dcx {

namespace {

struct music_state
{
	sdl::music_hook hook;
	/* The song mix_play_file started, for the message when it cannot start. */
	std::string filename;
	/* 0..1, the menu's 0..8 as the SDL2 build sets it. */
	float volume{1.0f};
	bool midi_decoder{false};
};

music_state music;

/* Above any normal song: a guard against a very large allocation, as in the
 * SDL2 build.
 */
constexpr PHYSFS_sint64 music_file_limit{1024 * 1024 * 256};

std::vector<uint8_t> read_filesystem_file(const char *const path)
{
	auto data{sdl::read_whole_file(path, music_file_limit)};
	if (data)
		return std::move(data).value();
	if (data.error() == sdl::file_read_error::over_limit)
		con_printf(CON_NORMAL, "error: music file \"%s\" exceeds size limit of 256MB", path);
	return {};
}

/* A whole music file: a filesystem path, ~ for the home folder (the Mac OS X
 * jukebox's descent.m3u), or a PhysFS path, in the SDL2 build's order.
 */
std::vector<uint8_t> read_music_file(const char *const filename)
{
	if (auto data{read_filesystem_file(filename)}; !data.empty())
		return data;
	if (*filename == '~')
	{
		const auto sep{PHYSFS_getDirSeparator()};
		const auto lensep{strlen(sep)};
		std::array<char, PATH_MAX> full_path;
		snprintf(full_path.data(), full_path.size(), "%s%s", PHYSFS_getUserDir(), &filename[1 + (!strncmp(&filename[1], sep, lensep) ? lensep : 0)]);
		if (auto data{read_filesystem_file(full_path.data())}; !data.empty())
			return data;
	}
	RAIIPHYSFS_File f{PHYSFS_openRead(filename)};
	if (!f)
		return {};
	const auto len{PHYSFS_fileLength(f)};
	if (len <= 0 || len > music_file_limit)
	{
		con_printf(CON_NORMAL, "error: music PhysFS file \"%s\" is empty or exceeds size limit of 256MB", filename);
		return {};
	}
	std::vector<uint8_t> data(static_cast<std::size_t>(len));
	const auto got{PHYSFSX_readBytes(f, data.data(), len)};
	data.resize(got > 0 ? static_cast<std::size_t>(got) : 0);
	return data;
}

}

void mix_music_init()
{
	const auto decoders{sdl::audio_decoders()};
	std::string list;
	for (const auto &d : decoders)
	{
		if (!list.empty())
			list += ", ";
		list += d;
	}
	con_printf(CON_NORMAL, "SDL audio: decoders: %s", list.c_str());
	music.midi_decoder = sdl::has_midi_decoder(decoders);
	if (!music.midi_decoder)
		con_printf(CON_URGENT, "SDL audio: error: this SDL3_mixer build lists no MIDI decoder (FLUIDSYNTH or TIMIDITY): MIDI music will not play");
}

void mix_music_frame()
{
	auto *const e{mix_effect_mixer()};
	if (e == nullptr)
		return;
	const auto failed{e->take_music_failed()};
	if (failed)
		/* The SDL2 build's message for a song that does not load. */
		con_printf(CON_CRITICAL, "Music %s could not be loaded: %s", music.filename.c_str(), failed->c_str());
	music.hook.frame(e->take_music_ended(), failed.has_value());
}

int mix_play_file(const char *filename, int loop, void (*const entry_hook_finished_track)())
{
	mix_free_music();
	auto *const e{mix_effect_mixer()};
	if (e == nullptr)
		return 0;
	std::vector<uint8_t> data;
	if (const auto fptr{strrchr(filename, '.')}; fptr && !d_stricmp(fptr, ".hmp"))
	{
		auto &&[v, hoe]{hmp2mid(filename)};
		if (hoe != hmp_open_error::None)
			/* hmp2mid printed an error message. */
			return 0;
		data = std::move(v);
	}
	else
		data = read_music_file(filename);
	if (data.empty())
	{
		con_printf(CON_CRITICAL, "Music %s could not be loaded: not found or empty", filename);
		return 0;
	}
	const char *const soundfont{CGameArg.SndSoundFont.empty() ? nullptr : CGameArg.SndSoundFont.c_str()};
	auto loaded{sdl::load_music(e->mixer(), data, soundfont, music.midi_decoder)};
	if (!loaded.audio)
	{
		con_printf(CON_CRITICAL, "Music %s could not be loaded: %s", filename, loaded.error.c_str());
		return 0;
	}
	music.hook.arm(entry_hook_finished_track ? entry_hook_finished_track : mix_free_music);
	music.filename = filename;
	e->set_music_volume(music.volume);
	e->play_music(std::move(loaded.audio), loop != 0);
	return 1;
}

void mix_set_music_volume(int vol)
{
	music.volume = sdl::music_volume_gain(vol);
	if (auto *const e{mix_effect_mixer()})
		e->set_music_volume(music.volume);
}

/* Unlike the SDL2 build's Mix_HaltMusic, this runs no finished-song hook:
 * no caller relies on one (songs.cpp, jukebox.cpp), and running it here
 * would start the next song from inside a stop.  A jukebox stopped mid-song
 * may so pick its next song differently from the SDL2 build.
 */
void mix_stop_music()
{
	if (auto *const e{mix_effect_mixer()})
		e->stop_music();
}

void mix_pause_music()
{
	if (auto *const e{mix_effect_mixer()})
		e->pause_music();
}

void mix_resume_music()
{
	if (auto *const e{mix_effect_mixer()})
		e->resume_music();
}

void mix_pause_resume_music()
{
	auto *const e{mix_effect_mixer()};
	if (e == nullptr)
		return;
	if (e->music_paused())
		e->resume_music();
	else if (e->music_playing())
		e->pause_music();
}

void mix_free_music()
{
	music.hook.disarm();
	mix_stop_music();
}

}
#else
#include <string.h>
#include <stdlib.h>

#include "args.h"
#include "digi.h"
#include "hmp.h"
#include "adlmidi_dynamic.h"
#include "digi_mixer_music.h"
#include "strutil.h"
#include "u_mem.h"
#include "config.h"
#include "console.h"
#include "physfsrwops.h"

#if SDL_MIXER_MAJOR_VERSION == 1
/* SDL_mixer-1 is inconsistent in its memory management.  On success, it takes
 * ownership.  On failure, it may free the resource immediately or may leave it
 * allocated, with no indication which was done.  Therefore, for simplicity,
 * always manage the object in Rebirth code.
 */
#define DXX_USE_SDL_RWOPS_MANAGEMENT	0
#else
/* SDL_mixer-2 (and possibly later versions, though that has not been checked)
 * are consistent, and can handle the memory management internally.  For
 * completeness, provide a compile-time knob for Rebirth to manage this, but
 * default it to delegate to SDL_mixer.
 */
#ifndef DXX_USE_SDL_RWOPS_MANAGEMENT
#define DXX_USE_SDL_RWOPS_MANAGEMENT	1
#endif
#endif

namespace dcx {

namespace {

struct Music_delete
{
	static void operator()(Mix_Music *m)
	{
		Mix_FreeMusic(m);
	}
};

struct music_storage_buffer
{
	std::vector<uint8_t> musicbuf;
};

class current_music_t :
	/* Inherit from `music_storage_buffer` first, so that `musicbuf` is
	 * destroyed after all other members of `current_music_t`.  This ensures
	 * that a `Mix_Music` or `SDL_RWops` that refers to the buffer is destroyed
	 * before the buffer is freed.
	 */
	public music_storage_buffer,
#if !DXX_USE_SDL_RWOPS_MANAGEMENT
	/* If the memory management is not delegated to SDL, then an additional
	 * `std::unique_ptr` is needed to track and free the memory.  Based on code
	 * inspection, SDL_mixer does not appear to access the `SDL_RWops`
	 * structure while processing the `Mix_FreeMusic` call.  However, since
	 * logically the `Mix_Music` depends on the `SDL_RWops`, it seems safer to
	 * assume that `Mix_FreeMusic` might access the `SDL_RWops`, and arrange
	 * for the lifetime of `SDL_RWops` to exceed the lifetime of the
	 * `Mix_Music`.  Therefore, the `SDL_RWops` is stored earlier in the class
	 * than the `Mix_Music`, and is destroyed later.
	 *
	 * If the memory management is delegated to SDL, then no additional storage
	 * is needed.
	 */
	RWops_ptr,
#endif
	std::unique_ptr<Mix_Music, Music_delete>
{
	using music_pointer = std::unique_ptr<Mix_Music, Music_delete>;
public:
#if DXX_USE_SDL_RWOPS_MANAGEMENT
	/* Inherit the zero-argument form of `reset`, since the `reset()` method on
	 * this class would only be a call to `music_pointer::reset()`.  Prohibit
	 * use of the one-argument form of `reset`, since it would not be visible
	 * in the `#else` case.
	 */
	using music_pointer::reset;
	void reset(auto) = delete;
#else
	void reset()
	{
		music_pointer::reset();
		RWops_ptr::reset();
	}
#endif
	[[nodiscard]]
	uintptr_t reset(RWops_ptr rw);
	using music_pointer::operator bool;
	using music_pointer::get;
};

uintptr_t current_music_t::reset(RWops_ptr rw)
{
	if (!rw)
	{
		/* As a special case, exit early if rw is nullptr.  SDL is
		 * guaranteed to fail in this case, but will set an error message
		 * when it does so.  The error message about a nullptr SDL_RWops
		 * will replace any prior error message, which might have been more
		 * useful.
		 */
		reset();
		return 0;
	}
	const auto music{Mix_LoadMUSType_RW(rw.get(), MUS_NONE, DXX_USE_SDL_RWOPS_MANAGEMENT)};
	music_pointer::reset(music);
#if DXX_USE_SDL_RWOPS_MANAGEMENT
	/* The `SDL_RWops` is now owned by SDL_mixer, so clear the
	 * `std::unique_ptr` to avoid freeing the `SDL_RWops`.
	 *
	 * If the load failed, then the `SDL_RWops` has already been freed.
	 * If the load succeeded, then SDL_mixer will free the `SDL_RWops` when the
	 * `Mix_Music` is destroyed.
	 */
	rw.release();
#else
	if (music)
	{
		/* The `SDL_RWops` remains owned by Rebirth, but must remain alive
		 * until the `Mix_Music` is destroyed, so transfer the `SDL_RWops` into
		 * the class to extend its lifetime.
		 *
		 * The previous `Mix_Music`, if any, was freed by the
		 * `music_pointer::reset` above, so the previous `SDL_RWops`, if any,
		 * can now be freed safely.
		 */
		RWops_ptr::operator=(std::move(rw));
	}
	else
	{
		/* If `!music`, the SDL_RWops is owned by Rebirth, but not needed since
		 * there is no music to use it.  Allow it to expire at the end of the
		 * function.
		 *
		 * Clear the prior RWops_ptr, if any, since it corresponds to the prior
		 * music that was freed above.
		 */
		RWops_ptr::reset();
	}
#endif
	/* Return the pointer as an integer, because callers should only check for
	 * success versus failure, and should not dereference the `Mix_Music *`.
	 */
	return reinterpret_cast<uintptr_t>(music);
}

static current_music_t current_music;

static void mix_set_music_type_sdlmixer(int loop, void (*const hook_finished_track)())
{
	Mix_PlayMusic(current_music.get(), (loop ? -1 : 1));
	Mix_HookMusicFinished(hook_finished_track);
}

#if DXX_USE_ADLMIDI
static ADL_MIDIPlayer_t current_adlmidi;
static ADL_MIDIPlayer *get_adlmidi()
{
	if (!CGameCfg.ADLMIDI_enabled)
		return nullptr;
	ADL_MIDIPlayer *adlmidi = current_adlmidi.get();
	if (!adlmidi)
	{
		int sample_rate;
		Mix_QuerySpec(&sample_rate, nullptr, nullptr);
		adlmidi = adl_init(sample_rate);
		if (adlmidi)
		{
			adl_switchEmulator(adlmidi, ADLMIDI_EMU_DOSBOX);
			adl_setNumChips(adlmidi, CGameCfg.ADLMIDI_num_chips);
			adl_setBank(adlmidi, CGameCfg.ADLMIDI_bank);
			adl_setSoftPanEnabled(adlmidi, 1);
			current_adlmidi.reset(adlmidi);
		}
	}
	return adlmidi;
}

static void mix_adlmidi(void *udata, Uint8 *stream, int len);

static void mix_set_music_type_adl(int loop, void (*const hook_finished_track)())
{
	ADL_MIDIPlayer *adlmidi = get_adlmidi();
	adl_setLoopEnabled(adlmidi, loop);
	Mix_HookMusic(&mix_adlmidi, nullptr);
	Mix_HookMusicFinished(hook_finished_track);
}
#endif

enum class CurrentMusicType : uint8_t
{
	None,
#if DXX_USE_ADLMIDI
	ADLMIDI,
#endif
	SDLMixer,
};

static CurrentMusicType current_music_type{CurrentMusicType::None};

static CurrentMusicType load_mus_data(const char *filename, std::span<const uint8_t> data, int loop, void (*const hook_finished_track)());
static CurrentMusicType load_mus_file(const char *filename, int loop, void (*const hook_finished_track)());

#ifdef _WIN32
// Windows native-MIDI stuff.
static std::unique_ptr<hmp_file> cur_hmp;
static uint8_t digi_win32_midi_song_playing;
static uint8_t already_playing;
#endif

}

/*
 *  Plays a music file from an absolute path or a relative path
 */

int mix_play_file(const char *filename, int loop, void (*const entry_hook_finished_track)())
{
	mix_free_music();	// stop and free what we're already playing, if anything

	const auto hook_finished_track = entry_hook_finished_track ? entry_hook_finished_track : mix_free_music;
	// It's a .hmp!
	if (const auto fptr = strrchr(filename, '.'); fptr && !d_stricmp(fptr, ".hmp"))
	{
		if (auto &&[v, hoe] = hmp2mid(filename); hoe == hmp_open_error::None)
		{
			current_music_type = load_mus_data(filename, v, loop, hook_finished_track);
			if (current_music_type != CurrentMusicType::None)
			{
				current_music.musicbuf = std::move(v);
				return 1;
			}
		}
		else
			/* hmp2mid printed an error message, so there is no need for another one here */
			return 0;
	}

	// try loading music via given filename
	{
		current_music_type = load_mus_file(filename, loop, hook_finished_track);
		if (current_music_type != CurrentMusicType::None)
			return 1;
	}

	// allow the shell convention tilde character to mean the user's home folder
	// chiefly used for default jukebox level song music referenced in 'descent.m3u' for Mac OS X
	if (*filename == '~')
	{
		const auto sep = PHYSFS_getDirSeparator();
		const auto lensep = strlen(sep);
		std::array<char, PATH_MAX> full_path;
		snprintf(full_path.data(), PATH_MAX, "%s%s", PHYSFS_getUserDir(),
				 &filename[1 + (!strncmp(&filename[1], sep, lensep)
			? lensep
			: 0)]);
		current_music_type = load_mus_file(full_path.data(), loop, hook_finished_track);
		if (current_music_type != CurrentMusicType::None)
			return 1;
	}

	// still nothin'? Let's open via PhysFS in case it's located inside an archive
	{
		if (RAIIPHYSFS_File filehandle{PHYSFS_openRead(filename)})
		{
			const auto len{PHYSFS_fileLength(filehandle)};
			/* Set an arbitrary cap on how large a single music file can be.
			 * This should be high enough that no normal music file can trigger
			 * it.  The cap is intended to prevent creating a very large
			 * allocation.
			 */
			if (len > 1024u * 1024u * 256u)
			{
				con_printf(CON_NORMAL, "error: music PhysFS file \"%s\" exceeds size limit of 256MB", filename);
				current_music_type = CurrentMusicType::None;
				return 1;
			}
			/* `current_music.musicbuf` is unused after the `mix_free_music`
			 * above.  The contents of the memory managed by `musicbuf` are
			 * undefined, but the vector may have `capacity() > 0`.  Use that
			 * potentially allocated buffer here, which may save a new
			 * allocation.
			 */
			current_music.musicbuf.resize(len);
			const auto bufsize{PHYSFSX_readBytes(filehandle, current_music.musicbuf.data(), len)};
			/* Adjust size to match what is actually used by the loaded data.
			 * This should be the same length as was used above, but set it to
			 * be thorough.
			 */
			current_music.musicbuf.resize(bufsize);
			current_music_type = load_mus_data(filename, current_music.musicbuf, loop, hook_finished_track);
			if (current_music_type != CurrentMusicType::None)
				return 1;
		}
		else
			con_printf(CON_VERBOSE, "warning: failed to open PhysFS file \"%s\"", filename);
	}

	con_printf(CON_CRITICAL, "Music %s could not be loaded: %s", filename, Mix_GetError());
	mix_stop_music();

	return 0;
}

// What to do when stopping song playback
void mix_free_music()
{
	Mix_HaltMusic();
#if DXX_USE_ADLMIDI
	/* Only ADLMIDI can set a hook, so if ADLMIDI is compiled out, there is no
	 * need to clear the hook.
	 *
	 * When ADLMIDI is supported, clear unconditionally, instead of checking
	 * whether the music type requires it.
	 */
	Mix_HookMusic(nullptr, nullptr);
#endif
	current_music.reset();
#if DXX_HAVE_POISON_VALGRIND || !defined(NDEBUG)
	/* In a debug build, destroy the storage buffer, so that any underlying
	 * allocation is freed and any stale pointers become dangling.  This may
	 * help analysis tools recognize those pointers as dangling.
	 *
	 * In a non-debug build, let the buffer remain as it was.  There should not
	 * be any dangling pointers, but if there are, they will be less likely to
	 * crash if the buffer remains intact.  Also, letting the buffer remain may
	 * avoid a new allocation on the next music load.
	 */
	current_music.musicbuf = {};
#endif
	current_music_type = CurrentMusicType::None;
}

void mix_set_music_volume(int vol)
{
	vol *= MIX_MAX_VOLUME/8;
	Mix_VolumeMusic(vol);
}

void mix_stop_music()
{
	Mix_HaltMusic();
}

void mix_pause_music()
{
	Mix_PauseMusic();
}

void mix_resume_music()
{
	Mix_ResumeMusic();
}

void mix_pause_resume_music()
{
	if (Mix_PausedMusic())
		Mix_ResumeMusic();
	else if (Mix_PlayingMusic())
		Mix_PauseMusic();
}

namespace {

static CurrentMusicType load_mus_data(const char *const filename, const std::span<const uint8_t> data, int loop, void (*const hook_finished_track)())
{
#if DXX_USE_ADLMIDI
	const auto adlmidi = get_adlmidi();
	if (adlmidi && adl_openData(adlmidi, data.data(), data.size()) == 0)
	{
		mix_set_music_type_adl(loop, hook_finished_track);
		return CurrentMusicType::ADLMIDI;
	}
	else
#endif
	{
		if (current_music.reset(RWops_ptr{SDL_RWFromConstMem(data.data(), data.size())}))
		{
			mix_set_music_type_sdlmixer(loop, hook_finished_track);
			return CurrentMusicType::SDLMixer;
		}
		else
			con_printf(CON_VERBOSE, "warning: failed to load music from data from file \"%s\"", filename);
	}
	return CurrentMusicType::None;
}

static CurrentMusicType load_mus_file(const char *filename, int loop, void (*const hook_finished_track)())
{
#if DXX_USE_ADLMIDI
	const auto adlmidi = get_adlmidi();
	if (adlmidi && adl_openFile(adlmidi, filename) == 0)
	{
		mix_set_music_type_adl(loop, hook_finished_track);
		return CurrentMusicType::ADLMIDI;
	}
	else
#endif
	if (RWops_ptr rw{SDL_RWFromFile(filename, "rb")})
	{
		if (current_music.reset(std::move(rw)))
		{
			mix_set_music_type_sdlmixer(loop, hook_finished_track);
			return CurrentMusicType::SDLMixer;
		}
		else
			con_printf(CON_VERBOSE, "warning: failed to load music from filesystem file \"%s\"", filename);
	}
	else
		/* The caller speculatively tries to treat inputs variously as PhysFS
		 * files and filesystem files.  Failure to open this path as a
		 * filesystem file is not necessarily an error.
		 */
		con_printf(CON_VERBOSE, "warning: failed to open filesystem file \"%s\"", filename);
	return CurrentMusicType::None;
}

#if DXX_USE_ADLMIDI
static int16_t sat16(int32_t x)
{
	x = (x < INT16_MIN) ? INT16_MIN : x;
	x = (x > INT16_MAX) ? INT16_MAX : x;
	return x;
}

static void mix_adlmidi(void *, Uint8 *stream, int len)
{
	ADLMIDI_AudioFormat format;
	format.containerSize = sizeof(int16_t);
	format.sampleOffset = 2 * format.containerSize;
	format.type = ADLMIDI_SampleType_S16;

	ADL_MIDIPlayer *adlmidi = get_adlmidi();
	int sampleCount = len / format.containerSize;
	adl_playFormat(adlmidi, sampleCount, stream, stream + format.containerSize, &format);

	const auto samples = reinterpret_cast<int16_t *>(stream);
	const auto amplify = [](int16_t i) { return sat16(2 * i); };
	std::transform(samples, samples + sampleCount, samples, amplify);
}
#endif

}

#ifdef _WIN32
void digi_win32_pause_midi_song()
{
	hmp_pause(cur_hmp.get());
}

void digi_win32_resume_midi_song()
{
	hmp_resume(cur_hmp.get());
}

void digi_win32_stop_midi_song()
{
	if (!digi_win32_midi_song_playing)
		return;
	digi_win32_midi_song_playing = 0;
	cur_hmp.reset();
	hmp_reset();
}

void digi_win32_set_midi_volume( int mvolume )
{
	hmp_setvolume(cur_hmp.get(), mvolume*MIDI_VOLUME_SCALE/8);
}

int digi_win32_play_midi_song( const char * filename, int loop )
{
	if (!already_playing)
	{
		hmp_reset();
		already_playing = 1;
	}
	digi_win32_stop_midi_song();

	if (filename == NULL)
		return 0;

	if ((cur_hmp = std::get<0>(hmp_open(filename))))
	{
		/*
		 * FIXME: to be implemented as soon as we have some kind or checksum function - replacement for ugly hack in hmp.c for descent.hmp
		 * if (***filesize check*** && ***CRC32 or MD5 check***)
		 *	(((*cur_hmp).trks)[1]).data[6] = 0x6C;
		 */
		if (hmp_play(cur_hmp.get(),loop) != 0)
			return 0;	// error
		digi_win32_midi_song_playing = 1;
		digi_win32_set_midi_volume(CGameCfg.MusicVolume);
		return 1;
	}

	return 0;
}
#endif

}
#endif

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <SDL_mixer.h>
#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/sdl/audio_gains.h"
#include "common/sdl/dxx-sdl.h"
#include "common/sdl/sdl_deleter.h"

namespace dcx::sdl {

using mix_mixer_ptr = sdl_unique_ptr<MIX_Mixer, MIX_DestroyMixer>;
using mix_audio_ptr = sdl_unique_ptr<MIX_Audio, MIX_DestroyAudio>;
using mix_track_ptr = sdl_unique_ptr<MIX_Track, MIX_DestroyTrack>;

/* SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, whose definition is a C cast. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
inline constexpr SDL_AudioDeviceID default_playback_device{SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK};
#pragma GCC diagnostic pop

/* As many tracks as the SDL2 build has channels (digi_mixer.cpp). */
inline constexpr std::size_t effect_track_count{64};
inline constexpr const char *effect_tag{"sfx"};
inline constexpr const char *voice_tag{"voice"};

/* "name: F32 2 ch 48000 Hz", what the game logs of a device at open and on
 * each SDL_EVENT_AUDIO_DEVICE_* event (docs/agents/sdl-audio.md area 3).
 */
[[nodiscard]]
inline std::string audio_device_text(const char *const name, const SDL_AudioSpec &spec)
{
	std::string text{name != nullptr ? name : "(unnamed)"};
	text += ": ";
	text += SDL_GetAudioFormatName(spec.format);
	text += ' ';
	text += std::to_string(spec.channels);
	text += " ch ";
	text += std::to_string(spec.freq);
	text += " Hz";
	return text;
}

/* The sound effects and the briefing voice of the SDL3 build, on one
 * SDL3_mixer mixer: 64 effect tracks tagged "sfx" and two voice tracks
 * tagged "voice" (docs/agents/sdl-audio.md areas 4 and 5).
 *
 * The game thread calls everything here; SDL_mixer's mixing thread reads
 * only SDL_mixer's own state.  A call queues a command, and commit_frame,
 * once per frame, hands the frame's commands to the mixer: first
 * MIX_SetTrackAudio for each start, outside the mixer lock (its stream
 * creation was not traced for device locks), then one batch under
 * MIX_LockMixer holding only track calls (play, stop, stereo, gain, loops,
 * playing), so the mix sees a frame's changes at one sample frame and
 * SDL_mixer 3.2.4's unlocked mix loop never races them.  MIX_SetTrackAudio
 * goes only to a track that is not playing, which keeps the track's decoder
 * and input state out of the mix.  It does not keep out the track's
 * output_spec: 3.2.4's mix loop reads that of every track in a group,
 * playing or not, without the lock (SDL_mixer.c:581-588), and
 * MIX_SetTrackAudio rewrites it (SetTrackOutputStreamFormat, :174-181).
 * That read is SDL_mixer's own and is fixed upstream by 4aab76ed760c; the
 * pin move that takes it is #76's.  A track whose stop is queued is not
 * reused until the commit has stopped it, a new voice goes to the voice
 * track that is not playing, and a start that would still find its track
 * playing is dropped.  Whether a track still plays is polled in that batch
 * (MIX_TrackPlaying), not reported by a callback on the mixing thread.
 *
 * The mixer is a device's (MIX_CreateMixerDevice) in the game and a
 * generated one (MIX_CreateMixer, pulled by MIX_Generate) in the tests.
 */
class effect_mixer
{
  public:
	using track_index = uint8_t;
	/* Takes the mixer; with tracks it could not create, usable() is false
	 * and every call does nothing.
	 */
	explicit effect_mixer(mix_mixer_ptr m) : mixer_{std::move(m)}
	{
		if (!mixer_)
			return;
		for (auto &t : tracks_)
		{
			t.reset(MIX_CreateTrack(mixer_.get()));
			if (!t || !MIX_TagTrack(t.get(), effect_tag))
				return;
		}
		for (auto &v : voices_)
		{
			v.reset(MIX_CreateTrack(mixer_.get()));
			if (!v || !MIX_TagTrack(v.get(), voice_tag))
				return;
		}
		usable_ = true;
	}
	[[nodiscard]]
	bool usable() const
	{
		return usable_;
	}
	[[nodiscard]]
	MIX_Mixer *mixer() const
	{
		return mixer_.get();
	}
	/* Starts audio on the first of the first max_tracks tracks neither
	 * playing nor stopped in this frame, at gains, looping forever or once;
	 * nothing when there is none.
	 */
	std::optional<track_index> start(MIX_Audio *const audio, const stereo_gains gains,
	                                 const bool looping, const std::size_t max_tracks)
	{
		if (!usable_ || audio == nullptr)
			return std::nullopt;
		const std::size_t limit{std::min(max_tracks, effect_track_count)};
		for (std::size_t i{0}; i < limit; ++i)
		{
			if (playing_.test(i) || stopping_.test(i))
				continue;
			const auto t{static_cast<track_index>(i)};
			playing_.set(i);
			queue_.push_back({.kind = command_kind::play,
			                  .track = t,
			                  .audio = audio,
			                  .gains = gains,
			                  .looping = looping});
			return t;
		}
		return std::nullopt;
	}
	void set_gains(const track_index t, const stereo_gains gains)
	{
		if (usable_ && t < effect_track_count)
			queue_.push_back({.kind = command_kind::stereo, .track = t, .gains = gains});
	}
	void stop(const track_index t)
	{
		if (!usable_ || t >= effect_track_count || !playing_.test(t))
			return;
		playing_.reset(t);
		stopping_.set(t);
		queue_.push_back({.kind = command_kind::stop, .track = t});
	}
	/* Stops every track, the voice too, at once, as Mix_HaltChannel(-1)
	 * does: it commits, so a blocking stretch with no commit (a level load)
	 * does not keep a looping effect playing.
	 */
	void stop_all()
	{
		if (!usable_)
			return;
		for (std::size_t i{0}; i < effect_track_count; ++i)
			stop(static_cast<track_index>(i));
		stop_voice();
		commit_frame();
	}
	/* The gain of every track, effects and voice (the digi volume). */
	void set_volume(const float gain)
	{
		if (usable_)
			queue_.push_back({.kind = command_kind::volume, .gain = gain});
	}
	/* Whether the game started t and it has not ended, as of the last
	 * commit_frame.
	 */
	[[nodiscard]]
	bool playing(const track_index t) const
	{
		return t < effect_track_count && playing_.test(t);
	}
	/* Plays audio once, at unit stereo gains, on the voice track the
	 * playing voice is not on, and stops that one in the same batch; the
	 * mixer keeps the audio until another voice replaces it.
	 */
	void play_voice(mix_audio_ptr audio)
	{
		if (!usable_ || !audio)
			return;
		/* An earlier voice of this frame never plays: the new one takes
		 * its track, which the frame has not given audio yet.
		 */
		if (std::erase_if(queue_,
		                  [](const command &c) { return c.kind == command_kind::voice_play; }) == 0)
		{
			queue_.push_back({.kind = command_kind::voice_stop, .track = voice_});
			voice_ = static_cast<track_index>(voice_ ^ 1u);
		}
		voice_audio_[voice_] = std::move(audio);
		voice_playing_ = true;
		queue_.push_back({.kind = command_kind::voice_play,
		                  .track = voice_,
		                  .audio = voice_audio_[voice_].get()});
	}
	void stop_voice()
	{
		if (!usable_)
			return;
		voice_playing_ = false;
		queue_.push_back({.kind = command_kind::voice_stop, .track = voice_});
	}
	[[nodiscard]]
	bool voice_playing() const
	{
		return voice_playing_;
	}
	/* Hands the queued commands to the mixer, as described above. */
	void commit_frame()
	{
		if (!usable_)
			return;
		for (auto &c : queue_)
			if (c.kind == command_kind::play || c.kind == command_kind::voice_play)
			{
				MIX_Track *const t{c.kind == command_kind::play ? tracks_[c.track].get()
				                                                : voices_[c.track].get()};
				/* Only the game starts a track, so one not playing now stays
				 * so until the batch plays it.  Not playing keeps the decoder
				 * and input state out of the mix, not output_spec (see the
				 * class comment).
				 */
				if (MIX_TrackPlaying(t) || !MIX_SetTrackAudio(t, c.audio))
					c.audio = nullptr;
			}
		MIX_Mixer *const m{mixer_.get()};
		MIX_LockMixer(m);
		for (const auto &c : queue_)
			apply(m, c);
		for (std::size_t i{0}; i < effect_track_count; ++i)
			if (playing_.test(i) && !MIX_TrackPlaying(tracks_[i].get()))
				playing_.reset(i);
		if (voice_playing_ && !MIX_TrackPlaying(voices_[voice_].get()))
			voice_playing_ = false;
		MIX_UnlockMixer(m);
		queue_.clear();
		stopping_.reset();
	}

  private:
	enum class command_kind : uint8_t
	{
		play,
		stop,
		stereo,
		volume,
		voice_play,
		voice_stop,
	};
	struct command
	{
		command_kind kind;
		track_index track{0};
		MIX_Audio *audio{nullptr};
		stereo_gains gains{};
		bool looping{false};
		float gain{0};
	};
	static void set_stereo(MIX_Track *const t, const stereo_gains g)
	{
		const MIX_StereoGains gains{.left = g.left, .right = g.right};
		MIX_SetTrackStereo(t, &gains);
	}
	void apply(MIX_Mixer *const m, const command &c)
	{
		switch (c.kind)
		{
		case command_kind::play: {
			if (c.audio == nullptr)
				break;
			MIX_Track *const t{tracks_[c.track].get()};
			set_stereo(t, c.gains);
			MIX_PlayTrack(t, 0);
			/* After the play, which sets the loop count from its options. */
			MIX_SetTrackLoops(t, c.looping ? -1 : 0);
			break;
		}
		case command_kind::stop:
			MIX_StopTrack(tracks_[c.track].get(), 0);
			break;
		case command_kind::stereo:
			set_stereo(tracks_[c.track].get(), c.gains);
			break;
		case command_kind::volume:
			MIX_SetTagGain(m, effect_tag, c.gain);
			MIX_SetTagGain(m, voice_tag, c.gain);
			break;
		case command_kind::voice_play:
			if (c.audio != nullptr)
				MIX_PlayTrack(voices_[c.track].get(), 0);
			break;
		case command_kind::voice_stop:
			MIX_StopTrack(voices_[c.track].get(), 0);
			break;
		}
	}
	/* Declared first, destroyed last: the tracks go before their mixer. */
	mix_mixer_ptr mixer_;
	std::array<mix_audio_ptr, 2> voice_audio_{};
	std::array<mix_track_ptr, effect_track_count> tracks_{};
	std::array<mix_track_ptr, 2> voices_{};
	std::vector<command> queue_;
	std::bitset<effect_track_count> playing_;
	/* Tracks whose stop the next commit applies. */
	std::bitset<effect_track_count> stopping_;
	/* The voice track of the last voice. */
	track_index voice_{0};
	bool voice_playing_{false};
	bool usable_{false};
};

}

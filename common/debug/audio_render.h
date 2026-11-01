/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "maths.h"
#include "tools/dev/upstream_end.h"
// clang-format on

namespace dcx {

/* The harness's deterministic audio render (DXX_DBG_AUDIO_RENDER=path, SDL3
 * only): the effects and music mix on a mixer with no device, and each game
 * frame renders the audio its FrameTime covers into the file at path, as
 * signed 16-bit little-endian stereo at audio_render_rate.  With
 * DXX_DBG_FIXED_FRAMETIME the file depends on the game's frames alone, not
 * on the wall clock, so two runs write the same bytes (clip-check.sh).
 */
inline constexpr int audio_render_rate{44100};
inline constexpr int audio_render_channels{2};

/* The path DXX_DBG_AUDIO_RENDER names, or nullptr when it is unset or
 * empty.
 */
[[nodiscard]]
inline const char *read_harness_audio_render()
{
	const char *const v{std::getenv("DXX_DBG_AUDIO_RENDER")};
	return v != nullptr && *v != '\0' ? v : nullptr;
}

/* How many frames of audio each game frame renders: its FrameTime (a fix,
 * F1_0 a second) at rate, the fraction of a frame carried to the next game
 * frame, so the total never drifts from the game time.
 */
class audio_render_clock
{
	std::int64_t carry_{0};

  public:
	static constexpr std::int64_t one_second{F1_0};
	[[nodiscard]]
	std::size_t frames_due(const std::int32_t frame_time, const int rate)
	{
		if (frame_time <= 0 || rate <= 0)
			return 0;
		const std::int64_t total{std::int64_t{frame_time} * rate + carry_};
		carry_ = total % one_second;
		return static_cast<std::size_t>(total / one_second);
	}
};

/* The file the render writes, closed with the sink. */
class audio_render_sink
{
	struct file_closer
	{
		void operator()(std::FILE *const f) const
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the unique_ptr owns f; no gsl
			(void)std::fclose(f);
		}
	};
	std::unique_ptr<std::FILE, file_closer> file_;

  public:
	explicit audio_render_sink(const char *const path) : file_{std::fopen(path, "wb")}
	{}
	[[nodiscard]]
	bool is_open() const
	{
		return file_ != nullptr;
	}
	/* Appends samples; false when the file is not open or the write
	 * failed.
	 */
	bool write(const std::span<const std::int16_t> samples)
	{
		// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-libc-call): the span's own size
		return file_ && std::fwrite(samples.data(), sizeof(std::int16_t), samples.size(),
		                            file_.get()) == samples.size();
	}
};

/* The render: each game frame that advanced the game time renders the audio
 * its frame time covers, once, however often the frame commits.
 */
class audio_render
{
	audio_render_sink sink_;
	audio_render_clock clock_;
	std::int64_t rendered_game_time_;
	std::vector<std::int16_t> buffer_;

  public:
	audio_render(const char *const path, const std::int64_t game_time)
	    : sink_{path}, rendered_game_time_{game_time}
	{}
	[[nodiscard]]
	bool is_open() const
	{
		return sink_.is_open();
	}
	/* generate(span) fills the span with the mix, true on success; false
	 * when it or the write failed.
	 */
	template <typename Generate>
	bool render_frame(const std::int64_t game_time, const std::int32_t frame_time,
	                  Generate &&generate)
	{
		if (game_time == rendered_game_time_)
			return true;
		rendered_game_time_ = game_time;
		const auto frames{clock_.frames_due(frame_time, audio_render_rate)};
		if (frames == 0)
			return true;
		buffer_.resize(frames * audio_render_channels);
		return generate(std::span<std::int16_t>{buffer_}) && sink_.write(buffer_);
	}
};

}

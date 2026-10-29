/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Trace recording (#18): a decorator around the render device that passes
 * every call on and writes the calls of a chosen frame range as a trace
 * (trace.h), so any game run can be replayed without the game by rtrace
 * and rtrace-gpu.  Recording is off unless the front end makes a
 * recorder: front_device then hands out the device itself, so no
 * decorator sits in the call path.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string_view>

#include "common/render/device.h"
#include "common/render/render_device.h"
#include "common/render/trace.h"

namespace dcx::render {

/* The frames a recording keeps, first to last inclusive, numbered by
 * presents from the start of the run, 0 first.  The frame hash log counts
 * from 0 again in each section, so a frame's number here is its place
 * among the log's frame lines, its section headers left out.
 */
struct frame_range
{
	std::uint32_t first;
	std::uint32_t last;
	constexpr bool operator==(const frame_range &) const = default;
};

/* Where a recorder puts its trace: each call appends the bytes written
 * since the last, the header with the first frame's.  It is called once
 * per recorded frame, at its present.
 */
using trace_out = std::function<void(std::span<const std::uint8_t>)>;

/* The size of the render target now, read when the recording starts. */
using target_size = std::function<extent()>;

/* Why a recording ended before its last frame. */
enum class stop_reason : std::uint8_t
{
	/* A call the trace format cannot hold: a texture side over
	 * trace::max_texture_side, a draw over its vertex limit.
	 */
	trace_limit,
	/* The render target changed size in range (a mode change): a trace
	 * has one size, in its header.
	 */
	target_resized,
};

/* Told why a recording ended early, and the detail: the harness ends the
 * run for a trace_limit and logs a target_resized (harness_front).  The trace keeps the frames
 * before; every call still reaches the device.
 */
using trace_stop = std::function<void(stop_reason, std::string_view)>;

/* Passes every call on to the inner device and records the calls of the
 * frames in range.  Until the range starts it keeps a copy of every slot's
 * image, so that the trace starts with the textures its draws sample.
 * Reads, eye selection and region copies are passed on unrecorded: the
 * trace format holds none of them.
 */
class recording_device final : public device
{
	device *inner_;
	frame_range range_;
	target_size target_;
	trace_out out_;
	trace_stop stop_;
	/* The index of the frame being drawn. */
	std::uint32_t frame_{0};
	/* Each slot's image, by slot index, until the recording starts. */
	std::map<std::uint32_t, trace::texture> textures_;
	/* While recording. */
	std::optional<trace::writer> writer_;
	/* The size in the trace's header, read when the recording started. */
	extent recorded_target_{};
	/* Once the last frame in range is handed out. */
	bool done_{false};
	/* Records the frame starting now when it is in range. */
	void start_if_due();
	/* Runs write, a call to the writer, and ends the recording with a
	 * trace_limit when the writer refuses it, so that no exception passes
	 * through the game.
	 */
	template <typename F>
	void guarded(F &&write);
	/* Ends the recording before its last frame and tells stop_ why. */
	void stop(stop_reason r, std::string_view why);

  public:
	/* Throws std::invalid_argument for a range whose last frame comes
	 * before its first.
	 */
	recording_device(device &inner, frame_range range, target_size target, trace_out out,
	                 trace_stop stop = {});
	/* Passes the calls from now on to inner, a device that took the
	 * textures of the one before (a new GL context), and reports its caps.
	 */
	void rebind(device &inner)
	{
		inner_ = &inner;
		take_caps(inner);
	}
	/* Whether the last frame in range was recorded and handed out. */
	[[nodiscard]]
	bool finished() const
	{
		return done_;
	}
	void draw(const draw_call &call) override;
	void texture(texture_slot slot, const texture_image *image) override;
	void clear(clear_targets targets) override;
	void present() override;
	void read_pixels(read_source source, const read_rect &rect, pixel_format format,
	                 std::span<std::uint8_t> pixels) override;
	void select_eye(eye e) override;
	void copy_region(const copy_rect &rect, bool from_left_eye) override;
};

/* The device the front end draws through: inner itself when recorder is
 * null (recording off), else recorder, around inner from now on.
 */
[[nodiscard]]
inline device &front_device(device &inner, recording_device *const recorder)
{
	if (!recorder)
		return inner;
	recorder->rebind(inner);
	return *recorder;
}

/* Releases slot's image through front, the device the front end draws
 * through; nothing once front is null, after the devices are gone.
 */
inline void release_texture(device *const front, const texture_slot slot)
{
	if (front)
		front->texture(slot, nullptr);
}

/* Nulls the front end's device pointer when destroyed.  A static one
 * declared after the devices front points into is destroyed before them
 * at exit, so the texture releases that later static destructors make
 * (the fonts') find no device instead of a destroyed one.
 */
class front_reset
{
	device *&front_;

  public:
	explicit front_reset(device *&front) : front_{front}
	{}
	~front_reset()
	{
		front_ = nullptr;
	}
	front_reset(const front_reset &) = delete;
	front_reset &operator=(const front_reset &) = delete;
	front_reset(front_reset &&) = delete;
	front_reset &operator=(front_reset &&) = delete;
};

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The render device (ADR 2, GLOSSARY.md): the seam between the front end
 * and a renderer.  Its interface is draw, texture and three frame
 * operations (clear, present, read_pixels); a draw call carries everything
 * it depends on, so no state carries from one draw to the next.  Adapters:
 * GL (gl_device.h), SDL_GPU (sdlgpu_device.h: headless, clear and
 * readback so far, #24), and later recording and fake.
 */

#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>

#include "common/render/caps.h"
#include "common/render/readback.h"
#include "common/render/render_device.h"

namespace dcx::render {

/* What a clear clears, of the whole target: colour to (0, 0, 0, 0), depth
 * to 1.  The game clears depth alone in mid-frame for the rear view.
 */
struct clear_targets
{
	bool color;
	bool depth;
	constexpr bool operator==(const clear_targets &) const = default;
};

/* An eye of quad-buffer stereo. */
enum class eye : std::uint8_t
{
	left,
	right,
};

/* A rectangle of the target copied to another place of it, in pixels with
 * the origin at the bottom left (as viewport).
 */
struct copy_rect
{
	std::int32_t src_x;
	std::int32_t src_y;
	std::int32_t dest_x;
	std::int32_t dest_y;
	render::extent size;
	constexpr bool operator==(const copy_rect &) const = default;
};

/* All calls run on the thread that created the device.  The caps are data
 * the adapter fixes when it is created, not an entry point.
 */
class device
{
	render::caps caps_;

  public:
	explicit device(const render::caps &c) : caps_{c}
	{}
	virtual ~device() = default;
	/* Draws call.  Its vertices are read during the call only.  A call
	 * with no vertices draws nothing: an adapter or a replay may skip it.
	 */
	virtual void draw(const draw_call &call) = 0;
	/* Gives slot the image, which later draws sample; a null image releases
	 * the slot.
	 */
	virtual void texture(texture_slot slot, const texture_image *image) = 0;
	virtual void clear(clear_targets targets) = 0;
	/* Shows what was drawn since the last present. */
	virtual void present() = 0;
	/* Reads rect of source into pixels, its top row first (readback.h).
	 * Throws std::length_error when pixels is not image_bytes(rect.size,
	 * format) long.
	 */
	virtual void read_pixels(read_source source, const read_rect &rect, pixel_format format,
	                         std::span<std::uint8_t> pixels) = 0;
	/* Sends the draws that follow to the eye's buffers.  Needs
	 * caps().quad_buffer_stereo; throws std::logic_error without it.
	 */
	virtual void select_eye(eye e)
	{
		(void)e;
		throw std::logic_error("device::select_eye: no quad_buffer_stereo cap");
	}
	/* Copies rect of the target; with from_left_eye, from the left eye's
	 * buffer to the right eye's.  Needs caps().copy_region (and
	 * quad_buffer_stereo for from_left_eye); throws std::logic_error without
	 * them.
	 */
	virtual void copy_region(const copy_rect &rect, bool from_left_eye)
	{
		(void)rect, (void)from_left_eye;
		throw std::logic_error("device::copy_region: no copy_region cap");
	}
	[[nodiscard]]
	const render::caps &caps() const
	{
		return caps_;
	}
};

}

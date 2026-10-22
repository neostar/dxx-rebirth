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
 * GL (gl_device.h), and later SDL_GPU, recording and fake.
 */

#pragma once

#include <cstdint>
#include <span>

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
	/* Draws call.  Its vertices are read during the call only. */
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
	[[nodiscard]]
	const render::caps &caps() const
	{
		return caps_;
	}
};

}

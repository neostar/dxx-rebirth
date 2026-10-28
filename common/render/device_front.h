/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The device the GL front end (similar/arch/ogl/ogl.cpp) draws through.
 * A dbg_harness build may record a trace (#18): its front is
 * harness_front (common/debug/trace_harness.h), a recorder around the GL
 * device when DXX_DBG_TRACE asks for one.  Any other build has no
 * recorder code at all: its front is the GL device itself, by its final
 * type, so the calls through it stay direct.
 */

#pragma once

#include <stdexcept>

#include "common/render/device.h"
#include "common/render/gl_device.h"
#include "common/render/render_device.h"

#if DXX_USE_DBG_HARNESS
#include "common/debug/trace_harness.h"

// clang-format off
#include "tools/dev/upstream_begin.h"
#include "console.h"
#include "dxxerror.h"
#include "tools/dev/upstream_end.h"
// clang-format on
#endif

namespace dcx::render {

/* The size of the game's screen now, for a recording's header. */
using screen_size = extent (*)();

#if DXX_USE_DBG_HARNESS
/* harness_front, reporting through the game: UserError and the console. */
class device_front : public harness_front
{
  public:
	device_front()
	    : harness_front{{.fail = [](const char *const m) { UserError("%s", m); },
		                 .note = [](const char *const m) { con_printf(CON_NORMAL, "%s", m); }},
		                read_harness_trace()}
	{}
	void bind(gl_device &inner, const screen_size size)
	{
		harness_front::bind(inner, size);
	}
};
#else
class device_front
{
	/* Static, outside the object: GCC's lifetime dead-store elimination
	 * drops a store to a member in its object's destructor (#18), so
	 * a null stored there would not reach the destructors that run later.
	 */
	static inline gl_device *front{nullptr};

  public:
	device_front() = default;
	/* Destroyed before the device (declared after it) at exit, so the
	 * texture releases that later static destructors make (the fonts')
	 * reach no device: the null is stored outside the object, and
	 * release_texture reads it without the destroyed object.
	 */
	~device_front()
	{
		front = nullptr;
	}
	device_front(const device_front &) = delete;
	device_front &operator=(const device_front &) = delete;
	device_front(device_front &&) = delete;
	device_front &operator=(device_front &&) = delete;
	void bind(gl_device &inner, screen_size)
	{
		front = &inner;
	}
	/* Throws std::logic_error before the first bind. */
	[[nodiscard]]
	gl_device &get() const
	{
		if (!front) [[unlikely]]
			throw std::logic_error("ogl_device: no render device before gr_set_mode");
		return *front;
	}
	static void release_texture(const texture_slot slot)
	{
		if (front)
			front->texture(slot, nullptr);
	}
};
#endif

}

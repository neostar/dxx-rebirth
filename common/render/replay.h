/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Replays a trace (trace.h) through a render device and reads back each
 * frame it presents, so rtrace compares frames, never calls
 * (docs/agents/testing-games.md).
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <vector>

#include "common/render/device.h"
#include "common/render/render_device.h"
#include "common/render/trace.h"

namespace dcx::render {

/* One presented frame of a replay: its RGB pixels, top row first, as the
 * frame hash log reads them, and their hash (common/debug/frame_hash.h).
 */
struct replayed_frame
{
	/* The present record's frame index. */
	std::uint32_t frame;
	render::extent size;
	std::vector<std::uint8_t> rgb;
	std::uint64_t hash;
};

using frame_sink = std::function<void(const replayed_frame &)>;

/* Replays every record of r through d: clears, textures and draws as they
 * come, and at each present reads back the image being drawn, hands it to
 * sink and then presents it.  d draws to a target of r's header size.
 * Stops at the first malformed record, after the frames before it.
 */
[[nodiscard]]
std::expected<void, trace::read_error> replay(trace::reader &r, device &d, const frame_sink &sink);

}

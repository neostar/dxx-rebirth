/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * What rtrace and rtrace-gpu share: replaying a trace file through a render
 * device and logging one frame hash line per presented frame.
 */

#pragma once

#include <functional>
#include <ostream>
#include <string_view>

#include "common/render/device.h"
#include "common/render/render_device.h"

namespace dcx::render::rtrace {

/* Makes a device drawing to a target of the given size and hands it to the
 * function for the replay's length.
 */
using device_host = std::function<void(extent, const std::function<void(device &)> &)>;

/* Replays the trace at path through host's device, writing the frame hash
 * lines to log: 0 when every record replayed and a frame was presented, 1
 * otherwise, after the frames before the failure.  With frames_dir not
 * empty, each frame's RGB pixels, top row first (the bytes its line
 * hashes), also go to frames_dir/FRAME.rgb, FRAME its index; a file that
 * cannot be written fails the replay.  Errors go to stderr, after the
 * program's name.
 */
[[nodiscard]]
int replay_file(std::string_view path, std::ostream &log, const device_host &host,
                std::string_view name, std::string_view frames_dir);

/* The whole program: `name TRACE [LOG]`, the lines to LOG, created or
 * emptied first, or to stdout, and with RTRACE_FRAMES_DIR set, each frame
 * to that directory (replay_file); 2 on a usage error, else replay_file's
 * status, 1 when LOG cannot be written or an exception escapes.
 */
[[nodiscard]]
int run(int argc, char **argv, const char *name, const device_host &host);

}

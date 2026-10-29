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

/* What a replay writes beside its frame hash lines.  With frames_dir not
 * empty, each frame's RGB pixels, top row first (the bytes its line
 * hashes), go to frames_dir/FRAME.rgb, FRAME its index; a file that cannot
 * be written fails the replay.  With game_log, each frame's line also goes
 * there with the hash of its rows bottom first, the order GL reads them in
 * and the game's frame hash log hashes them in (DXX_DBG_FRAMEHASH), so a
 * replay's frames compare with the game's line by line.
 */
struct replay_outputs
{
	std::string_view frames_dir;
	std::ostream *game_log;
};

/* Replays the trace at path through host's device, writing the frame hash
 * lines, rows top first, to log, and outputs' files: 0 when every record
 * replayed and a frame was presented, 1 otherwise, after the frames before
 * the failure.  Errors go to stderr, after the program's name.
 */
[[nodiscard]]
int replay_file(std::string_view path, std::ostream &log, const device_host &host,
                std::string_view name, const replay_outputs &outputs);

/* The whole program: `name TRACE [LOG]`, the lines to LOG, created or
 * emptied first, or to stdout; with RTRACE_FRAMES_DIR set, each frame to
 * that directory, and with RTRACE_GAME_LOG set, the game-order lines to
 * that file, created or emptied first (replay_outputs); 2 on a usage
 * error, else replay_file's status, 1 when a log cannot be written or an
 * exception escapes.
 */
[[nodiscard]]
int run(int argc, char **argv, const char *name, const device_host &host);

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * rtrace: replays a trace (.rtr, common/render/trace.h) through the GL
 * adapter on a hidden window and prints one frame hash log line per
 * presented frame (common/debug/frame_hash.h), without the game.
 *
 *   rtrace TRACE [LOG]
 *
 * The lines go to LOG, created or emptied first, or to stdout without one:
 * a wrapper such as tools/dev/leaks.sh keeps the program's stdout.
 *
 * Exit status 0 when every record replayed and at least one frame was
 * presented, 1 on an unreadable or malformed trace, a trace without a
 * present or a failed replay (after the frames before it), 2 on a usage
 * error.  Run it through tools/dev/no-window.sh (gl_host.h).
 */

#include <functional>

#include "common/render/device.h"
#include "common/render/render_device.h"
#include "common/render/rtrace/gl_host.h"
#include "common/render/rtrace/replay_file.h"

int main(const int argc, char **const argv)
{
	namespace render = dcx::render;
	return render::rtrace::run(
	    argc, argv, "rtrace",
	    [](const render::extent target, const std::function<void(render::device &)> &use) {
		    render::rtrace::gl_host host{target};
		    use(host.device());
	    });
}

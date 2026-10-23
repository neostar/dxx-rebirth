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

#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/debug/frame_hash.h"
#include "common/render/replay.h"
#include "common/render/rtrace/gl_host.h"
#include "common/render/trace.h"

namespace {

namespace render = dcx::render;

[[nodiscard]]
int replay_file(const std::string_view path, std::ostream &log)
{
	std::ifstream in{std::string{path}, std::ios::binary};
	if (!in)
	{
		std::cerr << "rtrace: cannot open " << path << '\n';
		return 1;
	}
	const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{in},
	                                      std::istreambuf_iterator<char>{}};
	if (in.bad())
	{
		std::cerr << "rtrace: cannot read " << path << '\n';
		return 1;
	}
	render::trace::reader r{bytes};
	const auto h{r.header()};
	if (!h)
	{
		std::cerr << "rtrace: " << path << ": " << render::trace::to_string(h.error())
		          << " header\n";
		return 1;
	}
	/* The frame hash log gives each side in 16 bits. */
	constexpr std::uint32_t max_side{std::numeric_limits<std::uint16_t>::max()};
	if (const auto t{h.value().target}; t.width > max_side || t.height > max_side)
	{
		std::cerr << "rtrace: " << path << ": a target side over " << max_side << " pixels\n";
		return 1;
	}
	render::rtrace::gl_host host{h.value().target};
	unsigned frames{0};
	const auto done{
	    render::replay(r, host.device(), [&log, &frames](const render::replayed_frame &f) {
		    ++frames;
		    const auto line{
		        dcx::frame_hash_line(dcx::frame_index{f.frame},
				                     {.width = static_cast<std::uint16_t>(f.size.width),
				                      .height = static_cast<std::uint16_t>(f.size.height)},
				                     f.hash)};
		    log << line;
	    })};
	if (!done)
	{
		std::cerr << "rtrace: " << path << ": " << render::trace::to_string(done.error())
		          << " at byte " << r.offset() << '\n';
		return 1;
	}
	if (frames == 0)
	{
		std::cerr << "rtrace: " << path << ": no frame presented\n";
		return 1;
	}
	if (!log.flush())
	{
		std::cerr << "rtrace: cannot write the frame hashes\n";
		return 1;
	}
	return 0;
}

}

int main(const int argc, char **const argv)
{
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-container): argv holds argc pointers
	const std::span<char *const> args{argv, static_cast<std::size_t>(argc)};
	if (args.size() != 2 && args.size() != 3)
	{
		std::cerr << "usage: rtrace TRACE [LOG]\n";
		return 2;
	}
	try
	{
		if (args.size() == 2)
			return replay_file(args[1], std::cout);
		std::ofstream log{args[2], std::ios::binary | std::ios::trunc};
		if (!log)
		{
			std::cerr << "rtrace: cannot create " << args[2] << '\n';
			return 1;
		}
		const int rc{replay_file(args[1], log)};
		log.close();
		if (rc == 0 && !log)
		{
			std::cerr << "rtrace: cannot write " << args[2] << '\n';
			return 1;
		}
		return rc;
	}
	catch (const std::exception &e)
	{
		std::cerr << "rtrace: " << e.what() << '\n';
		return 1;
	}
}

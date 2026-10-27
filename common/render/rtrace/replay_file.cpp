/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/rtrace/replay_file.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
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
#include "common/render/device.h"
#include "common/render/replay.h"
#include "common/render/trace.h"

namespace dcx::render::rtrace {

int replay_file(const std::string_view path, std::ostream &log, const device_host &host,
                const std::string_view name)
{
	std::ifstream in{std::string{path}, std::ios::binary};
	if (!in)
	{
		std::cerr << name << ": cannot open " << path << '\n';
		return 1;
	}
	const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{in},
	                                      std::istreambuf_iterator<char>{}};
	if (in.bad())
	{
		std::cerr << name << ": cannot read " << path << '\n';
		return 1;
	}
	render::trace::reader r{bytes};
	const auto h{r.header()};
	if (!h)
	{
		std::cerr << name << ": " << path << ": " << render::trace::to_string(h.error())
		          << " header\n";
		return 1;
	}
	/* The frame hash log gives each side in 16 bits. */
	constexpr std::uint32_t max_side{std::numeric_limits<std::uint16_t>::max()};
	if (const auto t{h.value().target}; t.width > max_side || t.height > max_side)
	{
		std::cerr << name << ": " << path << ": a target side over " << max_side << " pixels\n";
		return 1;
	}
	unsigned frames{0};
	std::expected<void, render::trace::read_error> done;
	host(h.value().target, [&](render::device &d) {
		done = render::replay(r, d, [&log, &frames](const render::replayed_frame &f) {
			++frames;
			const auto line{
			    dcx::frame_hash_line(dcx::frame_index{f.frame},
				                     {.width = static_cast<std::uint16_t>(f.size.width),
				                      .height = static_cast<std::uint16_t>(f.size.height)},
				                     f.hash)};
			log << line;
		});
	});
	if (!done)
	{
		std::cerr << name << ": " << path << ": " << render::trace::to_string(done.error())
		          << " at byte " << r.offset() << '\n';
		return 1;
	}
	if (frames == 0)
	{
		std::cerr << name << ": " << path << ": no frame presented\n";
		return 1;
	}
	if (!log.flush())
	{
		std::cerr << name << ": cannot write the frame hashes\n";
		return 1;
	}
	return 0;
}

int run(const int argc, char **const argv, const char *const name, const device_host &host)
{
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-container): argv holds argc pointers
	const std::span<char *const> args{argv, static_cast<std::size_t>(argc)};
	if (args.size() != 2 && args.size() != 3)
	{
		std::cerr << "usage: " << name << " TRACE [LOG]\n";
		return 2;
	}
	try
	{
		if (args.size() == 2)
			return replay_file(args[1], std::cout, host, name);
		std::ofstream log{args[2], std::ios::binary | std::ios::trunc};
		if (!log)
		{
			std::cerr << name << ": cannot create " << args[2] << '\n';
			return 1;
		}
		const int rc{replay_file(args[1], log, host, name)};
		log.close();
		if (rc == 0 && !log)
		{
			std::cerr << name << ": cannot write " << args[2] << '\n';
			return 1;
		}
		return rc;
	}
	catch (const std::exception &e)
	{
		std::cerr << name << ": " << e.what() << '\n';
		return 1;
	}
}

}

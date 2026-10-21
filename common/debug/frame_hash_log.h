/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "frame_hash.h"

namespace dcx {

/* What plays while a frame is presented: a demo, named by its file, or no
 * demo (title screens, menus, the game).  Each change starts a section of
 * the frame hash log.
 */
class frame_hash_section
{
	/* The demo's file; nullopt when no demo plays. */
	std::optional<std::string> demo_file;
	explicit frame_hash_section(std::optional<std::string> file) : demo_file{std::move(file)}
	{}

  public:
	[[nodiscard]]
	static frame_hash_section no_demo()
	{
		return frame_hash_section{std::nullopt};
	}
	[[nodiscard]]
	static frame_hash_section demo_playback(const std::string_view file)
	{
		return frame_hash_section{std::string{file}};
	}
	/* "# demo playback <file>\n" or "# no demo\n". */
	[[nodiscard]]
	std::string header_line() const
	{
		if (!demo_file)
			return "# no demo\n";
		return "# demo playback " + demo_file.value() + '\n';
	}
	bool operator==(const frame_hash_section &) const = default;
};

/* The frame hash log: one line per presented frame, in sections that each
 * start at a change of what plays and count frames from 0 again, so a
 * demo's frames line up across runs however long the title screens and
 * menus before it took on the wall clock.  Writer is called with each line
 * and returns false when it could not write it.
 */
template <typename Writer>
requires(std::is_invocable_r_v<bool, Writer &, std::string_view>)
class frame_hash_log
{
	Writer write;
	/* The section being written; nullopt before the first frame. */
	std::optional<frame_hash_section> section{};
	std::uint32_t next_frame{0};

  public:
	explicit frame_hash_log(Writer w) : write{std::move(w)}
	{}
	/* Log one frame of `size` whose RGB pixels are `pixels`, while
	 * `now` plays.  false when a line could not be written: the log is
	 * then incomplete.
	 */
	[[nodiscard]]
	bool log_frame(const frame_hash_section &now, const frame_size size,
	               const std::span<const std::byte> pixels)
	{
		if (section != now)
		{
			section = now;
			next_frame = 0;
			if (!write(now.header_line()))
				return false;
		}
		return write(frame_hash_line(frame_index{next_frame++}, size, frame_hash(pixels)));
	}
};

/* A frame hash log writer that owns its file and flushes each line, so a
 * run that ends by a signal leaves every line written so far.
 */
class frame_hash_file_writer
{
	struct closer
	{
		void operator()(std::FILE *const f) const
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the unique_ptr owns f; no gsl
			(void)std::fclose(f);
		}
	};
	std::unique_ptr<std::FILE, closer> file;

  public:
	/* Take ownership of `f`, which must not be nullptr. */
	explicit frame_hash_file_writer(std::FILE *const f) : file{f}
	{}
	/* The writer for a new, empty file at `path`; nullopt when it cannot
	 * be created (errno says why).
	 */
	[[nodiscard]]
	static std::optional<frame_hash_file_writer> open(const char *const path)
	{
		std::FILE *const f{std::fopen(path, "w")};
		if (f == nullptr)
			return std::nullopt;
		return frame_hash_file_writer{f};
	}
	bool operator()(const std::string_view line) const
	{
		// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-libc-call): a view's own size
		return std::fwrite(line.data(), 1, line.size(), file.get()) == line.size() &&
		       std::fflush(file.get()) == 0;
	}
};

}

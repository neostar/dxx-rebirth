/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/frame_hash_log.h"

#include <array>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth frame hash log
#include <boost/test/unit_test.hpp>

namespace {

/* A writer that keeps every line, and fails once `failing` is set. */
struct recording_writer
{
	std::vector<std::string> *lines;
	const bool *failing;
	bool operator()(const std::string_view line) const
	{
		if (*failing)
			return false;
		lines->emplace_back(line);
		return true;
	}
};

constexpr dcx::frame_size size{.width = 2, .height = 1};
constexpr std::array<std::byte, 6> black{};

/* The frame number at the start of a log line. */
std::string_view frame_of(const std::string_view line)
{
	return line.substr(0, line.find(' '));
}

}

/* Test that a section header names what plays: the demo's file, or no demo. */
BOOST_AUTO_TEST_CASE(section_headers)
{
	BOOST_TEST(dcx::frame_hash_section::no_demo().header_line() == "# no demo\n");
	BOOST_TEST(dcx::frame_hash_section::demo_playback("demos/a.dem").header_line() ==
	           "# demo playback demos/a.dem\n");
}

/* Test that the log opens a section before the first frame and counts its frames from 0. */
BOOST_AUTO_TEST_CASE(first_frame_opens_a_section)
{
	std::vector<std::string> lines;
	const bool failing{false};
	dcx::frame_hash_log log{recording_writer{&lines, &failing}};
	const auto menu{dcx::frame_hash_section::no_demo()};
	BOOST_TEST(log.log_frame(menu, size, black));
	BOOST_TEST(log.log_frame(menu, size, black));
	BOOST_REQUIRE(lines.size() == 3u);
	BOOST_TEST(lines[0] == "# no demo\n");
	BOOST_TEST(lines[1] == dcx::frame_hash_line(dcx::frame_index{0}, size, dcx::frame_hash(black)));
	BOOST_TEST(frame_of(lines[2]) == "1");
}

/* Test that a change of what plays, also from one demo to another, starts a new section
 * that counts from 0 again, and that frames of the same section do not.
 */
BOOST_AUTO_TEST_CASE(each_change_starts_a_section)
{
	std::vector<std::string> lines;
	const bool failing{false};
	dcx::frame_hash_log log{recording_writer{&lines, &failing}};
	const auto menu{dcx::frame_hash_section::no_demo()};
	const auto a{dcx::frame_hash_section::demo_playback("demos/a.dem")};
	const auto b{dcx::frame_hash_section::demo_playback("demos/b.dem")};
	for (const auto &section : {menu, menu, a, a, a, b, menu})
		BOOST_TEST(log.log_frame(section, size, black));
	const std::vector<std::string> expected_starts{
	    "# no demo\n", "0",           "1", "# demo playback demos/a.dem\n",
	    "0",           "1",           "2", "# demo playback demos/b.dem\n",
	    "0",           "# no demo\n", "0"};
	BOOST_REQUIRE(lines.size() == expected_starts.size());
	for (std::size_t i{0}; i != lines.size(); ++i)
		BOOST_TEST((lines[i].starts_with('#') ? lines[i] : std::string{frame_of(lines[i])}) ==
		           expected_starts[i]);
}

/* Test that a failed write of a header or of a frame line is reported to the caller. */
BOOST_AUTO_TEST_CASE(write_errors_are_reported)
{
	std::vector<std::string> lines;
	bool failing{true};
	dcx::frame_hash_log log{recording_writer{&lines, &failing}};
	const auto menu{dcx::frame_hash_section::no_demo()};
	BOOST_TEST(!log.log_frame(menu, size, black));
	failing = false;
	BOOST_TEST(log.log_frame(menu, size, black));
	failing = true;
	BOOST_TEST(!log.log_frame(menu, size, black));
}

/* Test that a failed header write is reported though the frame line after it is written. */
BOOST_AUTO_TEST_CASE(header_write_errors_are_reported)
{
	dcx::frame_hash_log log{[](const std::string_view line) { return !line.starts_with('#'); }};
	BOOST_TEST(!log.log_frame(dcx::frame_hash_section::no_demo(), size, black));
}

/* Test that the file writer writes each line through to its file. */
BOOST_AUTO_TEST_CASE(file_writer_writes_lines)
{
	std::FILE *const f{std::tmpfile()};
	BOOST_REQUIRE(f != nullptr);
	const dcx::frame_hash_file_writer write{f};
	BOOST_TEST(write("# no demo\n"));
	BOOST_TEST(write("0 2x1 0000000000000001\n"));
	BOOST_REQUIRE(std::fseek(f, 0, SEEK_SET) == 0);
	std::string text;
	for (int c{std::fgetc(f)}; c != EOF; c = std::fgetc(f))
		text += static_cast<char>(c);
	BOOST_TEST(text == "# no demo\n0 2x1 0000000000000001\n");
}

/* Test that the file writer reports a write the file refuses. */
BOOST_AUTO_TEST_CASE(file_writer_reports_a_failed_write)
{
	std::FILE *const read_only{std::fopen("/dev/null", "r")};
	BOOST_REQUIRE(read_only != nullptr);
	const dcx::frame_hash_file_writer write{read_only};
	BOOST_TEST(!write("# no demo\n"));
}

/* Test that the file writer reports a line that fwrite buffers but fflush cannot write:
 * a pipe, fully buffered as a stream on no terminal is, whose reading end is closed.
 */
BOOST_AUTO_TEST_CASE(file_writer_reports_a_failed_flush)
{
	/* A failed write to the pipe raises SIGPIPE, which would end the test. */
	BOOST_REQUIRE(std::signal(SIGPIPE, SIG_IGN) != SIG_ERR);
	std::array<int, 2> ends{};
	BOOST_REQUIRE(pipe(ends.data()) == 0);
	BOOST_REQUIRE(close(ends[0]) == 0);
	std::FILE *const f{fdopen(ends[1], "w")};
	BOOST_REQUIRE(f != nullptr);
	const dcx::frame_hash_file_writer write{f};
	BOOST_TEST(!write("# no demo\n"));
}

/* Test that opening a log in a missing directory fails rather than logging nowhere. */
BOOST_AUTO_TEST_CASE(file_writer_open_fails_for_a_missing_directory)
{
	BOOST_TEST(!dcx::frame_hash_file_writer::open("/nonexistent-dir/frames.log").has_value());
}

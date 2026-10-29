/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/trace_harness.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <variant>
#include <vector>

#include "common/render/device.h"
#include "common/render/recording_device.h"
#include "common/render/render_device.h"
#include "common/render/trace.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth trace harness
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

/* Test that a frame range reads as FIRST-LAST, a single frame included. */
BOOST_AUTO_TEST_CASE(frame_range_reads_first_and_last)
{
	const auto r{dcx::parse_frame_range("120-540")};
	BOOST_REQUIRE(r.has_value());
	BOOST_TEST((r.value() == render::frame_range{.first = 120, .last = 540}));
	const auto one{dcx::parse_frame_range("7-7")};
	BOOST_REQUIRE(one.has_value());
	BOOST_TEST((one.value() == render::frame_range{.first = 7, .last = 7}));
}

/* Test that anything but two whole numbers in order is no range. */
BOOST_AUTO_TEST_CASE(malformed_frame_range_refused)
{
	for (const std::string_view s : {"", "5", "-5", "5-", "a-5", "5-b", "5-3", "1-2-3", " 1-2",
	                                 "1-2 ", "+1-2", "4294967296-1"})
		BOOST_TEST(!dcx::parse_frame_range(s).has_value(), "'" << s << "' read as a range");
}

/* Test that without DXX_DBG_TRACE nothing is asked for, so no recorder is
 * made, even with DXX_DBG_TRACE_FRAMES set; with both, the path and range.
 */
BOOST_AUTO_TEST_CASE(trace_asked_for_only_with_its_path)
{
	BOOST_REQUIRE(setenv("DXX_DBG_TRACE_FRAMES", "1-2", 1) == 0);
	BOOST_REQUIRE(unsetenv("DXX_DBG_TRACE") == 0);
	BOOST_TEST(!dcx::read_harness_trace().has_value());
	BOOST_REQUIRE(setenv("DXX_DBG_TRACE", "", 1) == 0);
	BOOST_TEST(!dcx::read_harness_trace().has_value());
	BOOST_REQUIRE(setenv("DXX_DBG_TRACE", "/x.rtr", 1) == 0);
	const auto t{dcx::read_harness_trace()};
	BOOST_REQUIRE(t.has_value());
	BOOST_TEST(std::string_view{t.value().path} == "/x.rtr");
	BOOST_REQUIRE(t.value().frames.has_value());
	BOOST_TEST((t.value().frames.value() == render::frame_range{.first = 1, .last = 2}));
	BOOST_REQUIRE(unsetenv("DXX_DBG_TRACE_FRAMES") == 0);
	const auto missing{dcx::read_harness_trace()};
	BOOST_REQUIRE(missing.has_value());
	BOOST_TEST(!missing.value().frames.has_value());
}

namespace {

/* What a harness run would have ended with: report.fail throws it here, as
 * the game's ends the run.
 */
struct harness_failure : std::runtime_error
{
	using std::runtime_error::runtime_error;
};

std::vector<std::string> notes;

[[noreturn]]
void fail_with(const char *const message)
{
	throw harness_failure{message};
}

void note(const char *const message)
{
	notes.emplace_back(message);
}

std::vector<std::string> exit_notes;

void exit_note(const char *const message)
{
	exit_notes.emplace_back(message);
}

constexpr dcx::harness_report report{.fail = fail_with, .note = note, .exit_note = exit_note};

constexpr render::extent screen{.width = 32, .height = 16};

/* The fake device under the front: it counts the textures that reach it. */
class fake_device final : public render::device
{
  public:
	unsigned textures{0};
	unsigned presents{0};
	fake_device() : device{render::caps{}}
	{}
	void draw(const render::draw_call &) override
	{}
	void texture(render::texture_slot, const render::texture_image *) override
	{
		++textures;
	}
	void clear(render::clear_targets) override
	{}
	void present() override
	{
		++presents;
	}
	void read_pixels(render::read_source, const render::read_rect &, render::pixel_format,
	                 std::span<std::uint8_t>) override
	{}
};

/* A file in the temporary directory, removed when the test ends. */
struct temp_file
{
	std::filesystem::path path;
	explicit temp_file(const std::string &name)
	    : path{std::filesystem::temp_directory_path() /
		       (name + '-' + std::to_string(::getpid()) + ".rtr")}
	{}
	~temp_file()
	{
		std::error_code ignored;
		std::filesystem::remove(path, ignored);
	}
	temp_file(const temp_file &) = delete;
	temp_file &operator=(const temp_file &) = delete;
	temp_file(temp_file &&) = delete;
	temp_file &operator=(temp_file &&) = delete;
	[[nodiscard]]
	std::vector<std::uint8_t> bytes() const
	{
		std::ifstream in{path, std::ios::binary};
		return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
	}
};

}

/* Test that with no trace asked for the front end draws through the device
 * itself, and a texture release reaches it.
 */
BOOST_AUTO_TEST_CASE(no_request_draws_through_the_device_itself)
{
	fake_device inner;
	dcx::harness_front front{report, std::nullopt};
	front.bind(inner, [] { return screen; });
	BOOST_TEST(&front.get() == &inner);
	front.release_texture({5});
	BOOST_TEST(inner.textures == 1u);
}

/* Test that with a trace asked for the front end draws through a recorder
 * that writes the frames in range to the file, whose header holds the
 * screen's size, and that the run's log names what it records.
 */
BOOST_AUTO_TEST_CASE(request_records_its_frames_to_the_file)
{
	const temp_file file{"test-trace-harness-records"};
	const std::string path{file.path.string()};
	notes.clear();
	{
		fake_device inner;
		dcx::harness_front front{
		    report, dcx::trace_request{.path = path.c_str(),
			                           .frames_text = "1-1",
			                           .frames = render::frame_range{.first = 1, .last = 1}}};
		front.bind(inner, [] { return screen; });
		BOOST_TEST(&front.get() != &inner);
		for (unsigned f{0}; f != 3; ++f)
		{
			front.get().clear({.color = true, .depth = false});
			front.get().present();
		}
		BOOST_TEST(inner.presents == 3u);
		exit_notes.clear();
	}
	BOOST_REQUIRE(exit_notes.size() == 1u);
	BOOST_TEST(exit_notes.front() == "DXX_DBG_TRACE: recorded frames 1-1 of 1-1");
	const auto bytes{file.bytes()};
	const auto records{render::trace::read_all(bytes)};
	BOOST_REQUIRE(records.has_value());
	std::vector<std::uint32_t> presented;
	for (const auto &r : records.value())
		if (const auto *const p{std::get_if<render::trace::present>(&r)})
			presented.push_back(p->frame);
	BOOST_TEST(presented == (std::vector<std::uint32_t>{1}), boost::test_tools::per_element());
	render::trace::reader r{bytes};
	const auto h{r.header()};
	BOOST_REQUIRE(h.has_value());
	BOOST_TEST((h.value().target == screen));
	BOOST_REQUIRE(!notes.empty());
	BOOST_TEST(notes.front() == "DXX_DBG_TRACE: frames 1-1 to " + path);
}

/* Test that a malformed DXX_DBG_TRACE_FRAMES fails the run, naming it. */
BOOST_AUTO_TEST_CASE(malformed_frames_fail_the_run)
{
	fake_device inner;
	dcx::harness_front front{
	    report, dcx::trace_request{.path = "/x.rtr", .frames_text = "9-1", .frames = std::nullopt}};
	BOOST_CHECK_EXCEPTION(front.bind(inner, [] { return screen; }), harness_failure,
	                      [](const harness_failure &e) {
		                      return std::string_view{e.what()}.starts_with(
		                          "DXX_DBG_TRACE_FRAMES=\"9-1\": not FIRST-LAST");
	                      });
}

/* Test that a trace file that cannot be written fails the run, naming it. */
BOOST_AUTO_TEST_CASE(unwritable_trace_fails_the_run)
{
	fake_device inner;
	dcx::harness_front front{
	    report, dcx::trace_request{.path = "/nonexistent-test-trace-harness/t.rtr",
		                           .frames_text = "0-0",
		                           .frames = render::frame_range{.first = 0, .last = 0}}};
	BOOST_CHECK_EXCEPTION(front.bind(inner, [] { return screen; }), harness_failure,
	                      [](const harness_failure &e) {
		                      return std::string_view{e.what()}.starts_with(
		                          "DXX_DBG_TRACE=\"/nonexistent-test-trace-harness/t.rtr\": "
		                          "cannot write: ");
	                      });
}

/* Test that the front end's device is refused before the first bind, when
 * there is none (ogl_device before gr_set_mode).
 */
BOOST_AUTO_TEST_CASE(no_device_before_the_first_bind)
{
	const dcx::harness_front front{report, std::nullopt};
	BOOST_CHECK_THROW((void)front.get(), std::logic_error);
}

/* Test that a texture the trace format cannot hold, in range, ends the run
 * through the game's report, naming the trace and the limit, rather than
 * throwing through the game.
 */
BOOST_AUTO_TEST_CASE(texture_over_the_limit_fails_the_run)
{
	const temp_file file{"test-trace-harness-limit"};
	const std::string path{file.path.string()};
	fake_device inner;
	dcx::harness_front front{
	    report, dcx::trace_request{.path = path.c_str(),
		                           .frames_text = "0-1",
		                           .frames = render::frame_range{.first = 0, .last = 1}}};
	front.bind(inner, [] { return screen; });
	const std::uint32_t side{render::trace::max_texture_side + 1};
	const std::vector<std::uint8_t> rgba(std::size_t{side} * 4, 255);
	const render::texture_image big{{.size = {.width = side, .height = 1},
	                                 .format = render::texture_format::rgba8,
	                                 .filter = render::filter::nearest,
	                                 .anisotropic = false,
	                                 .has_transparency = false},
	                                rgba};
	BOOST_CHECK_EXCEPTION(front.get().texture({3}, &big), harness_failure,
	                      [&path](const harness_failure &e) {
		                      return std::string_view{e.what()} ==
							         "DXX_DBG_TRACE=\"" + path +
							             "\": trace::writer: texture side over "
							             "max_texture_side: the trace format cannot hold it";
	                      });
	BOOST_TEST(inner.textures == 1u);
}

/* Test that a screen resized in mid-recording (a mode change) ends the
 * recording with a logged reason, and the run goes on.
 */
BOOST_AUTO_TEST_CASE(screen_resized_ends_the_recording_with_a_note)
{
	const temp_file file{"test-trace-harness-resized"};
	const std::string path{file.path.string()};
	static render::extent size;
	size = screen;
	notes.clear();
	fake_device inner;
	dcx::harness_front front{
	    report, dcx::trace_request{.path = path.c_str(),
		                           .frames_text = "0-3",
		                           .frames = render::frame_range{.first = 0, .last = 3}}};
	front.bind(inner, [] { return size; });
	front.get().present();
	size = {.width = 64, .height = 64};
	BOOST_CHECK_NO_THROW(front.get().present());
	BOOST_TEST(inner.presents == 2u);
	BOOST_REQUIRE(notes.size() == 2u);
	BOOST_TEST(notes.back() == "DXX_DBG_TRACE: recording ended: the render target changed from "
	                           "32x16 to 64x64 at frame 1");
}

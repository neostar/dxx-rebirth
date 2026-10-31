/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/input_script_harness.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth input script harness
#include <boost/test/unit_test.hpp>

namespace {

std::int32_t clock_step(const char *const script, const char *const fps)
{
	const dcx::harness_frame_time frame_time{.value = fps,
	                                         .policy = fps ? dcx::frame_time_policy::parse(fps)
	                                                       : dcx::frame_time_policy{}};
	return dcx::harness_clock_step(script, nullptr, frame_time);
}

std::optional<dcx::input_key> key_of(const std::string_view name)
{
	if (name == "ENTER")
		return dcx::input_key{13};
	return std::nullopt;
}

/* A file in the temporary directory holding `text`, removed with the object. */
class temporary_script
{
	std::filesystem::path path_;

  public:
	explicit temporary_script(const std::string_view name, const std::string_view text)
	    : path_{std::filesystem::temp_directory_path() / name}
	{
		std::ofstream out{path_, std::ios::binary};
		out << text;
		out.close();
		BOOST_REQUIRE(out.good());
	}
	temporary_script(const temporary_script &) = delete;
	temporary_script &operator=(const temporary_script &) = delete;
	temporary_script(temporary_script &&) = delete;
	temporary_script &operator=(temporary_script &&) = delete;
	~temporary_script()
	{
		std::error_code ignored;
		std::filesystem::remove(path_, ignored);
	}
	[[nodiscard]]
	const char *path() const
	{
		return path_.c_str();
	}
};

constexpr std::int32_t step_at_32{2048};

}

/* Test that the harness clock steps 1/fps only while a script plays with a fixed frame
 * time, and keeps the wall clock otherwise.
 */
BOOST_AUTO_TEST_CASE(clock_steps_only_with_script_and_fixed_frame_time)
{
	BOOST_TEST(clock_step("s.txt", "32") == step_at_32);
	BOOST_TEST(clock_step(nullptr, "32") == 0);
	BOOST_TEST(clock_step("s.txt", nullptr) == 0);
	BOOST_TEST(clock_step("s.txt", "fast") == 0);
}

/* Test that a recording (#99) runs the harness clock as a script does, so its replay sees the
 * same time, and that it too needs the fixed frame time.
 */
BOOST_AUTO_TEST_CASE(clock_steps_while_input_is_recorded)
{
	const auto frame_time{[](const char *const fps) {
		return dcx::harness_frame_time{.value = fps,
		                               .policy = fps ? dcx::frame_time_policy::parse(fps)
		                                             : dcx::frame_time_policy{}};
	}};
	BOOST_TEST(dcx::harness_clock_step(nullptr, "r.txt", frame_time("32")) == step_at_32);
	BOOST_TEST(dcx::harness_clock_step(nullptr, "r.txt", frame_time(nullptr)) == 0);
}

/* Test that the frame hash log's section is named after the recording when a run records while
 * a script plays (#99), so the recording's replay, which plays the recording as its script, writes
 * the same section name; after the script without a recording, and nothing without either.
 */
BOOST_AUTO_TEST_CASE(scene_is_named_after_the_recording_first)
{
	const char *const script{"/s/s5-mouse-turn.txt"};
	const char *const recording{"/r/s5.txt"};
	BOOST_TEST(dcx::harness_scene_path(script, recording) == recording);
	BOOST_TEST(dcx::harness_scene_path(recording, nullptr) == recording);
	BOOST_TEST(dcx::harness_scene_path(script, nullptr) == script);
	BOOST_TEST(dcx::harness_scene_path(nullptr, nullptr) == nullptr);
}

/* Test that a missing file reads as nothing rather than as an empty script. */
BOOST_AUTO_TEST_CASE(missing_file_is_not_read)
{
	BOOST_TEST(!dcx::read_harness_text_file("/nonexistent/dxx-input-script.txt").has_value());
}

/* Test that a file that opens but fails to read, a directory, reads as nothing rather than
 * as an empty script.
 */
BOOST_AUTO_TEST_CASE(failed_read_is_not_read)
{
	BOOST_TEST(
	    !dcx::read_harness_text_file(std::filesystem::temp_directory_path().c_str()).has_value());
}

/* Test that a script loads into a player and its scene's name. */
BOOST_AUTO_TEST_CASE(script_loads_with_its_scene_name)
{
	const temporary_script script{"dxx-s9-load.txt", "1 press ENTER\n3 end\n"};
	auto loaded{dcx::load_input_script(script.path(), step_at_32, key_of)};
	BOOST_REQUIRE(loaded.has_value());
	BOOST_TEST(loaded.value().scene == "dxx-s9-load");
	BOOST_TEST(loaded.value().player.next_tick().empty());
	BOOST_TEST(loaded.value().player.next_tick().size() == 1u);
}

/* Test that a script is refused without the fixed frame time that times it, when its file
 * cannot be read, and with the line of a bad line.
 */
BOOST_AUTO_TEST_CASE(unplayable_script_is_refused_with_why)
{
	const temporary_script script{"dxx-s9-bad.txt", "1 press ENTER\n2 jump ENTER\n3 end\n"};
	const auto error{[](const char *const path, const std::int32_t step) {
		const auto loaded{dcx::load_input_script(path, step, key_of)};
		BOOST_REQUIRE(!loaded.has_value());
		return loaded.error();
	}};
	BOOST_TEST(error(script.path(), 0) == "needs DXX_DBG_FIXED_FRAMETIME, which times the script");
	BOOST_TEST(error("/nonexistent/dxx-input-script.txt", step_at_32) == "cannot read the file");
	BOOST_TEST(error(script.path(), step_at_32).starts_with("line 2: "));
}

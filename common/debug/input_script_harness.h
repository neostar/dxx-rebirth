/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "common/debug/input_recorder.h"
#include "frame_time_harness.h"
#include "input_script.h"

namespace dcx {

/* DXX_DBG_INPUT_SCRIPT: the input script (input_script.h) the debug harness
 * plays; nullptr when unset, which plays none.
 */
[[nodiscard]]
inline const char *read_harness_input_script_path()
{
	return std::getenv("DXX_DBG_INPUT_SCRIPT");
}

/* How far the harness clock advances at each timer_update, in `fix`
 * seconds: 1/fps of DXX_DBG_FIXED_FRAMETIME (`frame_time`) while the input
 * script at `script_path` plays or input is recorded to `record_path`
 * (input_recorder.h), so every screen timed by timer_query (title screens,
 * briefings, the menu cursor, the automap) repeats in a replay, and a
 * recording runs on the clock its replay will.  0 keeps the wall clock:
 * neither, or no fixed frame time, which load_input_script and
 * open_harness_recording refuse.
 */
[[nodiscard]]
inline std::int32_t harness_clock_step(const char *const script_path, const char *const record_path,
                                       const harness_frame_time &frame_time)
{
	if ((script_path == nullptr && record_path == nullptr) || frame_time.value == nullptr ||
	    !frame_time.policy)
		return 0;
	return frame_time.policy.value().frame_time(0);
}

/* The harness clock step of this run, from its environment. */
[[nodiscard]]
inline std::int32_t read_harness_clock_step()
{
	return harness_clock_step(read_harness_input_script_path(), read_harness_record_path(),
	                          harness_frame_time_of_run());
}

/* Whether the harness clock replaces the wall clock in this run.  Then a
 * frame limiter need not sleep: game time steps per timer_update call, so
 * a sleep changes no frame and only slows the run (#41).
 */
[[nodiscard]]
inline bool harness_clock_running()
{
	static const bool running{read_harness_clock_step() != 0};
	return running;
}

struct harness_file_closer
{
	void operator()(std::FILE *const f) const
	{
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the unique_ptr owns f; no gsl
		(void)std::fclose(f);
	}
};

/* The content of the file at `path`; nullopt when it cannot be opened or a
 * read fails (a directory opens, then fails to read).  stdio, since a
 * filebuf reports a failed read as the end of the file.
 */
[[nodiscard]]
inline std::optional<std::string> read_harness_text_file(const char *const path)
{
	const std::unique_ptr<std::FILE, harness_file_closer> file{std::fopen(path, "rb")};
	if (!file)
		return std::nullopt;
	std::string text;
	std::array<char, 4096> buffer{};
	/* No read after the end of the file or a failed read. */
	while (!std::feof(file.get()) && !std::ferror(file.get()))
	{
		// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-libc-call): the array's own size
		const std::size_t n{std::fread(buffer.data(), 1, buffer.size(), file.get())};
		text.append(buffer.data(), n);
	}
	if (std::ferror(file.get()))
		return std::nullopt;
	return text;
}

/* An input script as the harness plays it. */
struct harness_input_script
{
	input_script_player player;
	/* Its scene's name, for the frame hash log. */
	std::string scene;
};

/* Load the input script at `path` for a run whose harness clock steps
 * `clock_step` (harness_clock_step), naming keys with `key_of`
 * (parse_input_script); why it cannot be played otherwise.
 */
template <typename KeyOf>
[[nodiscard]]
std::expected<harness_input_script, std::string>
load_input_script(const char *const path, const std::int32_t clock_step, KeyOf &&key_of)
{
	if (clock_step == 0)
		return std::unexpected{"needs DXX_DBG_FIXED_FRAMETIME, which times the script"};
	const auto text{read_harness_text_file(path)};
	if (!text)
		return std::unexpected{"cannot read the file"};
	auto events{parse_input_script(text.value(), std::forward<KeyOf>(key_of))};
	if (!events)
		return std::unexpected{"line " + std::to_string(events.error().line) + ": " +
		                       events.error().message};
	return harness_input_script{.player = input_script_player{std::move(events).value()},
	                            .scene = input_scene_name(path)};
}

/* The file that names the run's scene in the frame hash log: the recording
 * `record_path` when input is recorded, even while the script `script_path`
 * plays, since the recording's replay plays it as its script and must write
 * its frames under the live run's header; else the script; nullptr without
 * either.
 */
[[nodiscard]]
inline const char *harness_scene_path(const char *const script_path, const char *const record_path)
{
	return record_path ? record_path : script_path;
}

/* The scene of this run (harness_scene_path), for the frame hash log;
 * nullptr without a script or a recording.
 */
[[nodiscard]]
inline const char *harness_scene_name()
{
	static const char *const path{
	    harness_scene_path(read_harness_input_script_path(), read_harness_record_path())};
	static const std::string scene{path ? input_scene_name(path) : std::string{}};
	return path ? scene.c_str() : nullptr;
}

}

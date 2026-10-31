/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "common/debug/input_recorder.h"
#include "common/debug/input_script_harness.h"

namespace dcx {

/* A run's input recording (DXX_DBG_RECORD_INPUT): the file, the recorder
 * writing into it line by line (each flushed, so a crash keeps what came
 * before), and the harness ticks.  Destroyed at exit without an end line,
 * it ends the recording at the tick no event_process reached, so a replay
 * exits where the run did, and notes on stderr what it recorded.
 */
class harness_input_recording
{
	std::unique_ptr<std::FILE, harness_file_closer> file_;
	std::string path_;
	input_recorder recorder_;
	std::int32_t clock_step_;
	std::optional<harness_tick> end_at_;
	/* Whether ticks keep the wall clock's pace: a live session, which no
	 * script drives.
	 */
	bool paced_;
	std::chrono::steady_clock::time_point start_{};
	harness_tick next_{0};
	/* The tick under way: the last next_tick's. */
	harness_tick current_{0};

	void write(const std::string_view line) const
	{
		const std::string text{std::string{line} + '\n'};
		// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-libc-call): the string's own size
		if (std::fwrite(text.data(), 1, text.size(), file_.get()) != text.size() ||
		    std::fflush(file_.get()) != 0)
		{
			/* No UserError here: the game may be exiting.  The replay of a
			 * cut recording would fail on its missing end line anyway.
			 */
			(void)std::fprintf(stderr, "DXX_DBG_RECORD_INPUT=\"%s\": write failed: %s\n",
			                   path_.c_str(), std::strerror(errno));
			std::_Exit(1);
		}
	}

  public:
	harness_input_recording(std::unique_ptr<std::FILE, harness_file_closer> file, std::string path,
	                        input_recorder::key_name_of name_of, input_recorder::key_of named,
	                        const std::int32_t clock_step, const std::optional<harness_tick> end_at,
	                        const bool paced)
	    : file_{std::move(file)}, path_{std::move(path)},
	      recorder_{[this](const std::string_view line) { write(line); }, std::move(name_of),
		            std::move(named)},
	      clock_step_{clock_step}, end_at_{end_at}, paced_{paced}
	{}
	harness_input_recording(const harness_input_recording &) = delete;
	harness_input_recording &operator=(const harness_input_recording &) = delete;
	harness_input_recording(harness_input_recording &&) = delete;
	harness_input_recording &operator=(harness_input_recording &&) = delete;
	~harness_input_recording()
	{
		try
		{
			recorder_.end(next_);
			(void)std::fprintf(stderr, "%s\n", exit_note().c_str());
		}
		catch (const std::exception &)
		{
			(void)std::fprintf(stderr, "DXX_DBG_RECORD_INPUT: cannot end the recording of %s\n",
			                   path_.c_str());
		}
	}
	/* Starts the next harness tick and returns it; a paced recording
	 * first waits until the tick's wall time.
	 */
	harness_tick next_tick()
	{
		const auto now{std::chrono::steady_clock::now()};
		if (next_ == harness_tick{0})
			start_ = now;
		else if (paced_)
			std::this_thread::sleep_for(harness_pace_wait(next_, clock_step_, now - start_));
		current_ = next_;
		next_ = harness_tick{std::to_underlying(current_) + 1};
		return current_;
	}
	/* Whether DXX_DBG_RECORD_INPUT_TICKS ends the run at `tick`. */
	[[nodiscard]]
	bool ends_at(const harness_tick tick) const
	{
		return end_at_ == tick;
	}
	/* The tick under way: the last next_tick's. */
	[[nodiscard]]
	harness_tick tick() const
	{
		return current_;
	}
	input_recorder &recorder()
	{
		return recorder_;
	}
	[[nodiscard]]
	std::string exit_note() const
	{
		return "DXX_DBG_RECORD_INPUT: " + recorder_.summary() + " to " + path_;
	}
};

/* Open the recording at `path` for a run whose harness clock steps
 * `clock_step` (harness_clock_step), ending at `ticks`, paced to the wall
 * clock when no script plays (`paced`); why it cannot record otherwise.
 */
[[nodiscard]]
inline std::expected<std::unique_ptr<harness_input_recording>, std::string>
open_harness_recording(const char *const path, const std::int32_t clock_step,
                       const harness_record_ticks &ticks, const bool paced,
                       input_recorder::key_name_of name_of, input_recorder::key_of named)
{
	if (clock_step == 0)
		return std::unexpected{std::string{"needs DXX_DBG_FIXED_FRAMETIME, which times the ticks"}};
	if (ticks.value && !ticks.end)
		return std::unexpected{std::string{"DXX_DBG_RECORD_INPUT_TICKS=\""} + ticks.value +
		                       "\": not a tick from 0 to 4294967295"};
	std::unique_ptr<std::FILE, harness_file_closer> file{std::fopen(path, "wb")};
	if (!file)
		return std::unexpected{"cannot write: " + std::string{std::strerror(errno)}};
	return std::make_unique<harness_input_recording>(
	    std::move(file), path, std::move(name_of), std::move(named), clock_step, ticks.end, paced);
}

}

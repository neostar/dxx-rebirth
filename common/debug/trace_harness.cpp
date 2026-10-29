/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/trace_harness.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace dcx {

namespace {

/* Ends the run with message through report; fail does not return. */
void fail_run(const harness_report &report, const std::string &message)
{
	report.fail(message.c_str());
}

/* The file a recorder writes its trace to; a write that fails ends the
 * run, as an unwritable file at the start does.
 */
class trace_file
{
	struct closer
	{
		void operator()(std::FILE *const f) const
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the unique_ptr owns f; no gsl
			(void)std::fclose(f);
		}
	};
	std::unique_ptr<std::FILE, closer> file_;
	std::string path_;
	harness_report report_;

  public:
	trace_file(std::FILE *const file, std::string path, const harness_report report)
	    : file_{file}, path_{std::move(path)}, report_{report}
	{}
	void write(const std::span<const std::uint8_t> bytes) const
	{
		// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-libc-call): the span's own size
		if (std::fwrite(bytes.data(), 1, bytes.size(), file_.get()) != bytes.size() ||
		    std::fflush(file_.get()) != 0)
			fail_run(report_,
			         "DXX_DBG_TRACE=\"" + path_ + "\": write failed: " + std::strerror(errno));
	}
};

}

harness_front::harness_front(const harness_report report, std::optional<trace_request> request)
    : report_{report}, request_{std::move(request)}
{}

void harness_front::bind(render::device &inner, render::target_size target)
{
	if (request_)
	{
		const trace_request request{request_.value()};
		request_.reset();
		const std::string path{request.path};
		if (!request.frames)
		{
			fail_run(report_, std::string{"DXX_DBG_TRACE_FRAMES=\""} + request.frames_text +
			                      "\": not FIRST-LAST, two frame indices in order");
			return;
		}
		std::FILE *const opened{std::fopen(request.path, "wb")};
		if (!opened)
		{
			fail_run(report_,
			         "DXX_DBG_TRACE=\"" + path + "\": cannot write: " + std::strerror(errno));
			return;
		}
		const auto file{std::make_shared<const trace_file>(opened, path, report_)};
		const auto frames{request.frames.value()};
		const std::string started{"DXX_DBG_TRACE: frames " + std::to_string(frames.first) + '-' +
		                          std::to_string(frames.last) + " to " + path};
		report_.note(started.c_str());
		recorder_ = std::make_unique<render::recording_device>(
		    inner, frames, std::move(target),
		    [file](const std::span<const std::uint8_t> bytes) { file->write(bytes); });
	}
	front_ = &render::front_device(inner, recorder_.get());
}

render::device &harness_front::get() const
{
	if (!front_)
		throw std::logic_error("ogl_device: no render device before gr_set_mode");
	return *front_;
}

void harness_front::release_texture(const render::texture_slot slot) const
{
	render::release_texture(front_, slot);
}

}

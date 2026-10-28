/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/assertion_exit.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace dcx::sdl {

namespace {

[[noreturn]]
SDL_AssertState SDLCALL exit_one(const SDL_AssertData *const data, void *)
{
	const auto text{[](const char *const s) { return std::string{s ? s : "?"}; }};
	const SDL_AssertData d{data ? *data : SDL_AssertData{}};
	const std::string message{"SDL assertion failed: " + text(d.condition) + " (" +
	                          text(d.filename) + ':' + std::to_string(d.linenum) + ")\n"};
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-libc-call): a string's own size
	std::fwrite(message.data(), 1, message.size(), stderr);
	std::fflush(stderr);
	std::_Exit(EXIT_FAILURE);
}

}

void exit_on_sdl_assertion()
{
	SDL_SetAssertionHandler(exit_one, nullptr);
}

}

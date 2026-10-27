/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <string>
#include <utility>

namespace dcx::sdl {

/* A stub that makes the SDL3 build link and does nothing else (#92).  Each
 * stub site declares one `static link_stub` and logs, on its first use only,
 * which ticket replaces it:
 *
 *   if (stub.first_use())
 *       con_printf(CON_NORMAL, "%s", stub.message().c_str());
 */
class link_stub
{
	const char *const what;
	const char *const ticket;
	bool logged{false};

  public:
	constexpr link_stub(const char *const stubbed, const char *const replaced_by)
	    : what{stubbed}, ticket{replaced_by}
	{}

	/* True the first time it is called, false afterwards. */
	[[nodiscard]]
	bool first_use()
	{
		return !std::exchange(logged, true);
	}

	[[nodiscard]]
	std::string message() const
	{
		return std::string{"SDL3 link stub: "} + what + " is stubbed until " + ticket;
	}
};

}

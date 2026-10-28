/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/assertion_exit.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common/sdl/dxx-sdl.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth sdl assertion exit
#include <boost/test/unit_test.hpp>

/* Test that a failed SDL assertion, with the handler installed, ends the
 * process with status 1 and the assertion on stderr.  A child process
 * trips it: with the handler missing or letting it pass, the child goes
 * on and exits 0, and the test fails without SDL's dialog (the child sets
 * SDL_ASSERT=ignore first, which SDL reads only for its default handler).
 */
BOOST_AUTO_TEST_CASE(failed_assertion_exits_one_with_its_message)
{
	std::array<int, 2> fds{};
	BOOST_REQUIRE(::pipe(fds.data()) == 0);
	const pid_t child{::fork()};
	BOOST_REQUIRE(child >= 0);
	if (child == 0)
	{
		::dup2(fds[1], STDERR_FILENO);
		::close(fds[0]);
		::setenv("SDL_ASSERT", "ignore", 1);
		dcx::sdl::exit_on_sdl_assertion();
		SDL_assert_release(1 == 2);
		std::_Exit(0);
	}
	::close(fds[1]);
	std::string err;
	std::array<char, 256> buf{};
	for (ssize_t n; (n = ::read(fds[0], buf.data(), buf.size())) > 0;)
		err.append(buf.data(), static_cast<std::size_t>(n));
	::close(fds[0]);
	int status{};
	BOOST_REQUIRE(::waitpid(child, &status, 0) == child);
	BOOST_REQUIRE(WIFEXITED(status));
	BOOST_TEST(WEXITSTATUS(status) == 1);
	BOOST_TEST(err.find("SDL assertion failed: 1 == 2 (") != std::string::npos, "stderr: " << err);
}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/caps.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth caps
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

/* Test that a GL context without GL_EXT_texture_filter_anisotropic, whose
 * limit the GL layer leaves at 0, has no anisotropy.
 */
BOOST_AUTO_TEST_CASE(gl_caps_no_anisotropy_without_extension)
{
	BOOST_TEST(!render::gl_caps(0.0f).anisotropy);
}

/* Test that a limit of exactly 1, which filters as without anisotropy, has
 * no anisotropy.
 */
BOOST_AUTO_TEST_CASE(gl_caps_no_anisotropy_at_limit_one)
{
	BOOST_TEST(!render::gl_caps(1.0f).anisotropy);
}

/* Test that a limit above 1 has anisotropy. */
BOOST_AUTO_TEST_CASE(gl_caps_anisotropy_above_one)
{
	BOOST_TEST(render::gl_caps(1.5f).anisotropy);
	BOOST_TEST(render::gl_caps(16.0f).anisotropy);
}

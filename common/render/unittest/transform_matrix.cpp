/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/transform_matrix.h"

#include <array>

#include "common/render/ambient.h"
#include "common/render/render_device.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth transform matrix
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;

/* Test that the game's 2D projection, glOrtho(0, 1, 0, 1, -1, 1), builds
 * the matrix GL builds: the values glGetFloatv(GL_PROJECTION_MATRIX) reads
 * after it on Apple's GL (M1 Max), column-major.
 */
BOOST_AUTO_TEST_CASE(screen_ortho_is_gls)
{
	const render::matrix expected{{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1}};
	BOOST_TEST(render::matrix_of(render::screen_ortho) == expected,
	           boost::test_tools::per_element());
}

/* Test that an asymmetric ortho builds GL's matrix, each element rounded
 * from double as GL rounds it: glOrtho(-3, 5, -2, 6, 0.5, 100), read back
 * from Apple's GL as for the screen ortho.
 */
BOOST_AUTO_TEST_CASE(asymmetric_ortho_is_gls)
{
	const render::ortho o{
	    .left = -3, .right = 5, .bottom = -2, .top = 6, .z_near = 0.5, .z_far = 100};
	const render::matrix expected{
	    {0.25f, 0, 0, 0, 0, 0.25f, 0, 0, 0, 0, -0.0201005025f, 0, -0.25f, -0.5f, -1.01005025f, 1}};
	BOOST_TEST(render::matrix_of(o) == expected, boost::test_tools::per_element());
}

/* Test that the 3D view's projection, gluPerspective(90, 1, 0.1, 5000),
 * builds the matrix Apple's GL builds (read back as for the screen ortho),
 * not SGI's GLU form, whose element 10 is -0x1.0002ap+0, a ULP away.
 */
BOOST_AUTO_TEST_CASE(view_perspective_is_gls)
{
	const render::matrix expected{
	    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -0x1.00029ep+0f, -1, 0, 0, -0x1.999bb2p-3f, 0}};
	BOOST_TEST(render::matrix_of(render::view_perspective) == expected,
	           boost::test_tools::per_element());
}

/* Test that a stereo eye's shift lands in element 8 of the perspective. */
BOOST_AUTO_TEST_CASE(stereo_shift_lands_in_element_8)
{
	render::perspective p{render::view_perspective};
	p.stereo_dx = -0.0375f;
	const auto m{render::matrix_of(p)};
	BOOST_TEST(m[8] == -0.0375f);
}

/* Test that glLoadIdentity, glTranslatef(t), glScalef(s) builds T * S:
 * the scale on the diagonal, the translation in the last column.
 */
BOOST_AUTO_TEST_CASE(translate_scale_is_translate_then_scale)
{
	const render::translate_scale ts{.translate = {{1, 2, 3}}, .scale = {{4, 5, 6}}};
	const render::matrix expected{{4, 0, 0, 0, 0, 5, 0, 0, 0, 0, 6, 0, 1, 2, 3, 1}};
	BOOST_TEST(render::matrix_of(render::modelview{ts}) == expected,
	           boost::test_tools::per_element());
}

/* Test that the identity modelview is the identity matrix. */
BOOST_AUTO_TEST_CASE(identity_is_identity)
{
	const render::matrix expected{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	BOOST_TEST(render::matrix_of(render::modelview{render::identity{}}) == expected,
	           boost::test_tools::per_element());
}

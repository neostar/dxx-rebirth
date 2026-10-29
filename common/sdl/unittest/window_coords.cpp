/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* The window module's conversion from window coordinates to pixels (#43, #48): the seam is
 * the pure conversion, a point and a pixel density in, the pixel the menus hit-test out.
 */

#include "common/sdl/window_coords.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth window coords
#include <boost/test/unit_test.hpp>

using dcx::sdl::pixel_point;
using dcx::sdl::window_to_pixels;

/* Test that at density 1, the window SDL3 creates until #48, a point is its own pixel. */
BOOST_AUTO_TEST_CASE(density_one_keeps_the_point)
{
	BOOST_TEST((window_to_pixels({.x = 17, .y = 230}, 1.f) == pixel_point{.x = 17, .y = 230}));
}

/* Test that on a Retina window, density 2, a point covers two pixels on each axis. */
BOOST_AUTO_TEST_CASE(density_two_doubles_the_point)
{
	BOOST_TEST((window_to_pixels({.x = 10, .y = 20}, 2.f) == pixel_point{.x = 20, .y = 40}));
}

/* Test that a fractional density, 150% on Wayland, lands on the pixel the point starts in. */
BOOST_AUTO_TEST_CASE(fractional_density_lands_on_the_pixel_the_point_starts_in)
{
	BOOST_TEST((window_to_pixels({.x = 3, .y = 5}, 1.5f) == pixel_point{.x = 4, .y = 7}));
}

/* Test that a density SDL could not report (0, its failure value) keeps the point rather than
 * putting the cursor at the corner.
 */
BOOST_AUTO_TEST_CASE(unknown_density_keeps_the_point)
{
	BOOST_TEST((window_to_pixels({.x = 9, .y = 4}, 0.f) == pixel_point{.x = 9, .y = 4}));
}

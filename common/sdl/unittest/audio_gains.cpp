/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/audio_gains.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth audio gains
#include <boost/test/unit_test.hpp>

namespace {

constexpr int32_t f1_0{0x10000};

}

/* Test that a full-volume effect panned hard left or right plays on that
 * side alone at unit gain.
 */
BOOST_AUTO_TEST_CASE(hard_pan_plays_one_side_at_unit_gain)
{
	const auto left{dcx::sdl::effect_gains(f1_0, 0)};
	BOOST_TEST(left.left == 1.0f);
	BOOST_TEST(left.right == 0.0f);
	const auto right{dcx::sdl::effect_gains(f1_0, 0xffff)};
	BOOST_TEST(right.left == 0.0f);
	BOOST_TEST(right.right == 1.0f);
}

/* Test that the centre is linear, about half per side (-6 dB), as SDL2_mixer
 * plays it: the byte pan 127 gives 128/255 left and 127/255 right.
 */
BOOST_AUTO_TEST_CASE(centre_is_half_per_side)
{
	const auto g{dcx::sdl::effect_gains(f1_0, 0x7fff)};
	BOOST_TEST(g.left == 128.0f / 255.0f);
	BOOST_TEST(g.right == 127.0f / 255.0f);
}

/* Test that the volume scales both sides by its byte over 255. */
BOOST_AUTO_TEST_CASE(volume_scales_both_sides)
{
	const auto g{dcx::sdl::effect_gains(f1_0 / 2, 0xffff)};
	BOOST_TEST(g.left == 0.0f);
	BOOST_TEST(g.right == 128.0f / 255.0f);
	const auto silent{dcx::sdl::effect_gains(0, 0x7fff)};
	BOOST_TEST(silent.left == 0.0f);
	BOOST_TEST(silent.right == 0.0f);
}

/* Test that pan and volume are quantised to a byte as fix2byte does: values
 * within one byte step give the same gains, and volumes of 1.0 and over clamp.
 */
BOOST_AUTO_TEST_CASE(pan_and_volume_are_quantised_to_a_byte)
{
	const auto a{dcx::sdl::effect_gains(0x8000, 0x4000)};
	const auto b{dcx::sdl::effect_gains(0x80ff, 0x40ff)};
	BOOST_TEST(a.left == b.left);
	BOOST_TEST(a.right == b.right);
	const auto over{dcx::sdl::effect_gains(2 * f1_0, 0xffff)};
	BOOST_TEST(over.right == 1.0f);
	BOOST_TEST(dcx::sdl::gain_byte(0xff00) == 255);
	BOOST_TEST(dcx::sdl::gain_byte(0xfeff) == 254);
}

/* Test that the effect volume follows the SDL2 build's Mix_Volume of
 * fix2byte(fixmul(d, d)) over 128: the menu's top setting (d = F1_0 / 2)
 * is half, half of it an eighth, none silent, and over F1_0 clamps at 1.
 */
BOOST_AUTO_TEST_CASE(effect_volume_follows_the_sdl2_mix_volume)
{
	BOOST_TEST(dcx::sdl::effect_volume_gain(f1_0 / 2) == 0.5f);
	BOOST_TEST(dcx::sdl::effect_volume_gain(f1_0 / 4) == 0.125f);
	BOOST_TEST(dcx::sdl::effect_volume_gain(0) == 0.0f);
	BOOST_TEST(dcx::sdl::effect_volume_gain(2 * f1_0) == 1.0f);
}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/audio_gains.h"

#include <set>
// clang-format off
#include "tools/dev/upstream_begin.h"
#include "maths.h"
#include "tools/dev/upstream_end.h"
// clang-format on

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth audio gains
#include <boost/test/unit_test.hpp>

/* Test that a full-volume effect panned hard left or right plays on that
 * side alone at unit gain.
 */
BOOST_AUTO_TEST_CASE(hard_pan_plays_one_side_at_unit_gain)
{
	const auto left{::dcx::sdl::effect_gains(F1_0, 0)};
	BOOST_TEST(left.left == 1.0f);
	BOOST_TEST(left.right == 0.0f);
	const auto right{::dcx::sdl::effect_gains(F1_0, 0xffff)};
	BOOST_TEST(right.left == 2.0f / 65536.0f);
	BOOST_TEST(right.right == 1.0f);
}

/* Test that the centre plays at full level on both sides, the DOS pan law
 * (+6 dB per side against SDL2_mixer's linear law).
 */
BOOST_AUTO_TEST_CASE(centre_is_full_level_per_side)
{
	const auto g{::dcx::sdl::effect_gains(F1_0, 0x7fff)};
	BOOST_TEST(g.left == 1.0f);
	BOOST_TEST(g.right == 65534.0f / 65536.0f);
}

/* Test that the volume scales both sides, and that a volume above F1_0
 * plays at full scale.
 */
BOOST_AUTO_TEST_CASE(volume_scales_both_sides_up_to_full_scale)
{
	const auto g{::dcx::sdl::effect_gains(F1_0 / 2, 0xc000)};
	BOOST_TEST(g.left == 0.25f);
	BOOST_TEST(g.right == 0.5f);
	const auto silent{::dcx::sdl::effect_gains(0, 0x7fff)};
	BOOST_TEST(silent.left == 0.0f);
	BOOST_TEST(silent.right == 0.0f);
	const auto over{::dcx::sdl::effect_gains(3 * F1_0, 0)};
	BOOST_TEST(over.left == 1.0f);
}

/* Test that the effect volume follows the SDL2 build's Mix_Volume of
 * fix2byte(fixmul(d, d)) over 128: the menu's top setting (d = F1_0 / 2)
 * is half, half of it an eighth, none silent, and over F1_0 clamps at 1.
 */
BOOST_AUTO_TEST_CASE(effect_volume_follows_the_sdl2_mix_volume)
{
	BOOST_TEST(::dcx::sdl::effect_volume_gain(F1_0 / 2) == 0.5f);
	BOOST_TEST(::dcx::sdl::effect_volume_gain(F1_0 / 4) == 0.125f);
	BOOST_TEST(::dcx::sdl::effect_volume_gain(0) == 0.0f);
	BOOST_TEST(::dcx::sdl::effect_volume_gain(2 * F1_0) == 1.0f);
}

/* Test that the SDL3 build loads an 8-bit effect sample at the effects
 * gain, every source value kept: 256 distinct 16-bit values, full scale at
 * 3/32.
 */
BOOST_AUTO_TEST_CASE(effect_sample_keeps_every_source_value)
{
	const std::set<int> values{[] {
		std::set<int> v;
		for (int s{0}; s < 256; ++s)
			v.insert(::dcx::sdl::effect_sample(static_cast<uint8_t>(s)));
		return v;
	}()};
	BOOST_TEST(values.size() == 256u);
	BOOST_TEST(::dcx::sdl::effect_sample(0) == -32768 * 3 / 32);
	BOOST_TEST(::dcx::sdl::effect_sample(0x80) == 0);
}

/* Test that each step 1..8 of the effects volume gives a distinct level,
 * 3/64 of full scale for a full-scale centred effect at step 8, as in the
 * SDL2 build.
 */
BOOST_AUTO_TEST_CASE(each_effects_step_gives_a_distinct_level)
{
	std::set<float> levels;
	for (int step{1}; step <= 8; ++step)
	{
		const float level{::dcx::sdl::effect_volume_gain(step * (F1_0 / 2) / 8) *
		                  ::dcx::sdl::effect_gains(F1_0, 0x7fff).left *
		                  static_cast<float>(-::dcx::sdl::effect_sample(0))};
		BOOST_TEST(level > 0.0f);
		levels.insert(level);
		if (step == 8)
			BOOST_TEST(level == 32768.0f * 3 / 64);
	}
	BOOST_TEST(levels.size() == 8u);
}

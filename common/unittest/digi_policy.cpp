#include "digi_policy.h"
#include <bitset>
#include <cmath>
#include <set>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth digi policy
#include <boost/test/unit_test.hpp>

namespace {

using namespace dcx;

}

namespace digi_policy_check {

/* The formulas the tables of digi_policy.h hold. */
constexpr int music_volume(const int step)
{
	return (step * 128 * digi_gain_numerator + 128) / (8 * digi_gain_denominator);
}

constexpr int midi_volume(const int step)
{
	const int x{128 * 128 * digi_gain_numerator * step / (8 * digi_gain_denominator)};
	int r{0};
	while ((r + 1) * (r + 1) <= x)
		++r;
	return x - r * r > (r + 1) * (r + 1) - x ? r + 1 : r;
}

constexpr bool tables_match()
{
	for (std::size_t step{0}; step <= 8; ++step)
		if (digi_music_volume_table[step] != music_volume(static_cast<int>(step)) || digi_midi_volume_table[step] != midi_volume(static_cast<int>(step)))
			return false;
	return true;
}

static_assert(tables_match());

}

/* Test that the effects gain keeps all 256 values of an 8-bit source
 * sample, converted to 16 bits, and scales full scale to 3/32.
 */
BOOST_AUTO_TEST_CASE(effects_gain_keeps_every_source_value)
{
	std::set<int> values;
	for (int s{0}; s < 256; ++s)
	{
		std::array<int16_t, 1> v{{static_cast<int16_t>((s - 0x80) * 256)}};
		digi_scale_effect_samples(v);
		values.insert(v[0]);
	}
	BOOST_TEST(values.size() == 256u);
	std::array<int16_t, 2> full{{INT16_MIN, 0}};
	digi_scale_effect_samples(full);
	BOOST_TEST(full[0] == -32768 * 3 / 32);
	BOOST_TEST(full[1] == 0);
}

/* Test that each step 1..8 of the music volume gives a distinct, non-zero
 * SDL_mixer music volume, 3/32 of full at step 8.
 */
BOOST_AUTO_TEST_CASE(each_music_step_gives_a_distinct_volume)
{
	std::set<int> v;
	for (int step{1}; step <= 8; ++step)
	{
		BOOST_TEST(digi_music_volume(step) > 0);
		v.insert(digi_music_volume(step));
	}
	BOOST_TEST(v.size() == 8u);
	BOOST_TEST(digi_music_volume(0) == 0);
	BOOST_TEST(digi_music_volume(8) == 128 * 3 / 32);
	BOOST_TEST(digi_music_volume(9) == 128 * 3 / 32);
}

/* Test that each step 1..8 gives a distinct, non-zero Windows MIDI channel
 * volume, and that step 8 is -20.6 dB (3/32) within 0.3 dB under the
 * General MIDI CC7 curve, 40 log10(cc / 127) dB.
 */
BOOST_AUTO_TEST_CASE(each_music_step_gives_a_distinct_windows_midi_volume)
{
	std::set<int> v;
	for (int step{1}; step <= 8; ++step)
	{
		BOOST_TEST(digi_midi_volume(step) > 0);
		v.insert(digi_midi_volume(step));
	}
	BOOST_TEST(v.size() == 8u);
	const double db{40.0 * std::log10(digi_midi_volume(8) / 128.0)};
	BOOST_TEST(std::fabs(db - -20.56) <= 0.3);
}

/* Test that the pan law is the DOS game's: centre at full level on both
 * sides, the near side at full level, the far side falling linearly.
 */
BOOST_AUTO_TEST_CASE(pan_law_keeps_the_near_side_at_full_level)
{
	const auto centre{digi_pan_levels(0x7fff)};
	BOOST_TEST(centre.left == 0x10000);
	BOOST_TEST(centre.right == 0xfffe);
	const auto quarter_right{digi_pan_levels(0xc000)};
	BOOST_TEST(quarter_right.left == 0x8000);
	BOOST_TEST(quarter_right.right == 0x10000);
	const auto quarter_left{digi_pan_levels(0x4000)};
	BOOST_TEST(quarter_left.left == 0x10000);
	BOOST_TEST(quarter_left.right == 0x8000);
	const auto hard_left{digi_pan_levels(0)};
	BOOST_TEST(hard_left.left == 0x10000);
	BOOST_TEST(hard_left.right == 0);
}

/* Test that ambience stops at a quarter of the voices granted, and never
 * below one: 16 of 64, 4 of 16.
 */
BOOST_AUTO_TEST_CASE(ambience_stops_at_a_quarter_of_the_granted_voices)
{
	BOOST_TEST(digi_ambient_sound_limit(64) == 16);
	BOOST_TEST(digi_ambient_sound_limit(16) == 4);
	BOOST_TEST(digi_ambient_sound_limit(2) == 1);
}

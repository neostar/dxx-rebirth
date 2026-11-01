/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/debug/audio_render.h"

#include <cstddef>
#include <cstdint>
#include <span>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth audio render
#include <boost/test/unit_test.hpp>

/* Test that 30 fixed frames a second render one second of audio in total,
 * though no frame holds a whole number of audio frames: the fraction
 * carries.
 */
BOOST_AUTO_TEST_CASE(fixed_frames_sum_to_the_game_time)
{
	::dcx::audio_render_clock render_clock;
	constexpr std::int32_t step{65536 / 30};
	std::size_t total{0};
	for (int f{0}; f < 30; ++f)
	{
		const auto n{render_clock.frames_due(step, 44100)};
		BOOST_TEST((n == 1469u || n == 1470u));
		total += n;
	}
	/* 30 steps of 2184 are 65520 of 65536: 44089.2 frames. */
	BOOST_TEST(total == 44089u);
}

/* Test that a frame shorter than one audio frame renders none, and its time
 * is not lost.
 */
BOOST_AUTO_TEST_CASE(short_frames_carry_their_time)
{
	::dcx::audio_render_clock render_clock;
	BOOST_TEST(render_clock.frames_due(1, 44100) == 0u);
	BOOST_TEST(render_clock.frames_due(1, 44100) == 1u);
}

/* Test that a frame with no time, or a negative one, renders nothing. */
BOOST_AUTO_TEST_CASE(no_time_renders_nothing)
{
	::dcx::audio_render_clock render_clock;
	BOOST_TEST(render_clock.frames_due(0, 44100) == 0u);
	BOOST_TEST(render_clock.frames_due(-65536, 44100) == 0u);
	BOOST_TEST(render_clock.frames_due(65536, 44100) == 44100u);
}

/* Test that a frame renders once, however often it commits, and only when
 * the game time advanced; and that a failed mix fails the render.
 */
BOOST_AUTO_TEST_CASE(each_game_frame_renders_once)
{
	::dcx::audio_render render{"/dev/null", 0};
	BOOST_REQUIRE(render.is_open());
	std::size_t calls{0}, frames{0};
	const auto generate{[&](const std::span<std::int16_t> out) {
		++calls;
		frames += out.size() / 2;
		return true;
	}};
	BOOST_TEST(render.render_frame(0, 65536, generate));
	BOOST_TEST(calls == 0u);
	BOOST_TEST(render.render_frame(65536, 65536, generate));
	BOOST_TEST(render.render_frame(65536, 65536, generate));
	BOOST_TEST(calls == 1u);
	BOOST_TEST(frames == 44100u);
	BOOST_TEST(!render.render_frame(131072, 65536, [](std::span<std::int16_t>) { return false; }));
}

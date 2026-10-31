/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/effect_mixer.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth effect mixer
#include <boost/test/unit_test.hpp>

/* The seam: SDL3_mixer's own generated mixer (MIX_CreateMixer), pulled with
 * MIX_Generate, stands in for the device the game opens; no driver, pacing
 * or file (docs/agents/sdl-audio.md area 8).  The effects are synthetic: a
 * constant U8 level, which SDL turns into a constant float.
 */
namespace {

using dcx::sdl::effect_mixer;
using dcx::sdl::stereo_gains;

constexpr int mix_rate{22050};
/* U8 0xc0 is (0xc0 - 0x80) / 0x80 = 0.5 as float. */
constexpr uint8_t effect_level{0xc0};
constexpr float effect_value{0.5f};
constexpr stereo_gains unit{.left = 1.0f, .right = 1.0f};

struct mixer_init
{
	mixer_init()
	{
		if (!MIX_Init())
			throw std::runtime_error{std::string{"MIX_Init failed: "} + SDL_GetError()};
	}
	~mixer_init()
	{
		MIX_Quit();
	}
	mixer_init(const mixer_init &) = delete;
	mixer_init &operator=(const mixer_init &) = delete;
	mixer_init(mixer_init &&) = delete;
	mixer_init &operator=(mixer_init &&) = delete;
};

effect_mixer make_mixer(const int rate = mix_rate)
{
	const SDL_AudioSpec spec{.format = SDL_AUDIO_F32, .channels = 2, .freq = rate};
	return effect_mixer{dcx::sdl::mix_mixer_ptr{MIX_CreateMixer(&spec)}};
}

/* frames of the effect level, U8 mono at rate. */
dcx::sdl::mix_audio_ptr make_effect(MIX_Mixer *const m, const std::size_t frames,
                                    const int rate = mix_rate)
{
	const std::vector<uint8_t> data(frames, effect_level);
	const SDL_AudioSpec spec{.format = SDL_AUDIO_U8, .channels = 1, .freq = rate};
	return dcx::sdl::mix_audio_ptr{MIX_LoadRawAudio(m, data.data(), data.size(), &spec)};
}

struct side_means
{
	float left;
	float right;
};

/* Generates frames of stereo output, silence after the tracks end, and
 * returns them interleaved.
 */
std::vector<float> generate(MIX_Mixer *const m, const std::size_t frames)
{
	std::vector<float> out(frames * 2);
	const auto bytes{static_cast<int>(out.size() * sizeof(float))};
	BOOST_REQUIRE(MIX_Generate(m, out.data(), bytes) >= 0);
	return out;
}

side_means means(const std::span<const float> out)
{
	double left{0}, right{0};
	for (std::size_t i{0}; i + 1 < out.size(); i += 2)
	{
		left += out[i];
		right += out[i + 1];
	}
	const auto frames{static_cast<double>(out.size()) / 2.0};
	return {.left = static_cast<float>(left / frames), .right = static_cast<float>(right / frames)};
}

bool near(const float a, const float b)
{
	return std::fabs(a - b) < 1e-4f;
}

}

BOOST_GLOBAL_FIXTURE(mixer_init);

/* Test that an effect plays at the stereo gains the game computed, linear on
 * each side: the centre is half per side, hard left is silent on the right.
 */
BOOST_AUTO_TEST_CASE(effect_plays_at_its_stereo_gains)
{
	auto mixer{make_mixer()};
	BOOST_REQUIRE(mixer.usable());
	const auto effect{make_effect(mixer.mixer(), 4096)};
	BOOST_REQUIRE(effect);
	const auto centre{dcx::sdl::effect_gains(0x10000, 0x7fff)};
	BOOST_REQUIRE(mixer.start(effect.get(), centre, false, 64).has_value());
	mixer.commit_frame();
	const auto m{means(generate(mixer.mixer(), 1024))};
	BOOST_TEST(near(m.left, effect_value * centre.left));
	BOOST_TEST(near(m.right, effect_value * centre.right));
	const auto left{mixer.start(effect.get(), dcx::sdl::effect_gains(0x10000, 0), false, 64)};
	BOOST_REQUIRE(left.has_value());
	mixer.stop(0);
	mixer.commit_frame();
	const auto l{means(generate(mixer.mixer(), 1024))};
	BOOST_TEST(near(l.left, effect_value));
	BOOST_TEST(near(l.right, 0.0f));
}

/* Test that queued commands reach the mix only at the frame's commit. */
BOOST_AUTO_TEST_CASE(commands_take_effect_at_the_commit)
{
	auto mixer{make_mixer()};
	const auto effect{make_effect(mixer.mixer(), 4096)};
	BOOST_REQUIRE(mixer.start(effect.get(), unit, false, 64).has_value());
	BOOST_TEST(near(means(generate(mixer.mixer(), 512)).left, 0.0f));
	mixer.commit_frame();
	BOOST_TEST(near(means(generate(mixer.mixer(), 512)).left, effect_value));
}

/* Test that a stopped effect is silent and no longer playing, and that a
 * new gain reaches a playing one.
 */
BOOST_AUTO_TEST_CASE(stop_silences_and_set_gains_changes_the_level)
{
	auto mixer{make_mixer()};
	const auto effect{make_effect(mixer.mixer(), 8192)};
	const auto t{mixer.start(effect.get(), unit, true, 64)};
	BOOST_REQUIRE(t.has_value());
	mixer.commit_frame();
	mixer.set_gains(t.value(), {.left = 0.25f, .right = 0.75f});
	mixer.commit_frame();
	const auto g{means(generate(mixer.mixer(), 512))};
	BOOST_TEST(near(g.left, 0.25f * effect_value));
	BOOST_TEST(near(g.right, 0.75f * effect_value));
	mixer.stop(t.value());
	BOOST_TEST(!mixer.playing(t.value()));
	mixer.commit_frame();
	const auto s{means(generate(mixer.mixer(), 512))};
	BOOST_TEST(near(s.left, 0.0f));
	BOOST_TEST(near(s.right, 0.0f));
}

/* Test that an effect started in the frame another one stops plays its own
 * samples, alone: the stopped track, still playing until the commit, is not
 * given the new audio (sdl-audio.md area 4), as after a kill-and-restart in
 * digi_sync_sounds().
 */
BOOST_AUTO_TEST_CASE(a_stopped_track_is_not_reused_in_its_frame)
{
	auto mixer{make_mixer()};
	const auto effect{make_effect(mixer.mixer(), 8192)};
	const auto old{mixer.start(effect.get(), {.left = 1.0f, .right = 0.0f}, true, 64)};
	BOOST_REQUIRE(old.has_value());
	mixer.commit_frame();
	BOOST_TEST(near(means(generate(mixer.mixer(), 512)).left, effect_value));
	mixer.stop(old.value());
	const auto restarted{mixer.start(effect.get(), {.left = 0.0f, .right = 1.0f}, true, 64)};
	BOOST_REQUIRE(restarted.has_value());
	mixer.commit_frame();
	const auto m{means(generate(mixer.mixer(), 512))};
	BOOST_TEST(near(m.left, 0.0f));
	BOOST_TEST(near(m.right, effect_value));
	BOOST_TEST(mixer.playing(restarted.value()));
}

/* Test that stop_all silences a looping effect at once, without waiting for
 * a commit that a blocking stretch (a level load) would hold back.
 */
BOOST_AUTO_TEST_CASE(stop_all_silences_at_once)
{
	auto mixer{make_mixer()};
	const auto effect{make_effect(mixer.mixer(), 8192)};
	const auto t{mixer.start(effect.get(), unit, true, 64)};
	BOOST_REQUIRE(t.has_value());
	mixer.commit_frame();
	mixer.stop_all();
	const auto m{means(generate(mixer.mixer(), 512))};
	BOOST_TEST(near(m.left, 0.0f));
	BOOST_TEST(near(m.right, 0.0f));
	BOOST_TEST(!mixer.playing(t.value()));
}

/* Test that a one-shot effect ends after its length and the game sees it end
 * at the next commit, while a looping one plays on past its length.
 */
BOOST_AUTO_TEST_CASE(one_shot_ends_and_looping_plays_on)
{
	auto mixer{make_mixer()};
	const auto effect{make_effect(mixer.mixer(), 1000)};
	const auto once{mixer.start(effect.get(), {.left = 1.0f, .right = 0.0f}, false, 64)};
	const auto loop{mixer.start(effect.get(), {.left = 0.0f, .right = 1.0f}, true, 64)};
	BOOST_REQUIRE(once.has_value());
	BOOST_REQUIRE(loop.has_value());
	BOOST_TEST(once.value() != loop.value());
	mixer.commit_frame();
	const auto out{generate(mixer.mixer(), 4000)};
	const auto tail{means(std::span{out}.subspan(std::size_t{2} * 3000))};
	BOOST_TEST(near(tail.left, 0.0f));
	BOOST_TEST(near(tail.right, effect_value));
	mixer.commit_frame();
	BOOST_TEST(!mixer.playing(once.value()));
	BOOST_TEST(mixer.playing(loop.value()));
}

/* Test that an effect starts only on a track not playing, among the first
 * max_tracks of the 64, and none when all play.
 */
BOOST_AUTO_TEST_CASE(tracks_run_out_at_the_limit)
{
	auto mixer{make_mixer()};
	const auto effect{make_effect(mixer.mixer(), 64)};
	for (std::size_t i{0}; i < 8; ++i)
		BOOST_TEST(mixer.start(effect.get(), unit, true, 8).value() == i);
	BOOST_TEST(!mixer.start(effect.get(), unit, true, 8).has_value());
	mixer.stop(3);
	/* Its stop reaches the mixer only at the commit. */
	BOOST_TEST(!mixer.start(effect.get(), unit, true, 8).has_value());
	mixer.commit_frame();
	BOOST_TEST(mixer.start(effect.get(), unit, true, 8).value() == 3);
	for (std::size_t i{8}; i < dcx::sdl::effect_track_count; ++i)
		BOOST_TEST(mixer.start(effect.get(), unit, true, 1000).has_value());
	BOOST_TEST(!mixer.start(effect.get(), unit, true, 1000).has_value());
}

/* Test that an effect at the sounds' own 11025 Hz keeps its duration when
 * the mixer runs at a device rate: SDL resamples it once (area 2).
 */
BOOST_AUTO_TEST_CASE(effect_keeps_its_duration_at_the_device_rate)
{
	auto mixer{make_mixer(48000)};
	const auto effect{make_effect(mixer.mixer(), 11025 / 4, 11025)};
	BOOST_REQUIRE(mixer.start(effect.get(), {.left = 1.0f, .right = 0.0f}, false, 64).has_value());
	mixer.commit_frame();
	const auto out{generate(mixer.mixer(), 24000)};
	std::size_t sounding{0};
	for (std::size_t i{0}; i < out.size(); i += 2)
		if (out[i] > effect_value / 2)
			++sounding;
	/* A quarter second: 12000 frames, give or take the resampler's edges. */
	BOOST_TEST(sounding > 11900u);
	BOOST_TEST(sounding < 12100u);
}

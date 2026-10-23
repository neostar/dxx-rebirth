/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The SDL_GPU device's tests on a real GPU (#24): a headless device clears
 * its offscreen target and reads it back.  A GPU program (ADR 5): it runs
 * only through tools/dev/gpu-validate.sh, which fails it on any validation
 * message, from check.sh's gpu_tests step (tools/dev/gpu-programs).  Its
 * main() keeps the process a background app; the device never does that.
 */

#include "common/render/sdlgpu_device.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "common/render/device.h"
#include "common/render/readback.h"
#include "common/sdl/dxx-sdl.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_NO_MAIN
#define BOOST_TEST_MODULE Rebirth sdlgpu device
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;
namespace sdlgpu = dcx::render::sdlgpu;

namespace {

/* SDL_CreateGPUDevice needs the video subsystem (spike #55, K1), though no
 * window is made; one per test, so each test starts from a fresh SDL.
 */
struct video_fixture
{
	video_fixture()
	{
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
			throw std::runtime_error(SDL_GetError());
	}
	~video_fixture()
	{
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}
	video_fixture(const video_fixture &) = delete;
	video_fixture &operator=(const video_fixture &) = delete;
	video_fixture(video_fixture &&) = delete;
	video_fixture &operator=(video_fixture &&) = delete;
};

/* An asymmetric clear colour, so that a channel swap or a missed clear
 * shows (the spike's K1).
 */
constexpr std::array<std::uint8_t, 4> clear_rgba{{32, 64, 96, 255}};

[[nodiscard]]
sdlgpu::config test_config(const render::extent target)
{
	return {.target = target, .debug_mode = true, .driver = {}, .clear_rgba = clear_rgba};
}

/* M6 (research #71): how long the pipeline set took to build from MSL. */
void print_m6(const char *which, const sdlgpu::device &d)
{
	std::cout << "sdlgpu: M6, " << which << ": pipeline set of " << sdlgpu::pipeline_set_size
	          << " built in "
	          << std::chrono::duration<double, std::milli>(d.pipeline_build_time()).count()
	          << " ms\n";
}

}

BOOST_FIXTURE_TEST_SUITE(sdlgpu_device, video_fixture)

/* Test that the device holds its closed pipeline set, its samplers and its
 * two targets while it lives, and nothing of any kind after it is gone
 * (the resource-leak gate, #56).
 */
BOOST_AUTO_TEST_CASE(no_resource_outlives_the_device)
{
	auto d{sdlgpu::device::create(test_config({64, 48}))};
	const auto ledger{d->ledger()};
	const auto live{ledger->counts()};
	BOOST_TEST(live.pipelines == sdlgpu::pipeline_set_size);
	BOOST_TEST(live.samplers == 2u);
	/* Colour, depth and the presented copy. */
	BOOST_TEST(live.textures == 3u);
	/* The shaders are released once the pipeline set is built. */
	BOOST_TEST(live.shaders == 0u);
	print_m6("first device", *d);
	d.reset();
	BOOST_TEST((ledger->counts() == sdlgpu::resource_counts{}));
}

/* Test that the device reports the caps the probe found: anisotropy on a
 * GPU that has D32_FLOAT depth, which every Metal GPU has.
 */
BOOST_AUTO_TEST_CASE(device_caps_come_from_the_probe)
{
	auto d{sdlgpu::device::create(test_config({8, 8}))};
	BOOST_TEST(d->caps().anisotropic);
	BOOST_TEST(d->probed().max_anisotropy == 16.f);
	BOOST_TEST(d->probed().supports_samples(1));
}

BOOST_AUTO_TEST_SUITE_END()

/* Test that the ledger counts a release of a kind with none alive as bad
 * instead of wrapping its count.
 */
BOOST_AUTO_TEST_CASE(ledger_counts_a_double_release)
{
	sdlgpu::resource_ledger l;
	l.created(sdlgpu::resource_ledger::kind::shader);
	l.released(sdlgpu::resource_ledger::kind::shader);
	l.released(sdlgpu::resource_ledger::kind::shader);
	BOOST_TEST(l.counts().shaders == 0u);
	BOOST_TEST(l.counts().bad_releases == 1u);
}

int main(const int argc, char **const argv)
{
	/* Video init on macOS makes the process a foreground app that takes
	 * focus; this test stays in the background.  Here, never in the
	 * device: the game is a foreground app.  Overriding, since SDL refuses a
	 * plain hint that the environment (no-window.sh) already sets.
	 */
	if (!SDL_SetHintWithPriority(SDL_HINT_MAC_BACKGROUND_APP, "1", SDL_HINT_OVERRIDE))
	{
		std::cerr << "test-sdlgpu: SDL_SetHintWithPriority: " << SDL_GetError() << '\n';
		return EXIT_FAILURE;
	}
	return boost::unit_test::unit_test_main([] { return true; }, argc, argv);
}

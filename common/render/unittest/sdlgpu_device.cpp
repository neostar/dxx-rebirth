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

/* Test that the first frame read back is the clear colour, in every pixel:
 * SDL's Metal backend can show garbage in a first frame (SDL #8070).
 */
BOOST_AUTO_TEST_CASE(first_frame_is_the_clear_colour)
{
	auto d{sdlgpu::device::create(test_config({64, 48}))};
	print_m6("first device", *d);
	d->clear({.color = true, .depth = true});
	d->present();
	std::vector<std::uint8_t> pixels(render::image_bytes({64, 48}, render::pixel_format::rgba8));
	d->read_pixels(render::read_source::presented, {.x = 0, .y = 0, .size = {64, 48}},
	               render::pixel_format::rgba8, pixels);
	std::size_t wrong{0};
	for (std::size_t i{0}; i < pixels.size(); ++i)
		if (pixels[i] != clear_rgba[i % 4])
			++wrong;
	BOOST_TEST(wrong == 0u);
}

/* Test that the image being drawn reads back before a present, as RGB when
 * asked, and that a rectangle reaching past the target's corner reads zero
 * outside it.
 */
BOOST_AUTO_TEST_CASE(drawing_reads_back_clipped_with_zero_outside)
{
	auto d{sdlgpu::device::create(test_config({64, 48}))};
	d->clear({.color = true, .depth = true});
	/* 4x4 at (62, 46): 2x2 inside, at the rectangle's top left. */
	std::array<std::uint8_t, std::size_t{4} * 4 * 3> rgb{};
	rgb.fill(0xaa);
	d->read_pixels(render::read_source::drawing, {.x = 62, .y = 46, .size = {4, 4}},
	               render::pixel_format::rgb8, rgb);
	std::array<std::uint8_t, std::size_t{4} * 4 * 3> expected{};
	for (const std::size_t pixel : {0u, 1u, 4u, 5u})
		for (std::size_t c{0}; c < 3; ++c)
			expected[pixel * 3 + c] = clear_rgba[c];
	BOOST_TEST(rgb == expected, boost::test_tools::per_element());
}

/* Test that a readback of a buffer of the wrong size throws and that one
 * wholly outside the target reads zero.
 */
BOOST_AUTO_TEST_CASE(read_pixels_checks_its_buffer)
{
	auto d{sdlgpu::device::create(test_config({64, 48}))};
	d->clear({.color = true, .depth = true});
	std::array<std::uint8_t, 15> short_buffer{};
	BOOST_CHECK_THROW(d->read_pixels(render::read_source::drawing, {.x = 0, .y = 0, .size = {2, 2}},
	                                 render::pixel_format::rgba8, short_buffer),
	                  std::length_error);
	std::array<std::uint8_t, 16> outside{};
	outside.fill(0xaa);
	d->read_pixels(render::read_source::drawing, {.x = 64, .y = 0, .size = {2, 2}},
	               render::pixel_format::rgba8, outside);
	const std::array<std::uint8_t, 16> zero{};
	BOOST_TEST(outside == zero, boost::test_tools::per_element());
}

/* Test that the device holds its closed pipeline set, its samplers and its
 * two targets while it lives, and nothing of any kind after it is gone,
 * also after readbacks (the resource-leak gate, #56).
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
	d->clear({.color = true, .depth = true});
	d->present();
	std::array<std::uint8_t, 4> pixel{};
	d->read_pixels(render::read_source::presented, {.x = 0, .y = 0, .size = {1, 1}},
	               render::pixel_format::rgba8, pixel);
	BOOST_TEST(ledger->counts().buffers == 0u);
	print_m6("later device", *d);
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

/* Test that the presented image reads zero before the first present, and
 * that a clear queued after a present leaves it as presented until the
 * next present, while the image being drawn reads the clear.
 */
BOOST_AUTO_TEST_CASE(presented_reads_what_was_presented)
{
	auto d{sdlgpu::device::create(test_config({8, 8}))};
	std::array<std::uint8_t, 4> pixel{};
	pixel.fill(0xaa);
	d->read_pixels(render::read_source::presented, {.x = 3, .y = 3, .size = {1, 1}},
	               render::pixel_format::rgba8, pixel);
	const std::array<std::uint8_t, 4> zero{};
	BOOST_TEST(pixel == zero, boost::test_tools::per_element());
	d->clear({.color = true, .depth = true});
	d->present();
	/* Queued, not encoded: the presented image keeps the first clear. */
	d->clear({.color = true, .depth = false});
	d->read_pixels(render::read_source::presented, {.x = 3, .y = 3, .size = {1, 1}},
	               render::pixel_format::rgba8, pixel);
	BOOST_TEST(pixel == clear_rgba, boost::test_tools::per_element());
}

/* Test that a device whose configuration does not change its clear colour
 * clears to device.h's (0, 0, 0, 0), and reads that back from the image
 * being drawn before any present.
 */
BOOST_AUTO_TEST_CASE(default_clear_is_zero)
{
	const sdlgpu::config c{.target = {8, 8}, .debug_mode = true, .driver = {}};
	auto d{sdlgpu::device::create(c)};
	d->clear({.color = true, .depth = true});
	std::array<std::uint8_t, 4> pixel{};
	pixel.fill(0xaa);
	d->read_pixels(render::read_source::drawing, {.x = 0, .y = 0, .size = {1, 1}},
	               render::pixel_format::rgba8, pixel);
	const std::array<std::uint8_t, 4> zero{};
	BOOST_TEST(pixel == zero, boost::test_tools::per_element());
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

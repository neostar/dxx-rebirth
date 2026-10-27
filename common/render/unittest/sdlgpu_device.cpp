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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/render/ambient.h"
#include "common/render/device.h"
#include "common/render/readback.h"
#include "common/render/render_device.h"
#include "common/render/replay.h"
#include "common/render/rtrace/sample_trace.h"
#include "common/render/trace.h"
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

constexpr render::extent target{.width = 64, .height = 48};
constexpr std::array<float, 2> no_uv{{0, 0}};

/* An untextured quad in the game's 2D projection from row y0 (0 the
 * bottom, 1 the top) up, in colour rgba, drawn with state s over the whole
 * target.  The call views the quad's vertices.
 */
struct quad
{
	std::array<render::vertex, 4> vertices;
	render::draw_call call;
	explicit quad(const std::array<float, 4> &rgba, const float y0 = 0,
	              const render::draw_state &s = {})
	    : vertices{{
	          {.pos = {{0, y0, 0}}, .rgba = rgba, .uv = no_uv},
	          {.pos = {{1, y0, 0}}, .rgba = rgba, .uv = no_uv},
	          {.pos = {{1, 1, 0}}, .rgba = rgba, .uv = no_uv},
	          {.pos = {{0, 1, 0}}, .rgba = rgba, .uv = no_uv},
	      }},
	      call{.topology = render::topology::fan,
		       .vertices = vertices,
		       .texture = render::texture_slot::none,
		       .state = s,
		       .viewport = {.x = 0, .y = 0, .size = target},
		       .transforms = {.projection = render::screen_ortho, .modelview = render::identity{}}}
	{}
	quad(const quad &) = delete;
	quad &operator=(const quad &) = delete;
	quad(quad &&) = delete;
	quad &operator=(quad &&) = delete;
	~quad() = default;
};

/* The image being drawn on d, a device for target, as RGB. */
[[nodiscard]]
std::vector<std::uint8_t> drawing(render::device &d)
{
	std::vector<std::uint8_t> rgb(render::image_bytes(target, render::pixel_format::rgb8));
	d.read_pixels(render::read_source::drawing, {.x = 0, .y = 0, .size = target},
	              render::pixel_format::rgb8, rgb);
	return rgb;
}

/* How many pixels of rgb are not p. */
[[nodiscard]]
std::size_t pixels_other_than(const std::span<const std::uint8_t> rgb,
                              const std::array<std::uint8_t, 3> p)
{
	std::size_t n{0};
	for (std::size_t i{0}; i + 2 < rgb.size(); i += 3)
		if (rgb[i] != p[0] || rgb[i + 1] != p[1] || rgb[i + 2] != p[2])
			++n;
	return n;
}

constexpr std::array<std::uint8_t, 3> clear_rgb{{clear_rgba[0], clear_rgba[1], clear_rgba[2]}};

constexpr std::array<std::uint8_t, 3> red{{255, 0, 0}};
constexpr std::array<std::uint8_t, 3> green{{0, 255, 0}};
constexpr std::array<std::uint8_t, 3> blue{{0, 0, 255}};
constexpr std::array<std::uint8_t, 3> white{{255, 255, 255}};

/* A 2x2 texture image, nearest-filtered unless asked otherwise, and the
 * pixels it views: the checker's first row, at v = 0, red then green; its
 * second blue then white.
 */
struct texture_pixels
{
	std::array<std::uint8_t, 16> rgba;
	render::texture_image image;
	explicit texture_pixels(const std::array<std::uint8_t, 16> &p,
	                        const render::filter f = render::filter::nearest)
	    : rgba{p}, image{{.size = {2, 2},
		                  .format = render::texture_format::rgba8,
		                  .filter = f,
		                  .anisotropic = false,
		                  .has_transparency = false},
		                 rgba}
	{}
	texture_pixels(const texture_pixels &) = delete;
	texture_pixels &operator=(const texture_pixels &) = delete;
	texture_pixels(texture_pixels &&) = delete;
	texture_pixels &operator=(texture_pixels &&) = delete;
	~texture_pixels() = default;
};

[[nodiscard]]
texture_pixels checker(const render::filter f = render::filter::nearest)
{
	return texture_pixels{{{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}},
	                      f};
}

/* A white quad from row y0 up over the whole target, textured from slot
 * with uv 0 to 1 across the target.
 */
struct textured_quad : quad
{
	textured_quad(const render::texture_slot slot, const float y0 = 0) : quad{{{1, 1, 1, 1}}, y0}
	{
		vertices[0].uv = {{0, 0}};
		vertices[1].uv = {{1, 0}};
		vertices[2].uv = {{1, 1}};
		vertices[3].uv = {{0, 1}};
		vertices[0].pos[1] = 0;
		vertices[1].pos[1] = 0;
		call.texture = slot;
		/* The quad's lower rows lie under y0's viewport cut, not its uv. */
		if (y0 > 0)
			call.viewport = {.x = 0,
			                 .y = static_cast<std::int32_t>(static_cast<float>(target.height) * y0),
			                 .size = {.width = target.width,
			                          .height = static_cast<std::uint32_t>(
			                              static_cast<float>(target.height) * (1 - y0))}};
	}
};

/* How many pixels of the w x h block at column x, row y (from the top) of
 * rgb, a target-sized RGB image, are not p.
 */
[[nodiscard]]
std::size_t block_other_than(const std::span<const std::uint8_t> rgb, const std::uint32_t x,
                             const std::uint32_t y, const std::uint32_t w, const std::uint32_t h,
                             const std::array<std::uint8_t, 3> p)
{
	std::size_t n{0};
	for (std::uint32_t row{y}; row != y + h; ++row)
		n += pixels_other_than(
		    rgb.subspan((std::size_t{row} * target.width + x) * 3, std::size_t{w} * 3), p);
	return n;
}

/* The frame hashes of trace, replayed on a Metal device that clears to the
 * game's colour, as GL's replay clears.
 */
[[nodiscard]]
std::vector<std::uint64_t> replayed_hashes(const std::vector<std::uint8_t> &trace)
{
	render::trace::reader r{trace};
	const auto h{r.header()};
	BOOST_REQUIRE(h.has_value());
	auto d{sdlgpu::device::create(
	    {.target = h.value().target, .debug_mode = true, .driver = {}, .clear_rgba = {}})};
	BOOST_REQUIRE(d->driver() == "metal");
	std::vector<std::uint64_t> hashes;
	const auto done{render::replay(
	    r, *d, [&hashes](const render::replayed_frame &f) { hashes.push_back(f.hash); })};
	BOOST_TEST(done.has_value());
	return hashes;
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

/* Test that the device holds its closed pipeline and sampler sets and its
 * targets while it lives, and nothing of any kind after it is gone, also
 * after a texture and readbacks (the resource-leak gate, #56).
 */
BOOST_AUTO_TEST_CASE(no_resource_outlives_the_device)
{
	auto d{sdlgpu::device::create(test_config({64, 48}))};
	const auto ledger{d->ledger()};
	const auto live{ledger->counts()};
	BOOST_TEST(live.pipelines == sdlgpu::pipeline_set_size);
	BOOST_TEST(live.samplers == sdlgpu::sampler_set_size);
	/* Colour, depth, the presented copy and the white texture. */
	BOOST_TEST(live.textures == 4u);
	/* The shaders are released once the pipeline set is built. */
	BOOST_TEST(live.shaders == 0u);
	const auto image{checker()};
	d->texture({1}, &image.image);
	BOOST_TEST(ledger->counts().textures == 5u);
	d->texture({1}, nullptr);
	BOOST_TEST(ledger->counts().textures == 4u);
	d->texture({1}, &image.image);
	d->clear({.color = true, .depth = true});
	d->present();
	std::array<std::uint8_t, 4> pixel{};
	d->read_pixels(render::read_source::presented, {.x = 0, .y = 0, .size = {1, 1}},
	               render::pixel_format::rgba8, pixel);
	/* The transfer ring the texture went through; no vertex buffer, with
	 * no vertices drawn.
	 */
	BOOST_TEST(ledger->counts().buffers == 1u);
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
	BOOST_TEST(d->probed().anisotropy_with_nearest);
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

/* Test that an untextured quad over the whole target reads back in its
 * colour, in every pixel.
 */
BOOST_AUTO_TEST_CASE(untextured_quad_reads_back_its_colour)
{
	auto d{sdlgpu::device::create(test_config(target))};
	d->clear({.color = true, .depth = true});
	const quad q{{{1, 0, 0, 1}}};
	d->draw(q.call);
	BOOST_TEST(pixels_other_than(drawing(*d), {{255, 0, 0}}) == 0u);
}

/* Test that GL's bottom-left origin holds: a quad over the top half fills
 * the first half of the rows read back, and a GL viewport of the bottom
 * half puts a full quad in the last half.
 */
BOOST_AUTO_TEST_CASE(draws_keep_gls_orientation)
{
	auto d{sdlgpu::device::create(test_config(target))};
	d->clear({.color = true, .depth = true});
	const quad top{{{1, 0, 0, 1}}, 0.5f};
	d->draw(top.call);
	quad bottom{{{0, 1, 0, 1}}};
	bottom.call.viewport = {.x = 0, .y = 0, .size = {.width = 64, .height = 24}};
	d->draw(bottom.call);
	const auto rgb{drawing(*d)};
	const std::span<const std::uint8_t> all{rgb};
	BOOST_TEST(pixels_other_than(all.first(all.size() / 2), {{255, 0, 0}}) == 0u);
	BOOST_TEST(pixels_other_than(all.subspan(all.size() / 2), {{0, 255, 0}}) == 0u);
}

/* Test that a quad whose alpha is under its alpha reference is discarded,
 * that a normal-blended half-alpha quad mixes with the clear colour and
 * that one drawn without blending replaces it: each draw state takes its
 * own pipeline.
 */
BOOST_AUTO_TEST_CASE(alpha_test_and_blend)
{
	auto d{sdlgpu::device::create(test_config(target))};
	d->clear({.color = true, .depth = true});
	const quad faint{{{1, 1, 1, 0.01f}}, 0, {.blend = render::blend::none}};
	d->draw(faint.call);
	BOOST_TEST(pixels_other_than(drawing(*d), clear_rgb) == 0u);
	const quad half{{{1, 1, 1, 0.5f}}};
	d->draw(half.call);
	const auto rgb{drawing(*d)};
	/* 0.5 * 255 + 0.5 * c, rounded either way by the GPU. */
	for (std::size_t c{0}; c != 3; ++c)
	{
		const int expected{(255 + clear_rgba[c]) / 2};
		BOOST_TEST(std::abs(int{rgb[c]} - expected) <= 1);
	}
	const quad unblended{{{1, 1, 1, 0.5f}}, 0, {.blend = render::blend::none}};
	d->draw(unblended.call);
	BOOST_TEST(pixels_other_than(drawing(*d), {{255, 255, 255}}) == 0u);
}

namespace {

/* The first pixel of an RGB image. */
[[nodiscard]]
std::array<int, 3> first_pixel(const std::vector<std::uint8_t> &rgb)
{
	return {{rgb[0], rgb[1], rgb[2]}};
}

/* The first pixel after d drew an opaque base of (0.5, 0.4, 0.3) and then a
 * quad of rgba under blend b, the way the game's flash follows a scene.
 */
[[nodiscard]]
std::array<int, 3> blended_over_base(const render::blend b, const std::array<float, 4> &rgba)
{
	auto d{sdlgpu::device::create(test_config(target))};
	d->clear({.color = true, .depth = true});
	const quad base{{{0.5f, 0.4f, 0.3f, 1}}, 0, {.blend = render::blend::none}};
	d->draw(base.call);
	const quad over{rgba, 0, {.blend = b}};
	d->draw(over.call);
	const auto rgb{drawing(*d)};
	BOOST_TEST(pixels_other_than(rgb, {{rgb[0], rgb[1], rgb[2]}}) == 0u);
	return first_pixel(rgb);
}

/* Check that each channel of pixel is within one level of expected, which
 * the GPU may round either way; a failure names the channel and both values.
 */
void expect_pixel(const std::array<int, 3> &pixel, const std::array<int, 3> &expected)
{
	for (std::size_t c{0}; c != 3; ++c)
		BOOST_TEST(std::abs(pixel[c] - expected[c]) <= 1,
		           "channel " << c << " is " << pixel[c] << ", expected " << expected[c]);
}

}

/* Test that the weapon-hit palette flash, additive_c of the mixed-sign
 * (0.6, -0.1, -0.2), adds to the scene and clamps at both ends, as GL's
 * unsigned colour does: (0.5, 0.4, 0.3) becomes (1, 0.4, 0.3).
 */
BOOST_AUTO_TEST_CASE(red_flash_adds_and_clamps)
{
	expect_pixel(blended_over_base(render::blend::additive_c, {{0.6f, -0.1f, -0.2f, 1}}),
	             {{255, 102, 77}});
}

/* Test that the negative palette flash, palfx_neg of (0.754, 0.397, 0) (a
 * brightness of (-19, -10, 0) out of 63, scaled by 2.5), scales the scene
 * by one minus the flash: (0.5, 0.4, 0.3) becomes (0.123, 0.241, 0.3).
 */
BOOST_AUTO_TEST_CASE(negative_flash_darkens_by_the_flash)
{
	expect_pixel(blended_over_base(render::blend::palfx_neg, {{0.754f, 0.397f, 0, 1}}),
	             {{31, 61, 77}});
}

/* Test that additive_a adds the colour scaled by its alpha, normal mixes
 * by alpha, and additive_c ignores alpha, over (0.5, 0.4, 0.3): (0, 1, 0)
 * at 0.25 under additive_a gives (0.5, 0.65, 0.3); (0, 0, 1) at 0.25 under
 * normal gives (0.375, 0.3, 0.475); (0.2, 0.2, 0.2) at 0.25 under
 * additive_c gives (0.7, 0.6, 0.5).
 */
BOOST_AUTO_TEST_CASE(blends_weigh_the_colour_by_alpha_as_gl_does)
{
	expect_pixel(blended_over_base(render::blend::additive_a, {{0, 1, 0, 0.25f}}),
	             {{128, 166, 77}});
	expect_pixel(blended_over_base(render::blend::normal, {{0, 0, 1, 0.25f}}), {{96, 77, 121}});
	expect_pixel(blended_over_base(render::blend::additive_c, {{0.2f, 0.2f, 0.2f, 0.25f}}),
	             {{179, 153, 128}});
}

/* Test that draws in every state of the pipeline set take a pipeline of
 * the set built at creation, none new, and that the frame's transfer and
 * vertex buffers die with the device.
 */
BOOST_AUTO_TEST_CASE(draws_take_pipelines_of_the_set)
{
	auto d{sdlgpu::device::create(test_config(target))};
	const auto ledger{d->ledger()};
	d->clear({.color = true, .depth = true});
	for (const auto b : {render::blend::normal, render::blend::additive_a,
	                     render::blend::additive_c, render::blend::palfx_neg, render::blend::none})
		for (const auto z : {render::depth::off, render::depth::test_and_write})
			for (const auto c : {render::cull::none, render::cull::front})
			{
				const quad q{{{0.2f, 0.2f, 0.2f, 1}}, 0, {.blend = b, .depth = z, .cull = c}};
				d->draw(q.call);
			}
	d->present();
	const auto live{ledger->counts()};
	BOOST_TEST(live.pipelines == sdlgpu::pipeline_set_size);
	BOOST_TEST(live.shaders == 0u);
	BOOST_TEST(live.buffers == 2u);
	d.reset();
	BOOST_TEST((ledger->counts() == sdlgpu::resource_counts{}));
}

/* Test R3 on the first synthetic trace: the sample trace's frames
 * (sample_trace.h, the writer of the committed
 * common/render/rtrace/synthetic/sample.rtr) hash as GL draws them.  The
 * hashes are the ones tools/dev/rtrace-tests.sh pins, from Apple's GL on
 * an M1 Max, which runs on Metal: they hold bit for bit only for SDL_GPU on
 * Metal, which the test asserts; on lavapipe (#25) R3 compares with FLIP.
 */
BOOST_AUTO_TEST_CASE(sample_trace_frames_match_gl)
{
	const std::vector<std::uint64_t> gl{0x86388f1d5003af2au, 0x9f40fbb36ef64325u};
	BOOST_TEST(replayed_hashes(dcx::render::rtrace::sample_trace()) == gl,
	           boost::test_tools::per_element());
}

/* Test R3 on the overbright trace (#27): textured quads, repeated and
 * clamped, RGBA and RGB, and an untextured triangle, under vertex colours
 * above 1, hash as GL draws them, which clamps each vertex's colour before
 * interpolating (spike T9, vclamp_off).  Pinned as the sample's are.
 */
BOOST_AUTO_TEST_CASE(overbright_trace_frames_match_gl)
{
	const std::vector<std::uint64_t> gl{0x5d1b6b445fb7f05au, 0x418a435cc9c67e56u};
	BOOST_TEST(replayed_hashes(dcx::render::rtrace::overbright_trace()) == gl,
	           boost::test_tools::per_element());
}

/* Test R3 on the flash trace (#30): the red and the negative palette flash
 * and the translucent blends over a scene hash as GL draws them.  Pinned as
 * the sample's are.
 */
BOOST_AUTO_TEST_CASE(flash_trace_frames_match_gl)
{
	const std::vector<std::uint64_t> gl{0xa088becd21e46f87u, 0x4142e7869f743d59u,
	                                    0xc33733e407fec502u};
	BOOST_TEST(replayed_hashes(dcx::render::rtrace::flash_trace()) == gl,
	           boost::test_tools::per_element());
}

/* Test that the transfer ring grows on a GPU: after a small frame that is
 * presented and not waited for, a frame of 2000 quads (432 000 bytes of
 * vertices, past the first 64 KiB) draws its first and last quads where
 * they belong, with still one transfer and one vertex buffer, and a small
 * frame after it keeps their size.
 */
BOOST_AUTO_TEST_CASE(transfer_ring_grows_and_keeps_its_size)
{
	auto d{sdlgpu::device::create(test_config(target))};
	const auto ledger{d->ledger()};
	d->clear({.color = true, .depth = true});
	const quad small{{{0, 0, 1, 1}}};
	d->draw(small.call);
	d->present();
	const auto first{d->upload_buffer_sizes()};
	BOOST_TEST(first.vertices == 65536u);
	d->clear({.color = true, .depth = true});
	std::vector<std::unique_ptr<quad>> quads;
	for (int i{0}; i != 1999; ++i)
		quads.push_back(std::make_unique<quad>(std::array<float, 4>{{1, 0, 0, 1}}));
	quads.push_back(std::make_unique<quad>(std::array<float, 4>{{0, 1, 0, 1}}, 0.5f));
	for (const auto &q : quads)
		d->draw(q->call);
	const auto rgb{drawing(*d)};
	const std::span<const std::uint8_t> all{rgb};
	BOOST_TEST(pixels_other_than(all.first(all.size() / 2), {{0, 255, 0}}) == 0u);
	BOOST_TEST(pixels_other_than(all.subspan(all.size() / 2), {{255, 0, 0}}) == 0u);
	BOOST_TEST(ledger->counts().buffers == 2u);
	const auto grown{d->upload_buffer_sizes()};
	BOOST_TEST(grown.vertices == 524288u);
	BOOST_TEST(grown.transfer == 524288u);
	d->clear({.color = true, .depth = true});
	d->draw(small.call);
	d->present();
	BOOST_TEST((d->upload_buffer_sizes() == grown));
	BOOST_TEST(ledger->counts().buffers == 2u);
	BOOST_TEST(pixels_other_than(drawing(*d), {{0, 0, 255}}) == 0u);
}

/* Test that front culling culls counter-clockwise faces, GL's front faces:
 * the quad wound counter-clockwise draws nothing, the same quad wound
 * clockwise draws.
 */
BOOST_AUTO_TEST_CASE(front_culling_culls_counter_clockwise)
{
	auto d{sdlgpu::device::create(test_config(target))};
	d->clear({.color = true, .depth = true});
	const quad ccw{{{1, 0, 0, 1}}, 0, {.cull = render::cull::front}};
	d->draw(ccw.call);
	BOOST_TEST(pixels_other_than(drawing(*d), clear_rgb) == 0u);
	quad cw{{{1, 0, 0, 1}}, 0, {.cull = render::cull::front}};
	std::ranges::reverse(cw.vertices);
	d->draw(cw.call);
	BOOST_TEST(pixels_other_than(drawing(*d), {{255, 0, 0}}) == 0u);
}

namespace {

/* A quad over the whole target at depth z (the game's 2D ortho maps z to
 * GL's window depth (1 - z) / 2), in colour rgba, with depth mode m.
 */
struct depth_quad : quad
{
	depth_quad(const std::array<float, 4> &rgba, const float z, const render::depth m)
	    : quad{rgba, 0, {.depth = m}}
	{
		for (auto &v : vertices)
			v.pos[2] = z;
	}
};

}

/* Test that test_and_write keeps the nearer of two draws, whichever comes
 * last, and lets a draw at equal depth pass (LEQUAL), in both halves of
 * the depth range.
 */
BOOST_AUTO_TEST_CASE(depth_test_keeps_the_nearer)
{
	for (const float near_z : {0.5f, -0.2f})
	{
		BOOST_TEST_CONTEXT("near z " << near_z)
		{
			const float far_z{near_z - 0.4f};
			auto d{sdlgpu::device::create(test_config(target))};
			d->clear({.color = true, .depth = true});
			const depth_quad near_red{{{1, 0, 0, 1}}, near_z, render::depth::test_and_write};
			const depth_quad far_green{{{0, 1, 0, 1}}, far_z, render::depth::test_and_write};
			d->draw(near_red.call);
			d->draw(far_green.call);
			BOOST_TEST(pixels_other_than(drawing(*d), {{255, 0, 0}}) == 0u);
			const depth_quad equal_blue{{{0, 0, 1, 1}}, near_z, render::depth::test_and_write};
			d->draw(equal_blue.call);
			BOOST_TEST(pixels_other_than(drawing(*d), {{0, 0, 255}}) == 0u);
		}
	}
}

/* Test that a draw with depth off writes no depth: a farther draw with
 * test_and_write after it still passes against the cleared depth.
 */
BOOST_AUTO_TEST_CASE(depth_off_writes_no_depth)
{
	auto d{sdlgpu::device::create(test_config(target))};
	d->clear({.color = true, .depth = true});
	const depth_quad near_red{{{1, 0, 0, 1}}, 0.5f, render::depth::off};
	const depth_quad far_green{{{0, 1, 0, 1}}, -0.5f, render::depth::test_and_write};
	d->draw(near_red.call);
	d->draw(far_green.call);
	BOOST_TEST(pixels_other_than(drawing(*d), {{0, 255, 0}}) == 0u);
}

/* Test that a textured quad samples its texture as GL does: the texture's
 * first row at v = 0, the bottom of the target, read back last; nearest
 * filtering gives four solid quadrants.
 */
BOOST_AUTO_TEST_CASE(textured_quad_samples_its_texture)
{
	auto d{sdlgpu::device::create(test_config(target))};
	const auto image{checker()};
	d->texture({1}, &image.image);
	d->clear({.color = true, .depth = true});
	const textured_quad q{{1}};
	d->draw(q.call);
	const auto rgb{drawing(*d)};
	BOOST_TEST(block_other_than(rgb, 0, 24, 32, 24, red) == 0u);
	BOOST_TEST(block_other_than(rgb, 32, 24, 32, 24, green) == 0u);
	BOOST_TEST(block_other_than(rgb, 0, 0, 32, 24, blue) == 0u);
	BOOST_TEST(block_other_than(rgb, 32, 0, 32, 24, white) == 0u);
}

/* Test that a trilinear texture magnifies linearly: the checker drawn over
 * the target is solid near each quadrant's centre, where the clamped
 * filter reads one texel, and blends across the boundary between red and
 * green, where nearest filtering would give one or the other.
 */
BOOST_AUTO_TEST_CASE(trilinear_texture_blends_when_magnified)
{
	auto d{sdlgpu::device::create(test_config(target))};
	const auto image{checker(render::filter::trilinear)};
	d->texture({1}, &image.image);
	d->clear({.color = true, .depth = true});
	const textured_quad q{{1}};
	d->draw(q.call);
	const auto rgb{drawing(*d)};
	/* Row 36 from the top is row 11 from the bottom: texel row 0 alone. */
	BOOST_TEST(block_other_than(rgb, 15, 36, 1, 1, red) == 0u);
	BOOST_TEST(block_other_than(rgb, 48, 36, 1, 1, green) == 0u);
	BOOST_TEST(block_other_than(rgb, 15, 11, 1, 1, blue) == 0u);
	BOOST_TEST(block_other_than(rgb, 48, 11, 1, 1, white) == 0u);
	for (const std::uint32_t x : {31u, 32u})
	{
		BOOST_TEST(block_other_than(rgb, x, 36, 1, 1, red) == 1u);
		BOOST_TEST(block_other_than(rgb, x, 36, 1, 1, green) == 1u);
	}
}

/* Test that a slot given a new image after a draw sampled it, in the same
 * frame, keeps the earlier draw's image: the top half drawn before the
 * change shows the checker, the bottom half after it the new all-green
 * image.
 */
BOOST_AUTO_TEST_CASE(texture_changed_mid_frame_keeps_earlier_draws)
{
	auto d{sdlgpu::device::create(test_config(target))};
	const auto image{checker()};
	d->texture({1}, &image.image);
	d->clear({.color = true, .depth = true});
	const textured_quad top{{1}, 0.5f};
	d->draw(top.call);
	const texture_pixels all_green{
	    {{0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255}}};
	d->texture({1}, &all_green.image);
	textured_quad bottom{{1}};
	bottom.call.viewport = {.x = 0, .y = 0, .size = {.width = 64, .height = 24}};
	d->draw(bottom.call);
	d->present();
	std::vector<std::uint8_t> rgb(render::image_bytes(target, render::pixel_format::rgb8));
	d->read_pixels(render::read_source::presented, {.x = 0, .y = 0, .size = target},
	               render::pixel_format::rgb8, rgb);
	BOOST_TEST(block_other_than(rgb, 0, 0, 32, 12, blue) == 0u);
	BOOST_TEST(block_other_than(rgb, 32, 0, 32, 12, white) == 0u);
	BOOST_TEST(block_other_than(rgb, 0, 12, 32, 12, red) == 0u);
	BOOST_TEST(block_other_than(rgb, 0, 24, 64, 24, green) == 0u);
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

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/caps.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "common/render/sdlgpu_probe.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth caps
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;
namespace sdlgpu = dcx::render::sdlgpu;

/* Test that a GL context without GL_EXT_texture_filter_anisotropic, whose
 * limit the GL layer leaves at 0, has no anisotropy.
 */
BOOST_AUTO_TEST_CASE(gl_caps_no_anisotropy_without_extension)
{
	BOOST_TEST(!render::gl_caps(0.0f).anisotropic);
}

/* Test that a limit of exactly 1, which filters as without anisotropy, has
 * no anisotropy.
 */
BOOST_AUTO_TEST_CASE(gl_caps_no_anisotropy_at_limit_one)
{
	BOOST_TEST(!render::gl_caps(1.0f).anisotropic);
}

/* Test that a limit above 1 has anisotropy. */
BOOST_AUTO_TEST_CASE(gl_caps_anisotropy_above_one)
{
	BOOST_TEST(render::gl_caps(1.5f).anisotropic);
	BOOST_TEST(render::gl_caps(16.0f).anisotropic);
}

/* Test that quad-buffer stereo is a cap of a desktop GL context with
 * stereo buffers only, and copy_region of any desktop GL context.
 */
BOOST_AUTO_TEST_CASE(gl_caps_stereo_needs_desktop_gl_with_stereo_buffers)
{
	const auto caps_of{[](const bool stereo_buffers, const bool desktop) {
		return render::gl_caps(
		    {.max_anisotropy = 0.f, .stereo_buffers = stereo_buffers, .desktop = desktop});
	}};
	BOOST_TEST(caps_of(true, true).quad_buffer_stereo);
	BOOST_TEST(!caps_of(false, true).quad_buffer_stereo);
	BOOST_TEST(!caps_of(true, false).quad_buffer_stereo);
	BOOST_TEST(caps_of(false, true).copy_region);
	BOOST_TEST(!caps_of(true, false).copy_region);
	BOOST_TEST(!render::gl_caps(16.0f).quad_buffer_stereo);
}

namespace {

/* A device that supports the depth formats and sample counts given, kept
 * by value: an initializer_list's elements end with the call.
 */
[[nodiscard]]
sdlgpu::format_queries device_with(const std::initializer_list<sdlgpu::depth_format> depths,
                                   const std::initializer_list<std::uint32_t> samples)
{
	return {
	    .depth_supported =
	        [d = std::vector(depths)](const sdlgpu::depth_format f) {
		        return std::ranges::contains(d, f);
	        },
	    .samples_supported = [s = std::vector(samples)](
	                             const std::uint32_t n) { return std::ranges::contains(s, n); },
	};
}

}

/* Test that the probe prefers D32_FLOAT depth on a device with both depth
 * formats, as Vulkan devices may have.
 */
BOOST_AUTO_TEST_CASE(probe_prefers_d32_float)
{
	const auto c{sdlgpu::probe_caps(
	    device_with({sdlgpu::depth_format::d24_unorm, sdlgpu::depth_format::d32_float}, {1}), 1)};
	BOOST_REQUIRE(c.has_value());
	BOOST_TEST((c.value().depth == sdlgpu::depth_format::d32_float));
}

/* Test that a device with D24_UNORM depth only fails the probe, naming it,
 * so the game falls back to GL until M5 decides; one without either depth
 * format fails it too.
 */
BOOST_AUTO_TEST_CASE(probe_fails_without_d32_float)
{
	const auto d24{sdlgpu::probe_caps(device_with({sdlgpu::depth_format::d24_unorm}, {1}), 1)};
	BOOST_REQUIRE(!d24.has_value());
	BOOST_TEST((d24.error() == sdlgpu::probe_failure::d24_only));
	const auto none{sdlgpu::probe_caps(device_with({}, {1}), 1)};
	BOOST_REQUIRE(!none.has_value());
	BOOST_TEST((none.error() == sdlgpu::probe_failure::no_depth_format));
}

/* Test that the probe records the sample counts the device supports among
 * 1, 2, 4 and 8: Apple GPUs up to Apple9 give 1 and 4.
 */
BOOST_AUTO_TEST_CASE(probe_records_sample_counts)
{
	const auto c{sdlgpu::probe_caps(device_with({sdlgpu::depth_format::d32_float}, {1, 4}), 1)};
	BOOST_REQUIRE(c.has_value());
	BOOST_TEST(c.value().supports_samples(1));
	BOOST_TEST(!c.value().supports_samples(2));
	BOOST_TEST(c.value().supports_samples(4));
	BOOST_TEST(!c.value().supports_samples(8));
}

/* Test that anisotropy is the requested one clamped to 16, which SDL passes
 * on unclamped, and that 1 or less counts as none in the common caps.
 */
BOOST_AUTO_TEST_CASE(probe_clamps_anisotropy_to_16)
{
	const auto d32{device_with({sdlgpu::depth_format::d32_float}, {1})};
	const auto high{sdlgpu::probe_caps(d32, 32)};
	BOOST_REQUIRE(high.has_value());
	BOOST_TEST(high.value().max_anisotropy == 16.f);
	BOOST_TEST(high.value().common.anisotropic);
	const auto four{sdlgpu::probe_caps(d32, 4)};
	BOOST_REQUIRE(four.has_value());
	BOOST_TEST(four.value().max_anisotropy == 4.f);
	const auto one{sdlgpu::probe_caps(d32, 1)};
	BOOST_REQUIRE(one.has_value());
	BOOST_TEST(!one.value().common.anisotropic);
}

/* Test that anisotropy below 1, zero, negative or NaN asks for none: 1. */
BOOST_AUTO_TEST_CASE(probe_clamps_anisotropy_up_to_1)
{
	const auto d32{device_with({sdlgpu::depth_format::d32_float}, {1})};
	for (const float requested : {0.f, -4.f, std::numeric_limits<float>::quiet_NaN()})
	{
		const auto c{sdlgpu::probe_caps(d32, requested)};
		BOOST_REQUIRE(c.has_value());
		BOOST_TEST(c.value().max_anisotropy == 1.f);
		BOOST_TEST(!c.value().common.anisotropic);
	}
}

/* Test that the probe rejects queries it cannot ask, naming them, instead
 * of calling an empty function.
 */
BOOST_AUTO_TEST_CASE(probe_rejects_missing_queries)
{
	auto q{device_with({sdlgpu::depth_format::d32_float}, {1})};
	q.samples_supported = nullptr;
	BOOST_CHECK_THROW(std::ignore = sdlgpu::probe_caps(q, 1), std::invalid_argument);
	auto r{device_with({sdlgpu::depth_format::d32_float}, {1})};
	r.depth_supported = nullptr;
	BOOST_CHECK_THROW(std::ignore = sdlgpu::probe_caps(r, 1), std::invalid_argument);
}

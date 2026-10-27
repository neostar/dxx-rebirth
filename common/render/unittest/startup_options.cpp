/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/rtrace/startup_options.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth D3D12 startup options
#include <boost/test/unit_test.hpp>

using dcx::render::rtrace::says_gpu_based_validation_on;

/* Test that the debug layer's startup options as Chromium issue 42241494
 * quotes them pass: "is enabled", then "(disabled by default)".
 */
BOOST_AUTO_TEST_CASE(chromium_form_passes)
{
	BOOST_TEST(says_gpu_based_validation_on(
	    "D3D12 MESSAGE: Device Debug Layer Startup Options: GPU-Based Validation is enabled "
	    "(disabled by default). This results in new validation not possible during API calls "
	    "on the CPU, by creating patched shaders that have validation added directly to the "
	    "shader. However, it can slow things down a lot, especially for applications with "
	    "numerous PSOs."));
}

/* Test the other forms that say on: a ':' or '=' and enabled, on or true,
 * with or without "is", in any case.
 */
BOOST_AUTO_TEST_CASE(other_on_forms_pass)
{
	BOOST_TEST(says_gpu_based_validation_on("GPU-Based Validation: Enabled"));
	BOOST_TEST(says_gpu_based_validation_on("gpu based validation = TRUE; x"));
	BOOST_TEST(says_gpu_based_validation_on("GPU_Based_Validation is on."));
}

/* Test that a text saying off, an unknown word after the key, a key after
 * another word (Disable, Non-), a missing key and an empty text fail; the
 * token must be enabled, on or true exactly, so "enabled" inside another
 * word does not count.
 */
BOOST_AUTO_TEST_CASE(off_or_missing_fails)
{
	BOOST_TEST(!says_gpu_based_validation_on(
	    "Device Debug Layer Startup Options: GPU-Based Validation is disabled."));
	BOOST_TEST(!says_gpu_based_validation_on("GPU-Based Validation: Disabled"));
	BOOST_TEST(!says_gpu_based_validation_on("GPU-Based Validation: not enabled"));
	BOOST_TEST(!says_gpu_based_validation_on("GPU-Based Validation: enabledness"));
	BOOST_TEST(!says_gpu_based_validation_on("GPU-Based Validation is"));
	BOOST_TEST(!says_gpu_based_validation_on("Disable GPU-Based Validation: true"));
	BOOST_TEST(!says_gpu_based_validation_on("Non-GPU-Based Validation is enabled"));
	BOOST_TEST(!says_gpu_based_validation_on("Device Debug Layer Startup Options: none"));
	BOOST_TEST(!says_gpu_based_validation_on(""));
}

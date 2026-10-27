/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Reading the D3D12 debug layer's startup options message
 * (CREATEDEVICE_DEBUG_LAYER_STARTUP_OPTIONS), which rtrace-gpu prints on
 * Windows to confirm GPU-based validation (#49). Portable, so it is
 * unit-tested where the build runs.
 */

#pragma once

#include <string_view>

namespace dcx::render::rtrace {

/* Whether the startup options text says GPU-based validation is on. Case,
 * spaces, '-' and '_' are ignored; the words "GPU based validation" must be
 * followed, after an optional ':' or '=' and an optional "is", by exactly
 * "enabled", "on" or "true", up to one of ",;.()" or the text's end. Chromium
 * issue 42241494 quotes the text as "GPU-Based Validation is enabled
 * (disabled by default)."
 */
[[nodiscard]]
bool says_gpu_based_validation_on(std::string_view text);

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstdlib>

#include "common/render/gl_errors.h"

namespace dcx {

/* DXX_DBG_FRAMEHASH: the file the debug harness writes one frame hash
 * line to per presented frame; nullptr when unset, which writes no log.
 */
[[nodiscard]]
inline const char *read_harness_frame_hash_path()
{
	return std::getenv("DXX_DBG_FRAMEHASH");
}

}

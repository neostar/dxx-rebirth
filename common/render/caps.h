/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * Caps (GLOSSARY.md, ADR 2): what a renderer supports beyond the common
 * set, so game code asks for a feature instead of reading a GL limit.
 */

#pragma once

namespace dcx::render {

struct caps
{
	/* Anisotropic texture filtering. */
	bool anisotropy;
	constexpr bool operator==(const caps &) const = default;
};

/* The caps of a GL context whose GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT is
 * max_anisotropy (0 without GL_EXT_texture_filter_anisotropic).  The
 * texture upload applies anisotropy only above 1, so 1 counts as none.
 */
[[nodiscard]]
constexpr caps gl_caps(const float max_anisotropy)
{
	return caps{.anisotropy = max_anisotropy > 1.0f};
}

}

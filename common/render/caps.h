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
	bool anisotropic{false};
	/* Quad-buffer stereo: select_eye sends draws to one eye's buffers, and
	 * copy_region can copy the left eye's image to the right's.
	 */
	bool quad_buffer_stereo{false};
	/* copy_region: copies a rectangle of the target to another place. */
	bool copy_region{false};
	constexpr bool operator==(const caps &) const = default;
};

/* What the GL adapter reads from its context when it is created. */
struct gl_context_features
{
	/* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, 0 without
	 * GL_EXT_texture_filter_anisotropic.
	 */
	float max_anisotropy;
	/* GL_STEREO: the context has left and right back buffers. */
	bool stereo_buffers;
	/* Desktop GL, which has glCopyPixels; GLES has not. */
	bool desktop;
};

/* The caps of a GL context with features.  The texture upload applies
 * anisotropy only above 1, so 1 counts as none.  Quad-buffer stereo needs
 * both a stereo context and desktop GL; copy_region needs desktop GL.
 */
[[nodiscard]]
constexpr caps gl_caps(const gl_context_features &features)
{
	return caps{.anisotropic = features.max_anisotropy > 1.0f,
	            .quad_buffer_stereo = features.desktop && features.stereo_buffers,
	            .copy_region = features.desktop};
}

/* The caps of a desktop GL context without a stereo buffer. */
[[nodiscard]]
constexpr caps gl_caps(const float max_anisotropy)
{
	return gl_caps(gl_context_features{
	    .max_anisotropy = max_anisotropy, .stereo_buffers = false, .desktop = true});
}

}

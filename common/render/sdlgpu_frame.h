/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The SDL_GPU adapter's frame logic, without SDL (research #71, Design):
 * the frame record, the pipeline and sampler keys, the bind cache, the
 * upload plan, and the GPU command seam they drive.  sdlgpu_device.cpp gives the
 * seam its SDL_GPU adapter; the tests give it a counting fake, so that the
 * SDL calls a frame makes are counted without a GPU.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "common/render/device.h"
#include "common/render/render_device.h"
#include "common/render/transform_matrix.h"

namespace dcx::render::sdlgpu {

/* What selects a pipeline of the closed set: the draw state's blend, depth
 * and cull, and whether the fragment shader may discard (alpha_test, #29:
 * on Apple GPUs a discarding shader makes opaque draws slower, so draws
 * the alpha test cannot change use the shader without it).  Alpha
 * reference, wrap and line width are uniform, sampler and CPU state
 * (research #71, area 3).
 */
struct pipeline_key
{
	render::blend blend{render::blend::normal};
	render::depth depth{render::depth::off};
	render::cull cull{render::cull::none};
	bool alpha_test{false};
	constexpr bool operator==(const pipeline_key &) const = default;
};

inline constexpr std::size_t blend_count{5};
inline constexpr std::size_t depth_count{2};
inline constexpr std::size_t cull_count{2};
/* The closed pipeline set: one pipeline per key, built at creation.  The
 * (test_and_write, alpha_test false) pipelines are reserved for when
 * needs_alpha_test's depth-tested exemption goes; all 40 cost 476 ms to build
 * with a cold OS shader cache, 7.8-13 ms after.
 */
inline constexpr std::size_t pipeline_set_size{blend_count * depth_count * cull_count * 2};

/* Whether a draw runs the discarding shader.  Where the alpha test can
 * change a pixel: the reference is above 0 (alpha_test_off discards
 * nothing) and either the draw's texture has transparent pixels or a
 * vertex alpha, clamped to [0, 1] as GL clamps it, is below the reference
 * (an interpolated alpha is never below the least vertex alpha); an opaque
 * texture under opaque vertices has alpha 1 throughout.
 *
 * Every depth-tested draw takes the discarding shader as well, for now: on
 * Metal the shader without discard depth-tests early, and there the depth
 * clamp ties every fragment with z_ndc below 0 at the viewport's minimum
 * (the band ADR 2's depth pairing rule records), where the discarding one
 * keeps GL's order; test-sdlgpu's depth_test_keeps_the_nearer and
 * depth_band_keeps_the_nearer go red without this line.  Whether to change
 * the depth model, and so drop it, is the owner's call (#28).
 */
[[nodiscard]]
constexpr bool needs_alpha_test(const draw_state &s, const bool transparent_texture,
                                const std::span<const vertex> vertices)
{
	if (s.depth == render::depth::test_and_write)
		return true;
	if (!(s.alpha_ref > 0))
		return false;
	if (transparent_texture)
		return true;
	for (const auto &v : vertices)
		if (!(v.rgba[3] >= s.alpha_ref))
			return true;
	return false;
}

[[nodiscard]]
constexpr pipeline_key key_of(const draw_state &s, const bool alpha_test)
{
	return {.blend = s.blend, .depth = s.depth, .cull = s.cull, .alpha_test = alpha_test};
}

/* The key's place in the pipeline set, below pipeline_set_size.  Throws
 * std::out_of_range on an enum value outside its members.
 */
[[nodiscard]]
constexpr std::size_t index_of(const pipeline_key &k)
{
	const std::size_t b{std::to_underlying(k.blend)};
	const std::size_t z{std::to_underlying(k.depth)};
	const std::size_t c{std::to_underlying(k.cull)};
	if (b >= blend_count || z >= depth_count || c >= cull_count)
		throw std::out_of_range("sdlgpu: a draw state outside the pipeline set");
	return ((b * depth_count + z) * cull_count + c) * 2 + (k.alpha_test ? 1 : 0);
}

/* The key at index i of the pipeline set: index_of's inverse. */
[[nodiscard]]
constexpr pipeline_key key_at(const std::size_t i)
{
	if (i >= pipeline_set_size)
		throw std::out_of_range("sdlgpu: past the pipeline set");
	return {.blend = static_cast<render::blend>(i / (depth_count * cull_count * 2)),
	        .depth = static_cast<render::depth>(i / (cull_count * 2) % depth_count),
	        .cull = static_cast<render::cull>(i / 2 % cull_count),
	        .alpha_test = i % 2 != 0};
}

/* What selects a sampler of the closed sampler set: the texture's filter
 * and whether it asks for anisotropy (texture_desc), and the draw's wrap.
 * The device builds the set at creation, one sampler per key, the filters
 * as ogl_loadtexture sets them per texture and the wrap as the front end
 * sets it per draw.
 */
struct sampler_key
{
	render::filter filter{render::filter::nearest};
	render::wrap wrap{render::wrap::clamp};
	bool anisotropic{false};
	constexpr bool operator==(const sampler_key &) const = default;
};

inline constexpr std::size_t filter_count{2};
inline constexpr std::size_t wrap_count{2};
/* The closed sampler set: one sampler per key, built at creation. */
inline constexpr std::size_t sampler_set_size{filter_count * wrap_count * 2};

/* The key's place in the sampler set, below sampler_set_size.  Throws
 * std::out_of_range on an enum value outside its members.
 */
[[nodiscard]]
constexpr std::size_t index_of(const sampler_key &k)
{
	const std::size_t f{std::to_underlying(k.filter)};
	const std::size_t w{std::to_underlying(k.wrap)};
	if (f >= filter_count || w >= wrap_count)
		throw std::out_of_range("sdlgpu: a sampler outside the sampler set");
	return (f * wrap_count + w) * 2 + (k.anisotropic ? 1 : 0);
}

/* The key at index i of the sampler set: index_of's inverse. */
[[nodiscard]]
constexpr sampler_key sampler_key_at(const std::size_t i)
{
	if (i >= sampler_set_size)
		throw std::out_of_range("sdlgpu: past the sampler set");
	return {.filter = static_cast<render::filter>(i / (wrap_count * 2)),
	        .wrap = static_cast<render::wrap>(i / 2 % wrap_count),
	        .anisotropic = i % 2 != 0};
}

/* The vertex stage's uniforms (b0, space1): projection, then modelview. */
struct vertex_uniforms
{
	matrix projection;
	matrix modelview;
	constexpr bool operator==(const vertex_uniforms &) const = default;
};

/* The fragment stage's uniforms (b0, space3), as tex_modulate.frag.hlsl
 * lays them out.
 */
struct fragment_uniforms
{
	float alpha_ref;
	std::uint32_t use_texture;
	std::array<float, 2> pad;
	constexpr bool operator==(const fragment_uniforms &) const = default;
};

/* An SDL_GPU viewport: origin at the top left, and the depth range 0.5 to
 * 1 that maps GL's clip depth (pos_color_uv.vert.hlsl).
 */
struct gpu_viewport
{
	float x;
	float y;
	float w;
	float h;
	float min_depth;
	float max_depth;
	constexpr bool operator==(const gpu_viewport &) const = default;
};

/* The GL viewport v, origin at the bottom left of target, as SDL_GPU's. */
[[nodiscard]]
constexpr gpu_viewport viewport_on(const extent target, const viewport &v)
{
	const auto top{static_cast<std::int64_t>(target.height) - v.y -
	               static_cast<std::int64_t>(v.size.height)};
	return {.x = static_cast<float>(v.x),
	        .y = static_cast<float>(top),
	        .w = static_cast<float>(v.size.width),
	        .h = static_cast<float>(v.size.height),
	        .min_depth = 0.5f,
	        .max_depth = 1.f};
}

/* A GL viewport fitted to the target (fit_viewport): the part of it the
 * target holds, as SDL_GPU's, and the change of normalised device
 * coordinates from the whole viewport to that part: ndc' = ndc * scale +
 * offset per axis, x then y.
 */
struct fitted_viewport
{
	gpu_viewport viewport;
	bool clamped;
	std::array<float, 2> scale;
	std::array<float, 2> offset;
};

/* The GL viewport v on target.  SDL's debug mode asserts on a viewport
 * that reaches past its render target (SDL_gpu.c, SDL_SetGPUViewport,
 * 3.4.18), so one that does is clamped to the target, with the scale and
 * offset that keep each pixel inside where the whole viewport put it;
 * nullopt when no pixel of it is inside.  A viewport inside is v as it
 * is, scale 1 and offset 0.
 */
[[nodiscard]]
constexpr std::optional<fitted_viewport> fit_viewport(const extent target, const viewport &v)
{
	const gpu_viewport whole{viewport_on(target, v)};
	const auto x0{std::max(0.f, whole.x)};
	const auto y0{std::max(0.f, whole.y)};
	const auto x1{std::min(static_cast<float>(target.width), whole.x + whole.w)};
	const auto y1{std::min(static_cast<float>(target.height), whole.y + whole.h)};
	if (!(x1 > x0 && y1 > y0))
		return std::nullopt;
	gpu_viewport part{whole};
	part.x = x0;
	part.y = y0;
	part.w = x1 - x0;
	part.h = y1 - y0;
	if (part == whole)
		return fitted_viewport{
		    .viewport = whole, .clamped = false, .scale = {{1, 1}}, .offset = {{0, 0}}};
	/* x grows right in both; SDL_GPU's y grows down while ndc's grows up. */
	return fitted_viewport{.viewport = part,
	                       .clamped = true,
	                       .scale = {{whole.w / part.w, whole.h / part.h}},
	                       .offset = {{(2 * (whole.x - part.x) + whole.w - part.w) / part.w,
	                                   (part.h - whole.h - 2 * (whole.y - part.y)) / part.h}}};
}

/* How a render pass begins: each target cleared or loaded. */
struct pass_ops
{
	bool clear_color;
	bool clear_depth;
	constexpr bool operator==(const pass_ops &) const = default;
};

/* One texture's rows in the frame's transfer buffer: size.width *
 * size.height RGBA pixels, tightly packed (SDL #16474: Metal ignores a row
 * length), at offset.
 */
struct texture_rows
{
	texture_slot slot;
	extent size;
	std::size_t offset;
	std::span<const std::uint8_t> rgba;
};

/* A frame's uploads as one plan (upload_plan): the transfer buffer's size,
 * the vertices and where they lie, and each texture's rows.
 */
struct frame_uploads
{
	std::size_t size;
	std::span<const vertex> vertices;
	std::size_t vertex_offset;
	std::vector<texture_rows> textures;
};

/* The GPU command seam: the SDL_GPU calls a frame makes, in the adapter's
 * terms, not a mirror of SDL_gpu.h.  upload runs before the frame's first
 * pass, in its own copy pass; the rest inside a pass.
 */
class commands
{
  public:
	virtual ~commands() = default;
	/* The frame's vertices, into the vertex buffer that bind_vertex_buffer
	 * binds at offset 0, and its textures' rows, each into its slot's
	 * texture: one transfer buffer, one copy pass.
	 */
	virtual void upload(const frame_uploads &u) = 0;
	virtual void begin_pass(const pass_ops &ops) = 0;
	virtual void end_pass() = 0;
	virtual void bind_pipeline(const pipeline_key &key) = 0;
	virtual void set_viewport(const gpu_viewport &v) = 0;
	virtual void bind_vertex_buffer() = 0;
	/* The 1x1 white texture with the clamp sampler: the fragment shader
	 * samples even untextured, so a texture must be bound.
	 */
	virtual void bind_untextured() = 0;
	/* The texture of slot with the sampler of key. */
	virtual void bind_texture(texture_slot slot, const sampler_key &key) = 0;
	virtual void push_vertex_uniforms(const vertex_uniforms &u) = 0;
	virtual void push_fragment_uniforms(const fragment_uniforms &u) = 0;
	virtual void draw(std::uint32_t first_vertex, std::uint32_t count) = 0;
};

/* Where each upload lies in the frame's transfer buffer: every reservation
 * starts at a multiple of 512 bytes, the offset D3D12 copies from without a
 * realignment (research #71, area 2).
 */
class upload_plan
{
	std::size_t size_{0};

  public:
	static constexpr std::size_t alignment{512};
	/* Reserves bytes: their offset. */
	std::size_t reserve(const std::size_t bytes)
	{
		const std::size_t offset{(size_ + alignment - 1) / alignment * alignment};
		size_ = offset + bytes;
		return offset;
	}
	/* The bytes the transfer buffer needs. */
	[[nodiscard]]
	std::size_t size() const
	{
		return size_;
	}
};

/* The capacity a buffer of capacity current grows to for needed bytes:
 * current when it fits, else the next power of two from 64 KiB up, so a
 * frame keeps its high-water mark.
 */
[[nodiscard]]
constexpr std::size_t grown_capacity(const std::size_t current, const std::size_t needed)
{
	if (needed <= current)
		return current;
	std::size_t c{std::size_t{64} * 1024};
	while (c < needed)
		c *= 2;
	return c;
}

/* One frame's draws, clears and texture uploads, in game order, until they
 * are encoded; and the description of every slot's texture, which outlives
 * the frame, since a draw's sampler comes from it.
 */
class frame
{
	struct recorded_draw
	{
		pipeline_key key;
		gpu_viewport viewport;
		vertex_uniforms vertex;
		fragment_uniforms fragment;
		texture_slot texture;
		sampler_key sampler;
		std::uint32_t first;
		std::uint32_t count;
	};
	struct pending_upload
	{
		texture_slot slot;
		extent size;
		std::vector<std::uint8_t> rgba;
	};
	extent target_;
	std::vector<vertex> vertices_;
	std::vector<std::variant<recorded_draw, clear_targets>> records_;
	std::vector<pending_upload> uploads_;
	/* Each slot's texture, by slot index. */
	std::map<std::uint32_t, texture_desc> textures_;
	/* The slots this frame's draws sample. */
	std::set<std::uint32_t> sampled_;

  public:
	explicit frame(const extent target) : target_{target}
	{}
	/* Records call.  Throws std::invalid_argument on a textured draw of a
	 * slot that holds no texture.  Lines and points come as quads
	 * (line_quads.h), drawn without culling.
	 */
	void draw(const draw_call &call);
	void clear(const clear_targets targets)
	{
		records_.emplace_back(targets);
	}
	/* Gives slot image, copied, uploaded when the frame is encoded; a null
	 * image releases the slot.  A slot given twice uploads the last image.
	 * Change no slot that samples() names: the device encodes the frame
	 * first.  Throws std::invalid_argument on texture_slot::none.
	 */
	void texture(texture_slot slot, const texture_image *image);
	/* The draws whose viewport reached past the target, since the frame
	 * was made: clamped to it (fit_viewport), or dropped with no pixel of
	 * it inside.
	 */
	struct fixes
	{
		unsigned clamped;
		unsigned dropped;
	};
	[[nodiscard]]
	fixes viewport_fixes() const
	{
		return fixes_;
	}
	/* Whether a draw of this frame samples slot. */
	[[nodiscard]]
	bool samples(const texture_slot slot) const
	{
		return sampled_.contains(slot.index);
	}
	/* Encodes the frame into c and empties it.  undefined names the targets
	 * nothing has drawn to yet, which the first pass clears.  Returns
	 * whether anything was encoded.
	 */
	bool encode(commands &c, clear_targets undefined);

  private:
	fixes fixes_{};
};

}

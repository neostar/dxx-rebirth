/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/sdlgpu_frame.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "common/render/device.h"
#include "common/render/line_quads.h"
#include "common/render/render_device.h"
#include "common/render/transform_matrix.h"

namespace dcx::render::sdlgpu {

namespace {

/* What is bound in the current pass: SDL forgets it when a pass ends, so
 * each pass starts it empty.
 */
struct pass_bindings
{
	std::optional<pipeline_key> pipeline;
	std::optional<gpu_viewport> viewport;
	/* The vertex buffer, bound at a pass's first draw: a pass of clears
	 * alone binds nothing.
	 */
	bool vertex_buffer{false};
	/* The texture and sampler: none and the clamp sampler for the white
	 * texture.
	 */
	std::optional<std::pair<texture_slot, sampler_key>> texture;
};

/* What one encode has bound: the pass's bindings, and the uniform blocks
 * last pushed, which stay with the command buffer across its passes
 * (SDL_gpu.h: pushed uniform data "keeps its value throughout the
 * command buffer").
 */
struct bind_cache
{
	pass_bindings pass;
	std::optional<vertex_uniforms> vertex;
	std::optional<fragment_uniforms> fragment;
};

/* Whether value differs from what is bound, which it then becomes. */
template <typename T>
[[nodiscard]]
bool changes(std::optional<T> &bound, const T &value)
{
	if (bound == value)
		return false;
	bound = value;
	return true;
}

/* p followed by the fitted viewport's change of normalised device
 * coordinates, in clip space: x' = x * scale + w * offset, likewise y.
 * p is column-major, as GL keeps it.
 */
[[nodiscard]]
matrix refit(matrix p, const fitted_viewport &f)
{
	if (!f.clamped)
		return p;
	for (std::size_t column{0}; column != 4; ++column)
	{
		const float w{p.at(column * 4 + 3)};
		for (std::size_t row{0}; row != 2; ++row)
		{
			float &e{p.at(column * 4 + row)};
			e = e * f.scale.at(row) + w * f.offset.at(row);
		}
	}
	return p;
}

}

void frame::draw(const draw_call &call)
{
	sampler_key sampler{};
	bool transparent{false};
	const bool textured{call.texture != texture_slot::none};
	if (textured)
	{
		const auto t{textures_.find(call.texture.index)};
		if (t == textures_.end())
			throw std::invalid_argument("sdlgpu: a textured draw of a slot that holds no texture");
		sampler = {.filter = t->second.filter,
		           .wrap = call.state.wrap,
		           .anisotropic = t->second.anisotropic};
		transparent = t->second.has_transparency;
	}
	std::vector<vertex> list;
	switch (call.topology)
	{
	case topology::fan:
		list = fan_to_list(call.vertices);
		break;
	case topology::triangles:
		/* GL draws no incomplete last triangle. */
		list.assign(call.vertices.begin(),
		            call.vertices.begin() +
		                static_cast<std::ptrdiff_t>(call.vertices.size() / 3 * 3));
		break;
	case topology::strip:
		list = strip_to_list(call.vertices);
		break;
	case topology::lines:
		list = lines_to_list(expansion_view_of(call), call.vertices);
		break;
	case topology::line_loop:
		list = line_loop_to_list(expansion_view_of(call), call.vertices);
		break;
	case topology::points:
		list = points_to_list(expansion_view_of(call), call.vertices);
		break;
	}
	if (list.empty())
		return;
	const auto fitted{fit_viewport(target_, call.viewport)};
	if (!fitted)
	{
		++fixes_.dropped;
		return;
	}
	if (fitted->clamped)
		++fixes_.clamped;
	/* GL culls triangles only: the quads of a line or a point are drawn
	 * whichever way they wind.
	 */
	draw_state state{call.state};
	if (call.topology == topology::lines || call.topology == topology::line_loop ||
	    call.topology == topology::points)
		state.cull = render::cull::none;
	if (vertices_.size() + list.size() > std::numeric_limits<std::uint32_t>::max())
		throw std::length_error("sdlgpu: more vertices in a frame than a draw can start at");
	const auto first{static_cast<std::uint32_t>(vertices_.size())};
	const auto count{static_cast<std::uint32_t>(list.size())};
	vertices_.insert(vertices_.end(), list.begin(), list.end());
	if (textured)
		sampled_.insert(call.texture.index);
	records_.emplace_back(recorded_draw{
	    .key = key_of(state, needs_alpha_test(state, transparent, call.vertices)),
	    .viewport = fitted->viewport,
	    .vertex = {.projection = refit(matrix_of(call.transforms.projection), fitted.value()),
		           .modelview = matrix_of(call.transforms.modelview)},
	    .fragment = {.alpha_ref = call.state.alpha_ref,
		             .use_texture = textured ? 1u : 0u,
		             .pad = {}},
	    .texture = call.texture,
	    .sampler = sampler,
	    .first = first,
	    .count = count});
}

void frame::texture(const texture_slot slot, const texture_image *const image)
{
	if (slot == texture_slot::none)
		throw std::invalid_argument("sdlgpu: texture_slot::none holds no texture");
	const auto pending{[slot](const pending_upload &u) { return u.slot == slot; }};
	if (image == nullptr)
	{
		std::erase_if(uploads_, pending);
		textures_.erase(slot.index);
		return;
	}
	/* Everything that may throw comes first, so that a failure leaves the
	 * frame as it was: the copy, the room for it, the description.
	 */
	const texture_desc &d{image->desc()};
	pending_upload u{
	    .slot = slot, .size = d.size, .rgba = {image->rgba().begin(), image->rgba().end()}};
	/* The texel GL samples. GL_RGB8 keeps no alpha: 1. GL_LUMINANCE4_ALPHA4
	 * keeps one grey, the red, and the alpha; GL_INTENSITY4 keeps the red
	 * alone, and samples it as all four channels. Apple's GL keeps these
	 * and GL_RGBA2 at eight bits (rtrace.cpp), so nothing is quantized.
	 * RGBA8 and RGBA2 change no texel, so most uploads skip the pass.
	 */
	const texture_format format{d.format};
	if (format != texture_format::rgba8 && format != texture_format::rgba2)
		for (std::size_t i{0}; i + 3 < u.rgba.size(); i += 4)
			switch (format)
			{
			case texture_format::rgb8:
				u.rgba[i + 3] = 255;
				break;
			case texture_format::intensity4:
				u.rgba[i + 3] = u.rgba[i];
				[[fallthrough]];
			case texture_format::luminance4_alpha4:
				u.rgba[i + 1] = u.rgba[i];
				u.rgba[i + 2] = u.rgba[i];
				break;
			case texture_format::rgba8:
			case texture_format::rgba2:
				break;
			}
	uploads_.reserve(uploads_.size() + 1);
	textures_.insert_or_assign(slot.index, d);
	std::erase_if(uploads_, pending);
	uploads_.push_back(std::move(u));
}

bool frame::encode(commands &c, const clear_targets undefined)
{
	/* The clears the next pass makes: at first, the targets nothing has
	 * drawn to yet.
	 */
	clear_targets pending{undefined};
	bool in_pass{false};
	bool encoded{false};
	bind_cache bound;
	/* One plan for the frame: the vertices, then each texture's rows. */
	if (!vertices_.empty() || !uploads_.empty())
	{
		upload_plan plan;
		frame_uploads u{.size = 0,
		                .vertices = vertices_,
		                .vertex_offset = plan.reserve(vertices_.size() * sizeof(vertex)),
		                .textures = {}};
		u.textures.reserve(uploads_.size());
		for (const auto &t : uploads_)
			u.textures.push_back({.slot = t.slot,
			                      .size = t.size,
			                      .offset = plan.reserve(t.rgba.size()),
			                      .rgba = t.rgba});
		u.size = plan.size();
		c.upload(u);
	}
	const auto begin{[&] {
		c.begin_pass({.clear_color = pending.color, .clear_depth = pending.depth});
		pending = {.color = false, .depth = false};
		in_pass = true;
		encoded = true;
		bound.pass = {};
	}};
	for (const auto &r : records_)
	{
		if (const auto *const clear{std::get_if<clear_targets>(&r)})
		{
			if (!clear->color && !clear->depth)
				continue;
			/* SDL_GPU clears only as a pass's load op: a clear ends the pass
			 * it follows.
			 */
			if (in_pass)
			{
				c.end_pass();
				in_pass = false;
			}
			pending.color = pending.color || clear->color;
			pending.depth = pending.depth || clear->depth;
			continue;
		}
		const auto &d{std::get<recorded_draw>(r)};
		if (!in_pass)
			begin();
		if (!bound.pass.vertex_buffer)
		{
			c.bind_vertex_buffer();
			bound.pass.vertex_buffer = true;
		}
		if (changes(bound.pass.texture, std::pair{d.texture, d.sampler}))
		{
			if (d.texture == texture_slot::none)
				c.bind_untextured();
			else
				c.bind_texture(d.texture, d.sampler);
		}
		if (changes(bound.pass.pipeline, d.key))
			c.bind_pipeline(d.key);
		if (changes(bound.pass.viewport, d.viewport))
			c.set_viewport(d.viewport);
		if (changes(bound.vertex, d.vertex))
			c.push_vertex_uniforms(d.vertex);
		if (changes(bound.fragment, d.fragment))
			c.push_fragment_uniforms(d.fragment);
		c.draw(d.first, d.count);
	}
	/* Clears after the last draw, or alone: a pass of their own. */
	if (!in_pass && (pending.color || pending.depth))
		begin();
	if (in_pass)
		c.end_pass();
	const bool uploaded{!vertices_.empty() || !uploads_.empty()};
	vertices_.clear();
	records_.clear();
	uploads_.clear();
	sampled_.clear();
	return encoded || uploaded;
}

}

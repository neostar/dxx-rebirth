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
#include <variant>
#include <vector>

#include "common/render/device.h"
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
	/* The vertex buffer and the white texture, bound at a pass's first
	 * draw: a pass of clears alone binds nothing.
	 */
	bool resources{false};
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

}

void frame::draw(const draw_call &call)
{
	if (call.texture != texture_slot::none)
		throw std::logic_error("sdlgpu: textured draws come with #27");
	const auto *const o{std::get_if<ortho>(&call.transforms.projection)};
	if (o == nullptr)
		throw std::logic_error("sdlgpu: perspective projections come with #28");
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
		/* The front end draws none yet; the 3D view may (#28). */
		throw std::logic_error("sdlgpu: strips come with #28");
	case topology::lines:
	case topology::line_loop:
	case topology::points:
		throw std::logic_error("sdlgpu: lines and points come with #32");
	}
	if (list.empty())
		return;
	if (vertices_.size() + list.size() > std::numeric_limits<std::uint32_t>::max())
		throw std::length_error("sdlgpu: more vertices in a frame than a draw can start at");
	const auto first{static_cast<std::uint32_t>(vertices_.size())};
	const auto count{static_cast<std::uint32_t>(list.size())};
	vertices_.insert(vertices_.end(), list.begin(), list.end());
	records_.emplace_back(recorded_draw{
	    .key = key_of(call.state),
	    .viewport = viewport_on(target_, call.viewport),
	    .vertex = {.projection = matrix_of(*o), .modelview = matrix_of(call.transforms.modelview)},
	    .fragment = {.alpha_ref = call.state.alpha_ref, .use_texture = 0, .pad = {}},
	    .first = first,
	    .count = count});
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
	/* One plan for the frame's uploads. */
	if (!vertices_.empty())
	{
		upload_plan plan;
		frame_uploads u{.size = 0,
		                .vertices = vertices_,
		                .vertex_offset = plan.reserve(vertices_.size() * sizeof(vertex))};
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
		if (!bound.pass.resources)
		{
			c.bind_vertex_buffer();
			c.bind_untextured();
			bound.pass.resources = true;
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
	vertices_.clear();
	records_.clear();
	return encoded;
}

}

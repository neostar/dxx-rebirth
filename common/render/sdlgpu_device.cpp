/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/sdlgpu_device.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "common/render/device.h"
#include "common/render/readback.h"
#include "common/render/render_device.h"
#include "common/render/sdlgpu_frame.h"
#include "common/render/sdlgpu_probe.h"
#include "common/sdl/dxx-sdl.h"
#include "common/sdl/sdl_deleter.h"
#include "common/sdl/sdl_wrap_gpu.h"
#include "shaders/generated/pos_color_uv_vert.h"
#include "shaders/generated/tex_modulate_frag.h"

namespace dcx::render::sdlgpu {

namespace {

[[noreturn]]
void sdl_failed(const char *what)
{
	throw std::runtime_error(std::string{"sdlgpu: "} + what + ": " + SDL_GetError());
}

using gpu_device = sdl::sdl_unique_ptr<SDL_GPUDevice, SDL_DestroyGPUDevice>;
/* An SDL property set, destroyed with the guard. */
struct property_set
{
	SDL_PropertiesID id{SDL_CreateProperties()};
	property_set()
	{
		if (id == 0)
			sdl_failed("SDL_CreateProperties");
	}
	~property_set()
	{
		SDL_DestroyProperties(id);
	}
	property_set(const property_set &) = delete;
	property_set &operator=(const property_set &) = delete;
	property_set(property_set &&) = delete;
	property_set &operator=(property_set &&) = delete;
};

/* An SDL_GPU object of the device, released with it and counted in the
 * ledger while it lives (a null ledger: not counted).
 */
template <typename T, void (*release)(SDL_GPUDevice *, T *), resource_ledger::kind K>
class handle
{
	SDL_GPUDevice *device_{nullptr};
	T *object_{nullptr};
	resource_ledger *ledger_{nullptr};

  public:
	handle() = default;
	/* Throws std::runtime_error, naming what, when object is null. */
	handle(SDL_GPUDevice *const d, T *const object, resource_ledger *const l, const char *what)
	    : device_{d}, object_{object}, ledger_{l}
	{
		if (object_ == nullptr)
			sdl_failed(what);
		if (ledger_)
			ledger_->created(K);
	}
	handle(handle &&other) noexcept
	    : device_{other.device_}, object_{std::exchange(other.object_, nullptr)},
	      ledger_{other.ledger_}
	{}
	handle &operator=(handle &&other) noexcept
	{
		if (this != &other)
		{
			reset();
			device_ = other.device_;
			object_ = std::exchange(other.object_, nullptr);
			ledger_ = other.ledger_;
		}
		return *this;
	}
	handle(const handle &) = delete;
	handle &operator=(const handle &) = delete;
	~handle()
	{
		reset();
	}
	void reset()
	{
		if (object_ == nullptr)
			return;
		release(device_, std::exchange(object_, nullptr));
		if (ledger_)
			ledger_->released(K);
	}
	[[nodiscard]]
	T *get() const
	{
		return object_;
	}
};

using texture_handle =
    handle<SDL_GPUTexture, SDL_ReleaseGPUTexture, resource_ledger::kind::texture>;
using buffer_handle = handle<SDL_GPUBuffer, SDL_ReleaseGPUBuffer, resource_ledger::kind::buffer>;
using transfer_handle =
    handle<SDL_GPUTransferBuffer, SDL_ReleaseGPUTransferBuffer, resource_ledger::kind::buffer>;
using sampler_handle =
    handle<SDL_GPUSampler, SDL_ReleaseGPUSampler, resource_ledger::kind::sampler>;
using pipeline_handle = handle<SDL_GPUGraphicsPipeline, SDL_ReleaseGPUGraphicsPipeline,
                               resource_ledger::kind::pipeline>;
/* Shaders live only while the pipeline set is built. */
using shader_handle = handle<SDL_GPUShader, SDL_ReleaseGPUShader, resource_ledger::kind::shader>;

/* A command buffer that is cancelled unless it was submitted, so that a
 * failed frame leaves nothing recorded behind.
 */
class command_buffer
{
	SDL_GPUCommandBuffer *buffer_;

  public:
	explicit command_buffer(SDL_GPUDevice *const d) : buffer_{SDL_AcquireGPUCommandBuffer(d)}
	{
		if (buffer_ == nullptr)
			sdl_failed("SDL_AcquireGPUCommandBuffer");
	}
	command_buffer(const command_buffer &) = delete;
	command_buffer &operator=(const command_buffer &) = delete;
	command_buffer(command_buffer &&) = delete;
	command_buffer &operator=(command_buffer &&) = delete;
	~command_buffer()
	{
		if (buffer_ != nullptr)
			if (!SDL_CancelGPUCommandBuffer(buffer_))
				sdl::log_gpu_error("SDL_CancelGPUCommandBuffer");
	}
	[[nodiscard]]
	SDL_GPUCommandBuffer *get() const
	{
		return buffer_;
	}
	void submit()
	{
		if (!SDL_SubmitGPUCommandBuffer(std::exchange(buffer_, nullptr)))
			sdl_failed("SDL_SubmitGPUCommandBuffer");
	}
	/* Submits and waits until the GPU has run the buffer. */
	void submit_and_wait(SDL_GPUDevice *const d)
	{
		SDL_GPUFence *const fence{
		    SDL_SubmitGPUCommandBufferAndAcquireFence(std::exchange(buffer_, nullptr))};
		if (fence == nullptr)
			sdl_failed("SDL_SubmitGPUCommandBufferAndAcquireFence");
		const bool waited{SDL_WaitForGPUFences(d, true, &fence, 1)};
		SDL_ReleaseGPUFence(d, fence);
		if (!waited)
			sdl_failed("SDL_WaitForGPUFences");
	}
};

[[nodiscard]]
SDL_GPUTextureFormat to_sdl(const depth_format f)
{
	return f == depth_format::d32_float ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
	                                    : SDL_GPU_TEXTUREFORMAT_D24_UNORM;
}

constexpr SDL_GPUTextureFormat color_format{SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};

[[nodiscard]]
SDL_GPUSampleCount to_sample_count(const std::uint32_t samples)
{
	switch (samples)
	{
	case 2:
		return SDL_GPU_SAMPLECOUNT_2;
	case 4:
		return SDL_GPU_SAMPLECOUNT_4;
	case 8:
		return SDL_GPU_SAMPLECOUNT_8;
	default:
		return SDL_GPU_SAMPLECOUNT_1;
	}
}

/* The name of d's driver; empty where SDL reports none. */
[[nodiscard]]
std::string driver_name(SDL_GPUDevice *const d)
{
	const char *const name{SDL_GetGPUDeviceDriver(d)};
	return name ? name : "";
}

[[nodiscard]]
format_queries queries_of(SDL_GPUDevice *const d)
{
	return {
	    .depth_supported =
	        [d](const depth_format f) {
		        return SDL_GPUTextureSupportsFormat(d, to_sdl(f), SDL_GPU_TEXTURETYPE_2D,
				                                    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
	        },
	    .samples_supported =
	        [d](const std::uint32_t samples) {
		        return SDL_GPUTextureSupportsSampleCount(d, color_format, to_sample_count(samples));
	        },
	    .anisotropy_with_nearest = driver_allows_nearest_anisotropy(driver_name(d)),
	};
}

/* Creates the device through properties: debug mode as configured, every
 * shader format the generated headers carry, the configured driver.
 */
[[nodiscard]]
gpu_device create_gpu_device(const config &c)
{
	const property_set props;
	const SDL_PropertiesID id{props.id};
	/* Explicit, from the configuration (research #71, area 8). */
	if (!SDL_SetBooleanProperty(id, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, c.debug_mode) ||
	    !SDL_SetBooleanProperty(id, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true) ||
	    !SDL_SetBooleanProperty(id, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true) ||
	    !SDL_SetBooleanProperty(id, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true))
		sdl_failed("SDL_SetBooleanProperty");
	if (!c.driver.empty() &&
	    !SDL_SetStringProperty(id, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, c.driver.c_str()))
		sdl_failed("SDL_SetStringProperty");
	gpu_device d{SDL_CreateGPUDeviceWithProperties(id)};
	if (!d)
		sdl_failed("SDL_CreateGPUDeviceWithProperties");
	/* SDL tells nothing of debug mode on Metal: gpu-validate.sh fails a run
	 * without this line.
	 */
	if (c.debug_mode)
		SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "sdlgpu: debug mode on");
	return d;
}

[[nodiscard]]
texture_handle create_target(SDL_GPUDevice *const d, resource_ledger &l,
                             const SDL_GPUTextureFormat format,
                             const SDL_GPUTextureUsageFlags usage, const render::extent size)
{
	SDL_GPUTextureCreateInfo info{};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = format;
	info.usage = usage;
	info.width = size.width;
	info.height = size.height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	return {d, SDL_CreateGPUTexture(d, &info), &l, "SDL_CreateGPUTexture"};
}

/* The sampler of key: choose_sampler's choice mapped to SDL's create info. */
[[nodiscard]]
sampler_handle create_sampler(SDL_GPUDevice *const d, resource_ledger &l, const sampler_key &key,
                              const device_caps &caps)
{
	const auto choice{choose_sampler(key.filter, key.anisotropic, caps)};
	const auto address{key.wrap == render::wrap::repeat ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT
	                                                    : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE};
	SDL_GPUSamplerCreateInfo info{};
	info.min_filter = choice.linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
	info.mag_filter = info.min_filter;
	info.mipmap_mode =
	    choice.linear_mipmap ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	info.address_mode_u = address;
	info.address_mode_v = address;
	info.address_mode_w = address;
	info.max_anisotropy = choice.anisotropic ? caps.max_anisotropy : 1.f;
	info.enable_anisotropy = choice.anisotropic;
	info.max_lod = 1000.f;
	return {d, SDL_CreateGPUSampler(d, &info), &l, "SDL_CreateGPUSampler"};
}

/* The 1x1 opaque white texture untextured draws bind (sdlgpu_frame.h),
 * uploaded before the device is handed out.
 */
[[nodiscard]]
texture_handle create_white(SDL_GPUDevice *const d, resource_ledger &l)
{
	texture_handle white{create_target(d, l, color_format, SDL_GPU_TEXTUREUSAGE_SAMPLER, {1, 1})};
	constexpr std::uint32_t bytes{4};
	SDL_GPUTransferBufferCreateInfo info{};
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	info.size = bytes;
	const transfer_handle upload{d, SDL_CreateGPUTransferBuffer(d, &info), &l,
	                             "SDL_CreateGPUTransferBuffer"};
	const auto mapped{sdl::map_for_writing(d, upload.get(), bytes, false)};
	if (mapped.empty())
		sdl_failed("SDL_MapGPUTransferBuffer");
	std::ranges::fill(mapped, std::byte{0xff});
	SDL_UnmapGPUTransferBuffer(d, upload.get());
	command_buffer cb{d};
	SDL_GPUCopyPass *const copy{SDL_BeginGPUCopyPass(cb.get())};
	if (copy == nullptr)
		sdl_failed("SDL_BeginGPUCopyPass");
	SDL_GPUTextureTransferInfo from{};
	from.transfer_buffer = upload.get();
	SDL_GPUTextureRegion to{};
	to.texture = white.get();
	to.w = 1;
	to.h = 1;
	to.d = 1;
	SDL_UploadToGPUTexture(copy, &from, &to, false);
	SDL_EndGPUCopyPass(copy);
	cb.submit_and_wait(d);
	return white;
}

/* The shader format the device takes, asked of the device rather than read
 * from its backend's name: MSL, then SPIR-V, then DXIL.
 */
struct shader_code
{
	SDL_GPUShaderFormat format;
	std::span<const std::uint8_t> code;
	/* NUL-terminated for SDL. */
	std::string entry_point;
};

template <typename blobs>
[[nodiscard]]
shader_code pick_code(const SDL_GPUShaderFormat formats)
{
	if (formats & SDL_GPU_SHADERFORMAT_MSL)
		return {SDL_GPU_SHADERFORMAT_MSL, blobs::msl, std::string{blobs::msl_entry_point}};
	if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
		return {SDL_GPU_SHADERFORMAT_SPIRV, blobs::spirv, std::string{blobs::spirv_entry_point}};
	if (formats & SDL_GPU_SHADERFORMAT_DXIL)
		return {SDL_GPU_SHADERFORMAT_DXIL, blobs::dxil, std::string{blobs::dxil_entry_point}};
	throw std::runtime_error("sdlgpu: the device takes none of MSL, SPIR-V and DXIL");
}

struct vertex_blobs
{
	static constexpr auto &msl{shaders::pos_color_uv_vert::msl};
	static constexpr auto &spirv{shaders::pos_color_uv_vert::spirv};
	static constexpr auto &dxil{shaders::pos_color_uv_vert::dxil};
	static constexpr auto msl_entry_point{shaders::pos_color_uv_vert::msl_entry_point};
	static constexpr auto spirv_entry_point{shaders::pos_color_uv_vert::spirv_entry_point};
	static constexpr auto dxil_entry_point{shaders::pos_color_uv_vert::dxil_entry_point};
};

struct fragment_blobs
{
	static constexpr auto &msl{shaders::tex_modulate_frag::msl};
	static constexpr auto &spirv{shaders::tex_modulate_frag::spirv};
	static constexpr auto &dxil{shaders::tex_modulate_frag::dxil};
	static constexpr auto msl_entry_point{shaders::tex_modulate_frag::msl_entry_point};
	static constexpr auto spirv_entry_point{shaders::tex_modulate_frag::spirv_entry_point};
	static constexpr auto dxil_entry_point{shaders::tex_modulate_frag::dxil_entry_point};
};

[[nodiscard]]
shader_handle create_shader(SDL_GPUDevice *const d, resource_ledger &l, const shader_code &code,
                            const SDL_GPUShaderStage stage, const std::uint32_t samplers,
                            const std::uint32_t uniform_buffers)
{
	SDL_GPUShaderCreateInfo info{};
	info.code_size = code.code.size();
	info.code = code.code.data();
	info.entrypoint = code.entry_point.c_str();
	info.format = code.format;
	info.stage = stage;
	info.num_samplers = samplers;
	info.num_uniform_buffers = uniform_buffers;
	return {d, SDL_CreateGPUShader(d, &info), &l, "SDL_CreateGPUShader"};
}

[[nodiscard]]
SDL_GPUColorTargetBlendState blend_state(const render::blend b)
{
	SDL_GPUColorTargetBlendState s{};
	s.color_blend_op = SDL_GPU_BLENDOP_ADD;
	s.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	s.enable_blend = b != render::blend::none;
	const auto factors{[&s](const SDL_GPUBlendFactor src, const SDL_GPUBlendFactor dst) {
		/* glBlendFunc sets the alpha factors with the colour ones. */
		s.src_color_blendfactor = src;
		s.src_alpha_blendfactor = src;
		s.dst_color_blendfactor = dst;
		s.dst_alpha_blendfactor = dst;
	}};
	switch (b)
	{
	case render::blend::normal:
		factors(SDL_GPU_BLENDFACTOR_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA);
		break;
	case render::blend::additive_a:
		factors(SDL_GPU_BLENDFACTOR_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ONE);
		break;
	case render::blend::additive_c:
		factors(SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ONE);
		break;
	case render::blend::palfx_neg:
		factors(SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR);
		break;
	case render::blend::none:
		factors(SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ZERO);
		break;
	}
	return s;
}

/* The closed pipeline set, indexed by (blend, depth, cull); returns how long
 * it took, shaders included (M6).
 */
std::chrono::nanoseconds build_pipelines(SDL_GPUDevice *const d, resource_ledger &l,
                                         const device_caps &caps,
                                         std::vector<pipeline_handle> &pipelines)
{
	const auto start{std::chrono::steady_clock::now()};
	const auto formats{SDL_GetGPUShaderFormats(d)};
	const auto vertex{create_shader(
	    d, l, pick_code<vertex_blobs>(formats), SDL_GPU_SHADERSTAGE_VERTEX,
	    shaders::pos_color_uv_vert::samplers, shaders::pos_color_uv_vert::uniform_buffers)};
	const auto fragment{create_shader(
	    d, l, pick_code<fragment_blobs>(formats), SDL_GPU_SHADERSTAGE_FRAGMENT,
	    shaders::tex_modulate_frag::samplers, shaders::tex_modulate_frag::uniform_buffers)};
	SDL_GPUVertexBufferDescription buffer{};
	buffer.slot = 0;
	buffer.pitch = sizeof(render::vertex);
	buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
	const std::array<SDL_GPUVertexAttribute, 3> attributes{{
	    {.location = 0,
		 .buffer_slot = 0,
		 .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
		 .offset = offsetof(render::vertex, pos)},
	    {.location = 1,
		 .buffer_slot = 0,
		 .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
		 .offset = offsetof(render::vertex, rgba)},
	    {.location = 2,
		 .buffer_slot = 0,
		 .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
		 .offset = offsetof(render::vertex, uv)},
	}};
	pipelines.reserve(pipeline_set_size);
	/* In the set's order, so that index_of finds each key's pipeline. */
	for (std::size_t i{0}; i != pipeline_set_size; ++i)
	{
		const auto [b, z, c]{key_at(i)};
		SDL_GPUColorTargetDescription target{};
		target.format = color_format;
		target.blend_state = blend_state(b);
		SDL_GPUGraphicsPipelineCreateInfo info{};
		info.vertex_shader = vertex.get();
		info.fragment_shader = fragment.get();
		info.vertex_input_state.vertex_buffer_descriptions = &buffer;
		info.vertex_input_state.num_vertex_buffers = 1;
		info.vertex_input_state.vertex_attributes = attributes.data();
		info.vertex_input_state.num_vertex_attributes = attributes.size();
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode =
		    c == render::cull::front ? SDL_GPU_CULLMODE_FRONT : SDL_GPU_CULLMODE_NONE;
		info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
		/* Depth clamp: the exact transform model (pos_color_uv.vert.hlsl). */
		info.rasterizer_state.enable_depth_clip = false;
		info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
		info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
		info.depth_stencil_state.enable_depth_test = z == render::depth::test_and_write;
		info.depth_stencil_state.enable_depth_write = z == render::depth::test_and_write;
		info.target_info.color_target_descriptions = &target;
		info.target_info.num_color_targets = 1;
		info.target_info.depth_stencil_format = to_sdl(caps.depth);
		info.target_info.has_depth_stencil_target = true;
		pipelines.emplace_back(d, SDL_CreateGPUGraphicsPipeline(d, &info), &l,
		                       "SDL_CreateGPUGraphicsPipeline");
	}
	return std::chrono::steady_clock::now() - start;
}

/* A texture slot's SDL texture and its size. */
struct slot_texture
{
	texture_handle handle;
	render::extent size{};
};

/* The GPU command seam's SDL_GPU adapter (sdlgpu_frame.h): one frame's
 * calls into one command buffer.  The upload is the transfer ring: the
 * frame's one transfer buffer, laid out by the frame's upload plan, is
 * mapped with cycle=true, so a buffer still in flight is never written, and
 * copied to the vertex buffer and the textures (cycle=true) in a copy pass
 * of its own before the frame's first render pass (research #71, area 2).
 * Both buffers grow by grown_capacity and keep their size.
 */
class sdl_commands final : public commands
{
  public:
	/* What the commands draw with, owned by the device. */
	struct resources
	{
		resource_ledger &ledger;
		transfer_handle &transfer;
		std::size_t &transfer_capacity;
		buffer_handle &vertices;
		std::size_t &vertex_capacity;
		const std::vector<pipeline_handle> &pipelines;
		SDL_GPUTexture *color;
		SDL_GPUTexture *depth;
		SDL_GPUTexture *white;
		const std::array<sampler_handle, sampler_set_size> &samplers;
		const std::map<std::uint32_t, slot_texture> &textures;
		SDL_FColor clear_color;
	};

  private:
	SDL_GPUDevice *device_;
	SDL_GPUCommandBuffer *cb_;
	const resources &r_;
	SDL_GPURenderPass *pass_{nullptr};

  public:
	sdl_commands(SDL_GPUDevice *const d, SDL_GPUCommandBuffer *const cb, const resources &r)
	    : device_{d}, cb_{cb}, r_{r}
	{}
	sdl_commands(const sdl_commands &) = delete;
	sdl_commands &operator=(const sdl_commands &) = delete;
	sdl_commands(sdl_commands &&) = delete;
	sdl_commands &operator=(sdl_commands &&) = delete;
	/* A pass left open by an exception is ended, so that the command
	 * buffer can be cancelled.
	 */
	~sdl_commands() override
	{
		if (pass_ != nullptr)
			SDL_EndGPURenderPass(pass_);
	}

	void upload(const frame_uploads &u) override
	{
		const auto vertex_bytes{std::as_bytes(u.vertices)};
		const std::size_t transfer_size{grown_capacity(r_.transfer_capacity, u.size)};
		if (transfer_size > std::numeric_limits<std::uint32_t>::max())
			throw std::length_error("sdlgpu: a frame's uploads past a transfer buffer's size");
		if (transfer_size != r_.transfer_capacity)
		{
			SDL_GPUTransferBufferCreateInfo info{};
			info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
			info.size = static_cast<std::uint32_t>(transfer_size);
			r_.transfer = {device_, SDL_CreateGPUTransferBuffer(device_, &info), &r_.ledger,
			               "SDL_CreateGPUTransferBuffer"};
			r_.transfer_capacity = transfer_size;
		}
		if (const auto c{grown_capacity(r_.vertex_capacity, vertex_bytes.size())};
		    c != r_.vertex_capacity)
		{
			SDL_GPUBufferCreateInfo info{};
			info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
			info.size = static_cast<std::uint32_t>(c);
			r_.vertices = {device_, SDL_CreateGPUBuffer(device_, &info), &r_.ledger,
			               "SDL_CreateGPUBuffer"};
			r_.vertex_capacity = c;
		}
		/* Checked before the buffer is mapped, so that the throw leaves it
		 * unmapped.
		 */
		for (const auto &t : u.textures)
			if (t.rgba.size() != std::size_t{t.size.width} * t.size.height * 4)
				throw std::length_error("sdlgpu: texture rows that do not match their size");
		const auto mapped{sdl::map_for_writing(device_, r_.transfer.get(), u.size, true)};
		if (mapped.empty())
			sdl_failed("SDL_MapGPUTransferBuffer");
		std::ranges::copy(vertex_bytes,
		                  mapped.subspan(u.vertex_offset, vertex_bytes.size()).begin());
		for (const auto &t : u.textures)
			std::ranges::copy(std::as_bytes(t.rgba),
			                  mapped.subspan(t.offset, t.rgba.size()).begin());
		SDL_UnmapGPUTransferBuffer(device_, r_.transfer.get());
		SDL_GPUCopyPass *const copy{SDL_BeginGPUCopyPass(cb_)};
		if (copy == nullptr)
			sdl_failed("SDL_BeginGPUCopyPass");
		if (!vertex_bytes.empty())
		{
			SDL_GPUTransferBufferLocation from{};
			from.transfer_buffer = r_.transfer.get();
			from.offset = static_cast<std::uint32_t>(u.vertex_offset);
			SDL_GPUBufferRegion to{};
			to.buffer = r_.vertices.get();
			to.size = static_cast<std::uint32_t>(vertex_bytes.size());
			SDL_UploadToGPUBuffer(copy, &from, &to, true);
		}
		for (const auto &t : u.textures)
		{
			/* Tight rows: pixels_per_row 0 (SDL #16474). */
			SDL_GPUTextureTransferInfo from{};
			from.transfer_buffer = r_.transfer.get();
			from.offset = static_cast<std::uint32_t>(t.offset);
			SDL_GPUTextureRegion to{};
			to.texture = r_.textures.at(t.slot.index).handle.get();
			to.w = t.size.width;
			to.h = t.size.height;
			to.d = 1;
			SDL_UploadToGPUTexture(copy, &from, &to, true);
		}
		SDL_EndGPUCopyPass(copy);
	}
	void begin_pass(const pass_ops &ops) override
	{
		SDL_GPUColorTargetInfo color_info{};
		color_info.texture = r_.color;
		color_info.clear_color = r_.clear_color;
		color_info.load_op = ops.clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
		color_info.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPUDepthStencilTargetInfo depth_info{};
		depth_info.texture = r_.depth;
		depth_info.clear_depth = 1.f;
		depth_info.load_op = ops.clear_depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
		depth_info.store_op = SDL_GPU_STOREOP_STORE;
		depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
		depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
		pass_ = SDL_BeginGPURenderPass(cb_, &color_info, 1, &depth_info);
		if (pass_ == nullptr)
			sdl_failed("SDL_BeginGPURenderPass");
	}
	void end_pass() override
	{
		SDL_EndGPURenderPass(std::exchange(pass_, nullptr));
	}
	void bind_pipeline(const pipeline_key &key) override
	{
		SDL_BindGPUGraphicsPipeline(pass_, r_.pipelines.at(index_of(key)).get());
	}
	void set_viewport(const gpu_viewport &v) override
	{
		const SDL_GPUViewport viewport{.x = v.x,
		                               .y = v.y,
		                               .w = v.w,
		                               .h = v.h,
		                               .min_depth = v.min_depth,
		                               .max_depth = v.max_depth};
		SDL_SetGPUViewport(pass_, &viewport);
	}
	void bind_vertex_buffer() override
	{
		const SDL_GPUBufferBinding binding{.buffer = r_.vertices.get(), .offset = 0};
		SDL_BindGPUVertexBuffers(pass_, 0, &binding, 1);
	}
	void bind_untextured() override
	{
		const SDL_GPUTextureSamplerBinding binding{
		    .texture = r_.white, .sampler = r_.samplers[index_of(sampler_key{})].get()};
		SDL_BindGPUFragmentSamplers(pass_, 0, &binding, 1);
	}
	void bind_texture(const texture_slot slot, const sampler_key &key) override
	{
		const SDL_GPUTextureSamplerBinding binding{.texture =
		                                               r_.textures.at(slot.index).handle.get(),
		                                           .sampler = r_.samplers.at(index_of(key)).get()};
		SDL_BindGPUFragmentSamplers(pass_, 0, &binding, 1);
	}
	void push_vertex_uniforms(const vertex_uniforms &u) override
	{
		SDL_PushGPUVertexUniformData(cb_, 0, &u, sizeof u);
	}
	void push_fragment_uniforms(const fragment_uniforms &u) override
	{
		SDL_PushGPUFragmentUniformData(cb_, 0, &u, sizeof u);
	}
	void draw(const std::uint32_t first_vertex, const std::uint32_t count) override
	{
		SDL_DrawGPUPrimitives(pass_, count, 1, first_vertex, 0);
	}
};

/* The uniform blocks as the shaders lay them out. */
static_assert(sizeof(vertex_uniforms) == 128);
static_assert(sizeof(fragment_uniforms) == 16);

}

/* Members are released in reverse: every object before the device. */
struct device::state
{
	std::shared_ptr<resource_ledger> ledger{std::make_shared<resource_ledger>()};
	config cfg;
	gpu_device gpu;
	device_caps caps{};
	texture_handle color;
	texture_handle depth;
	/* A copy of the colour target as the last present left it. */
	texture_handle presented;
	texture_handle white;
	/* The closed sampler set, indexed by sampler_key. */
	std::array<sampler_handle, sampler_set_size> samplers;
	/* Each slot's texture, by slot index.  Invariant: it holds a slot
	 * exactly when the frame record's description does (frame::texture),
	 * at that description's size; device::texture keeps the two in step by
	 * its order.
	 */
	std::map<std::uint32_t, slot_texture> textures;
	std::vector<pipeline_handle> pipelines;
	std::chrono::nanoseconds pipeline_time{};
	/* The transfer ring and the vertex buffer, made by the first frame
	 * with vertices.
	 */
	transfer_handle transfer;
	std::size_t transfer_capacity{0};
	buffer_handle vertices;
	std::size_t vertex_capacity{0};
	/* The draws and clears since the last flush. */
	sdlgpu::frame record{{1, 1}};
	/* A target not yet drawn to is undefined, so its first pass clears it. */
	bool color_defined{false};
	bool depth_defined{false};
	bool has_presented{false};

	/* Encodes the recorded frame into cb. */
	void flush(SDL_GPUCommandBuffer *const cb)
	{
		const auto unit{[](const std::uint8_t c) { return static_cast<float>(c) / 255.f; }};
		const sdl_commands::resources r{.ledger = *ledger,
		                                .transfer = transfer,
		                                .transfer_capacity = transfer_capacity,
		                                .vertices = vertices,
		                                .vertex_capacity = vertex_capacity,
		                                .pipelines = pipelines,
		                                .color = color.get(),
		                                .depth = depth.get(),
		                                .white = white.get(),
		                                .samplers = samplers,
		                                .textures = textures,
		                                .clear_color = {.r = unit(cfg.clear_rgba[0]),
		                                                .g = unit(cfg.clear_rgba[1]),
		                                                .b = unit(cfg.clear_rgba[2]),
		                                                .a = unit(cfg.clear_rgba[3])}};
		sdl_commands c{gpu.get(), cb, r};
		(void)record.encode(c, {.color = !color_defined, .depth = !depth_defined});
	}
	/* Marks the targets drawn to, once flush's buffer is submitted. */
	void flushed()
	{
		color_defined = true;
		depth_defined = true;
	}
};

device::device(passkey, const device_caps &probed, std::unique_ptr<state> s)
    : render::device{probed.common}, state_{std::move(s)}
{}

device::~device() = default;

std::unique_ptr<device> device::create(const config &c)
{
	if (c.target.width == 0 || c.target.height == 0)
		throw std::runtime_error("sdlgpu: the target has no pixels");
	auto s{std::make_unique<state>()};
	s->cfg = c;
	s->gpu = create_gpu_device(c);
	SDL_GPUDevice *const d{s->gpu.get()};
	auto &l{*s->ledger};
	const auto probed{probe_caps(queries_of(d), c.anisotropy)};
	if (!probed)
		throw std::runtime_error(std::string{"sdlgpu: "} + to_string(probed.error()));
	s->caps = probed.value();
	s->color = create_target(d, l, color_format, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, c.target);
	s->depth = create_target(d, l, to_sdl(s->caps.depth), SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
	                         c.target);
	s->presented = create_target(d, l, color_format, SDL_GPU_TEXTUREUSAGE_SAMPLER, c.target);
	s->white = create_white(d, l);
	s->record = sdlgpu::frame{c.target};
	for (std::size_t i{0}; i != sampler_set_size; ++i)
		s->samplers.at(i) = create_sampler(d, l, sampler_key_at(i), s->caps);
	s->pipeline_time = build_pipelines(d, l, s->caps, s->pipelines);
	const device_caps caps{s->caps};
	return std::make_unique<device>(passkey{}, caps, std::move(s));
}

void device::draw(const draw_call &call)
{
	state_->record.draw(call);
}

void device::texture(const texture_slot slot, const texture_image *const image)
{
	auto &s{*state_};
	if (slot == texture_slot::none)
		throw std::invalid_argument("sdlgpu: texture_slot::none holds no texture");
	/* A draw recorded before this call samples the slot's earlier image:
	 * encode and submit it first (research #71, area 2, item 4).
	 */
	if (s.record.samples(slot))
	{
		command_buffer cb{s.gpu.get()};
		s.flush(cb.get());
		cb.submit();
		s.flushed();
	}
	if (image == nullptr)
	{
		s.record.texture(slot, nullptr);
		s.textures.erase(slot.index);
		return;
	}
	const extent size{image->desc().size};
	/* The record and the handles change together or not at all: the new
	 * handle and the map's node are made first, the record changes, and
	 * only then does the handle take the node.
	 */
	const auto [t, inserted]{s.textures.try_emplace(slot.index)};
	std::optional<texture_handle> fresh;
	try
	{
		if (inserted || t->second.size != size)
			fresh = create_target(s.gpu.get(), *s.ledger, color_format,
			                      SDL_GPU_TEXTUREUSAGE_SAMPLER, size);
		s.record.texture(slot, image);
	}
	catch (...)
	{
		if (inserted)
			s.textures.erase(t);
		throw;
	}
	if (fresh)
		t->second = {.handle = std::move(fresh.value()), .size = size};
}

void device::clear(const clear_targets targets)
{
	state_->record.clear(targets);
}

void device::present()
{
	auto &s{*state_};
	command_buffer cb{s.gpu.get()};
	s.flush(cb.get());
	SDL_GPUCopyPass *const copy{SDL_BeginGPUCopyPass(cb.get())};
	if (copy == nullptr)
		sdl_failed("SDL_BeginGPUCopyPass");
	SDL_GPUTextureLocation from{};
	from.texture = s.color.get();
	SDL_GPUTextureLocation to{};
	to.texture = s.presented.get();
	SDL_CopyGPUTextureToTexture(copy, &from, &to, s.cfg.target.width, s.cfg.target.height, 1,
	                            false);
	SDL_EndGPUCopyPass(copy);
	cb.submit();
	s.flushed();
	s.has_presented = true;
}

void device::read_pixels(const read_source source, const read_rect &rect, const pixel_format format,
                         const std::span<std::uint8_t> pixels)
{
	auto &s{*state_};
	if (pixels.size() != image_bytes(rect.size, format))
		throw std::length_error("read_pixels: buffer size does not match the rectangle");
	const auto clip{clip_read_rect(s.cfg.target, rect)};
	if (!clip || (source == read_source::presented && !s.has_presented))
	{
		std::ranges::fill(pixels, std::uint8_t{0});
		return;
	}
	const auto &c{clip.value()};
	const std::size_t bytes{image_bytes(c.size, pixel_format::rgba8)};
	if (bytes > std::numeric_limits<std::uint32_t>::max())
		throw std::length_error("read_pixels: the rectangle is too large for a transfer buffer");
	SDL_GPUDevice *const d{s.gpu.get()};
	SDL_GPUTransferBufferCreateInfo info{};
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	info.size = static_cast<std::uint32_t>(bytes);
	const transfer_handle download{d, SDL_CreateGPUTransferBuffer(d, &info), s.ledger.get(),
	                               "SDL_CreateGPUTransferBuffer"};
	command_buffer cb{d};
	const bool drawing{source == read_source::drawing};
	if (drawing)
		s.flush(cb.get());
	SDL_GPUCopyPass *const copy{SDL_BeginGPUCopyPass(cb.get())};
	if (copy == nullptr)
		sdl_failed("SDL_BeginGPUCopyPass");
	SDL_GPUTextureRegion region{};
	region.texture = drawing ? s.color.get() : s.presented.get();
	region.x = c.source_x;
	region.y = c.source_y;
	region.w = c.size.width;
	region.h = c.size.height;
	region.d = 1;
	SDL_GPUTextureTransferInfo destination{};
	destination.transfer_buffer = download.get();
	/* Tightly packed rows, top row first: SDL_GPU's textures are top-down. */
	SDL_DownloadFromGPUTexture(copy, &region, &destination);
	SDL_EndGPUCopyPass(copy);
	cb.submit_and_wait(d);
	if (drawing)
		s.flushed();
	const auto mapped{sdl::map_for_reading(d, download.get(), bytes)};
	if (mapped.empty())
		sdl_failed("SDL_MapGPUTransferBuffer");
	try
	{
		place_rows(mapped, c, rect.size, format, pixels);
	}
	catch (...)
	{
		SDL_UnmapGPUTransferBuffer(d, download.get());
		throw;
	}
	SDL_UnmapGPUTransferBuffer(d, download.get());
}

const device_caps &device::probed() const
{
	return state_->caps;
}

std::shared_ptr<const resource_ledger> device::ledger() const
{
	return state_->ledger;
}

std::chrono::nanoseconds device::pipeline_build_time() const
{
	return state_->pipeline_time;
}

std::string device::driver() const
{
	return driver_name(state_->gpu.get());
}

device::buffer_sizes device::upload_buffer_sizes() const
{
	return {.transfer = state_->transfer_capacity, .vertices = state_->vertex_capacity};
}

}

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
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "common/render/device.h"
#include "common/render/readback.h"
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

[[nodiscard]]
sampler_handle create_sampler(SDL_GPUDevice *const d, resource_ledger &l,
                              const SDL_GPUSamplerAddressMode address, const device_caps &caps)
{
	SDL_GPUSamplerCreateInfo info{};
	info.min_filter = SDL_GPU_FILTER_LINEAR;
	info.mag_filter = SDL_GPU_FILTER_LINEAR;
	info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
	info.address_mode_u = address;
	info.address_mode_v = address;
	info.address_mode_w = address;
	info.max_anisotropy = caps.max_anisotropy;
	info.enable_anisotropy = caps.common.anisotropic;
	info.max_lod = 1000.f;
	return {d, SDL_CreateGPUSampler(d, &info), &l, "SDL_CreateGPUSampler"};
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
	for (const auto b : {render::blend::normal, render::blend::additive_a,
	                     render::blend::additive_c, render::blend::palfx_neg, render::blend::none})
		for (const auto z : {render::depth::off, render::depth::test_and_write})
			for (const auto c : {render::cull::none, render::cull::front})
			{
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
	std::array<sampler_handle, 2> samplers;
	std::vector<pipeline_handle> pipelines;
	std::chrono::nanoseconds pipeline_time{};
	/* The clears recorded since the last flush; a target not yet drawn to
	 * is undefined, so its first pass clears it.
	 */
	clear_targets pending{.color = false, .depth = false};
	bool color_defined{false};
	bool depth_defined{false};
	bool has_presented{false};

	/* Encodes the pending clears into cb as one render pass. */
	void encode_pending(SDL_GPUCommandBuffer *const cb)
	{
		const bool clear_color{pending.color || !color_defined};
		const bool clear_depth{pending.depth || !depth_defined};
		if (!clear_color && !clear_depth)
			return;
		SDL_GPUColorTargetInfo color_info{};
		color_info.texture = color.get();
		const auto unit{[](const std::uint8_t c) { return static_cast<float>(c) / 255.f; }};
		color_info.clear_color = {.r = unit(cfg.clear_rgba[0]),
		                          .g = unit(cfg.clear_rgba[1]),
		                          .b = unit(cfg.clear_rgba[2]),
		                          .a = unit(cfg.clear_rgba[3])};
		color_info.load_op = clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
		color_info.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPUDepthStencilTargetInfo depth_info{};
		depth_info.texture = depth.get();
		depth_info.clear_depth = 1.f;
		depth_info.load_op = clear_depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
		depth_info.store_op = SDL_GPU_STOREOP_STORE;
		depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
		depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
		SDL_GPURenderPass *const pass{SDL_BeginGPURenderPass(cb, &color_info, 1, &depth_info)};
		if (pass == nullptr)
			sdl_failed("SDL_BeginGPURenderPass");
		SDL_EndGPURenderPass(pass);
	}
	/* Marks what encode_pending encoded as done, once its buffer is
	 * submitted.
	 */
	void pending_done()
	{
		color_defined = true;
		depth_defined = true;
		pending = {.color = false, .depth = false};
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
	s->samplers = {create_sampler(d, l, SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, s->caps),
	               create_sampler(d, l, SDL_GPU_SAMPLERADDRESSMODE_REPEAT, s->caps)};
	s->pipeline_time = build_pipelines(d, l, s->caps, s->pipelines);
	const device_caps caps{s->caps};
	return std::make_unique<device>(passkey{}, caps, std::move(s));
}

void device::draw(const draw_call &)
{
	throw std::logic_error("sdlgpu: draws come with #26");
}

void device::texture(texture_slot, const texture_image *)
{
	throw std::logic_error("sdlgpu: textures come with #27");
}

/* The frame operations follow in the next commit. */
void device::clear(clear_targets)
{
	throw std::logic_error("sdlgpu: clear is not built yet");
}

void device::present()
{
	throw std::logic_error("sdlgpu: present is not built yet");
}

void device::read_pixels(read_source, const read_rect &, pixel_format, std::span<std::uint8_t>)
{
	throw std::logic_error("sdlgpu: read_pixels is not built yet");
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

}

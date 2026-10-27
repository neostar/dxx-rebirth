/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The SDL_GPU adapter of the render device (ADR 2), headless (#24): it
 * draws into an offscreen R8G8B8A8_UNORM colour target with D32_FLOAT
 * depth, which read_pixels reads, and needs no window; the swapchain blit
 * comes with #50.  It clears, presents and reads back, and draws
 * triangles in 2D (#26) through the frame record and the GPU
 * command seam (sdlgpu_frame.h), textured through the closed sampler set
 * (#27).  Built from the
 * spike's headless replayer (#55, K1) and research #71
 * (docs/agents/sdl-gpu-backends.md, areas 2, 3, 5, 8).
 *
 * SDL's video subsystem must be initialised: SDL_CreateGPUDevice needs it,
 * though no window is made.  On macOS that makes the process a foreground
 * app; a tool or test sets SDL's background-app hint in its own main(),
 * never here, since the game is a foreground app (check.sh's gpu_gate).  SDL3 only.
 */

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "common/render/device.h"
#include "common/render/sdlgpu_frame.h"
#include "common/render/sdlgpu_probe.h"

namespace dcx::render::sdlgpu {

/* The device's configuration; nothing is read from the environment, but
 * SDL itself lets SDL_GPU_DRIVER beat driver.
 */
struct config
{
	render::extent target{};
	/* SDL's GPU debug mode: on unless asked off, as SDL's own property is.
	 * Tests and checks keep it on and run under tools/dev/gpu-validate.sh,
	 * which fails a run without the `sdlgpu: debug mode on` line the device
	 * logs; the game opts out explicitly.
	 */
	bool debug_mode{true};
	/* SDL's driver name (metal, vulkan, direct3d12); empty for SDL's order:
	 * Metal, D3D12, Vulkan.
	 */
	std::string driver;
	/* Test surface: what a colour clear writes.  Zero by default, device.h's
	 * (0, 0, 0, 0), which the game keeps; tests pick an asymmetric colour so
	 * a channel swap or a missed clear shows.
	 */
	std::array<std::uint8_t, 4> clear_rgba{};
	/* Asked of every sampler, clamped to 16 by the probe. */
	float anisotropy{max_anisotropy_limit};
	/* Test surface: SDL's enable_depth_clip in every pipeline, which breaks
	 * the exact transform model (ADR 2's depth pairing rule): geometry
	 * nearer than about twice the near plane is clipped.  The game keeps
	 * it false, depth clamp.
	 */
	bool depth_clip{false};
};

/* Live SDL_GPU objects of each kind: textures (the targets), buffers
 * (transfer buffers too), samplers, graphics pipelines and shaders (alive
 * only while the pipeline set is built); and releases of a kind with none
 * alive, a double release, which a test expects none of.
 */
struct resource_counts
{
	std::size_t textures{0};
	std::size_t buffers{0};
	std::size_t samplers{0};
	std::size_t pipelines{0};
	std::size_t shaders{0};
	std::size_t bad_releases{0};
	constexpr bool operator==(const resource_counts &) const = default;
};

/* Test and measurement surface: counts every object the device creates and
 * releases.  Shared, so that a test reads it after the device is gone (the
 * resource-leak gate, #56).
 */
class resource_ledger
{
	std::array<std::size_t, 5> live_{};
	std::size_t bad_releases_{0};

  public:
	enum class kind : std::uint8_t
	{
		texture,
		buffer,
		sampler,
		pipeline,
		shader,
	};
	void created(const kind k)
	{
		++live_.at(std::to_underlying(k));
	}
	/* A release of a kind with none alive counts as bad, not as a wrap. */
	void released(const kind k) noexcept
	{
		const auto i{std::to_underlying(k)};
		if (i >= live_.size() || live_[i] == 0)
			++bad_releases_;
		else
			--live_[i];
	}
	[[nodiscard]]
	resource_counts counts() const
	{
		return {.textures = live_[0],
		        .buffers = live_[1],
		        .samplers = live_[2],
		        .pipelines = live_[3],
		        .shaders = live_[4],
		        .bad_releases = bad_releases_};
	}
};

class device final : public render::device
{
	struct state;
	std::unique_ptr<state> state_;
	/* Lets create() alone construct a device, through std::make_unique. */
	class passkey
	{
		friend class device;
		passkey() = default;
	};

  public:
	device(passkey, const device_caps &probed, std::unique_ptr<state> s);
	/* Creates the device, probes it, builds its targets, samplers and
	 * pipeline set.  Throws std::runtime_error, naming SDL's error or the
	 * probe's failure, when any of them cannot be had; what was made by then
	 * is released.
	 */
	[[nodiscard]]
	static std::unique_ptr<device> create(const config &c);
	~device() override;
	device(const device &) = delete;
	device &operator=(const device &) = delete;
	device(device &&) = delete;
	device &operator=(device &&) = delete;

	/* Records the call for the next flush (present, or a readback of the
	 * image being drawn).  Throws what frame::draw throws.
	 */
	void draw(const draw_call &call) override;
	/* Gives slot image, uploaded at the next flush; first flushes a
	 * recorded draw that samples the slot's earlier image.  The image's
	 * pixels upload as RGBA8, an RGB image with alpha 255; the other
	 * internal formats are not quantized as GL does, and trilinear
	 * textures have no mipmaps yet.  Throws std::invalid_argument on
	 * texture_slot::none.
	 */
	void texture(texture_slot slot, const texture_image *image) override;

	void clear(clear_targets targets) override;
	void present() override;
	/* Reads rect of the target: zero outside it (readback.h).  Throws
	 * std::length_error on a buffer of the wrong size, std::runtime_error
	 * when SDL fails, releasing what the readback made.
	 */
	void read_pixels(read_source source, const read_rect &rect, pixel_format format,
	                 std::span<std::uint8_t> pixels) override;

	/* Test and measurement surface: what the probe found, the ledger, and
	 * M6's time.
	 */
	[[nodiscard]]
	const device_caps &probed() const;
	[[nodiscard]]
	std::shared_ptr<const resource_ledger> ledger() const;
	/* M6: how long the pipeline set took to build, shaders included. */
	[[nodiscard]]
	std::chrono::nanoseconds pipeline_build_time() const;
	/* The driver SDL chose: metal, vulkan or direct3d12. */
	[[nodiscard]]
	std::string driver() const;
	/* The transfer ring's and the vertex buffer's sizes in bytes, 0 before
	 * the first frame with vertices.
	 */
	struct buffer_sizes
	{
		std::size_t transfer;
		std::size_t vertices;
		constexpr bool operator==(const buffer_sizes &) const = default;
	};
	[[nodiscard]]
	buffer_sizes upload_buffer_sizes() const;
};

}

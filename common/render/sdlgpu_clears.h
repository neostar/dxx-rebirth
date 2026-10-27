/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The values the SDL_GPU device clears its targets to, and the two places
 * they go: the D3D12 creation clear value of each target and the load ops
 * of each render pass.  D3D12 fixes a target's optimized clear value at
 * creation and its debug layer warns at every clear to another value
 * (EXECUTION WARNING #821 for depth, the WARP run of #28), comparing
 * exactly; Metal and Vulkan ignore the properties.  Both sites read one
 * target_clears, and test-sdlgpu's creation_clears_are_the_pass_clears
 * holds them equal (docs/agents/sdl-gpu-backends.md, section 9).  SDL3 only.
 */

#pragma once

#include <array>
#include <cstdint>

#include "common/render/sdlgpu_frame.h"
#include "common/sdl/dxx-sdl.h"

#if SDL_MAJOR_VERSION == 3
namespace dcx::render::sdlgpu {

/* What a colour clear and a depth clear write. */
struct target_clears
{
	SDL_FColor color;
	float depth;
};

/* The clears of a device that clears colour to rgba, in bytes, and depth
 * to GL's glClearDepth default, 1.
 */
[[nodiscard]]
constexpr target_clears target_clears_of(const std::array<std::uint8_t, 4> &rgba)
{
	const auto unit{[](const std::uint8_t c) { return static_cast<float>(c) / 255.f; }};
	return {
	    .color = {.r = unit(rgba[0]), .g = unit(rgba[1]), .b = unit(rgba[2]), .a = unit(rgba[3])},
	    .depth = 1.f};
}

/* Sets in props, the creation properties of a texture of usage, the D3D12
 * clear value of a colour target, a depth target, or neither.  False when
 * SDL fails (SDL_GetError says why).
 */
[[nodiscard]]
inline bool set_creation_clears(const SDL_PropertiesID props, const SDL_GPUTextureUsageFlags usage,
                                const target_clears &c)
{
	if ((usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) != 0 &&
	    !(SDL_SetFloatProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_R_FLOAT, c.color.r) &&
	      SDL_SetFloatProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_G_FLOAT, c.color.g) &&
	      SDL_SetFloatProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_B_FLOAT, c.color.b) &&
	      SDL_SetFloatProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_A_FLOAT, c.color.a)))
		return false;
	if ((usage & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) != 0 &&
	    !SDL_SetFloatProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT, c.depth))
		return false;
	return true;
}

/* A render pass's colour and depth target infos. */
struct pass_targets
{
	SDL_GPUColorTargetInfo color;
	SDL_GPUDepthStencilTargetInfo depth;
};

/* The target infos of a pass into color and depth with ops, each target
 * cleared, when its op asks, to c, and stored.
 */
[[nodiscard]]
inline pass_targets pass_targets_of(SDL_GPUTexture *const color, SDL_GPUTexture *const depth,
                                    const pass_ops &ops, const target_clears &c)
{
	pass_targets t{};
	t.color.texture = color;
	t.color.clear_color = c.color;
	t.color.load_op = ops.clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	t.color.store_op = SDL_GPU_STOREOP_STORE;
	t.depth.texture = depth;
	t.depth.clear_depth = c.depth;
	t.depth.load_op = ops.clear_depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	t.depth.store_op = SDL_GPU_STOREOP_STORE;
	t.depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	t.depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
	return t;
}

}
#endif

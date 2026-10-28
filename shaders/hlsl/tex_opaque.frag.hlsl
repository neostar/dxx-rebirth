/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * GL_MODULATE with clamping, without the alpha test: tex_modulate.frag.hlsl minus its
 * discard, for the draws the test cannot change (#29): an opaque texture under vertex alpha
 * at least the reference, or no reference.  On Apple GPUs a shader that discards makes
 * opaque draws slower.  The uniform block is the same, so one push serves both.
 */
Texture2D<float4> tex : register(t0, space2);
SamplerState smp : register(s0, space2);

cbuffer fragment_uniforms : register(b0, space3)
{
	/* Unused here; kept so that the layout is tex_modulate.frag.hlsl's. */
	float alpha_ref;
	/* 0 = untextured (colour only); a sampler must still be bound. */
	uint use_texture;
	float2 pad;
};

struct ps_input
{
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
};

float4 main(ps_input input) : SV_Target0
{
	float4 c = saturate(input.color);
	if (use_texture != 0)
		c *= tex.Sample(smp, input.uv);
	return c;
}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * GL_MODULATE with clamping plus the alpha test: saturate(colour) * texture, discarded below
 * alpha_ref (GL_GEQUAL; for GL_GREATER pass the next float above GL's reference).  SDL_GPU
 * fragment-stage resources: textures and samplers in (t[n]/s[n], space2), uniform buffers in
 * (b[n], space3).
 */
Texture2D<float4> tex : register(t0, space2);
SamplerState smp : register(s0, space2);

cbuffer fragment_uniforms : register(b0, space3)
{
	/* Discard when alpha < alpha_ref; 0 disables the test. */
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
	if (c.a < alpha_ref)
		discard;
	return c;
}

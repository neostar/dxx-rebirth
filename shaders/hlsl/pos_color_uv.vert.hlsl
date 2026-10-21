/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * GL's fixed-function vertex path in the exact transform model (ADR 2, spike #55 insight 2):
 * clip = P * (MV * v) with the two matrices kept apart, no depth remap in either; the pipeline
 * maps depth through a viewport of depth range 0.5..1 with depth clamp, and GL's near and far
 * planes clip through the two clip distances z + w and w - z.  Colour and uv pass through.
 * Vertex-stage uniform buffers live in (b[n], space1) for SDL_GPU.
 */
cbuffer vertex_uniforms : register(b0, space1)
{
	/* HLSL's default packing is column_major: GL's column-major float[16] uploads unchanged. */
	float4x4 projection;
	float4x4 modelview;
};

struct vs_input
{
	float3 pos : TEXCOORD0;
	float4 color : TEXCOORD1;
	float2 uv : TEXCOORD2;
};

struct vs_output
{
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
	float4 pos : SV_Position;
	float near_clip : SV_ClipDistance0;
	float far_clip : SV_ClipDistance1;
};

vs_output main(vs_input input)
{
	vs_output o;
	const float4 clip = mul(projection, mul(modelview, float4(input.pos, 1.0)));
	o.pos = clip;
	o.near_clip = clip.z + clip.w;
	o.far_clip = clip.w - clip.z;
	o.color = input.color;
	o.uv = input.uv;
	return o;
}

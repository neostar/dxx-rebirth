/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "shaders/generated/pos_color_uv_vert.h"
#include "shaders/generated/tex_modulate_frag.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth shader_blobs
#include <boost/test/unit_test.hpp>

namespace vert = dcx::shaders::pos_color_uv_vert;
namespace frag = dcx::shaders::tex_modulate_frag;

namespace {

using blobs = std::array<std::span<const std::uint8_t>, 2>;

/* Whether text `needle` occurs in blob `blob`. */
bool contains(const std::span<const std::uint8_t> blob, const std::string_view needle)
{
	return !std::ranges::search(blob, needle, [](const std::uint8_t b, const char c) {
		        return b == static_cast<std::uint8_t>(c);
	        }).empty();
}

/* The name SPIRV-Cross gave the value of `expr` in MSL `text`: the X of its line
 * "    <type> X = <expr>;", or empty without one.  The names are its SSA ids, which change with
 * any change to the shader, so tests find them by their expressions. */
std::string value_name(const std::string &text, const std::string_view type,
                       const std::string_view expr)
{
	const std::string tail{std::string{" = "} + std::string{expr} + ";\n"};
	const std::string head{std::string{"\n    "} + std::string{type} + " "};
	const auto end{text.find(tail)};
	if (end == std::string::npos)
		return {};
	const auto start{text.rfind(head, end)};
	if (start == std::string::npos || text.find('\n', start + 1) < end)
		return {};
	return text.substr(start + head.size(), end - start - head.size());
}

}

/* Test that both SPIR-V blobs start with SPIR-V's magic number, little endian. */
BOOST_AUTO_TEST_CASE(spirv_blobs_start_with_magic)
{
	for (const std::span<const std::uint8_t> spirv : blobs{{vert::spirv, frag::spirv}})
	{
		BOOST_REQUIRE(spirv.size() > 4);
		BOOST_TEST(spirv[0] == 0x03);
		BOOST_TEST(spirv[1] == 0x02);
		BOOST_TEST(spirv[2] == 0x23);
		BOOST_TEST(spirv[3] == 0x07);
	}
}

/* Test that both DXIL blobs are DXBC containers with a DXIL part, signed by the validator: its
 * 16-byte hash after the magic is not all zero. */
BOOST_AUTO_TEST_CASE(dxil_blobs_are_signed_containers)
{
	for (const std::span<const std::uint8_t> dxil : blobs{{vert::dxil, frag::dxil}})
	{
		BOOST_REQUIRE(dxil.size() > 20);
		BOOST_TEST(contains(dxil.first(4), "DXBC"));
		BOOST_TEST(contains(dxil, "DXIL"));
		const auto hash = dxil.subspan(4, 16);
		BOOST_TEST(std::ranges::any_of(hash, [](const std::uint8_t b) { return b != 0; }));
	}
}

/* Test that the MSL blobs define their stage's function under the MSL entry point. */
BOOST_AUTO_TEST_CASE(msl_blobs_define_entry_point)
{
	BOOST_TEST(vert::msl_entry_point == "main0");
	BOOST_TEST(frag::msl_entry_point == "main0");
	BOOST_TEST(contains(vert::msl, "vertex main0_out main0("));
	BOOST_TEST(contains(frag::msl, "fragment main0_out main0("));
}

/* Test that the vertex shader follows the exact transform model (ADR 2): clip = P * (MV * v)
 * with split matrices, and GL's near and far planes as the clip distances z + w and w - z.  The
 * position is precise (#31), so SPIRV-Cross multiplies with its spvFMulMatrixVector. */
BOOST_AUTO_TEST_CASE(vertex_shader_uses_exact_transform_model)
{
	const std::string msl{vert::msl.begin(), vert::msl.end()};
	BOOST_TEST(msl.contains("float4x4 projection;\n    float4x4 modelview;"));
	BOOST_TEST(msl.contains("float gl_ClipDistance [[clip_distance]] [2];"));
	const std::string clip{value_name(msl, "float4",
	                                  "spvFMulMatrixVector(vertex_uniforms.projection, "
	                                  "spvFMulMatrixVector(vertex_uniforms.modelview, "
	                                  "float4(in.in_var_TEXCOORD0, 1.0)))")};
	BOOST_REQUIRE(!clip.empty());
	BOOST_TEST(msl.contains("out.gl_Position = " + clip + ";"));
	const std::string z{value_name(msl, "float", clip + ".z")};
	const std::string w{value_name(msl, "float", clip + ".w")};
	BOOST_REQUIRE(!z.empty());
	BOOST_REQUIRE(!w.empty());
	BOOST_TEST(msl.contains("out.gl_ClipDistance[0u] = " + z + " + " + w + ";"));
	BOOST_TEST(msl.contains("out.gl_ClipDistance[1u] = " + w + " - " + z + ";"));
}

namespace {

/* How many OpDecorate NoContraction instructions the SPIR-V module `spirv` holds: after its
 * 5-word header come instructions of word count << 16 | opcode, OpDecorate (71) being three
 * words, a target id and its decoration, NoContraction (42). */
std::size_t no_contraction_decorations(const std::span<const std::uint8_t> spirv)
{
	const auto word{[&spirv](const std::size_t i) {
		const auto b{spirv.subspan(i * 4, 4)};
		return std::uint32_t{b[0]} | std::uint32_t{b[1]} << 8u | std::uint32_t{b[2]} << 16u |
		       std::uint32_t{b[3]} << 24u;
	}};
	constexpr std::uint32_t op_decorate{71}, no_contraction{42};
	std::size_t n{0};
	for (std::size_t i{5}; i < spirv.size() / 4;)
	{
		const std::uint32_t head{word(i)};
		const std::size_t count{head >> 16u};
		if (count == 0)
			break;
		if ((head & 0xffffu) == op_decorate && count == 3 && word(i + 2) == no_contraction)
			++n;
		i += count;
	}
	return n;
}

}

/* Test that the vertex shader's position is precise (#31): the two passes of an overlay wall
 * need bit-identical depth.  The SPIR-V decorates the position's arithmetic NoContraction and
 * the MSL multiplies the matrices with SPIRV-Cross's spvFMulMatrixVector, an ordered fma chain
 * the compiler may not reorder.  Without `precise` the SPIR-V holds no such decoration and
 * the MSL multiplies with `*`. */
BOOST_AUTO_TEST_CASE(vertex_position_is_precise)
{
	BOOST_TEST(no_contraction_decorations(vert::spirv) > 0u);
	BOOST_TEST(contains(vert::msl, "spvFMulMatrixVector(vertex_uniforms.projection"));
	BOOST_TEST(!contains(vert::msl, "vertex_uniforms.projection * "));
}

/* Test that each shader's stage and resource counts are the ones SDL_GPU must be given: the
 * vertex shader reads one uniform buffer, the fragment shader one sampler and one. */
BOOST_AUTO_TEST_CASE(stages_and_resource_counts)
{
	BOOST_TEST(vert::stage == "vertex");
	BOOST_TEST(vert::samplers == 0u);
	BOOST_TEST(vert::uniform_buffers == 1u);
	BOOST_TEST(vert::storage_textures == 0u);
	BOOST_TEST(vert::storage_buffers == 0u);
	BOOST_TEST(frag::stage == "fragment");
	BOOST_TEST(frag::samplers == 1u);
	BOOST_TEST(frag::uniform_buffers == 1u);
	BOOST_TEST(frag::storage_textures == 0u);
	BOOST_TEST(frag::storage_buffers == 0u);
}

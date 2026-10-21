/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The render device's value types (ADR 2, GLOSSARY.md): the backend-neutral
 * vocabulary that a draw call is made of.  Every type here compares by its
 * contents and carries no GL or SDL_GPU object.
 */

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace dcx::render {

/* The blend equations the game uses, named by the GL blend function each
 * selects: normal (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), additive_a (SRC_ALPHA,
 * ONE), additive_c (ONE, ONE), palfx_neg (ZERO, ONE_MINUS_SRC_COLOR), none
 * (blending off, as for movies).
 */
enum class blend : std::uint8_t
{
	normal,
	additive_a,
	additive_c,
	palfx_neg,
	none,
};

/* off: no depth test and no depth write; test_and_write: test LEQUAL and
 * write.
 */
enum class depth : std::uint8_t
{
	off,
	test_and_write,
};

/* none: no culling; front: cull front faces, with counter-clockwise faces in
 * front.
 */
enum class cull : std::uint8_t
{
	none,
	front,
};

enum class wrap : std::uint8_t
{
	clamp,
	repeat,
};

namespace detail {

/* Converts to any type: aggregate_size counts how many of these an
 * aggregate's brace initialiser takes, which is its number of fields.
 */
struct any_field
{
	template <typename T>
	operator T() const;
};

template <typename T, typename... fields>
[[nodiscard]]
consteval std::size_t aggregate_size()
{
	if constexpr (requires { T{fields{}..., any_field{}}; })
		return aggregate_size<T, fields..., any_field>();
	else
		return sizeof...(fields);
}

/* True when T's fields() names every field of T: a static_assert on it
 * fails when a field is added to T but not to fields(), which equality and
 * hashing read.
 */
template <typename T>
inline constexpr bool fields_complete{
    aggregate_size<T>() == std::tuple_size_v<decltype(std::declval<const T &>().fields())>};

}

/* The fixed-function state of one draw call.  alpha_ref is explicit per draw
 * (fragments with alpha below it are discarded), so no draw inherits the
 * alpha test of an earlier one.  line_width applies to lines and points, in
 * pixels.  Equality and hash both read fields().
 */
struct draw_state
{
	render::blend blend{render::blend::normal};
	render::depth depth{render::depth::off};
	render::cull cull{render::cull::none};
	float alpha_ref{0.02f};
	render::wrap wrap{render::wrap::clamp};
	float line_width{1.f};
	[[nodiscard]]
	constexpr auto fields() const
	{
		return std::tie(blend, depth, cull, alpha_ref, wrap, line_width);
	}
	[[nodiscard]]
	constexpr bool operator==(const draw_state &other) const
	{
		return fields() == other.fields();
	}
};

static_assert(detail::fields_complete<draw_state>);

namespace detail {

/* 64-bit FNV-1a, fed one canonical byte at a time. */
struct fnv1a
{
	std::uint64_t value{0xcbf29ce484222325u};
	constexpr void add(const std::uint8_t byte)
	{
		value = (value ^ byte) * 0x100000001b3u;
	}
	/* A one-byte enum as its value. */
	template <typename E>
	requires(std::is_enum_v<E> && sizeof(E) == 1)
	constexpr void add(const E e)
	{
		add(std::to_underlying(e));
	}
	/* A float as its IEEE bits, little-endian, with -0 as +0, since the two
	 * compare equal.
	 */
	constexpr void add(const float f)
	{
		const auto bits{std::bit_cast<std::uint32_t>(f == 0.f ? 0.f : f)};
		for (unsigned shift{0}; shift < 32; shift += 8)
			add(static_cast<std::uint8_t>(bits >> shift));
	}
};

}

/* The draw state's hash: FNV-1a over fields() in order, each enum as one
 * byte and each float as four.  It depends on no platform's std::hash, so it
 * is the same on every target and in every run.  Equal states hash equal; a
 * NaN field never compares equal, so it needs no rule.
 */
[[nodiscard]]
constexpr std::uint64_t hash(const draw_state &s)
{
	detail::fnv1a h;
	std::apply([&h](const auto &...field) { (h.add(field), ...); }, s.fields());
	return h.value;
}

/* One vertex as the front end fills it: position, colour and texture
 * coordinates.  The colour is not clamped to [0, 1]; adapters clamp it.
 */
struct vertex
{
	std::array<float, 3> pos;
	std::array<float, 4> rgba;
	std::array<float, 2> uv;
	constexpr bool operator==(const vertex &) const = default;
};

/* The triangles of a fan as a triangle list, for an adapter without fans.
 * GL draws fan triangle i as (v0, vi+1, vi+2) and takes its flat colour from
 * the last of these, vi+2.  Each is emitted rotated, as (vi+2, v0, vi+1):
 * the winding is unchanged, and GL's provoking vertex comes first, where the
 * SDL_GPU backends (Vulkan, Metal, D3D12) take it from.  A fan of fewer than
 * three vertices, which GL draws as nothing, gives an empty list.
 */
[[nodiscard]]
inline std::vector<vertex> fan_to_list(const std::span<const vertex> fan)
{
	std::vector<vertex> list;
	if (fan.size() < 3)
		return list;
	list.reserve(3 * (fan.size() - 2));
	for (std::size_t i{0}; i + 2 < fan.size(); ++i)
	{
		list.push_back(fan[i + 2]);
		list.push_back(fan.front());
		list.push_back(fan[i + 1]);
	}
	return list;
}

}

template <>
struct std::hash<dcx::render::draw_state>
{
	[[nodiscard]]
	static std::size_t operator()(const dcx::render::draw_state &s)
	{
		return std::hash<std::uint64_t>{}(dcx::render::hash(s));
	}
};

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

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
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

/* The alpha reference the game draws with unless it asks for another: GL's
 * alpha test passes a fragment whose alpha is at least this.
 */
inline constexpr float default_alpha_ref{0.02f};

/* The alpha reference of a draw with the alpha test off, as GL has it
 * before the first ogl_start_frame: no fragment's alpha is below 0, so none
 * is discarded.  The GL adapter turns GL_ALPHA_TEST off for it, as the code
 * not yet drawing through the device expects.
 */
inline constexpr float alpha_test_off{0.f};

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
	float alpha_ref{default_alpha_ref};
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

/* Transform recipes: each projection and modelview the game sets, described
 * by the arguments of the GL calls that build it, never as a matrix (ADR 2).
 * The GL adapter replays the same calls, so its matrices keep today's bits;
 * the SDL_GPU adapter builds its own matrices from them.
 */

/* gluPerspective(fovy, aspect, z_near, z_far); a non-zero stereo_dx is then
 * added to element 8 of the matrix, as the stereo modes do.
 */
struct perspective
{
	double fovy{};
	double aspect{};
	double z_near{};
	double z_far{};
	float stereo_dx{0};
	constexpr bool operator==(const perspective &) const = default;
};

/* glOrtho(left, right, bottom, top, z_near, z_far). */
struct ortho
{
	double left{};
	double right{};
	double bottom{};
	double top{};
	double z_near{};
	double z_far{};
	constexpr bool operator==(const ortho &) const = default;
};

/* glLoadIdentity, then glTranslatef(translate), then glScalef(scale). */
struct translate_scale
{
	std::array<float, 3> translate;
	std::array<float, 3> scale;
	constexpr bool operator==(const translate_scale &) const = default;
};

/* glLoadIdentity alone. */
struct identity
{
	constexpr bool operator==(const identity &) const = default;
};

using projection = std::variant<perspective, ortho>;
using modelview = std::variant<identity, translate_scale>;

/* The projection and modelview of one draw call. */
struct transforms
{
	render::projection projection;
	render::modelview modelview;
	constexpr bool operator==(const transforms &) const = default;
};

/* The primitive a draw call's vertices form, as GL's mode of the same name. */
enum class topology : std::uint8_t
{
	fan,
	strip,
	triangles,
	lines,
	line_loop,
	points,
};

/* A width and height in pixels, of a viewport or a texture image. */
struct extent
{
	std::uint32_t width;
	std::uint32_t height;
	constexpr bool operator==(const extent &) const = default;
};

/* A rectangle of the target in pixels, as glViewport takes it: origin at
 * the bottom left.
 */
struct viewport
{
	std::int32_t x;
	std::int32_t y;
	render::extent size;
	constexpr bool operator==(const viewport &) const = default;
};

/* The GL internal format a texture keeps, so the GL adapter creates it as
 * today and SDL_GPU can sample the same channels (ADR 2: no quantization).
 */
enum class texture_format : std::uint8_t
{
	rgba8,
	rgb8,
	intensity4,
	luminance4_alpha4,
	rgba2,
};

/* nearest: GL_NEAREST (with anisotropy, GL_NEAREST_MIPMAP_LINEAR and
 * mipmaps); trilinear: GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR and mipmaps.
 */
enum class filter : std::uint8_t
{
	nearest,
	trilinear,
};

/* Everything about a texture image but its pixels.  has_transparency says
 * whether any pixel's alpha is below 255, so an adapter can skip the alpha
 * discard; the GL adapter ignores it.
 */
struct texture_desc
{
	render::extent size;
	texture_format format;
	render::filter filter;
	bool anisotropic;
	bool has_transparency;
	constexpr bool operator==(const texture_desc &) const = default;
};

/* A texture's contents for one slot: size.width * size.height RGBA pixels,
 * already palette-expanded and padded to a power of two by the front end.
 * The constructor throws std::length_error when the pixel count does not
 * match the size, so no image breaks that rule.  The pixels are a
 * non-owning view: a copy of an image is shallow and views the same pixels,
 * so a recording adapter copies the pixels themselves (ADR 2).  Images
 * compare by their description and pixel values, not by where the pixels
 * are stored.
 */
class texture_image
{
	texture_desc desc_;
	std::span<const std::uint8_t> rgba_;

  public:
	texture_image(const texture_desc &desc, const std::span<const std::uint8_t> rgba)
	    : desc_{desc}, rgba_{rgba}
	{
		if (std::uint64_t{desc.size.width} * desc.size.height * 4 != rgba.size())
			throw std::length_error("texture_image: pixel count does not match size");
	}
	[[nodiscard]]
	const texture_desc &desc() const
	{
		return desc_;
	}
	[[nodiscard]]
	std::span<const std::uint8_t> rgba() const
	{
		return rgba_;
	}
	[[nodiscard]]
	bool operator==(const texture_image &other) const
	{
		return desc_ == other.desc_ && std::ranges::equal(rgba_, other.rgba_);
	}
};

/* A texture's place in the front end's texture pool, a strong type over the
 * pool index; none (index 0) draws untextured.
 */
struct texture_slot
{
	std::uint32_t index;
	static const texture_slot none;
	constexpr bool operator==(const texture_slot &) const = default;
};

inline constexpr texture_slot texture_slot::none{0};

/* One self-contained draw request (ADR 2): no state carries over from an
 * earlier draw call.  The vertices are a non-owning view, read during the
 * call only: a copy of a draw call is shallow and views the same vertices,
 * so a recording adapter copies the vertices themselves.  Calls compare by
 * their vertices' values, not by where the vertices are stored, and by every
 * other field through fields().
 */
struct draw_call
{
	render::topology topology;
	std::span<const vertex> vertices;
	texture_slot texture;
	draw_state state;
	render::viewport viewport;
	render::transforms transforms;
	/* Every field but the vertices, which compare by value. */
	[[nodiscard]]
	constexpr auto fields() const
	{
		return std::tie(topology, texture, state, viewport, transforms);
	}
	[[nodiscard]]
	bool operator==(const draw_call &other) const
	{
		return fields() == other.fields() && std::ranges::equal(vertices, other.vertices);
	}
};

/* fields() leaves out the vertices, compared separately. */
static_assert(detail::aggregate_size<draw_call>() ==
              std::tuple_size_v<decltype(std::declval<const draw_call &>().fields())> + 1);

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

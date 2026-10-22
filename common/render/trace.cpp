/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/trace.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dcx::render::trace {

namespace {

enum class kind : std::uint8_t
{
	clear = 1,
	present = 2,
	texture = 3,
	draw = 4,
};

/* The length of the header up to its version, its whole length, and a
 * record's before its payload.
 */
constexpr std::size_t version_end{magic.size() + 4};
constexpr std::size_t header_size{version_end + 8};
constexpr std::size_t record_head_size{5};

/* A clear's flags byte: bit 0 colour, bit 1 depth, the others zero. */
constexpr unsigned clear_color{1};
constexpr unsigned clear_depth{2};

/* Appends little-endian integers to a byte buffer. */
class sink
{
	std::vector<std::uint8_t> &out_;

  public:
	explicit sink(std::vector<std::uint8_t> &out) : out_{out}
	{}
	template <typename U>
	void le(const U v)
	{
		for (unsigned shift{0}; shift < 8 * sizeof(U); shift += 8)
			out_.push_back(static_cast<std::uint8_t>(v >> shift));
	}
	void le(const float v)
	{
		le(std::bit_cast<std::uint32_t>(v));
	}
	void le(const double v)
	{
		le(std::bit_cast<std::uint64_t>(v));
	}
	template <typename T, std::size_t N>
	void le(const std::array<T, N> &a)
	{
		for (const T v : a)
			le(v);
	}
};

/* The bytes of one vertex: position, colour and texture coordinates. */
constexpr std::size_t vertex_size{9 * sizeof(float)};

/* The encoders, the decoders and draw::call() name each field of these
 * types one by one: a field added to one of them fails here until they,
 * the layout in trace.h and the version change with it.
 */
template <typename T, std::size_t n>
constexpr bool has_fields{render::detail::aggregate_size<T>() == n};
static_assert(has_fields<draw_call, 6> && has_fields<trace::draw, 6> && has_fields<draw_state, 6> &&
              has_fields<trace::present, 1> && has_fields<trace::clear, 2> &&
              has_fields<header, 1> && has_fields<trace::texture, 3>);
static_assert(has_fields<render::viewport, 3> && has_fields<extent, 2> &&
              has_fields<render::transforms, 2> && has_fields<vertex, 3>);
static_assert(has_fields<perspective, 5> && has_fields<ortho, 6> &&
              has_fields<translate_scale, 2> && has_fields<texture_desc, 5>);
static_assert(std::variant_size_v<projection> == 2 && std::variant_size_v<modelview> == 2);

/* A visitor of one overload per alternative: std::visit fails to compile
 * when an alternative has none.
 */
template <typename... F>
struct overloads : F...
{
	using F::operator()...;
};

/* A draw's payload without its vertices is under 128 bytes, so one of
 * max_draw_vertices fits its u32 length, and its byte count a 32-bit size_t.
 */
static_assert(std::uint64_t{max_draw_vertices} * vertex_size + 128 <=
              std::numeric_limits<std::uint32_t>::max());

void encode(sink &s, const projection &p)
{
	s.le(static_cast<std::uint8_t>(p.index()));
	if (const auto *const q{std::get_if<perspective>(&p)})
	{
		s.le(q->fovy);
		s.le(q->aspect);
		s.le(q->z_near);
		s.le(q->z_far);
		s.le(q->stereo_dx);
	}
	else if (const auto *const o{std::get_if<ortho>(&p)})
		s.le(std::array<double, 6>{{o->left, o->right, o->bottom, o->top, o->z_near, o->z_far}});
}

void encode(sink &s, const modelview &m)
{
	s.le(static_cast<std::uint8_t>(m.index()));
	if (const auto *const t{std::get_if<translate_scale>(&m)})
	{
		s.le(t->translate);
		s.le(t->scale);
	}
}

/* Reads little-endian integers from a span; reading past its end yields
 * zeros and marks the cursor failed, so a decoder checks once at the end.
 */
class cursor
{
	std::span<const std::uint8_t> rest_;
	bool failed_{false};
	bool invalid_{false};

  public:
	explicit cursor(const std::span<const std::uint8_t> bytes) : rest_{bytes}
	{}
	[[nodiscard]]
	std::span<const std::uint8_t> bytes(const std::size_t n)
	{
		if (n > rest_.size())
		{
			failed_ = true;
			rest_ = {};
			return {};
		}
		const auto taken{rest_.first(n)};
		rest_ = rest_.subspan(n);
		return taken;
	}
	template <typename U>
	[[nodiscard]]
	U le()
	{
		U v{0};
		unsigned shift{0};
		for (const std::uint8_t b : bytes(sizeof(U)))
		{
			v = static_cast<U>(v | static_cast<U>(U{b} << shift));
			shift += 8;
		}
		return v;
	}
	/* An enum of up to last; another value marks the cursor invalid. */
	template <typename E>
	[[nodiscard]]
	E enumerated(const E last)
	{
		const auto v{le<std::underlying_type_t<E>>()};
		if (v > std::to_underlying(last))
		{
			invalid_ = true;
			return last;
		}
		return E{v};
	}
	[[nodiscard]]
	float f32()
	{
		return std::bit_cast<float>(le<std::uint32_t>());
	}
	[[nodiscard]]
	double f64()
	{
		return std::bit_cast<double>(le<std::uint64_t>());
	}
	template <std::size_t N>
	[[nodiscard]]
	std::array<float, N> f32s()
	{
		std::array<float, N> a{};
		for (float &v : a)
			v = f32();
		return a;
	}
	/* A boolean as 0 or 1; another value marks the cursor invalid and reads
	 * as false, so a decoder reads no fields that only true would add.
	 */
	[[nodiscard]]
	bool boolean()
	{
		const auto v{le<std::uint8_t>()};
		if (v > 1)
			invalid_ = true;
		return v == 1;
	}
	void invalidate()
	{
		invalid_ = true;
	}
	[[nodiscard]]
	bool invalid() const
	{
		return invalid_;
	}
	[[nodiscard]]
	std::size_t remaining() const
	{
		return rest_.size();
	}
	[[nodiscard]]
	bool failed() const
	{
		return failed_;
	}
};

trace::clear decode_clear(cursor &c)
{
	const auto flags{c.le<std::uint8_t>()};
	if (flags == 0 || flags > (clear_color | clear_depth))
		c.invalidate();
	return {.color = (flags & clear_color) != 0, .depth = (flags & clear_depth) != 0};
}

trace::texture decode_texture(cursor &c)
{
	trace::texture t{.slot = {c.le<std::uint32_t>()}, .desc = {}, .rgba = {}};
	if (t.slot == texture_slot::none)
		c.invalidate();
	if (!c.boolean())
		return t;
	extent size{.width = c.le<std::uint32_t>(), .height = c.le<std::uint32_t>()};
	if (size.width > max_texture_side || size.height > max_texture_side)
	{
		c.invalidate();
		size = {.width = 0, .height = 0};
	}
	t.desc = texture_desc{
	    .size = size,
	    .format = c.enumerated(texture_format::rgba2),
	    .filter = c.enumerated(filter::trilinear),
	    .anisotropic = c.boolean(),
	    .has_transparency = c.boolean(),
	};
	const auto rgba{c.bytes(std::size_t{4} * size.width * size.height)};
	t.rgba.assign(rgba.begin(), rgba.end());
	return t;
}

projection decode_projection(cursor &c)
{
	switch (c.le<std::uint8_t>())
	{
	case 0:
		return perspective{.fovy = c.f64(),
		                   .aspect = c.f64(),
		                   .z_near = c.f64(),
		                   .z_far = c.f64(),
		                   .stereo_dx = c.f32()};
	case 1:
		return ortho{.left = c.f64(),
		             .right = c.f64(),
		             .bottom = c.f64(),
		             .top = c.f64(),
		             .z_near = c.f64(),
		             .z_far = c.f64()};
	default:
		c.invalidate();
		return {};
	}
}

modelview decode_modelview(cursor &c)
{
	switch (c.le<std::uint8_t>())
	{
	case 0:
		return identity{};
	case 1:
		return translate_scale{.translate = c.f32s<3>(), .scale = c.f32s<3>()};
	default:
		c.invalidate();
		return {};
	}
}

trace::draw decode_draw(cursor &c)
{
	trace::draw d{
	    .topology = c.enumerated(topology::points),
	    .vertices = {},
	    .texture = {c.le<std::uint32_t>()},
	    .state = {.blend = c.enumerated(blend::none),
		          .depth = c.enumerated(depth::test_and_write),
		          .cull = c.enumerated(cull::front),
		          .alpha_ref = c.f32(),
		          .wrap = c.enumerated(wrap::repeat),
		          .line_width = c.f32()},
	    .viewport = {.x = std::bit_cast<std::int32_t>(c.le<std::uint32_t>()),
		             .y = std::bit_cast<std::int32_t>(c.le<std::uint32_t>()),
		             .size = {.width = c.le<std::uint32_t>(), .height = c.le<std::uint32_t>()}},
	    .transforms = {.projection = decode_projection(c), .modelview = decode_modelview(c)},
	};
	/* The count is checked against its bound, and then against the
	 * payload, before anything is allocated for it; the bound keeps the
	 * byte count from wrapping where size_t has 32 bits.
	 */
	auto count{c.le<std::uint32_t>()};
	if (count > max_draw_vertices)
	{
		c.invalidate();
		count = 0;
	}
	cursor v{c.bytes(vertex_size * count)};
	d.vertices.reserve(v.remaining() / vertex_size);
	while (v.remaining() != 0)
		d.vertices.push_back({.pos = v.f32s<3>(), .rgba = v.f32s<4>(), .uv = v.f32s<2>()});
	return d;
}

/* The record of kind k, or nullopt for an unknown kind. */
std::optional<record> decode(const std::uint8_t k, cursor &c)
{
	switch (k)
	{
	case std::to_underlying(kind::clear):
		return decode_clear(c);
	case std::to_underlying(kind::present):
		return trace::present{.frame = c.le<std::uint32_t>()};
	case std::to_underlying(kind::texture):
		return decode_texture(c);
	case std::to_underlying(kind::draw):
		return decode_draw(c);
	default:
		return std::nullopt;
	}
}

}

writer::writer(const extent target)
{
	if (target.width == 0 || target.height == 0)
		throw std::invalid_argument("trace::writer: render target with no pixels");
	bytes_.assign(magic.begin(), magic.end());
	sink s{bytes_};
	s.le(version);
	s.le(std::uint16_t{0});
	s.le(target.width);
	s.le(target.height);
}

void writer::clear(const trace::clear &c)
{
	if (!c.color && !c.depth)
		throw std::invalid_argument("trace::writer: clear of nothing");
	sink s{bytes_};
	s.le(std::to_underlying(kind::clear));
	s.le(std::uint32_t{1});
	s.le(static_cast<std::uint8_t>((c.color ? clear_color : 0u) | (c.depth ? clear_depth : 0u)));
}

void writer::present(const trace::present &p)
{
	sink s{bytes_};
	s.le(std::to_underlying(kind::present));
	s.le(std::uint32_t{4});
	s.le(p.frame);
}

void writer::texture(const texture_slot slot, const texture_image *const image)
{
	if (slot == texture_slot::none)
		throw std::invalid_argument("trace::writer: texture call for slot none");
	if (image && (image->desc().size.width > max_texture_side ||
	              image->desc().size.height > max_texture_side))
		throw std::length_error("trace::writer: texture side over max_texture_side");
	sink s{bytes_};
	s.le(std::to_underlying(kind::texture));
	if (!image)
	{
		s.le(std::uint32_t{5});
		s.le(slot.index);
		s.le(std::uint8_t{0});
		return;
	}
	const auto &d{image->desc()};
	const auto rgba{image->rgba()};
	s.le(static_cast<std::uint32_t>(17 + rgba.size()));
	s.le(slot.index);
	s.le(std::uint8_t{1});
	s.le(d.size.width);
	s.le(d.size.height);
	s.le(std::to_underlying(d.format));
	s.le(std::to_underlying(d.filter));
	s.le(std::uint8_t{d.anisotropic});
	s.le(std::uint8_t{d.has_transparency});
	/* Not insert(): GCC 14 reports a false stringop-overflow in it. */
	bytes_.reserve(bytes_.size() + rgba.size());
	for (const std::uint8_t b : rgba)
		bytes_.push_back(b);
}

void writer::draw(const draw_call &call)
{
	if (call.vertices.size() > max_draw_vertices)
		throw std::length_error("trace::writer: draw over max_draw_vertices");
	sink s{bytes_};
	s.le(std::to_underlying(kind::draw));
	const auto length_at{bytes_.size()};
	s.le(std::uint32_t{0});
	s.le(std::to_underlying(call.topology));
	s.le(call.texture.index);
	const auto &st{call.state};
	s.le(std::to_underlying(st.blend));
	s.le(std::to_underlying(st.depth));
	s.le(std::to_underlying(st.cull));
	s.le(st.alpha_ref);
	s.le(std::to_underlying(st.wrap));
	s.le(st.line_width);
	s.le(std::bit_cast<std::uint32_t>(call.viewport.x));
	s.le(std::bit_cast<std::uint32_t>(call.viewport.y));
	s.le(call.viewport.size.width);
	s.le(call.viewport.size.height);
	encode(s, call.transforms.projection);
	encode(s, call.transforms.modelview);
	s.le(static_cast<std::uint32_t>(call.vertices.size()));
	for (const auto &v : call.vertices)
	{
		s.le(v.pos);
		s.le(v.rgba);
		s.le(v.uv);
	}
	/* The payload's length, now that it is known. */
	const auto length{static_cast<std::uint32_t>(bytes_.size() - length_at - 4)};
	for (std::size_t i{0}; i < 4; ++i)
		bytes_[length_at + i] = static_cast<std::uint8_t>(length >> (8 * i));
}

void writer::write(const record &r)
{
	std::visit(overloads{
	               [this](const trace::clear &c) { clear(c); },
	               [this](const trace::present &p) { present(p); },
	               [this](const trace::texture &t) {
		               const auto image{t.image()};
		               texture(t.slot, image ? &image.value() : nullptr);
	               },
	               [this](const trace::draw &d) { draw(d.call()); },
	           },
	           r);
}

draw_call draw::call() const
{
	return {.topology = topology,
	        .vertices = vertices,
	        .texture = texture,
	        .state = state,
	        .viewport = viewport,
	        .transforms = transforms};
}

std::optional<texture_image> texture::image() const
{
	if (!desc)
		return std::nullopt;
	return texture_image{desc.value(), rgba};
}

std::vector<std::uint8_t> writer::take()
{
	return std::exchange(bytes_, {});
}

reader::reader(const std::span<const std::uint8_t> bytes) : bytes_{bytes}
{}

std::expected<header, read_error> reader::header()
{
	if (offset_ != 0)
		return header_;
	const auto n{std::min(bytes_.size(), magic.size())};
	if (!std::ranges::equal(bytes_.first(n), std::span{magic}.first(n)))
		return std::unexpected{read_error::bad_magic};
	/* The version first: another version may have another header. */
	if (bytes_.size() < version_end)
		return std::unexpected{read_error::truncated};
	cursor h{bytes_.subspan(magic.size())};
	const auto v{h.le<std::uint16_t>()};
	const auto zero{h.le<std::uint16_t>()};
	if (v != version || zero != 0)
		return std::unexpected{read_error::unsupported_version};
	if (bytes_.size() < header_size)
		return std::unexpected{read_error::truncated};
	const extent target{.width = h.le<std::uint32_t>(), .height = h.le<std::uint32_t>()};
	if (target.width == 0 || target.height == 0)
		return std::unexpected{read_error::bad_value};
	header_ = {.target = target};
	offset_ = header_size;
	return header_;
}

std::expected<std::optional<record>, read_error> reader::next()
{
	if (offset_ == 0)
	{
		if (const auto h{header()}; !h)
			return std::unexpected{h.error()};
	}
	if (offset_ == bytes_.size())
		return std::nullopt;
	cursor head{bytes_.subspan(offset_)};
	const auto k{head.le<std::uint8_t>()};
	const auto length{head.le<std::uint32_t>()};
	if (head.failed() || length > head.remaining())
		return std::unexpected{read_error::truncated};
	cursor payload{head.bytes(length)};
	auto r{decode(k, payload)};
	if (!r)
		return std::unexpected{read_error::unknown_kind};
	/* A payload too short is a bad length, whatever the zeros read past
	 * its end make of the fields.  Then a bad value comes before bytes
	 * to spare: after one (a projection tag of 2) the rest of the
	 * payload is not where the decoder looks.
	 */
	if (payload.failed())
		return std::unexpected{read_error::bad_length};
	if (payload.invalid())
		return std::unexpected{read_error::bad_value};
	if (payload.remaining() != 0)
		return std::unexpected{read_error::bad_length};
	offset_ += record_head_size + length;
	return std::move(r.value());
}

std::expected<std::vector<record>, read_error> read_all(const std::span<const std::uint8_t> bytes)
{
	reader rd{bytes};
	std::vector<record> records;
	for (;;)
	{
		auto r{rd.next()};
		if (!r)
			return std::unexpected{r.error()};
		if (!r.value())
			return records;
		records.push_back(std::move(r.value().value()));
	}
}

}

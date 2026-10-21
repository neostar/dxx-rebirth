/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/trace.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dcx::render::trace {

namespace {

enum class kind : std::uint8_t
{
	clear = 1,
	present = 2,
	texture = 3,
};

/* The header's length, and a record's before its payload. */
constexpr std::size_t header_size{magic.size() + 4};
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
};

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
	/* An enum of up to last, or a boolean as 0 or 1; another value marks
	 * the cursor invalid.  An invalid boolean reads as false, so the
	 * fields it would have added are not read.
	 */
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
	if (flags > (clear_color | clear_depth))
		c.invalidate();
	return {.color = (flags & clear_color) != 0, .depth = (flags & clear_depth) != 0};
}

trace::texture decode_texture(cursor &c)
{
	trace::texture t{.slot = {c.le<std::uint32_t>()}, .desc = {}, .rgba = {}};
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

/* The record of kind k, or nullopt for an unknown kind. */
std::optional<record> decode(const std::uint8_t k, cursor &c)
{
	switch (k)
	{
	case std::to_underlying(kind::clear):
		return decode_clear(c);
	case std::to_underlying(kind::present):
		return trace::present{};
	case std::to_underlying(kind::texture):
		return decode_texture(c);
	default:
		return std::nullopt;
	}
}

}

writer::writer()
{
	bytes_.assign(magic.begin(), magic.end());
	sink s{bytes_};
	s.le(version);
	s.le(std::uint16_t{0});
}

void writer::clear(const trace::clear &c)
{
	sink s{bytes_};
	s.le(std::to_underlying(kind::clear));
	s.le(std::uint32_t{1});
	s.le(static_cast<std::uint8_t>((c.color ? clear_color : 0u) | (c.depth ? clear_depth : 0u)));
}

void writer::present()
{
	sink s{bytes_};
	s.le(std::to_underlying(kind::present));
	s.le(std::uint32_t{0});
}

void writer::texture(const texture_slot slot, const texture_image *const image)
{
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
	if (d.size.width > max_texture_side || d.size.height > max_texture_side)
		throw std::length_error("trace::writer: texture side over max_texture_side");
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

std::expected<std::optional<record>, read_error> reader::next()
{
	if (error_)
		return std::unexpected{error_.value()};
	const auto fail{[this](const read_error e) {
		error_ = e;
		return std::unexpected{e};
	}};
	if (offset_ == 0)
	{
		const auto n{std::min(bytes_.size(), magic.size())};
		if (!std::ranges::equal(bytes_.first(n), std::span{magic}.first(n)))
			return fail(read_error::bad_magic);
		if (bytes_.size() < header_size)
			return fail(read_error::truncated);
		cursor h{bytes_.subspan(magic.size())};
		const auto v{h.le<std::uint16_t>()};
		const auto zero{h.le<std::uint16_t>()};
		if (v != version || zero != 0)
			return fail(read_error::unsupported_version);
		offset_ = header_size;
	}
	if (offset_ == bytes_.size())
		return std::nullopt;
	cursor head{bytes_.subspan(offset_)};
	const auto k{head.le<std::uint8_t>()};
	const auto length{head.le<std::uint32_t>()};
	if (head.failed() || length > head.remaining())
		return fail(read_error::truncated);
	cursor payload{head.bytes(length)};
	auto r{decode(k, payload)};
	if (!r)
		return fail(read_error::unknown_kind);
	/* A payload too short reads zeros, which may decode as a bad value:
	 * its length is the error.
	 */
	if (payload.failed() || payload.remaining() != 0)
		return fail(read_error::bad_length);
	if (payload.invalid())
		return fail(read_error::bad_value);
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

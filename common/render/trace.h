/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * The trace file format (.rtr, GLOSSARY.md): render-device calls as
 * self-contained records, texture pixels included, so a trace replays
 * without the game.  The writer turns calls into bytes and the reader turns
 * bytes back into records; neither touches a file, the caller does.
 *
 * Layout, every integer little-endian, every float as its IEEE bits:
 *   header: the 8 bytes of magic, u16 version, u16 zero
 *   record: u8 kind, u32 payload length, payload
 * A record's payload length must match what its kind holds exactly.  A
 * trace ends at the end of a record; a partial record is malformed.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "common/render/render_device.h"

namespace dcx::render::trace {

inline constexpr std::array<std::uint8_t, 8> magic{{'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n'}};
inline constexpr std::uint16_t version{1};

/* A clear of the whole target: colour to (0, 0, 0, 0), depth to 1.  The
 * game clears depth alone in mid-frame for the rear view (spike #55), so
 * each is recorded on its own.
 */
struct clear
{
	bool color;
	bool depth;
	constexpr bool operator==(const clear &) const = default;
};

/* The end of a frame: everything recorded since the last present is one
 * frame.
 */
struct present
{
	constexpr bool operator==(const present &) const = default;
};

/* The largest width or height of a texture image in a trace, a bound that
 * keeps a malformed size from asking for gigabytes.
 */
inline constexpr std::uint32_t max_texture_side{4096};

/* A texture call: the image for a slot, its pixels copied, or no image,
 * which releases the slot.
 */
struct texture
{
	texture_slot slot{};
	std::optional<texture_desc> desc{};
	std::vector<std::uint8_t> rgba{};
	/* A view of the image, valid while this record lives; nullopt for a
	 * release.
	 */
	[[nodiscard]]
	std::optional<texture_image> image() const;
	bool operator==(const texture &) const = default;
};

/* A draw call, its vertices copied. */
struct draw
{
	render::topology topology{};
	std::vector<vertex> vertices{};
	texture_slot texture{};
	draw_state state{};
	render::viewport viewport{};
	render::transforms transforms{};
	/* The call, viewing this record's vertices. */
	[[nodiscard]]
	draw_call call() const;
	bool operator==(const draw &) const = default;
};

using record = std::variant<clear, present, texture, draw>;

/* Turns device calls into the bytes of a trace, header first.  take()
 * hands over the bytes written so far, so a recorder can append them to
 * its file at each present instead of holding the whole trace.
 */
class writer
{
	std::vector<std::uint8_t> bytes_;

  public:
	writer();
	void clear(const trace::clear &c);
	void present();
	/* The image for a slot, or nullptr to release it.  Throws
	 * std::length_error for a side over max_texture_side, which the reader
	 * would reject.
	 */
	void texture(texture_slot slot, const texture_image *image);
	void draw(const draw_call &call);
	/* A record as read, written back as the same bytes. */
	void write(const record &r);
	[[nodiscard]]
	std::vector<std::uint8_t> take();
};

enum class read_error : std::uint8_t
{
	bad_magic,
	unsupported_version,
	/* The bytes end inside the header or a record. */
	truncated,
	unknown_kind,
	/* A payload length that does not match what the record holds. */
	bad_length,
	/* A field outside its range: an enum, a flag, a size over its bound. */
	bad_value,
};

/* Reads the records of a trace held in memory, one at a time.  The bytes
 * are viewed, not copied, and must outlive the reader; records own their
 * contents.  The first error is final: every later next() returns it.
 */
class reader
{
	std::span<const std::uint8_t> bytes_;
	std::size_t offset_{0};
	std::optional<read_error> error_;

  public:
	explicit reader(std::span<const std::uint8_t> bytes);
	/* The next record, or nullopt at the end of the trace. */
	[[nodiscard]]
	std::expected<std::optional<record>, read_error> next();
	/* Where the next record starts, or where the error was found. */
	[[nodiscard]]
	std::size_t offset() const
	{
		return offset_;
	}
};

/* Every record of a trace, or the first error. */
[[nodiscard]]
std::expected<std::vector<record>, read_error> read_all(std::span<const std::uint8_t> bytes);

}

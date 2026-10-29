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
 *   header: the 8 bytes of magic, u16 version, u16 zero, u32 width and
 *     u32 height of the render target, neither zero
 *   record: u8 kind, u32 payload length, payload
 * A record's payload length must match what its kind holds exactly.  A
 * trace ends at the end of a record; a partial record is malformed.
 * Payloads by kind (enums as u8, booleans as u8 0 or 1):
 *   1 clear: u8 flags, bit 0 colour, bit 1 depth, at least one set
 *   2 present: u32 frame
 *   3 texture: u32 slot (not 0, which is none), u8 has image; with one:
 *     u32 width, u32 height (each at most max_texture_side), format,
 *     filter, anisotropic, has_transparency, width*height*4 bytes
 *   4 draw: topology, u32 slot, blend, depth, cull, f32 alpha_ref, wrap,
 *     f32 line_width, i32 x, i32 y, u32 width, u32 height; u8 projection
 *     (0 perspective: f64 fovy, aspect, z_near, z_far, f32 stereo_dx;
 *     1 ortho: f64 left, right, bottom, top, z_near, z_far); u8 modelview
 *     (0 identity; 1 translate_scale: f32 translate[3], scale[3]); u32
 *     count (at most max_draw_vertices), then count vertices of f32 pos[3],
 *     rgba[4], uv[2]
 * A new kind or field, or a changed range, is a new version.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "common/render/render_device.h"

namespace dcx::render::trace {

inline constexpr std::array<std::uint8_t, 8> magic{{'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n'}};
inline constexpr std::uint16_t version{1};

/* What a trace holds before its records. */
struct header
{
	/* The size of the render target the calls drew to, which a replay
	 * creates.
	 */
	extent target;
	constexpr bool operator==(const header &) const = default;
};

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
 * frame.  frame is the game's index of it, its present's place in the run
 * from 0 (recording_device.h's frame_range), so a replay of a part of a
 * game can compare its frames with the frame hash log's lines.  rtrace's
 * lines hash rows top first, the log's bottom first (GL's readback): only
 * rtrace's game-order lines (RTRACE_GAME_LOG, rtrace/replay_file.h) equal
 * the log's.
 */
struct present
{
	std::uint32_t frame;
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

/* The most vertices of a draw in a trace, a bound that keeps a malformed
 * count from asking for gigabytes and a draw's payload within its u32
 * length: 2^20.
 */
inline constexpr std::uint32_t max_draw_vertices{1048576};

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
	/* Throws std::invalid_argument for a target with no pixels, which the
	 * reader would reject.
	 */
	explicit writer(extent target);
	/* Throws std::invalid_argument for a clear of neither colour nor
	 * depth, which the reader would reject.
	 */
	void clear(const trace::clear &c);
	void present(const trace::present &p);
	/* The image for a slot, or nullptr to release it.  Throws
	 * std::invalid_argument for slot none and std::length_error for a side
	 * over max_texture_side, which the reader would reject.
	 */
	void texture(texture_slot slot, const texture_image *image);
	/* Throws std::length_error for more than max_draw_vertices vertices,
	 * which the reader would reject.
	 */
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

/* The error's name for a message: "bad magic", "truncated", ... */
[[nodiscard]]
std::string_view to_string(read_error e);

/* Reads the records of a trace held in memory, one at a time.  The bytes
 * are viewed, not copied, and must outlive the reader; records own their
 * contents.  The first error is final: next() stays where it found it, so
 * every later call finds it again.
 */
class reader
{
	std::span<const std::uint8_t> bytes_;
	std::size_t offset_{0};
	trace::header header_{};

  public:
	explicit reader(std::span<const std::uint8_t> bytes);
	/* The header, read by the first call to this or next(). */
	[[nodiscard]]
	std::expected<trace::header, read_error> header();
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

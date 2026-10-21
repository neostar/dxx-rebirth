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
#include <cstdint>
#include <variant>
#include <vector>

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

using record = std::variant<clear, present>;

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
	[[nodiscard]]
	std::vector<std::uint8_t> take();
};

}

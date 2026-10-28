/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "common/sdl/dxx-sdl.h"

namespace dcx::sdl {

/* Text input for the game's name, chat and savegame-name fields (#42,
 * docs/agents/sdl-input.md, "Text input").  SDL3 starts no text input by
 * itself, and starting it may show an input method editor, so it runs only
 * while a field is open.
 */

/* Whether text input is on.  open() and close() are idempotent: a menu that
 * is activated twice opens once, and a close with nothing open does nothing.
 * If SDL refuses to start (no window yet), the gate stays closed, so key
 * events keep filling the text buffer.
 */
template <typename Ops>
class text_input_gate
{
	Ops ops;
	bool is_open{false};

  public:
	explicit text_input_gate(Ops o) : ops{std::move(o)}
	{}
	void open()
	{
		if (!is_open)
			is_open = ops.start();
	}
	void close()
	{
		if (is_open)
		{
			ops.stop();
			is_open = false;
		}
	}
	[[nodiscard]]
	bool active() const
	{
		return is_open;
	}
};

/* Append one code point to the first free (zero) slot of a buffer of game
 * characters, one byte each as the fonts take them: code points 32 to 254
 * (ASCII and Latin-1).  Anything else, and anything that does not fit, is
 * dropped.
 */
inline void append_game_character(const std::span<unsigned char> buffer, const std::uint32_t cp)
{
	if (cp < 32 || cp > 254)
		return;
	for (auto &slot : buffer)
		if (slot == 0)
		{
			slot = static_cast<unsigned char>(cp);
			return;
		}
}

/* Append the characters of UTF-8 text, as append_game_character does.  SDL_StepUTF8 decodes it:
 * it reports each bad byte (a bad continuation, an overlong form, a lead above 0xf4, a
 * truncated sequence) as SDL_INVALID_UNICODE_CODEPOINT, which is dropped like any code point
 * above 254, and the characters after it still arrive.
 */
inline void append_game_text(const std::span<unsigned char> buffer, const std::string_view utf8)
{
	const char *next{utf8.data()};
	std::size_t left{utf8.size()};
	while (const std::uint32_t cp{SDL_StepUTF8(&next, &left)})
		append_game_character(buffer, cp);
}

}

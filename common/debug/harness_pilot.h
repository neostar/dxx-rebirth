/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace dcx {

/* The pilot that `-pilot arg` names for the debug harness, which creates it
 * when its player file is missing: the name lowercased, without a ".plr"
 * extension, when the new-pilot menu would accept it (1 to 8 letters,
 * digits, `_` or `-`, menu.cpp's playername_allowed_chars and CALLSIGN_LEN);
 * nullopt otherwise.  A longer name is rejected rather than cut: upstream
 * cuts the argument to 12 characters and appends ".plr" only below 17
 * with the players directory, so for a 9 to 12 character name it checks
 * one file and the callsign names another, which creating the pilot would
 * overwrite.
 */
[[nodiscard]]
inline std::optional<std::string> harness_pilot_name(const std::string_view arg)
{
	constexpr std::size_t callsign_length{8};
	constexpr std::string_view extension{".plr"};
	std::string name;
	name.reserve(arg.size());
	std::ranges::transform(arg, std::back_inserter(name), [](const char c) {
		return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
	});
	if (name.ends_with(extension))
		name.resize(name.size() - extension.size());
	if (name.empty() || name.size() > callsign_length)
		return std::nullopt;
	const auto allowed{[](const char c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
	}};
	if (!std::ranges::all_of(name, allowed))
		return std::nullopt;
	return name;
}

}

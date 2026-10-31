/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#pragma once

#include <SDL_mixer.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/sdl/effect_mixer.h"

namespace dcx::sdl {

/* SDL3_mixer 3.2.4's FluidSynth properties, internal to it
 * (SDL_mixer_internal.h:229-231) and so not in its public header
 * (docs/agents/sdl-audio.md area 6).
 */
inline constexpr const char *fluidsynth_soundfont_iostream_property{
    "SDL_mixer.decoder.fluidsynth.soundfont_iostream"};
inline constexpr const char *fluidsynth_soundfont_path_property{
    "SDL_mixer.decoder.fluidsynth.soundfont_path"};
inline constexpr const char *fluidsynth_props_property{"SDL_mixer.decoder.fluidsynth.props"};

/* SDL2_mixer reaches 1.0 at full music volume, and SDL3_mixer sets the same:
 * parity, set here too so that it stays so (area 6).
 */
inline constexpr float fluidsynth_synth_gain{1.0f};

/* The first bytes of a SoundFont (a RIFF file) and of a MIDI file. */
inline constexpr std::array<uint8_t, 4> riff_magic{'R', 'I', 'F', 'F'};
inline constexpr std::array<uint8_t, 4> midi_magic{'M', 'T', 'h', 'd'};
/* A SoundFont's RIFF form type, bytes 8 to 11. */
inline constexpr std::array<uint8_t, 4> sfbk_form{'s', 'f', 'b', 'k'};

using io_stream_ptr = sdl_unique_ptr<SDL_IOStream, SDL_CloseIO>;

/* Why read_whole_file read nothing. */
enum class file_read_error : uint8_t
{
	unreadable,
	over_limit,
};

/* A whole file from the filesystem, or why it cannot be read; at most limit
 * bytes, a guard against a very large allocation.
 */
[[nodiscard]]
inline std::expected<std::vector<uint8_t>, file_read_error> read_whole_file(const char *const path,
                                                                            const uint64_t limit)
{
	const io_stream_ptr io{SDL_IOFromFile(path, "rb")};
	if (!io)
		return std::unexpected{file_read_error::unreadable};
	const Sint64 size{SDL_GetIOSize(io.get())};
	if (size < 0)
		return std::unexpected{file_read_error::unreadable};
	if (static_cast<uint64_t>(size) > limit)
		return std::unexpected{file_read_error::over_limit};
	std::vector<uint8_t> data(static_cast<std::size_t>(size));
	data.resize(SDL_ReadIO(io.get(), data.data(), data.size()));
	return data;
}

/* Whether an SDL3_mixer decoder, by its MIX_GetAudioDecoder name, plays
 * MIDI.  Native MIDI is gone from SDL3_mixer.
 */
[[nodiscard]]
inline bool is_midi_decoder(const std::string_view name)
{
	return name == "FLUIDSYNTH" || name == "TIMIDITY";
}

/* The decoders this SDL3_mixer build lists, after MIX_Init; the set differs
 * per build (area 9).
 */
[[nodiscard]]
inline std::vector<std::string> audio_decoders()
{
	std::vector<std::string> names;
	const int n{MIX_GetNumAudioDecoders()};
	for (int i{0}; i < n; ++i)
		if (const char *const name{MIX_GetAudioDecoder(i)})
			names.emplace_back(name);
	return names;
}

[[nodiscard]]
inline bool has_midi_decoder(const std::span<const std::string> decoders)
{
	for (const auto &d : decoders)
		if (is_midi_decoder(d))
			return true;
	return false;
}

/* Why the SoundFont at path cannot play MIDI, or nothing when it can be
 * read and starts as a RIFF file of form sfbk.  SDL3_mixer loads MIDI without a usable
 * SoundFont and plays silence (or, on Linux, a system SoundFont), so the
 * game checks before the load.
 */
[[nodiscard]]
inline std::optional<std::string> soundfont_problem(const char *const path)
{
	if (path == nullptr || *path == 0)
		return std::string{"no SoundFont given (-soundfont <file>)"};
	const io_stream_ptr io{SDL_IOFromFile(path, "rb")};
	if (!io)
		return std::string{"SoundFont \""} + path + "\" is not readable: " + SDL_GetError();
	/* RIFF, the chunk size, then the form type: a WAV or a DLS is RIFF too,
	 * and FluidSynth would reject it only when the song starts.
	 */
	std::array<uint8_t, 12> header{};
	const auto got{SDL_ReadIO(io.get(), header.data(), header.size())};
	if (got != header.size() || !std::ranges::equal(std::span{header}.first<4>(), riff_magic) ||
	    !std::ranges::equal(std::span{header}.last<4>(), sfbk_form))
		return std::string{"SoundFont \""} + path + "\" is not a SoundFont (no RIFF sfbk header)";
	return std::nullopt;
}

}

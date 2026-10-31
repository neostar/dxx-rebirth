/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/sdl/music_source.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth music source
#include <boost/test/unit_test.hpp>

/* The seam: the checks and the load run on SDL3_mixer itself, with a
 * generated mixer (MIX_CreateMixer) and files the test writes to a temporary
 * directory; no device, no game data, no SoundFont of the game's.
 */
namespace {

struct mixer_init
{
	mixer_init()
	{
		if (!MIX_Init())
			throw std::runtime_error{std::string{"MIX_Init failed: "} + SDL_GetError()};
	}
	~mixer_init()
	{
		MIX_Quit();
	}
	mixer_init(const mixer_init &) = delete;
	mixer_init &operator=(const mixer_init &) = delete;
	mixer_init(mixer_init &&) = delete;
	mixer_init &operator=(mixer_init &&) = delete;
};

/* A file with the given bytes under the temporary directory, removed at the
 * end of the test.
 */
struct temp_file
{
	std::filesystem::path path;
	temp_file(const char *const name, const std::vector<uint8_t> &bytes)
	    : path{std::filesystem::temp_directory_path() / name}
	{
		const std::string text(bytes.begin(), bytes.end());
		std::ofstream f{path, std::ios::binary};
		f.write(text.data(), static_cast<std::streamsize>(text.size()));
	}
	~temp_file()
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
	temp_file(const temp_file &) = delete;
	temp_file &operator=(const temp_file &) = delete;
	temp_file(temp_file &&) = delete;
	temp_file &operator=(temp_file &&) = delete;
};

/* The 12 bytes a SoundFont starts with: RIFF, a size, the form type sfbk. */
const std::vector<uint8_t> sfbk_header{'R', 'I', 'F', 'F', 4, 0, 0, 0, 's', 'f', 'b', 'k'};

}

BOOST_GLOBAL_FIXTURE(mixer_init);

/* Test that a missing, unreadable or non-RIFF SoundFont is reported with its
 * path, and a readable RIFF file passes.
 */
BOOST_AUTO_TEST_CASE(soundfont_problem_names_what_is_wrong)
{
	BOOST_TEST(dcx::sdl::soundfont_problem(nullptr).value().find("no SoundFont") !=
	           std::string::npos);
	BOOST_TEST(dcx::sdl::soundfont_problem("").value().find("no SoundFont") != std::string::npos);
	const auto missing{(std::filesystem::temp_directory_path() / "dxx-no-such.sf2").string()};
	BOOST_TEST(dcx::sdl::soundfont_problem(missing.c_str()).value().find("not readable") !=
	           std::string::npos);
	const temp_file text{"dxx-not-a-soundfont.sf2", {'h', 'e', 'l', 'l', 'o'}};
	BOOST_TEST(
	    dcx::sdl::soundfont_problem(text.path.string().c_str()).value().find("not a SoundFont") !=
	    std::string::npos);
	const temp_file riff{"dxx-riff.sf2", sfbk_header};
	BOOST_TEST(!dcx::sdl::soundfont_problem(riff.path.string().c_str()).has_value());
}

/* Test that a RIFF file of another form (a WAV, a DLS) or one cut short of
 * its form type is not taken for a SoundFont: FluidSynth would reject it only
 * when the song starts, after the load succeeded.
 */
BOOST_AUTO_TEST_CASE(soundfont_problem_wants_the_sfbk_form)
{
	const temp_file wav{"dxx-wav.sf2", {'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'A', 'V', 'E'}};
	BOOST_TEST(
	    dcx::sdl::soundfont_problem(wav.path.string().c_str()).value().find("not a SoundFont") !=
	    std::string::npos);
	const temp_file cut{"dxx-cut.sf2", {'R', 'I', 'F', 'F', 0, 0, 0, 0}};
	BOOST_TEST(
	    dcx::sdl::soundfont_problem(cut.path.string().c_str()).value().find("not a SoundFont") !=
	    std::string::npos);
}

/* Test that a whole file is read as it is, an empty one as nothing, and
 * that a missing file and one over the limit each say why they read nothing.
 */
BOOST_AUTO_TEST_CASE(read_whole_file_reads_up_to_the_limit)
{
	using dcx::sdl::file_read_error;
	using dcx::sdl::read_whole_file;
	const temp_file five{"dxx-five.bin", {1, 2, 3, 4, 5}};
	const auto path{five.path.string()};
	const auto whole{read_whole_file(path.c_str(), 5)};
	BOOST_REQUIRE(whole.has_value());
	BOOST_TEST(whole.value() == (std::vector<uint8_t>{1, 2, 3, 4, 5}));
	const auto over{read_whole_file(path.c_str(), 4)};
	BOOST_TEST((!over && over.error() == file_read_error::over_limit));
	const temp_file empty{"dxx-empty.bin", {}};
	const auto none{read_whole_file(empty.path.string().c_str(), 5)};
	BOOST_TEST((none && none.value().empty()));
	const auto missing{(std::filesystem::temp_directory_path() / "dxx-no-such.bin").string()};
	const auto gone{read_whole_file(missing.c_str(), 5)};
	BOOST_TEST((!gone && gone.error() == file_read_error::unreadable));
}

/* Test that only the MIDI decoders count as MIDI, and that the pinned
 * SDL3_mixer build lists one (FluidSynth, loaded at MIX_Init).
 */
BOOST_AUTO_TEST_CASE(midi_decoders_are_recognised)
{
	const std::vector<std::string> with{"WAV", "VORBIS", "FLUIDSYNTH"};
	const std::vector<std::string> without{"WAV", "VORBIS", "RAW"};
	BOOST_TEST(dcx::sdl::has_midi_decoder(with));
	BOOST_TEST(!dcx::sdl::has_midi_decoder(without));
	BOOST_TEST(dcx::sdl::is_midi_decoder("TIMIDITY"));
	BOOST_TEST(dcx::sdl::has_midi_decoder(dcx::sdl::audio_decoders()));
}

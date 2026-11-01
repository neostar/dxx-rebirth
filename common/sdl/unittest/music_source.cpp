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

/* A MIDI file of one empty track: header and an end-of-track event. */
const std::vector<uint8_t> tiny_midi{
    'M', 'T', 'h', 'd', 0,   0, 0, 6, 0, 0, 0,    1,    0,
    96,  'M', 'T', 'r', 'k', 0, 0, 0, 4, 0, 0xff, 0x2f, 0,
};

/* A WAV file of 4 frames of U8 mono silence at 22050 Hz. */
const std::vector<uint8_t> tiny_wav{
    'R', 'I', 'F', 'F', 40,  0,   0,   0,   'W',  'A',  'V', 'E', 'f',  'm',  't',  ' ',
    16,  0,   0,   0,   1,   0,   1,   0,   0x22, 0x56, 0,   0,   0x22, 0x56, 0,    0,
    1,   0,   8,   0,   'd', 'a', 't', 'a', 4,    0,    0,   0,   0x80, 0x80, 0x80, 0x80,
};

dcx::sdl::mix_mixer_ptr make_mixer()
{
	const SDL_AudioSpec spec{.format = SDL_AUDIO_F32, .channels = 2, .freq = 48000};
	return dcx::sdl::mix_mixer_ptr{MIX_CreateMixer(&spec)};
}

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

/* Test that MIDI does not load without a usable SoundFont or a MIDI decoder,
 * and says why, where SDL3_mixer alone would load it and play silence.
 */
BOOST_AUTO_TEST_CASE(midi_needs_a_soundfont_and_a_decoder)
{
	const auto mixer{make_mixer()};
	BOOST_REQUIRE(mixer);
	const auto missing{(std::filesystem::temp_directory_path() / "dxx-no-such.sf2").string()};
	const auto no_font{dcx::sdl::load_music(mixer.get(), tiny_midi, missing.c_str(), true)};
	BOOST_TEST(!no_font.audio);
	BOOST_TEST(no_font.error.find("not readable") != std::string::npos);
	const temp_file riff{"dxx-riff.sf2", sfbk_header};
	const auto no_decoder{
	    dcx::sdl::load_music(mixer.get(), tiny_midi, riff.path.string().c_str(), false)};
	BOOST_TEST(!no_decoder.audio);
	BOOST_TEST(no_decoder.error.find("no MIDI decoder") != std::string::npos);
}

/* Test that a song FluidSynth cannot start, a SoundFont with an sfbk header
 * and no content, is reported at the commit that tried to start it, once,
 * and does not count as playing or as ended.
 */
BOOST_AUTO_TEST_CASE(a_song_that_cannot_start_is_reported)
{
	dcx::sdl::effect_mixer mixer{make_mixer()};
	BOOST_REQUIRE(mixer.usable());
	BOOST_REQUIRE(dcx::sdl::has_midi_decoder(dcx::sdl::audio_decoders()));
	const temp_file font{"dxx-empty.sf2", sfbk_header};
	auto loaded{dcx::sdl::load_music(mixer.mixer(), tiny_midi, font.path.string().c_str(), true)};
	BOOST_REQUIRE(static_cast<bool>(loaded.audio));
	mixer.play_music(std::move(loaded.audio), false);
	mixer.commit_frame();
	const auto failed{mixer.take_music_failed()};
	BOOST_TEST(failed.has_value());
	BOOST_TEST(!mixer.take_music_failed().has_value());
	BOOST_TEST(!mixer.music_playing());
	BOOST_TEST(!mixer.take_music_ended());
}

/* Test that a song that could not start at stop_all's commit, with no
 * frame to take the failure (a level load), is not reported against the
 * next song.
 */
BOOST_AUTO_TEST_CASE(a_failure_seen_by_stop_all_is_not_reported_later)
{
	dcx::sdl::effect_mixer mixer{make_mixer()};
	BOOST_REQUIRE(mixer.usable());
	BOOST_REQUIRE(dcx::sdl::has_midi_decoder(dcx::sdl::audio_decoders()));
	const temp_file font{"dxx-empty.sf2", sfbk_header};
	auto loaded{dcx::sdl::load_music(mixer.mixer(), tiny_midi, font.path.string().c_str(), true)};
	BOOST_REQUIRE(static_cast<bool>(loaded.audio));
	mixer.play_music(std::move(loaded.audio), false);
	mixer.stop_all();
	auto wav{dcx::sdl::load_music(mixer.mixer(), tiny_wav, nullptr, false)};
	BOOST_REQUIRE(static_cast<bool>(wav.audio));
	mixer.play_music(std::move(wav.audio), true);
	mixer.commit_frame();
	BOOST_TEST(!mixer.take_music_failed().has_value());
	BOOST_TEST(mixer.music_playing());
}

/* Test that a song in another format loads with no SoundFont at all. */
BOOST_AUTO_TEST_CASE(other_formats_need_no_soundfont)
{
	const auto mixer{make_mixer()};
	BOOST_REQUIRE(mixer);
	const auto wav{dcx::sdl::load_music(mixer.get(), tiny_wav, nullptr, false)};
	BOOST_TEST(static_cast<bool>(wav.audio));
	BOOST_TEST(wav.error.empty());
}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/* A pilot file the SDL2 build wrote, read by the D2X SDL2 and SDL3 builds (#44).  The seam is
 * the game's own playsave.cpp and kconfig.cpp, the objects the D2 game links, reading and writing
 * pilot files through PhysicsFS in a scratch directory; the rest of the game is stubbed below.
 *
 * pilot_file_sdl2.inc holds the .plr and .plx of a new pilot made with a gamepad open (so the
 * gamepad defaults, axis-buttons among them) and then given the stick bindings below.  The SDL2
 * build in the reference container (Linux, SDL 2.30.0) wrote them: with DXX_PILOT_FILE_PRINT=1
 * test-pilot-file prints the file.  The format holds no game data.
 */

// clang-format off
#include "dxxsconf.h"
// clang-format on
#include <SDL.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unistd.h>
#include <vector>

// clang-format off
#include "tools/dev/upstream_begin.h"
#include <physfs.h>
#include "args.h"
#include "config.h"
#include "console.h"
#include "dxxerror.h"
#include "event.h"
#include "fwd-weapon.h"
#include "game.h"
#include "gamefont.h"
#include "gr.h"
#include "inferno.h"
#include "joy.h"
#include "kconfig.h"
#include "key.h"
#include "mission.h"
#include "mouse.h"
#include "movie.h"
#include "multi.h"
#include "newdemo.h"
#include "newmenu.h"
#include "playsave.h"
#include "screens.h"
#include "text.h"
#include "timer.h"
#include "window.h"
#include "tools/dev/upstream_end.h"
// clang-format on

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth pilot file
#include <boost/test/unit_test.hpp>

namespace {

constexpr std::string_view sdl2_pilot{"sdl2pad"};

/* The stick and axis-button bindings the pilot was given after the defaults: control (the index
 * in the joystick bindings) and number (the button or axis the game reports).  Numbers 21-32 are
 * the gamepad's axis-buttons, 33 and up a stick's controls after the gamepad's.
 */
struct stick_binding
{
	unsigned control;
	std::uint8_t number;
};
constexpr std::array<stick_binding, 4> stick_bindings{{
    {.control = 2, .number = 24},
    {.control = 3, .number = 31},
    {.control = 5, .number = 40},
    {.control = 6, .number = 70},
}};
/* A new pilot, made as the new-pilot menu makes one with a gamepad open: the gamepad's
 * defaults.
 */
void make_new_pilot()
{
	num_controllers = 1;
	new_player_config();
}

void give_stick_bindings()
{
	for (const auto &b : stick_bindings)
		PlayerCfg.KeySettings.Joystick[dxx_kconfig_ui_kc_joystick{b.control}] = b.number;
}

void use_pilot(const std::string_view name)
{
	InterfaceUniqueState.PilotName.copy_lower(std::span(name.data(), name.size()));
}

/* Read pilot `name` as the game does.  read_player_file sets a new pilot's defaults first; with
 * no gamepad open those are the keyboard's, so a gamepad binding the file does not give stays
 * different.
 */
void read_pilot(const std::string_view name)
{
	use_pilot(name);
	num_controllers = 0;
	BOOST_REQUIRE(read_player_file() == EZERO);
}

/* Every binding and joystick setting a pilot file holds.  The .plx's weapon keys (`rebirth`)
 * never load, on any build: read_player_dxx scans the text after the `=` with "%d=", so a load
 * keeps the defaults, and the pilot is given none of its own.
 */
struct bindings
{
	std::uint8_t control_type{0};
	std::vector<unsigned> keyboard{}, joystick{}, mouse{}, rebirth{};
	std::vector<int> joystick_sens{}, joystick_dead{}, joystick_linear{}, joystick_speed{};
};

/* The pilot file's bytes, pilot_plr and pilot_plx, and its bindings as the SDL2 build had
 * them, sdl2_bindings().
 */
#include "common/sdl/unittest/pilot_file_sdl2.inc"

template <typename T, typename A>
std::vector<T> to_vector(const A &a)
{
	return {a.begin(), a.end()};
}

bindings bindings_of_player_config()
{
	const auto &k{PlayerCfg.KeySettings};
	return {.control_type = PlayerCfg.ControlType,
	        .keyboard = to_vector<unsigned>(k.Keyboard),
	        .joystick = to_vector<unsigned>(k.Joystick),
	        .mouse = to_vector<unsigned>(k.Mouse),
	        .rebirth = to_vector<unsigned>(PlayerCfg.KeySettingsRebirth),
	        .joystick_sens = to_vector<int>(PlayerCfg.JoystickSens),
	        .joystick_dead = to_vector<int>(PlayerCfg.JoystickDead),
	        .joystick_linear = to_vector<int>(PlayerCfg.JoystickLinear),
	        .joystick_speed = to_vector<int>(PlayerCfg.JoystickSpeed)};
}

/* Compare field by field, each element on its own, so a failure names the binding. */
void check_equal(const bindings &got, const bindings &want)
{
	BOOST_TEST(got.control_type == want.control_type);
	BOOST_TEST(got.keyboard == want.keyboard, boost::test_tools::per_element());
	BOOST_TEST(got.joystick == want.joystick, boost::test_tools::per_element());
	BOOST_TEST(got.mouse == want.mouse, boost::test_tools::per_element());
	BOOST_TEST(got.rebirth == want.rebirth, boost::test_tools::per_element());
	BOOST_TEST(got.joystick_sens == want.joystick_sens, boost::test_tools::per_element());
	BOOST_TEST(got.joystick_dead == want.joystick_dead, boost::test_tools::per_element());
	BOOST_TEST(got.joystick_linear == want.joystick_linear, boost::test_tools::per_element());
	BOOST_TEST(got.joystick_speed == want.joystick_speed, boost::test_tools::per_element());
}

std::string pilot_path(const std::string_view name, const std::string_view extension)
{
	return PLAYER_DIRECTORY_STRING("") + std::string{name} + std::string{extension};
}

void write_file(const std::string &path, const std::span<const std::uint8_t> bytes)
{
	PHYSFS_File *const f{PHYSFS_openWrite(path.c_str())};
	BOOST_REQUIRE(f);
	BOOST_REQUIRE(PHYSFS_writeBytes(f, bytes.data(), bytes.size()) ==
	              static_cast<PHYSFS_sint64>(bytes.size()));
	BOOST_REQUIRE(PHYSFS_close(f));
}

std::vector<std::uint8_t> read_file(const std::string &path)
{
	PHYSFS_File *const f{PHYSFS_openRead(path.c_str())};
	BOOST_REQUIRE(f);
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(PHYSFS_fileLength(f)));
	BOOST_REQUIRE(PHYSFS_readBytes(f, bytes.data(), bytes.size()) ==
	              static_cast<PHYSFS_sint64>(bytes.size()));
	PHYSFS_close(f);
	return bytes;
}

/* The SDL2 pilot, as the game reads it. */
bindings load_sdl2_pilot()
{
	write_file(pilot_path(sdl2_pilot, ".plr"), pilot_plr);
	write_file(pilot_path(sdl2_pilot, ".plx"), pilot_plx);
	read_pilot(sdl2_pilot);
	return bindings_of_player_config();
}

template <typename T>
void print_list(const std::string_view name, const std::vector<T> &v)
{
	std::cout << "\t    ." << name << " = {";
	for (std::size_t i{0}; i != v.size(); ++i)
		std::cout << (i ? ", " : "") << v[i];
	std::cout << "},\n";
}

void print_bindings(const bindings &b)
{
	std::cout << "bindings sdl2_bindings()\n{\n\treturn {\n\t    .control_type = "
	          << unsigned{b.control_type} << ",\n";
	print_list("keyboard", b.keyboard);
	print_list("joystick", b.joystick);
	print_list("mouse", b.mouse);
	print_list("rebirth", b.rebirth);
	print_list("joystick_sens", b.joystick_sens);
	print_list("joystick_dead", b.joystick_dead);
	print_list("joystick_linear", b.joystick_linear);
	print_list("joystick_speed", b.joystick_speed);
	std::cout << "\t};\n}\n";
}

void print_bytes(const std::string_view name, const std::span<const std::uint8_t> bytes)
{
	std::cout << "constexpr std::array<std::uint8_t, " << bytes.size() << "> " << name << "{{";
	for (std::size_t i{0}; i != bytes.size(); ++i)
		std::cout << (i % 16 == 0 ? "\n    " : " ") << "0x" << std::hex << std::setw(2)
		          << std::setfill('0') << unsigned{bytes[i]} << std::dec << ",";
	std::cout << "\n}};\n";
}

/* PhysicsFS on a scratch directory, the game's user directory. */
struct scratch_user_directory
{
	std::filesystem::path directory;
	scratch_user_directory()
	{
		directory = std::filesystem::temp_directory_path() /
		            ("dxx-pilot-file-" + std::to_string(::getpid()));
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
		BOOST_REQUIRE(PHYSFS_init(nullptr));
		BOOST_REQUIRE(PHYSFS_setWriteDir(directory.c_str()));
		BOOST_REQUIRE(PHYSFS_mount(directory.c_str(), nullptr, 1));
	}
	~scratch_user_directory()
	{
		PHYSFS_deinit();
		std::error_code ec;
		std::filesystem::remove_all(directory, ec);
	}
	scratch_user_directory(const scratch_user_directory &) = delete;
	scratch_user_directory &operator=(const scratch_user_directory &) = delete;
	scratch_user_directory(scratch_user_directory &&) = delete;
	scratch_user_directory &operator=(scratch_user_directory &&) = delete;
};

BOOST_TEST_GLOBAL_FIXTURE(scratch_user_directory);

}

/* Test that the pilot file the SDL2 build wrote loads with every binding the SDL2 build had
 * given it.  With DXX_PILOT_FILE_PRINT=1, print this build's pilot_file_sdl2.inc instead (the
 * committed one comes from the SDL2 build).
 */
BOOST_AUTO_TEST_CASE(an_sdl2_pilot_file_loads_with_every_binding)
{
	if (const char *const print{std::getenv("DXX_PILOT_FILE_PRINT")};
	    print && std::string_view{print} == "1")
	{
		use_pilot(sdl2_pilot);
		make_new_pilot();
		give_stick_bindings();
		const auto b{bindings_of_player_config()};
		write_player_file();
		print_bytes("pilot_plr", read_file(pilot_path(sdl2_pilot, ".plr")));
		print_bytes("pilot_plx", read_file(pilot_path(sdl2_pilot, ".plx")));
		print_bindings(b);
		return;
	}
	check_equal(load_sdl2_pilot(), sdl2_bindings());
}

/* Test that a new pilot made with a gamepad open gets the bindings an SDL2 build gives one,
 * axis-buttons among them: given the same stick bindings, it has the SDL2 pilot's.
 */
BOOST_AUTO_TEST_CASE(a_new_pilot_with_a_gamepad_gets_sdl2s_defaults)
{
	make_new_pilot();
	give_stick_bindings();
	check_equal(bindings_of_player_config(), sdl2_bindings());
}

/* Test that a pilot file this build writes reads back with every binding: a new pilot with a
 * gamepad open, given the stick bindings, written and read.
 */
BOOST_AUTO_TEST_CASE(a_pilot_file_written_here_reads_back_the_same)
{
	constexpr std::string_view name{"roundtrp"};
	use_pilot(name);
	make_new_pilot();
	give_stick_bindings();
	const auto want{bindings_of_player_config()};
	write_player_file();
	read_pilot(name);
	check_equal(bindings_of_player_config(), want);
}

/* The rest of the game, which playsave.cpp, kconfig.cpp and physfsx.cpp name.  The globals hold
 * a game's values at start; a function these tests should not reach throws, which fails the test.
 */
namespace {

[[noreturn]]
void unreached(const char *const what)
{
	throw std::logic_error{std::string{"pilot_file: unreached game code called: "} + what};
}

}

/* The game declares these without [[noreturn]], which a later declaration may not add. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wsuggest-attribute=noreturn"
#endif

int Newdemo_state{ND_STATE_NORMAL};

int nm_messagebox(menu_title, const nm_messagebox_tie &, const char *format, ...)
{
	unreached(format);
}

void nm_draw_background(grs_canvas &, int, int, int, int)
{
	unreached("nm_draw_background");
}

namespace dcx {

CArg CGameArg;
CCfg CGameCfg;
d_interface_unique_state InterfaceUniqueState;
pressed_keys keyd_pressed;
playernum_t Player_num;
int num_controllers;
grs_canvas *grd_curcanv;
std::unique_ptr<grs_screen> grd_curscreen;
joybutton_text_t gcbutton_text;
const gamecontroller_axis_text_array gamecontroller_axis_text{};
const std::array<key_props, 256> key_properties{};
font_x_scale_proportion FNTScaleX;
font_y_scale_proportion FNTScaleY;
fix FrameTime;
game_mode_flags Game_mode;
// NOLINTNEXTLINE(readability-identifier-naming): the game's name
per_gamefont_index_array<loaded_game_font> Gamefonts;

#if DXX_HAVE_POISON
/* The game poisons PilotName; this test's global is read only after read_pilot sets it. */
d_interface_unique_state::d_interface_unique_state() = default;
#endif

void(con_printf)(const con_priority_wrapper, const char *, ...)
{}

void(con_puts)(const con_priority_wrapper, std::span<char>)
{}

void(con_puts)(const con_priority_wrapper, std::span<const char>)
{}

void(Error)(const char *, unsigned, const char *, const char *fmt, ...)
{
	unreached(fmt);
}

void(Error_puts)(const char *, unsigned, const char *, const char *str)
{
	unreached(str);
}

fix64 timer_query()
{
	unreached("timer_query");
}

void timer_delay_ms(unsigned)
{
	unreached("timer_delay_ms");
}

void timer_delay_bound(unsigned)
{
	unreached("timer_delay_bound");
}

unsigned get_border_x(const grs_canvas &)
{
	unreached("get_border_x");
}

unsigned get_border_y(const grs_canvas &)
{
	unreached("get_border_y");
}

int event_key_get(const d_event &)
{
	unreached("event_key_get");
}

int event_key_get_raw(const d_event &)
{
	unreached("event_key_get_raw");
}

std::tuple<int, int, int> mouse_get_pos()
{
	unreached("mouse_get_pos");
}

int apply_deadzone(int, int)
{
	unreached("apply_deadzone");
}

bool joy_translate_menu_key(const d_event &)
{
	unreached("joy_translate_menu_key");
}

const d_event_joystick_axis_value &event_joystick_get_axis(const d_event &)
{
	unreached("event_joystick_get_axis");
}

int event_joystick_get_button(const d_event &)
{
	unreached("event_joystick_get_button");
}

gr_string_size gr_get_string_size(const grs_font &, const char *)
{
	unreached("gr_get_string_size");
}

color_palette_index gr_find_closest_color(int, int, int)
{
	unreached("gr_find_closest_color");
}

void(gr_set_current_canvas)(grs_canvas &)
{
	unreached("gr_set_current_canvas");
}

void(gr_set_default_canvas)()
{
	unreached("gr_set_default_canvas");
}

void gr_rect(grs_canvas &, int, int, int, int, color_palette_index)
{
	unreached("gr_rect");
}

void gr_urect(grs_canvas &, int, int, int, int, color_palette_index)
{
	unreached("gr_urect");
}

void gr_string(grs_canvas &, const grs_font &, int, int, const char *s, int, int)
{
	unreached(s);
}

void gr_string(grs_canvas &, const grs_font &, int, int, const char *s)
{
	unreached(s);
}

int nm_messagebox_str(menu_title, const nm_messagebox_tie &, menu_subtitle)
{
	unreached("nm_messagebox_str");
}

newmenu_layout::adjusted_citem
newmenu_layout::adjusted_citem::create(std::ranges::subrange<newmenu_item *>, int)
{
	unreached("adjusted_citem::create");
}

void newmenu_layout::create_structure()
{
	unreached("create_structure");
}

window_event_result newmenu::event_handler(const d_event &)
{
	unreached("newmenu::event_handler");
}

int newmenu::process_until_closed(newmenu *)
{
	unreached("newmenu::process_until_closed");
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): throws before any member is read
window::window(grs_canvas &, uint16_t, uint16_t, uint16_t, uint16_t)
{
	unreached("window::window");
}

window::~window() = default;

void window::send_creation_events()
{
	unreached("window::send_creation_events");
}

mixin_trackable_window::~mixin_trackable_window() = default;

/* The globals above are empty, so their destructors free nothing. */
grs_main_canvas::~grs_main_canvas() = default;

void gr_free_bitmap_data(grs_bitmap &)
{}

}

namespace dsx {

// NOLINTBEGIN(readability-identifier-naming): the game's names
std::array<const char *, N_TEXT_STRINGS> Text_string;
Mission_ptr Current_mission;
game_cheats cheats;
Cfg GameCfg;
netgame_info Netgame;

Mission::~Mission() = default;
LoadedMovie::~LoadedMovie() = default;

int WriteConfigFile(const CCfg &, const Cfg &)
{
	return 0;
}

void InitWeaponOrdering()
{}
// NOLINTEND(readability-identifier-naming)

int set_screen_mode(int)
{
	unreached("set_screen_mode");
}

void game_flush_inputs(control_info &)
{
	unreached("game_flush_inputs");
}

}

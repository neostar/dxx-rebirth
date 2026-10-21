/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "dxxsconf.h"

#include "physfsrwops.h"

#include <SDL.h>
#include <physfs.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#if SDL_MAJOR_VERSION == 3
#include <string_view>
#endif

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth PhysicsFS RWops
#include <boost/test/unit_test.hpp>

namespace {

struct RAIItemporary_filesystem_directory
{
	std::filesystem::path directory;
	RAIItemporary_filesystem_directory(std::string &&directory_name_prefix) :
		directory{
			std::filesystem::temp_directory_path().append(
				(directory_name_prefix.append(std::to_string(/* unique time-based value to minimize chance of accidental collision */ std::chrono::steady_clock::now().time_since_epoch().count())), std::move(directory_name_prefix))
			)
		}
	{
		/* There are three possible results from calling `create_directory`:
		 * - true: directory created
		 * - false: directory already existed
		 * - exception: error trying to create directory
		 */
		if (!std::filesystem::create_directory(directory))
			/* No exception was thrown, but the directory was not created
			 * because it already existed.  Existence is very unlikely here
			 * due to the timestamp included in the path.
			 *
			 * Throw so that the directory will not be destroyed.
			 */
			throw std::runtime_error("Failed to create temporary directory " + directory.string());
	}
	~RAIItemporary_filesystem_directory()
	{
		std::filesystem::remove_all(directory);
	}
};

struct RAIIphysfs_init
{
	RAIIphysfs_init()
	{
		const auto argv0{boost::unit_test::framework::master_test_suite().argv[0]};
		if (!PHYSFS_init(argv0))
			throw std::runtime_error(PHYSFS_getLastError());
	}
	~RAIIphysfs_init()
	{
		PHYSFS_deinit();
	}
};

struct RAIItemporary_physfs_mounted_directory : RAIItemporary_filesystem_directory
{
	RAIItemporary_physfs_mounted_directory(std::string &&directory_name_prefix) :
		RAIItemporary_filesystem_directory{std::move(directory_name_prefix)}
	{
		if (!PHYSFS_mount(directory.string().c_str(), nullptr, 1))
			throw std::runtime_error(PHYSFS_getLastError());
	}

	~RAIItemporary_physfs_mounted_directory()
	{
		PHYSFS_unmount(directory.string().c_str());
	}
};

class physfsrwops_test_fixture : RAIIphysfs_init, RAIItemporary_physfs_mounted_directory
{
	static constexpr std::array<uint8_t, 7> payload{{1, 2, 3, 4, 5, 6, 7}};
	static constexpr char filename[]{"physfsrwops-test-data"};
public:
	physfsrwops_test_fixture() :
		RAIItemporary_physfs_mounted_directory{"dxx-rebirth-physfsrwops-"}
	{
		std::string pathname{directory / filename};
		std::ofstream output{pathname, std::ios::binary};
		output.write(reinterpret_cast<const char *>(payload.data()), payload.size());
		if (!output)
			throw std::runtime_error("Failed to create test input " + pathname);
	}

	static RWops_ptr open()
	{
		auto &&[rwops, error]{PHYSFSRWOPS_openRead(filename)};
		if (!rwops)
			throw std::runtime_error(PHYSFS_getErrorByCode(error));
		return std::move(rwops);
	}
#if SDL_MAJOR_VERSION == 3

	/* PHYSFS_unmount fails while a file opened through the mount is open. */
	bool unmount() const
	{
		return PHYSFS_unmount(directory.string().c_str());
	}
#endif
};

#if SDL_MAJOR_VERSION == 3
/* A PhysicsFS archiver whose one file, `unreadable`, fails every read, for
 * the read error path: a real file read fails only on an I/O error.
 * Mounted from memory under the name `unreadable.dxxfail`, it owns that
 * memory stream and destroys it when unmounted.
 */
constexpr char unreadable_file[]{"unreadable"};

PHYSFS_sint64 unreadable_read(PHYSFS_Io *, void *, PHYSFS_uint64)
{
	PHYSFS_setErrorCode(PHYSFS_ERR_IO);
	return -1;
}

PHYSFS_sint64 unreadable_write(PHYSFS_Io *, const void *, PHYSFS_uint64)
{
	PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
	return -1;
}

int unreadable_seek(PHYSFS_Io *, const PHYSFS_uint64 offset)
{
	return offset == 0;
}

PHYSFS_sint64 unreadable_tell(PHYSFS_Io *)
{
	return 0;
}

PHYSFS_sint64 unreadable_length(PHYSFS_Io *)
{
	return 7;
}

PHYSFS_Io *unreadable_duplicate(PHYSFS_Io *const io)
{
	return io;
}

int unreadable_flush(PHYSFS_Io *)
{
	return 1;
}

void unreadable_destroy(PHYSFS_Io *)
{
}

PHYSFS_Io unreadable_io{
	.version = 0,
	.opaque = nullptr,
	.read = unreadable_read,
	.write = unreadable_write,
	.seek = unreadable_seek,
	.tell = unreadable_tell,
	.length = unreadable_length,
	.duplicate = unreadable_duplicate,
	.flush = unreadable_flush,
	.destroy = unreadable_destroy,
};

void *unreadable_open_archive(PHYSFS_Io *const io, const char *, const int for_write, int *const claimed)
{
	*claimed = 1;
	if (for_write)
	{
		PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
		return nullptr;
	}
	return io;
}

PHYSFS_EnumerateCallbackResult unreadable_enumerate(void *, const char *, PHYSFS_EnumerateCallback, const char *, void *)
{
	return PHYSFS_ENUM_OK;
}

PHYSFS_Io *unreadable_open_read(void *, const char *const name)
{
	if (name != std::string_view{unreadable_file})
	{
		PHYSFS_setErrorCode(PHYSFS_ERR_NOT_FOUND);
		return nullptr;
	}
	return &unreadable_io;
}

PHYSFS_Io *unreadable_open_write(void *, const char *)
{
	PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
	return nullptr;
}

int unreadable_modify(void *, const char *)
{
	PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
	return 0;
}

int unreadable_stat(void *, const char *const name, PHYSFS_Stat *const stat)
{
	const std::string_view n{name};
	if (!n.empty() && n != unreadable_file)
	{
		PHYSFS_setErrorCode(PHYSFS_ERR_NOT_FOUND);
		return 0;
	}
	*stat = {
		.filesize = n.empty() ? 0 : 7,
		.modtime = 0,
		.createtime = 0,
		.accesstime = 0,
		.filetype = n.empty() ? PHYSFS_FILETYPE_DIRECTORY : PHYSFS_FILETYPE_REGULAR,
		.readonly = 1,
	};
	return 1;
}

void unreadable_close_archive(void *const opaque)
{
	const auto io{static_cast<PHYSFS_Io *>(opaque)};
	io->destroy(io);
}

constexpr PHYSFS_Archiver unreadable_archiver{
	.version = 0,
	.info = {
		.extension = "dxxfail",
		.description = "Rebirth test archive whose file fails every read",
		.author = "",
		.url = "",
		.supportsSymlinks = 0,
	},
	.openArchive = unreadable_open_archive,
	.enumerate = unreadable_enumerate,
	.openRead = unreadable_open_read,
	.openWrite = unreadable_open_write,
	.openAppend = unreadable_open_write,
	.remove = unreadable_modify,
	.mkdir = unreadable_modify,
	.stat = unreadable_stat,
	.closeArchive = unreadable_close_archive,
};
#endif

}

#if SDL_MAJOR_VERSION >= 2
BOOST_FIXTURE_TEST_CASE(size_preserves_position, physfsrwops_test_fixture)
{
	auto rwops{open()};
	BOOST_REQUIRE_EQUAL(SDL_RWseek(rwops.get(), 2, RW_SEEK_SET), 2);
	BOOST_TEST(SDL_RWsize(rwops.get()) == 7);
	BOOST_TEST(SDL_RWtell(rwops.get()) == 2);
}
#endif

#if SDL_MAJOR_VERSION <= 2
BOOST_FIXTURE_TEST_CASE(read_returns_complete_object_count, physfsrwops_test_fixture)
{
	auto rwops{open()};
	std::array<uint8_t, 6> buffer{};
	constexpr std::array<uint8_t, 6> expected{{1, 2, 3, 4, 5, 6}};
	BOOST_TEST(SDL_RWread(rwops.get(), buffer.data(), 2, 3) == 3u);
	BOOST_TEST(buffer == expected);
	BOOST_TEST(SDL_RWtell(rwops.get()) == 6);
}

BOOST_FIXTURE_TEST_CASE(read_does_not_count_partial_object, physfsrwops_test_fixture)
{
	auto rwops{open()};
	std::array<uint8_t, 8> buffer{};
	BOOST_TEST(SDL_RWread(rwops.get(), buffer.data(), 4, 2) == 1u);
	BOOST_TEST(buffer[6] == 7u);
	BOOST_TEST(SDL_RWtell(rwops.get()) == 7);
}

BOOST_FIXTURE_TEST_CASE(zero_length_read_does_not_move_position, physfsrwops_test_fixture)
{
	auto rwops{open()};
	uint8_t buffer{};
	BOOST_TEST(SDL_RWread(rwops.get(), &buffer, 0, 1) == 0u);
	BOOST_TEST(SDL_RWread(rwops.get(), &buffer, 1, 0) == 0u);
	BOOST_TEST(SDL_RWtell(rwops.get()) == 0);
}
#elif SDL_MAJOR_VERSION == 3
/* SDL3's SDL_ReadIO and SDL_WriteIO count bytes, where SDL2's SDL_RWread
 * and SDL_RWwrite count objects, and report end of file and errors through
 * SDL_GetIOStatus.
 */
BOOST_FIXTURE_TEST_CASE(read_returns_requested_bytes, physfsrwops_test_fixture)
{
	auto rwops{open()};
	std::array<uint8_t, 6> buffer{};
	constexpr std::array<uint8_t, 6> expected{{1, 2, 3, 4, 5, 6}};
	BOOST_TEST(SDL_ReadIO(rwops.get(), buffer.data(), buffer.size()) == 6u);
	BOOST_TEST(buffer == expected);
	BOOST_TEST(SDL_TellIO(rwops.get()) == 6);
}

BOOST_FIXTURE_TEST_CASE(read_past_end_reports_end_of_file, physfsrwops_test_fixture)
{
	auto rwops{open()};
	std::array<uint8_t, 8> buffer{};
	BOOST_TEST(SDL_ReadIO(rwops.get(), buffer.data(), buffer.size()) == 7u);
	BOOST_TEST(buffer[6] == 7u);
	BOOST_TEST(SDL_GetIOStatus(rwops.get()) == SDL_IO_STATUS_EOF);
	BOOST_TEST(SDL_TellIO(rwops.get()) == 7);
}

/* Test that a seek from the end lands relative to the file's length. */
BOOST_FIXTURE_TEST_CASE(seek_from_end, physfsrwops_test_fixture)
{
	auto rwops{open()};
	uint8_t byte{};
	BOOST_TEST(SDL_SeekIO(rwops.get(), -2, SDL_IO_SEEK_END) == 5);
	BOOST_TEST(SDL_ReadIO(rwops.get(), &byte, 1) == 1u);
	BOOST_TEST(byte == 6u);
}

/* Test that a failing PhysicsFS read reports an error, with its message
 * through SDL_GetError, and not end of file.
 */
BOOST_FIXTURE_TEST_CASE(read_error_sets_error, physfsrwops_test_fixture)
{
	constexpr std::array<uint8_t, 1> archive{};
	BOOST_REQUIRE(PHYSFS_registerArchiver(&unreadable_archiver));
	BOOST_REQUIRE(PHYSFS_mountMemory(archive.data(), archive.size(), nullptr, "unreadable.dxxfail", nullptr, 1));
	auto &&[rwops, error]{PHYSFSRWOPS_openRead(unreadable_file)};
	BOOST_REQUIRE(rwops);
	std::array<uint8_t, 4> buffer{};
	BOOST_TEST(SDL_ReadIO(rwops.get(), buffer.data(), buffer.size()) == 0u);
	BOOST_TEST(SDL_GetIOStatus(rwops.get()) == SDL_IO_STATUS_ERROR);
	BOOST_TEST(std::string_view{SDL_GetError()}.starts_with("PhysicsFS error: "));
}
#endif

#if SDL_MAJOR_VERSION == 2
BOOST_FIXTURE_TEST_CASE(read_rejects_size_product_overflow, physfsrwops_test_fixture)
{
	auto rwops{open()};
	uint8_t buffer{};
	SDL_ClearError();
	constexpr auto maximum{std::numeric_limits<std::size_t>::max()};
	BOOST_TEST(SDL_RWread(rwops.get(), &buffer, maximum, maximum) == 0u);
	BOOST_TEST(SDL_GetError()[0] != '\0');
	BOOST_TEST(SDL_RWtell(rwops.get()) == 0);
}
#elif SDL_MAJOR_VERSION == 3
BOOST_FIXTURE_TEST_CASE(read_rejects_oversized_request, physfsrwops_test_fixture)
{
	auto rwops{open()};
	uint8_t buffer{};
	SDL_ClearError();
	BOOST_TEST(SDL_ReadIO(rwops.get(), &buffer, std::numeric_limits<std::size_t>::max()) == 0u);
	BOOST_TEST(SDL_GetError()[0] != '\0');
	BOOST_TEST(SDL_GetIOStatus(rwops.get()) == SDL_IO_STATUS_ERROR);
	BOOST_TEST(SDL_TellIO(rwops.get()) == 0);
}
#endif

#if SDL_MAJOR_VERSION <= 2
BOOST_FIXTURE_TEST_CASE(write_error_returns_zero_objects, physfsrwops_test_fixture)
{
	auto rwops{open()};
	std::array<uint8_t, 6> buffer{};
	SDL_ClearError();
	BOOST_TEST(SDL_RWwrite(rwops.get(), buffer.data(), 2, 3) == 0u);
	BOOST_TEST(SDL_GetError()[0] != '\0');
}
#elif SDL_MAJOR_VERSION == 3
BOOST_FIXTURE_TEST_CASE(write_error_returns_zero_bytes, physfsrwops_test_fixture)
{
	auto rwops{open()};
	std::array<uint8_t, 6> buffer{};
	SDL_ClearError();
	BOOST_TEST(SDL_WriteIO(rwops.get(), buffer.data(), buffer.size()) == 0u);
	BOOST_TEST(SDL_GetError()[0] != '\0');
}

/* Test that SDL_CloseIO closes the PhysicsFS file: the mount cannot be
 * unmounted while it is open.  PHYSFS_deinit would close a leaked one
 * unseen.
 */
BOOST_FIXTURE_TEST_CASE(close_closes_file, physfsrwops_test_fixture)
{
	auto rwops{open()};
	BOOST_TEST(!unmount());
	BOOST_TEST(SDL_CloseIO(rwops.release()));
	BOOST_TEST(unmount());
}
#endif

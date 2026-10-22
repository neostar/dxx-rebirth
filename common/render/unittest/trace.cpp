/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/trace.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth trace
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;
namespace trace = dcx::render::trace;

namespace {

using bytes = std::vector<std::uint8_t>;

/* The render target of the traces written here, and the header of a
 * version 1 trace for it.
 */
constexpr render::extent target{.width = 640, .height = 480};
const bytes header{'D', 'X', 'X',  'R', 'T', 'R', '\r', '\n', 1, 0,
                   0,   0,   0x80, 2,   0,   0,   0xe0, 1,    0, 0};

bytes operator+(bytes a, const bytes &b)
{
	a.insert(a.end(), b.begin(), b.end());
	return a;
}

}

/* Test that a new writer holds the header alone. */
BOOST_AUTO_TEST_CASE(empty_trace_is_header)
{
	trace::writer w{target};
	const auto out{w.take()};
	BOOST_TEST(out == header);
}

/* Test that the reader gives the render target's size from the header,
 * with or without records after it.
 */
BOOST_AUTO_TEST_CASE(header_round_trip)
{
	trace::writer w{{.width = 1, .height = 65536}};
	auto out{w.take()};
	trace::reader alone{out};
	const auto h{alone.header()};
	BOOST_REQUIRE(h.has_value());
	BOOST_TEST((h.value() == trace::header{.target = {.width = 1, .height = 65536}}));
	const auto with_records{header + bytes{1, 1, 0, 0, 0, 3}};
	trace::reader rd{with_records};
	BOOST_TEST(rd.next().has_value());
	const auto again{rd.header()};
	BOOST_REQUIRE(again.has_value());
	BOOST_TEST((again.value() == trace::header{.target = target}));
}

/* Test that the writer refuses a render target with no pixels, which the
 * reader would reject.
 */
BOOST_AUTO_TEST_CASE(empty_target_not_written)
{
	BOOST_CHECK_THROW((trace::writer{{.width = 0, .height = 1}}), std::invalid_argument);
	BOOST_CHECK_THROW((trace::writer{{.width = 1, .height = 0}}), std::invalid_argument);
}

/* Test that clears and presents are written as their documented bytes, the
 * frame boundary among them.
 */
BOOST_AUTO_TEST_CASE(clear_and_present_bytes)
{
	trace::writer w{target};
	w.clear({.color = false, .depth = true});
	w.clear({.color = true, .depth = false});
	w.present({.frame = 0x01020304});
	const auto out{w.take()};
	/* kind 1 clear (flags: bit 0 colour, bit 1 depth), kind 2 present
	 * (u32 frame).
	 */
	BOOST_TEST(
	    (out == header + bytes{1, 1, 0, 0, 0, 2, 1, 1, 0, 0, 0, 1, 2, 4, 0, 0, 0, 4, 3, 2, 1}));
}

/* Test that the writer refuses a clear of nothing and a texture call for
 * slot none, which the reader would reject, leaving the trace as it was.
 */
BOOST_AUTO_TEST_CASE(empty_clear_and_slot_none_not_written)
{
	const std::vector<std::uint8_t> rgba{0, 0, 0, 255};
	const render::texture_image image{{.size = {.width = 1, .height = 1},
	                                   .format = render::texture_format::rgba8,
	                                   .filter = render::filter::nearest,
	                                   .anisotropic = false,
	                                   .has_transparency = false},
	                                  rgba};
	trace::writer w{target};
	BOOST_CHECK_THROW(w.clear({.color = false, .depth = false}), std::invalid_argument);
	BOOST_CHECK_THROW(w.texture(render::texture_slot::none, &image), std::invalid_argument);
	BOOST_CHECK_THROW(w.texture(render::texture_slot::none, nullptr), std::invalid_argument);
	BOOST_TEST(w.take() == header);
}

/* Test that take() hands over the bytes so far and the writer goes on
 * after them.
 */
BOOST_AUTO_TEST_CASE(take_hands_over_pieces)
{
	trace::writer w{target};
	w.present({.frame = 0});
	const auto first{w.take()};
	w.clear({.color = true, .depth = true});
	const auto second{w.take()};
	BOOST_TEST((first == header + bytes{2, 4, 0, 0, 0, 0, 0, 0, 0}));
	BOOST_TEST(second == (bytes{1, 1, 0, 0, 0, 3}));
}

/* Test that the reader gives back the records the writer was given, in
 * order, and an empty trace as no records.
 */
BOOST_AUTO_TEST_CASE(clear_and_present_round_trip)
{
	trace::writer w{target};
	const auto empty{trace::read_all(w.take())};
	BOOST_REQUIRE(empty.has_value());
	BOOST_TEST(empty.value().empty());
	w.clear({.color = false, .depth = true});
	w.clear({.color = true, .depth = true});
	w.present({.frame = 30});
	w.present({.frame = 0xffffffff});
	const auto records{trace::read_all(header + w.take())};
	BOOST_REQUIRE(records.has_value());
	const std::vector<trace::record> expected{
	    trace::clear{.color = false, .depth = true},
	    trace::clear{.color = true, .depth = true},
	    trace::present{.frame = 30},
	    trace::present{.frame = 0xffffffff},
	};
	BOOST_TEST((records.value() == expected));
}

/* Test that a texture call is recorded with its pixels, and a release with
 * none, and both read back as the same records.
 */
BOOST_AUTO_TEST_CASE(texture_round_trip)
{
	const std::vector<std::uint8_t> rgba{1, 2, 3, 4, 5, 6, 7, 255};
	const render::texture_desc desc{
	    .size = {.width = 2, .height = 1},
	    .format = render::texture_format::luminance4_alpha4,
	    .filter = render::filter::trilinear,
	    .anisotropic = true,
	    .has_transparency = true,
	};
	const render::texture_image image{desc, rgba};
	trace::writer w{target};
	w.texture({7}, &image);
	w.texture({7}, nullptr);
	const auto out{w.take()};
	/* kind 3, length 5: slot 7, no image. */
	BOOST_TEST((bytes(out.end() - 10, out.end()) == bytes{3, 5, 0, 0, 0, 7, 0, 0, 0, 0}));
	const auto records{trace::read_all(out)};
	BOOST_REQUIRE(records.has_value());
	const std::vector<trace::record> expected{
	    trace::texture{.slot = {7}, .desc = desc, .rgba = rgba},
	    trace::texture{.slot = {7}, .desc = std::nullopt, .rgba = {}},
	};
	BOOST_TEST((records.value() == expected));
	const auto read_image{std::get<trace::texture>(records.value().front()).image()};
	BOOST_REQUIRE(read_image.has_value());
	BOOST_TEST((read_image.value() == image));
	BOOST_TEST(!std::get<trace::texture>(records.value().back()).image().has_value());
}

/* Test that the writer refuses a texture over the size bound, which the
 * reader would reject.
 */
BOOST_AUTO_TEST_CASE(texture_over_bound_not_written)
{
	const std::vector<std::uint8_t> rgba(std::size_t{4} * (trace::max_texture_side + 1));
	const render::texture_image image{{.size = {.width = trace::max_texture_side + 1, .height = 1},
	                                   .format = render::texture_format::rgba8,
	                                   .filter = render::filter::nearest,
	                                   .anisotropic = false,
	                                   .has_transparency = false},
	                                  rgba};
	trace::writer w{target};
	BOOST_CHECK_THROW(w.texture({1}, &image), std::length_error);
	BOOST_TEST(w.take() == header);
}

namespace {

/* A HUD-like draw: ortho, identity, untextured, no vertices. */
const render::draw_call hud_draw{
    .topology = render::topology::line_loop,
    .vertices = {},
    .texture = render::texture_slot::none,
    .state = {.blend = render::blend::normal,
	          .depth = render::depth::off,
	          .cull = render::cull::none,
	          .alpha_ref = 0.5f,
	          .wrap = render::wrap::clamp,
	          .line_width = 2.f},
    .viewport = {.x = 0, .y = 0, .size = {.width = 640, .height = 480}},
    .transforms = {.projection =
	                   render::ortho{
                           .left = 0, .right = 1, .bottom = 0, .top = 1, .z_near = -1, .z_far = 1},
	               .modelview = render::identity{}},
};

/* A 3D draw: perspective with stereo offset, translate and scale,
 * textured, its colours outside [0, 1] as the front end may send them.
 */
const std::vector<render::vertex> fan{
    {.pos = {{-1.f, -0.f, 2.5f}}, .rgba = {{1.5f, -0.25f, 0.f, 1.f}}, .uv = {{0.f, 0.f}}},
    {.pos = {{1.f, 0.f, 2.5f}}, .rgba = {{1.f, 1.f, 1.f, 0.8f}}, .uv = {{1.f, 0.f}}},
    {.pos = {{1.f, 1.f, 2.5f}}, .rgba = {{0.f, 0.f, 1.f, 1.f}}, .uv = {{1.f, 1.f}}},
};
const render::draw_call world_draw{
    .topology = render::topology::fan,
    .vertices = fan,
    .texture = {42},
    .state = {.blend = render::blend::additive_c,
	          .depth = render::depth::test_and_write,
	          .cull = render::cull::front,
	          .alpha_ref = 0.8f,
	          .wrap = render::wrap::repeat,
	          .line_width = 1.f},
    .viewport = {.x = -16, .y = 8, .size = {.width = 320, .height = 200}},
    .transforms = {.projection = render::perspective{.fovy = 90,
	                                                 .aspect = 1.6,
	                                                 .z_near = 0.01,
	                                                 .z_far = 6400,
	                                                 .stereo_dx = -0.0625f},
	               .modelview = render::translate_scale{.translate = {{0.f, 0.f, -1.f}},
	                                                    .scale = {{1.f, 1.f, -1.f}}}},
};

/* Two frames using every record kind, with a mid-frame depth clear. */
bytes two_frames()
{
	const std::vector<std::uint8_t> rgba{255, 0, 0, 255};
	const render::texture_image image{{.size = {.width = 1, .height = 1},
	                                   .format = render::texture_format::rgba8,
	                                   .filter = render::filter::nearest,
	                                   .anisotropic = false,
	                                   .has_transparency = false},
	                                  rgba};
	trace::writer w{target};
	w.clear({.color = true, .depth = true});
	w.texture({42}, &image);
	w.draw(world_draw);
	w.clear({.color = false, .depth = true});
	w.draw(world_draw);
	w.draw(hud_draw);
	w.present({.frame = 60});
	w.texture({42}, nullptr);
	w.draw(hud_draw);
	w.present({.frame = 61});
	return w.take();
}

}

/* Test that draws read back with every field and vertex they were written
 * with, and give back the same calls.
 */
BOOST_AUTO_TEST_CASE(draw_round_trip)
{
	trace::writer w{target};
	(void)w.take();
	w.draw(hud_draw);
	w.draw(world_draw);
	const auto out{w.take()};
	/* alpha_ref 0.5 as little-endian IEEE bits, after kind, length,
	 * topology, slot, blend, depth and cull.
	 */
	BOOST_TEST((bytes(out.begin() + 13, out.begin() + 17) == bytes{0, 0, 0, 0x3f}));
	const auto records{trace::read_all(header + out)};
	BOOST_REQUIRE(records.has_value());
	BOOST_REQUIRE(records.value().size() == 2u);
	BOOST_TEST((std::get<trace::draw>(records.value().front()).call() == hud_draw));
	const auto &world{std::get<trace::draw>(records.value().back())};
	BOOST_TEST((world.vertices == fan));
	BOOST_TEST((world.call() == world_draw));
}

/* Test that a double is written as its IEEE bits, least significant byte
 * first across all eight, not as two 32-bit halves in either order.
 */
BOOST_AUTO_TEST_CASE(double_bytes)
{
	render::draw_call call{world_draw};
	call.transforms.projection =
	    render::perspective{.fovy = std::bit_cast<double>(std::uint64_t{0x0102030405060708u}),
		                    .aspect = 1,
		                    .z_near = 1,
		                    .z_far = 2,
		                    .stereo_dx = 0};
	trace::writer w{target};
	(void)w.take();
	w.draw(call);
	const auto out{w.take()};
	/* fovy follows the projection tag at byte 38 of the record. */
	BOOST_TEST((bytes(out.begin() + 39, out.begin() + 47) == bytes{8, 7, 6, 5, 4, 3, 2, 1}));
	const auto records{trace::read_all(header + out)};
	BOOST_REQUIRE(records.has_value());
	BOOST_TEST((std::get<trace::draw>(records.value().front()).call() == call));
}

/* Test that NaNs other than the canonical one, quiet and signalling, keep
 * their sign and payload bits through a write and a read.
 */
BOOST_AUTO_TEST_CASE(nan_payload_round_trip)
{
	const auto quiet{std::bit_cast<float>(std::uint32_t{0x7fc12345u})};
	const auto signalling{std::bit_cast<float>(std::uint32_t{0xffa00001u})};
	const std::vector<render::vertex> nans{
	    {.pos = {{quiet, signalling, 0.f}}, .rgba = {{1.f, 1.f, 1.f, 1.f}}, .uv = {{0.f, 0.f}}}};
	render::draw_call call{world_draw};
	call.vertices = nans;
	call.state.alpha_ref = signalling;
	trace::writer w{target};
	w.draw(call);
	const auto records{trace::read_all(w.take())};
	BOOST_REQUIRE(records.has_value());
	const auto &d{std::get<trace::draw>(records.value().front())};
	BOOST_REQUIRE(d.vertices.size() == 1u);
	BOOST_TEST(std::bit_cast<std::uint32_t>(d.state.alpha_ref) == 0xffa00001u);
	BOOST_TEST(std::bit_cast<std::uint32_t>(d.vertices.front().pos[0]) == 0x7fc12345u);
	BOOST_TEST(std::bit_cast<std::uint32_t>(d.vertices.front().pos[1]) == 0xffa00001u);
}

/* Test that the writer refuses a draw of more vertices than the reader
 * takes, leaving the trace as it was, and writes one of max_draw_vertices.
 */
BOOST_AUTO_TEST_CASE(draw_over_bound_not_written)
{
	const std::vector<render::vertex> many(std::size_t{trace::max_draw_vertices} + 1, fan.front());
	render::draw_call call{world_draw};
	call.vertices = many;
	trace::writer w{target};
	BOOST_CHECK_THROW(w.draw(call), std::length_error);
	BOOST_TEST(w.take() == header);
	call.vertices = std::span{many}.first(trace::max_draw_vertices);
	w.draw(call);
	const auto records{trace::read_all(header + w.take())};
	BOOST_REQUIRE(records.has_value());
	BOOST_TEST(std::get<trace::draw>(records.value().front()).vertices.size() ==
	           trace::max_draw_vertices);
}

/* Test that a trace of frames keeps its frame boundaries and clears in
 * order, and that writing its records back gives the same bytes.
 */
BOOST_AUTO_TEST_CASE(frames_round_trip)
{
	const auto out{two_frames()};
	const auto records{trace::read_all(out)};
	BOOST_REQUIRE(records.has_value());
	BOOST_TEST(records.value().size() == 10u);
	const auto presents{std::ranges::count_if(records.value(), [](const trace::record &r) {
		return std::holds_alternative<trace::present>(r);
	})};
	BOOST_TEST(presents == 2);
	BOOST_TEST((records.value().back() == trace::record{trace::present{.frame = 61}}));
	BOOST_TEST(
	    (records.value().at(3) == trace::record{trace::clear{.color = false, .depth = true}}));
	trace::writer again{target};
	for (const auto &r : records.value())
		again.write(r);
	BOOST_TEST((again.take() == out));
}

/* Test that a trace with any one bit flipped is read without undefined
 * behaviour (the asan profile checks it), and that whatever it accepts it
 * writes back as the same bytes: no two traces read as the same records.
 */
BOOST_AUTO_TEST_CASE(every_bit_flip_read_safely)
{
	const auto good{two_frames()};
	std::size_t accepted{0};
	for (std::size_t i{0}; i < good.size(); ++i)
		for (unsigned bit{0}; bit < 8; ++bit)
		{
			auto bad{good};
			bad.at(i) = static_cast<std::uint8_t>(bad.at(i) ^ (1u << bit));
			const auto records{trace::read_all(bad)};
			if (!records)
				continue;
			++accepted;
			trace::reader rd{bad};
			const auto h{rd.header()};
			BOOST_REQUIRE(h.has_value());
			trace::writer w{h.value().target};
			for (const auto &r : records.value())
				w.write(r);
			BOOST_TEST_CONTEXT("byte " << i << " bit " << bit)
			{
				BOOST_TEST((w.take() == bad));
			}
		}
	/* Flips in the target size, float fields and pixels are still valid
	 * traces.
	 */
	BOOST_TEST(accepted > 0u);
}

namespace {

/* hud_draw's record with the byte at `at` (counted from the record's
 * kind) set to value.
 */
bytes draw_record(const std::size_t at, const std::uint8_t value)
{
	trace::writer w{target};
	(void)w.take();
	w.draw(hud_draw);
	auto out{w.take()};
	out.at(at) = value;
	return out;
}

/* A texture record for slot 1 with the given side, format byte, flag byte
 * and number of pixel bytes; its length field is right.
 */
bytes texture_record(const std::uint8_t side, const std::uint8_t format, const std::uint8_t flag,
                     const std::size_t pixel_bytes)
{
	const auto length{static_cast<std::uint8_t>(17 + pixel_bytes)};
	return bytes{3, length, 0, 0, 0, 1, 0, 0, 0, 1, side, 0, 0, 0, 1, 0, 0, 0, format, 0, flag, 0} +
	       bytes(pixel_bytes);
}

/* Bytes b with the byte at `at` set to value. */
bytes with(bytes b, const std::size_t at, const std::uint8_t value)
{
	b.at(at) = value;
	return b;
}

/* One malformed trace and the error it must be rejected with; a few
 * well-formed ones (no error) show that each rejection is for its one flaw.
 */
struct malformed
{
	std::string_view name;
	bytes trace;
	std::optional<trace::read_error> error;
};

/* The malformed corpus: synthetic traces, each broken in one way. */
const std::vector<malformed> corpus{
    {"empty file", {}, trace::read_error::truncated},
    {"magic cut short", {'D', 'X', 'X'}, trace::read_error::truncated},
    {"header cut short",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n', 1},
	 trace::read_error::truncated},
    {"wrong magic",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\n', '\n', 1, 0, 0, 0},
	 trace::read_error::bad_magic},
    {"version 2",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n', 2, 0, 0, 0},
	 trace::read_error::unsupported_version},
    {"version big-endian",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n', 0, 1, 0, 0},
	 trace::read_error::unsupported_version},
    {"reserved not zero",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n', 1, 0, 1, 0},
	 trace::read_error::unsupported_version},
    {"header without target",
	 {'D', 'X', 'X', 'R', 'T', 'R', '\r', '\n', 1, 0, 0, 0, 0x80, 2, 0, 0, 0xe0, 1, 0},
	 trace::read_error::truncated},
    {"target width 0", with(with(header, 12, 0), 13, 0), trace::read_error::bad_value},
    {"target height 0", with(with(header, 16, 0), 17, 0), trace::read_error::bad_value},
    {"record head cut short", header + bytes{2, 0, 0}, trace::read_error::truncated},
    {"payload past the end", header + bytes{1, 1, 0, 0, 0}, trace::read_error::truncated},
    {"length near 4 GiB", header + bytes{1, 0xff, 0xff, 0xff, 0xff, 3},
	 trace::read_error::truncated},
    {"unknown kind 0", header + bytes{0, 0, 0, 0, 0}, trace::read_error::unknown_kind},
    {"unknown kind 255", header + bytes{255, 0, 0, 0, 0}, trace::read_error::unknown_kind},
    {"clear without flags", header + bytes{1, 0, 0, 0, 0}, trace::read_error::bad_length},
    {"clear with a spare byte", header + bytes{1, 2, 0, 0, 0, 3, 0}, trace::read_error::bad_length},
    {"present without frame", header + bytes{2, 0, 0, 0, 0}, trace::read_error::bad_length},
    {"present frame cut short", header + bytes{2, 3, 0, 0, 0, 1, 0, 0},
	 trace::read_error::bad_length},
    {"present with a spare byte", header + bytes{2, 5, 0, 0, 0, 1, 0, 0, 0, 0},
	 trace::read_error::bad_length},
    {"clear of nothing", header + bytes{1, 1, 0, 0, 0, 0}, trace::read_error::bad_value},
    {"texture for slot none", header + with(texture_record(1, 0, 0, 4), 5, 0),
	 trace::read_error::bad_value},
    {"release of slot none", header + bytes{3, 5, 0, 0, 0, 0, 0, 0, 0, 0},
	 trace::read_error::bad_value},
    {"clear flag bit 2", header + bytes{1, 1, 0, 0, 0, 4}, trace::read_error::bad_value},
    {"texture with one pixel", header + texture_record(1, 0, 0, 4), std::nullopt},
    {"texture pixels cut short", header + texture_record(1, 0, 0, 3),
	 trace::read_error::bad_length},
    {"texture pixels to spare", header + texture_record(1, 0, 0, 5), trace::read_error::bad_length},
    {"texture format 5", header + texture_record(1, 5, 0, 4), trace::read_error::bad_value},
    {"texture flag 2", header + texture_record(1, 0, 2, 4), trace::read_error::bad_value},
    {"texture image flag 2", header + bytes{3, 5, 0, 0, 0, 1, 0, 0, 0, 2},
	 trace::read_error::bad_value},
    {"texture side 4097",
	 header + bytes{3, 17, 0, 0, 0, 1, 0, 0, 0, 1, 1, 16, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
	 trace::read_error::bad_value},
    {"texture filter 2", header + with(texture_record(1, 0, 0, 4), 19, 2),
	 trace::read_error::bad_value},
    {"texture has_transparency 2", header + with(texture_record(1, 0, 0, 4), 21, 2),
	 trace::read_error::bad_value},
    {"texture height 4097",
	 header + bytes{3, 17, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 16, 0, 0, 0, 0, 0, 0},
	 trace::read_error::bad_value},
    {"texture side 2^32 - 1", header + bytes{3,   17,  0,   0,   0,   1,   0,   0, 0, 1, 255,
	                                         255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0},
	 trace::read_error::bad_value},
    {"draw unchanged", header + draw_record(5, 4), std::nullopt},
    {"draw topology 6", header + draw_record(5, 6), trace::read_error::bad_value},
    {"draw blend 5", header + draw_record(10, 5), trace::read_error::bad_value},
    {"draw depth 2", header + draw_record(11, 2), trace::read_error::bad_value},
    {"draw cull 2", header + draw_record(12, 2), trace::read_error::bad_value},
    {"draw wrap 2", header + draw_record(17, 2), trace::read_error::bad_value},
    {"draw projection 2", header + draw_record(38, 2), trace::read_error::bad_value},
    {"draw modelview 2", header + draw_record(87, 2), trace::read_error::bad_value},
    {"draw vertex missing", header + draw_record(88, 1), trace::read_error::bad_length},
    {"draw 2^24 vertices", header + draw_record(91, 1), trace::read_error::bad_value},
    {"draw 2^20 + 1 vertices", header + with(draw_record(90, 16), 88, 1),
	 trace::read_error::bad_value},
    {"draw 2^32 - 1 vertices",
	 header + with(with(with(draw_record(88, 255), 89, 255), 90, 255), 91, 255),
	 trace::read_error::bad_value},
    {"good record, then garbage", header + bytes{2, 4, 0, 0, 0, 1, 0, 0, 0, 9},
	 trace::read_error::truncated},
};

}

/* Test that each trace of the malformed corpus is rejected with its error,
 * and that the reader keeps that error once found.
 */
BOOST_AUTO_TEST_CASE(malformed_corpus_rejected)
{
	for (const auto &m : corpus)
	{
		BOOST_TEST_CONTEXT(m.name)
		{
			const auto r{trace::read_all(m.trace)};
			if (!m.error)
			{
				BOOST_TEST(r.has_value());
				continue;
			}
			/* Not BOOST_REQUIRE: one accepted trace must not hide the
			 * cases after it.
			 */
			BOOST_TEST(!r.has_value());
			if (r.has_value())
				continue;
			BOOST_TEST((r.error() == m.error));
			trace::reader rd{m.trace};
			auto next{rd.next()};
			while (next.has_value() && next.value().has_value())
				next = rd.next();
			const auto again{rd.next()};
			BOOST_REQUIRE(!again.has_value());
			BOOST_TEST((again.error() == m.error));
		}
	}
}

namespace {

/* Where each record of well-formed trace t ends, found from the record
 * lengths alone, after the end of the header.
 */
std::vector<std::size_t> record_ends(const bytes &t)
{
	std::vector<std::size_t> ends{header.size()};
	for (std::size_t at{header.size()}; at < t.size();)
	{
		std::uint32_t length{0};
		for (std::size_t i{0}; i < 4; ++i)
			length |= std::uint32_t{t.at(at + 1 + i)} << (8 * i);
		at += 5 + length;
		ends.push_back(at);
	}
	return ends;
}

}

/* Test that every proper prefix of a good trace is rejected as truncated,
 * and every prefix that ends at a record boundary reads, for a trace of
 * clears and a present and for two frames of every record kind.
 */
BOOST_AUTO_TEST_CASE(every_cut_is_truncated_or_whole)
{
	trace::writer w{target};
	w.clear({.color = true, .depth = true});
	w.present({.frame = 1});
	for (const auto &good : {w.take(), two_frames()})
	{
		const auto boundaries{record_ends(good)};
		BOOST_TEST(boundaries.back() == good.size());
		for (std::size_t n{0}; n <= good.size(); ++n)
		{
			BOOST_TEST_CONTEXT("first " << n << " of " << good.size() << " bytes")
			{
				const auto r{trace::read_all(std::span{good}.first(n))};
				if (std::ranges::find(boundaries, n) != boundaries.end())
					BOOST_TEST(r.has_value());
				else
					BOOST_TEST((!r.has_value() && r.error() == trace::read_error::truncated));
			}
		}
	}
}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/recording_device.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "common/render/ambient.h"
#include "common/render/device.h"
#include "common/render/frame_setup.h"
#include "common/render/render_device.h"
#include "common/render/trace.h"

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Rebirth recording device
#include <boost/test/unit_test.hpp>

namespace render = dcx::render;
namespace trace = dcx::render::trace;

namespace {

/* The fake device under the recorder: it counts what reaches it. */
class fake_device final : public render::device
{
  public:
	unsigned draws{0};
	unsigned clears{0};
	unsigned presents{0};
	unsigned textures{0};
	explicit fake_device(const render::caps &c = {}) : device{c}
	{}
	void draw(const render::draw_call &) override
	{
		++draws;
	}
	void texture(render::texture_slot, const render::texture_image *) override
	{
		++textures;
	}
	void clear(render::clear_targets) override
	{
		++clears;
	}
	void present() override
	{
		++presents;
	}
	void read_pixels(render::read_source, const render::read_rect &, render::pixel_format,
	                 std::span<std::uint8_t>) override
	{}
};

constexpr render::extent target{.width = 64, .height = 32};

/* A recorder around inner whose trace collects in bytes. */
struct recording
{
	std::vector<std::uint8_t> bytes;
	render::recording_device recorder;
	recording(render::device &inner, const render::frame_range range)
	    : recorder{inner, range, [] { return target; },
		           [this](const std::span<const std::uint8_t> b) {
		               bytes.insert(bytes.end(), b.begin(), b.end());
	               }}
	{}
	[[nodiscard]]
	std::vector<trace::record> records() const
	{
		auto r{trace::read_all(bytes)};
		BOOST_REQUIRE(r.has_value());
		return r.value();
	}
};

/* One draw whose vertex's x is the frame number, so a trace's draw names
 * its frame.
 */
void draw_frame(render::device &d, const float frame)
{
	const std::array<render::vertex, 1> v{
	    {{.pos = {{frame, 0, 0}}, .rgba = {{1, 1, 1, 1}}, .uv = {{0, 0}}}}};
	d.draw({.topology = render::topology::points,
	        .vertices = v,
	        .texture = render::texture_slot::none,
	        .state = {},
	        .viewport = {.x = 0, .y = 0, .size = target},
	        .transforms = {.projection = render::ortho{}, .modelview = render::identity{}}});
}

/* Plays frames 0 to count - 1: a colour clear, a draw, a present. */
void play(render::device &d, const unsigned count)
{
	for (unsigned f{0}; f != count; ++f)
	{
		d.clear({.color = true, .depth = false});
		draw_frame(d, static_cast<float>(f));
		d.present();
	}
}

/* The frame numbers of a trace's draws, by their vertex, and of its
 * presents.
 */
std::vector<float> drawn_frames(const std::vector<trace::record> &records)
{
	std::vector<float> frames;
	for (const auto &r : records)
		if (const auto *const d{std::get_if<trace::draw>(&r)})
			frames.push_back(d->vertices.at(0).pos[0]);
	return frames;
}

std::vector<std::uint32_t> presented_frames(const std::vector<trace::record> &records)
{
	std::vector<std::uint32_t> frames;
	for (const auto &r : records)
		if (const auto *const p{std::get_if<trace::present>(&r)})
			frames.push_back(p->frame);
	return frames;
}

}

/* Test that a recorder writes the frames of its range alone, each ending
 * in a present of its game index, and passes every frame on.
 */
BOOST_AUTO_TEST_CASE(records_the_frames_in_range)
{
	fake_device inner;
	recording rec{inner, {.first = 1, .last = 2}};
	play(rec.recorder, 4);
	BOOST_TEST(inner.draws == 4u);
	BOOST_TEST(inner.presents == 4u);
	BOOST_TEST(inner.clears == 4u);
	const auto records{rec.records()};
	BOOST_TEST(drawn_frames(records) == (std::vector<float>{1.f, 2.f}),
	           boost::test_tools::per_element());
	BOOST_TEST(presented_frames(records) == (std::vector<std::uint32_t>{1, 2}),
	           boost::test_tools::per_element());
	BOOST_TEST(rec.recorder.finished());
	trace::reader r{rec.bytes};
	const auto h{r.header()};
	BOOST_REQUIRE(h.has_value());
	BOOST_TEST((h.value().target == target));
}

/* Test that a trace starts with the image of every slot given before its
 * range, its pixels as they were, and without a slot released before it, so
 * the trace replays without the frames before it.
 */
BOOST_AUTO_TEST_CASE(textures_given_before_the_range_lead_the_trace)
{
	fake_device inner;
	recording rec{inner, {.first = 1, .last = 1}};
	std::vector<std::uint8_t> pixels{1, 2, 3, 4, 5, 6, 7, 8};
	const render::texture_image image{{.size = {.width = 2, .height = 1},
	                                   .format = render::texture_format::rgba8,
	                                   .filter = render::filter::nearest,
	                                   .anisotropic = false,
	                                   .has_transparency = true},
	                                  pixels};
	rec.recorder.texture({3}, &image);
	rec.recorder.texture({4}, &image);
	rec.recorder.texture({4}, nullptr);
	play(rec.recorder, 1);
	/* The game's copy may change once given; the trace keeps the pixels as
	 * they were given.
	 */
	pixels.assign(pixels.size(), 0);
	play(rec.recorder, 1);
	BOOST_TEST(inner.textures == 3u);
	const auto records{rec.records()};
	BOOST_REQUIRE(!records.empty());
	BOOST_REQUIRE(std::holds_alternative<trace::texture>(records.front()));
	const auto &t{std::get<trace::texture>(records.front())};
	BOOST_TEST(t.slot.index == 3u);
	BOOST_TEST(t.rgba == (std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}),
	           boost::test_tools::per_element());
	for (const auto &r : records)
		if (const auto *const other{std::get_if<trace::texture>(&r)})
			BOOST_TEST(other->slot.index != 4u);
}

/* Test that the depth clears of a frame are recorded, each of depth alone:
 * the one that starts the 3D frame and the rear view's in mid-frame
 * (begin_3d_frame, #16), after the frame's first draw.
 */
BOOST_AUTO_TEST_CASE(depth_clears_are_recorded_with_the_rear_views)
{
	fake_device inner;
	recording rec{inner, {.first = 0, .last = 0}};
	render::ambient a;
	render::begin_3d_frame(rec.recorder, a, 1.f);
	draw_frame(rec.recorder, 0);
	render::end_3d_frame(a);
	render::begin_3d_frame(rec.recorder, a, 1.f);
	draw_frame(rec.recorder, 0);
	rec.recorder.present();
	std::vector<std::string> kinds;
	for (const auto &r : rec.records())
	{
		if (const auto *const c{std::get_if<trace::clear>(&r)})
			kinds.emplace_back(c->color ? "colour clear" : c->depth ? "depth clear" : "?");
		else if (std::holds_alternative<trace::draw>(r))
			kinds.emplace_back("draw");
		else if (std::holds_alternative<trace::present>(r))
			kinds.emplace_back("present");
	}
	BOOST_TEST(kinds == (std::vector<std::string>{"depth clear", "draw", "depth clear", "draw",
	                                              "present"}),
	           boost::test_tools::per_element());
}

/* Test that with recording off the front end draws through the device
 * itself, with no recorder in the call path; with a recorder, through it.
 */
BOOST_AUTO_TEST_CASE(recording_off_draws_through_the_device_itself)
{
	fake_device inner;
	BOOST_TEST(&render::front_device(inner, nullptr) == &inner);
	recording rec{inner, {.first = 0, .last = 0}};
	fake_device other;
	render::device &front{render::front_device(other, &rec.recorder)};
	BOOST_TEST(&front == &rec.recorder);
	play(front, 1);
	BOOST_TEST(other.draws == 1u);
	BOOST_TEST(inner.draws == 0u);
}

/* Test that a range ending before it starts is refused. */
BOOST_AUTO_TEST_CASE(backward_range_refused)
{
	fake_device inner;
	BOOST_CHECK_THROW((render::recording_device{inner,
	                                            {.first = 2, .last = 1},
	                                            [] { return target; },
	                                            [](std::span<const std::uint8_t>) {}}),
	                  std::invalid_argument);
}

/* Test that the front end's devices torn down in exit order (the guard
 * declared last, so destroyed first, then the recorder, then the device)
 * leave the front pointer null, so a texture release that a later static
 * destructor makes (the fonts', at exit) touches no destroyed device.
 */
BOOST_AUTO_TEST_CASE(release_after_teardown_touches_no_device)
{
	render::device *front{nullptr};
	{
		const auto inner{std::make_unique<fake_device>()};
		const auto recorder{std::make_unique<render::recording_device>(
		    *inner, render::frame_range{.first = 0, .last = 0}, [] { return target; },
		    [](std::span<const std::uint8_t>) {})};
		const render::front_reset guard{front};
		front = &render::front_device(*inner, recorder.get());
		render::release_texture(front, {5});
		BOOST_TEST(inner->textures == 1u);
	}
	/* Required: with front left dangling, the release below would touch
	 * the destroyed recorder.
	 */
	BOOST_REQUIRE(front == nullptr);
	render::release_texture(front, {5});
}

namespace {

/* A recorder whose target size and stops the test sets and reads. */
struct watched_recording
{
	render::extent size{target};
	std::vector<std::uint8_t> bytes;
	std::vector<std::pair<render::stop_reason, std::string>> stops;
	render::recording_device recorder;
	watched_recording(render::device &inner, const render::frame_range range)
	    : recorder{inner, range, [this] { return size; },
		           [this](const std::span<const std::uint8_t> b) {
		               bytes.insert(bytes.end(), b.begin(), b.end());
	               },
		           [this](const render::stop_reason r, const std::string_view why) {
		               stops.emplace_back(r, why);
	               }}
	{}
	[[nodiscard]]
	std::vector<std::uint32_t> presented() const
	{
		auto r{trace::read_all(bytes)};
		BOOST_REQUIRE(r.has_value());
		return presented_frames(r.value());
	}
};

/* An opaque image width texels wide and 1 high. */
struct wide_image
{
	std::vector<std::uint8_t> rgba;
	render::texture_image image;
	explicit wide_image(const std::uint32_t width)
	    : rgba(std::size_t{width} * 4, 255), image{{.size = {.width = width, .height = 1},
		                                            .format = render::texture_format::rgba8,
		                                            .filter = render::filter::nearest,
		                                            .anisotropic = false,
		                                            .has_transparency = false},
		                                           rgba}
	{}
};

}

/* Test that a texture over the trace format's limit, given in range, ends
 * the recording with its reason instead of throwing through the game; the
 * frames before it stay in the trace and the device still gets it.
 */
BOOST_AUTO_TEST_CASE(oversized_texture_in_range_ends_the_recording)
{
	fake_device inner;
	watched_recording rec{inner, {.first = 0, .last = 3}};
	play(rec.recorder, 1);
	const wide_image big{trace::max_texture_side + 1};
	BOOST_CHECK_NO_THROW(rec.recorder.texture({7}, &big.image));
	play(rec.recorder, 1);
	BOOST_TEST(inner.textures == 1u);
	BOOST_TEST(rec.presented() == (std::vector<std::uint32_t>{0}),
	           boost::test_tools::per_element());
	BOOST_REQUIRE(rec.stops.size() == 1u);
	BOOST_TEST((rec.stops.front().first == render::stop_reason::trace_limit));
	BOOST_TEST(rec.recorder.finished());
}

/* Test that a texture over the limit given before the range ends the
 * recording when the range starts, where the trace would lead with it.
 */
BOOST_AUTO_TEST_CASE(oversized_texture_before_the_range_ends_it_at_its_start)
{
	fake_device inner;
	watched_recording rec{inner, {.first = 1, .last = 3}};
	const wide_image big{trace::max_texture_side + 1};
	rec.recorder.texture({7}, &big.image);
	BOOST_CHECK_NO_THROW(play(rec.recorder, 3));
	BOOST_TEST(inner.presents == 3u);
	BOOST_TEST(rec.bytes.empty());
	BOOST_REQUIRE(rec.stops.size() == 1u);
	BOOST_TEST((rec.stops.front().first == render::stop_reason::trace_limit));
}

/* Test that a recorder rebound around a new device (a new GL context, as a
 * stereo mode change makes) reports that device's caps, not the first's.
 */
BOOST_AUTO_TEST_CASE(rebind_takes_the_new_devices_caps)
{
	fake_device first;
	recording rec{first, {.first = 0, .last = 0}};
	fake_device stereo{{.anisotropic = true, .quad_buffer_stereo = true, .copy_region = true}};
	const render::device &front{render::front_device(stereo, &rec.recorder)};
	BOOST_TEST((front.caps() == stereo.caps()));
}

/* Test that a render target resized in mid-recording ends the recording
 * with its reason, the frame drawn at the new size left out: the trace has
 * one target size, in its header.  Every frame still reaches the device.
 */
BOOST_AUTO_TEST_CASE(target_resized_in_range_ends_the_recording)
{
	fake_device inner;
	watched_recording rec{inner, {.first = 0, .last = 3}};
	play(rec.recorder, 1);
	rec.size = {.width = 32, .height = 32};
	play(rec.recorder, 2);
	BOOST_TEST(inner.presents == 3u);
	BOOST_TEST(rec.presented() == (std::vector<std::uint32_t>{0}),
	           boost::test_tools::per_element());
	BOOST_REQUIRE(rec.stops.size() == 1u);
	BOOST_TEST((rec.stops.front().first == render::stop_reason::target_resized));
	BOOST_TEST(rec.stops.front().second ==
	           "the render target changed from 64x32 to 32x32 at frame 1");
	BOOST_TEST(rec.recorder.finished());
}

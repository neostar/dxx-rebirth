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
#include <span>
#include <stdexcept>
#include <string>
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
	fake_device() : device{render::caps{}}
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

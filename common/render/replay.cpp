/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/replay.h"

#include <cstddef>
#include <expected>
#include <span>
#include <variant>
#include <vector>

#include "common/debug/frame_hash.h"
#include "common/render/device.h"
#include "common/render/readback.h"
#include "common/render/trace.h"

namespace dcx::render {

namespace {

/* Plays one record on d; a present reads back the frame first. */
struct play
{
	device &d;
	const frame_sink &sink;
	render::extent target;
	void operator()(const trace::clear &c) const
	{
		d.clear({.color = c.color, .depth = c.depth});
	}
	void operator()(const trace::texture &t) const
	{
		const auto image{t.image()};
		d.texture(t.slot, image ? &image.value() : nullptr);
	}
	void operator()(const trace::draw &r) const
	{
		d.draw(r.call());
	}
	void operator()(const trace::present &p) const
	{
		replayed_frame f{.frame = p.frame,
		                 .size = target,
		                 .rgb = std::vector<std::uint8_t>(image_bytes(target, pixel_format::rgb8)),
		                 .hash = 0};
		d.read_pixels(read_source::drawing, {.x = 0, .y = 0, .size = target}, pixel_format::rgb8,
		              f.rgb);
		f.hash = frame_hash(std::as_bytes(std::span{f.rgb}));
		sink(f);
		d.present();
	}
};

}

std::expected<void, trace::read_error> replay(trace::reader &r, device &d, const frame_sink &sink)
{
	const auto h{r.header()};
	if (!h)
		return std::unexpected{h.error()};
	const play p{.d = d, .sink = sink, .target = h.value().target};
	for (;;)
	{
		auto next{r.next()};
		if (!next)
			return std::unexpected{next.error()};
		if (!next.value())
			return {};
		std::visit(p, next.value().value());
	}
}

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "common/render/recording_device.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace dcx::render {

recording_device::recording_device(device &inner, const frame_range range, target_size target,
                                   trace_out out, trace_stop stop)
    : device{inner.caps()}, inner_{&inner}, range_{range}, target_{std::move(target)},
      out_{std::move(out)}, stop_{std::move(stop)}
{
	if (range.last < range.first)
		throw std::invalid_argument("recording_device: last frame before the first");
	start_if_due();
}

void recording_device::start_if_due()
{
	if (frame_ != range_.first || writer_ || done_)
		return;
	guarded([this] {
		recorded_target_ = target_();
		writer_.emplace(recorded_target_);
		for (const auto &[index, t] : textures_)
			writer_->write(t);
	});
	textures_.clear();
}

template <typename F>
void recording_device::guarded(F &&write)
{
	try
	{
		write();
	}
	catch (const std::length_error &e)
	{
		stop(stop_reason::trace_limit, e.what());
	}
	catch (const std::invalid_argument &e)
	{
		stop(stop_reason::trace_limit, e.what());
	}
}

void recording_device::stop(const stop_reason r, const std::string_view why)
{
	writer_.reset();
	textures_.clear();
	done_ = true;
	if (stop_)
		stop_(r, why);
}

void recording_device::draw(const draw_call &call)
{
	inner_->draw(call);
	if (writer_)
		guarded([&] { writer_->draw(call); });
}

void recording_device::texture(const texture_slot slot, const texture_image *const image)
{
	inner_->texture(slot, image);
	if (writer_)
		guarded([&] { writer_->texture(slot, image); });
	else if (done_)
		return;
	else if (!image)
		textures_.erase(slot.index);
	else
	{
		/* Reuses the slot's buffer: a slot uploaded each frame (the movie
		 * frame's) allocates once.
		 */
		auto &kept{textures_.try_emplace(slot.index).first->second};
		kept.slot = slot;
		kept.desc = image->desc();
		kept.rgba.assign(image->rgba().begin(), image->rgba().end());
	}
}

void recording_device::clear(const clear_targets targets)
{
	inner_->clear(targets);
	if (writer_ && (targets.color || targets.depth))
		writer_->clear({.color = targets.color, .depth = targets.depth});
}

void recording_device::present()
{
	inner_->present();
	if (writer_)
	{
		writer_->present({.frame = frame_});
		const auto bytes{writer_->take()};
		if (const auto now{target_()}; now != recorded_target_)
			/* The frame drawn at the new size is left out. */
			stop(stop_reason::target_resized,
			     "the render target changed from " + std::to_string(recorded_target_.width) + 'x' +
			         std::to_string(recorded_target_.height) + " to " + std::to_string(now.width) +
			         'x' + std::to_string(now.height) + " at frame " + std::to_string(frame_));
		else
		{
			out_(bytes);
			recorded_ = {.first = recorded_ ? recorded_->first : frame_, .last = frame_};
			if (frame_ == range_.last)
			{
				writer_.reset();
				done_ = true;
			}
		}
	}
	++frame_;
	start_if_due();
}

void recording_device::read_pixels(const read_source source, const read_rect &rect,
                                   const pixel_format format, const std::span<std::uint8_t> pixels)
{
	inner_->read_pixels(source, rect, format, pixels);
}

void recording_device::select_eye(const eye e)
{
	inner_->select_eye(e);
}

void recording_device::copy_region(const copy_rect &rect, const bool from_left_eye)
{
	inner_->copy_region(rect, from_left_eye);
}

std::string recording_summary(const recording_device &recorder)
{
	const auto asked{recorder.range()};
	const std::string of{std::to_string(asked.first) + '-' + std::to_string(asked.last)};
	if (const auto r{recorder.recorded()})
		return "recorded frames " + std::to_string(r->first) + '-' + std::to_string(r->last) +
		       " of " + of;
	return "recorded no frame of " + of + ": the run presented " +
	       std::to_string(recorder.presented()) + " frames";
}

}

/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 * DXX_DBG_RECORD_INPUT: the debug harness records the input a run consumed
 * as an input script (input_script.h), which DXX_DBG_INPUT_SCRIPT replays
 * (#99).  The game hands each event to the recorder where event_poll
 * consumes it, at the harness tick of that event_process call, so a replay
 * pushes it at the same tick, ahead of the same poll.  Without
 * DXX_DBG_RECORD_INPUT nothing is recorded and no recorder is made.
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "common/debug/input_script.h"

namespace dcx {

/* Input a run consumed that an input script cannot hold yet: each becomes
 * a comment in the recording, and the replay differs from that tick on.
 */
enum class unrecorded_input : std::uint8_t
{
	/* Joystick and gamepad events: a real device is not one of the script's two
	 * virtual shapes, its events come in two kinds (joystick and gamepad) for
	 * one action, and a device plugged in at start has no attach to record.
	 */
	joystick,
	/* A quit (closing the window); end the session with
	 * DXX_DBG_RECORD_INPUT_TICKS instead.
	 */
	quit,
	/* SDL3 text the game read that is not its key's own character. */
	text,
};

/* Writes the events a run consumed as input script lines, through `sink`,
 * one line per call, without its newline.  A key goes in by the name
 * `key_name_of` gives it, and only when `key_of` reads that name back as the
 * same key; anything a script cannot hold becomes a comment, counted in
 * unrecorded().  The first end() ends the recording: nothing follows it.
 */
class input_recorder
{
  public:
	using line_sink = std::function<void(std::string_view)>;
	using key_name_of = std::function<std::string_view(input_key)>;
	using key_of = std::function<std::optional<input_key>(std::string_view)>;

  private:
	line_sink sink_;
	key_name_of key_name_of_;
	key_of key_of_;
	/* The shape of each slot's attached device, as the parser tracks it. */
	std::array<input_device_shape, input_device_count> shapes_{};
	unsigned recorded_{0};
	unsigned unrecorded_{0};
	std::optional<harness_tick> end_;

	void comment(const harness_tick tick, const std::string_view what)
	{
		++unrecorded_;
		sink_("# tick " + std::to_string(std::to_underlying(tick)) +
		      ": not recorded: " + std::string{what});
	}
	void line(const harness_tick tick, const std::string_view rest)
	{
		++recorded_;
		sink_(std::to_string(std::to_underlying(tick)) + ' ' + std::string{rest});
	}
	/* A mouse_move's motion or the wheel's steps, as parse_amount takes them. */
	[[nodiscard]]
	static constexpr bool scriptable_amount(const std::int32_t a)
	{
		return a >= -32767 && a <= 32767;
	}

	/* A joystick event as the script line the parser reads back, only when the device it names
	 * is attached in the script's terms and has the control: anything else is a comment.
	 */
	void record_joystick(const input_event &e)
	{
		const auto slot{std::to_string(e.device)};
		if (e.device >= input_device_count)
		{
			comment(e.tick, "a joystick in slot " + slot + ", beyond 7");
			return;
		}
		auto &shape{shapes_[e.device]};
		if (e.action == input_action::joy_attach)
		{
			if (shape != input_device_shape::none || e.shape == input_device_shape::none)
			{
				comment(e.tick, "joy_attach to slot " + slot + ", attached already or of no shape");
				return;
			}
			shape = e.shape;
			line(e.tick, "joy_attach " + slot +
			                 (shape == input_device_shape::gamepad ? " gamepad" : " stick"));
			return;
		}
		if (shape == input_device_shape::none)
		{
			comment(e.tick, "a joystick event for slot " + slot + ", which has no device");
			return;
		}
		const auto controls{input_controls_of(shape)};
		const auto control{std::to_string(e.control)};
		switch (e.action)
		{
		case input_action::joy_detach:
			shape = input_device_shape::none;
			line(e.tick, "joy_detach " + slot);
			return;
		case input_action::joy_axis:
			if (e.control >= controls.axes || e.value < -32768 || e.value > 32767)
			{
				comment(e.tick,
				        "joy_axis " + control + ", an axis its device lacks or beyond 16 bits");
				return;
			}
			line(e.tick, "joy_axis " + slot + ' ' + control + ' ' + std::to_string(e.value));
			return;
		case input_action::joy_button_down:
		case input_action::joy_button_up:
			if (e.control >= controls.buttons)
			{
				comment(e.tick, "joy_button " + control + ", a button its device lacks");
				return;
			}
			line(e.tick, "joy_button " + slot + ' ' + control +
			                 (e.action == input_action::joy_button_down ? " down" : " up"));
			return;
		case input_action::joy_hat: {
			const auto direction{input_script_detail::hat_direction_name(e.value)};
			if (e.control >= controls.hats || !direction)
			{
				comment(e.tick, "joy_hat " + control +
				                    ", a hat its device lacks or a direction "
				                    "no name holds");
				return;
			}
			line(e.tick, "joy_hat " + slot + ' ' + control + ' ' + std::string{direction.value()});
			return;
		}
		case input_action::joy_attach:
		case input_action::key_down:
		case input_action::key_up:
		case input_action::mouse_move:
		case input_action::mouse_down:
		case input_action::mouse_up:
		case input_action::wheel:
		case input_action::end:
			break;
		}
	}

  public:
	input_recorder(line_sink sink, key_name_of name_of, key_of named)
	    : sink_{std::move(sink)}, key_name_of_{std::move(name_of)}, key_of_{std::move(named)}
	{}
	void record(const input_event &e)
	{
		if (end_)
			return;
		switch (e.action)
		{
		case input_action::key_down:
		case input_action::key_up: {
			const auto name{key_name_of_(e.key)};
			if (name.empty() || key_of_(name) != e.key)
			{
				comment(e.tick, "key " + std::to_string(std::to_underlying(e.key)) +
				                    ", which no script name reads back");
				return;
			}
			line(e.tick,
			     (e.action == input_action::key_down ? "down " : "up ") + std::string{name});
			return;
		}
		case input_action::mouse_move:
			if (!scriptable_amount(e.x) || !scriptable_amount(e.y))
			{
				comment(e.tick, "mouse_move " + std::to_string(e.x) + ' ' + std::to_string(e.y) +
				                    ", beyond 32767");
				return;
			}
			line(e.tick, "mouse_move " + std::to_string(e.x) + ' ' + std::to_string(e.y));
			return;
		case input_action::mouse_down:
		case input_action::mouse_up:
			line(e.tick, (e.action == input_action::mouse_down ? "mouse_down " : "mouse_up ") +
			                 std::to_string(std::to_underlying(e.button)));
			return;
		case input_action::wheel:
			if (e.y == 0 || !scriptable_amount(e.y))
			{
				comment(e.tick, "wheel " + std::to_string(e.y) + ", 0 or beyond 32767");
				return;
			}
			line(e.tick, "wheel " + std::to_string(e.y));
			return;
		case input_action::joy_attach:
		case input_action::joy_detach:
		case input_action::joy_axis:
		case input_action::joy_button_down:
		case input_action::joy_button_up:
		case input_action::joy_hat:
			record_joystick(e);
			return;
		case input_action::end:
			end(e.tick);
			return;
		}
	}
	void skip(const harness_tick tick, const unrecorded_input what)
	{
		if (end_)
			return;
		switch (what)
		{
		case unrecorded_input::joystick:
			comment(tick, "a joystick or gamepad event (scripts hold virtual devices alone)");
			return;
		case unrecorded_input::quit:
			comment(tick, "a quit");
			return;
		case unrecorded_input::text:
			comment(tick, "text other than its key's own character");
			return;
		}
	}
	/* Ends the recording at `tick`: a replay exits there, as the run did. */
	void end(const harness_tick tick)
	{
		if (end_)
			return;
		end_ = tick;
		sink_(std::to_string(std::to_underlying(tick)) + " end");
	}
	[[nodiscard]]
	bool ended() const
	{
		return end_.has_value();
	}
	[[nodiscard]]
	unsigned unrecorded() const
	{
		return unrecorded_;
	}
	/* What was recorded, for the note at exit. */
	[[nodiscard]]
	std::string summary() const
	{
		return "recorded " + std::to_string(recorded_) +
		       (recorded_ == 1 ? " input event" : " input events") + " over ticks 0-" +
		       (end_ ? std::to_string(std::to_underlying(end_.value())) : std::string{"?"}) + "; " +
		       std::to_string(unrecorded_) + " not recorded (comments in the file)";
	}
};

}

// Oak Video Editor - Non-Linear Video Editor
// Copyright (C) 2026 Oak Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

//! The transport state machine: play/pause/step/seek semantics shared by the
//! transport drives of both monitors.
//!
//! Pure logic, no gpui dependency — the engine clocks and the widgets only
//! read the resulting state ([`TransportState::frame`],
//! [`TransportState::is_playing`]). Keeping this free of gpui types is what
//! makes the whole machine unit-testable with plain `#[test]`.

use gpui::timeline::Frame;

/// Whether the transport is rolling.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PlayState {
	/// Stopped; the playhead is stationary.
	Stopped,
	/// Playing; the playhead advances with the wall clock.
	Playing,
}

/// The transport state of one monitor.
///
/// Invariants:
/// * `frame` is always `>= Frame(0)`; it is clamped to `[0, length)` by every
///   mutating method that takes a sequence `length`.
/// * `in_point` / `out_point`, when both are set, satisfy
///   `in_point <= out_point`.
#[derive(Debug, Clone, PartialEq)]
pub struct TransportState {
	state: PlayState,
	frame: Frame,
	in_point: Option<Frame>,
	out_point: Option<Frame>,
}

impl Default for TransportState {
	fn default() -> Self {
		Self::new()
	}
}

impl TransportState {
	/// A stopped transport at frame zero with no loop range.
	pub fn new() -> Self {
		Self {
			state: PlayState::Stopped,
			frame: Frame::ZERO,
			in_point: None,
			out_point: None,
		}
	}

	/// The current playhead position.
	pub fn frame(&self) -> Frame {
		self.frame
	}

	/// The current play state.
	pub fn state(&self) -> PlayState {
		self.state
	}

	/// Whether playback is rolling.
	pub fn is_playing(&self) -> bool {
		self.state == PlayState::Playing
	}

	/// The loop range, if both points are set.
	pub fn loop_range(&self) -> Option<(Frame, Frame)> {
		match (self.in_point, self.out_point) {
			(Some(in_point), Some(out_point)) => Some((in_point, out_point)),
			_ => None,
		}
	}

	/// Starts playback. Idempotent: playing a playing transport is a no-op.
	pub fn play(&mut self) {
		self.state = PlayState::Playing;
	}

	/// Stops playback, leaving the playhead where it is. Idempotent.
	pub fn pause(&mut self) {
		self.state = PlayState::Stopped;
	}

	/// Toggles between playing and stopped.
	pub fn toggle(&mut self) {
		self.state = match self.state {
			PlayState::Stopped => PlayState::Playing,
			PlayState::Playing => PlayState::Stopped,
		};
	}

	/// Seeks to `frame`, clamped to `[0, length)`. Keeps the play state.
	pub fn seek(&mut self, frame: Frame, length: Frame) {
		self.frame = clamp_frame(frame, length);
	}

	/// Steps the playhead by `delta` frames, clamped to `[0, length)`.
	/// Keeps the play state (stepping while playing is jogging).
	pub fn step(&mut self, delta: i64, length: Frame) {
		self.seek(self.frame + Frame(delta), length);
	}

	/// Sets the loop-in point at the current playhead.
	pub fn set_in_point(&mut self) {
		self.in_point = Some(self.frame);
	}

	/// Sets the loop-out point at the current playhead.
	pub fn set_out_point(&mut self) {
		self.out_point = Some(self.frame);
		// Keep the range well-formed: an out point before the in point is
		// collapsed to the in point.
		if let (Some(in_point), Some(out_point)) = (self.in_point, self.out_point) {
			if out_point < in_point {
				self.out_point = Some(in_point);
			}
		}
	}

	/// Clears the loop range.
	pub fn clear_range(&mut self) {
		self.in_point = None;
		self.out_point = None;
	}
}

/// Clamps `frame` into `[0, length)`. A zero-length sequence clamps to zero.
fn clamp_frame(frame: Frame, length: Frame) -> Frame {
	let length = length.0.max(0);
	Frame(if length == 0 {
		0
	} else {
		frame.0.clamp(0, length - 1)
	})
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn initial_state_is_stopped_at_zero() {
		let transport = TransportState::new();
		assert_eq!(transport.state(), PlayState::Stopped);
		assert!(!transport.is_playing());
		assert_eq!(transport.frame(), Frame(0));
		assert_eq!(transport.loop_range(), None);
	}

	#[test]
	fn play_then_pause_keeps_the_frame() {
		let mut transport = TransportState::new();
		transport.play();
		assert!(transport.is_playing());
		transport.seek(Frame(42), Frame(100));
		transport.pause();
		assert_eq!(transport.state(), PlayState::Stopped);
		assert_eq!(transport.frame(), Frame(42));
	}

	#[test]
	fn play_is_idempotent() {
		let mut transport = TransportState::new();
		transport.play();
		transport.play();
		assert!(transport.is_playing());
	}

	#[test]
	fn toggle_switches_state() {
		let mut transport = TransportState::new();
		transport.toggle();
		assert!(transport.is_playing());
		transport.toggle();
		assert_eq!(transport.state(), PlayState::Stopped);
	}

	#[test]
	fn step_clamps_at_the_sequence_edges() {
		let mut transport = TransportState::new();
		transport.step(-5, Frame(100));
		assert_eq!(transport.frame(), Frame(0));

		transport.seek(Frame(99), Frame(100));
		transport.step(5, Frame(100));
		assert_eq!(transport.frame(), Frame(99));
	}

	#[test]
	fn step_while_playing_is_jogging() {
		let mut transport = TransportState::new();
		transport.play();
		transport.step(1, Frame(100));
		assert!(transport.is_playing());
		assert_eq!(transport.frame(), Frame(1));
	}

	#[test]
	fn seek_clamps_to_length() {
		let mut transport = TransportState::new();
		transport.seek(Frame(150), Frame(100));
		assert_eq!(transport.frame(), Frame(99));
		transport.seek(Frame(-10), Frame(100));
		assert_eq!(transport.frame(), Frame(0));
	}

	#[test]
	fn zero_length_sequence_clamps_to_zero() {
		let mut transport = TransportState::new();
		transport.seek(Frame(5), Frame(0));
		assert_eq!(transport.frame(), Frame(0));
	}

	#[test]
	fn loop_points_set_and_clear() {
		let mut transport = TransportState::new();
		assert_eq!(transport.loop_range(), None);

		transport.seek(Frame(10), Frame(100));
		transport.set_in_point();
		transport.seek(Frame(20), Frame(100));
		transport.set_out_point();
		assert_eq!(transport.loop_range(), Some((Frame(10), Frame(20))));

		transport.clear_range();
		assert_eq!(transport.loop_range(), None);
	}

	#[test]
	fn out_point_before_in_point_collapses_to_in_point() {
		let mut transport = TransportState::new();
		transport.seek(Frame(30), Frame(100));
		transport.set_in_point();
		transport.seek(Frame(5), Frame(100));
		transport.set_out_point();
		assert_eq!(transport.loop_range(), Some((Frame(30), Frame(30))));
	}
}

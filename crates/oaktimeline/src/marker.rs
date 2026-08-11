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

//! Timeline markers and the marker undo-command family
//! (`src/timeline/src/timelinemarker.h`): `TimelineMarker`/`TimelineMarkerList`
//! plus `MarkerAdd`/`MarkerRemove`/`MarkerChangeColor`/`MarkerChangeName`/
//! `MarkerChangeTime` commands.
//!
//! The list keeps markers sorted by time (`// CPP-PARITY:
//! src/timeline/src/timelinemarker.h` `insert_into_list`). De-Qt: no QObject,
//! no signals — change notifications are the facade's job. `set_time` does
//! not re-sort (the De-Qt marker has no parent pointer); callers restore
//! order via `TimelineMarkerList::resort`/`resort_at`.

use oakcore_rs::{Rational, TimeRange};

use crate::handle::{get, get_mut};
use crate::undocommon::{box_command, Command};

/// `TimelineMarker` — a named, colored time range on a timeline
/// (timelinemarker.h). De-Qt; `draw()` moved to the app layer.
pub struct TimelineMarker {
	/// Marker time range.
	time_: TimeRange,
	/// Marker name (may be empty).
	name_: String,
	/// Marker color index.
	color_: i32,
}

impl TimelineMarker {
	/// Default constructor.
	pub fn new() -> Self {
		Self::with_time(
			0,
			TimeRange::new(Rational::new(0, 1), Rational::new(0, 1)),
			"",
		)
	}

	/// Construct with color, time and optional name.
	pub fn with_time(color: i32, time: TimeRange, name: &str) -> Self {
		Self {
			time_: time,
			name_: name.to_string(),
			color_: color,
		}
	}

	/// The marker's time range.
	pub fn time(&self) -> &TimeRange {
		&self.time_
	}

	/// Set the time range. De-Qt: the marker has no parent pointer, so the
	/// owning list is not re-sorted here; callers use
	/// `TimelineMarkerList::resort`/`resort_at` when order must be restored.
	pub fn set_time(&mut self, time: TimeRange) {
		self.time_ = time;
	}

	/// Set the time to a zero-length range at `time`.
	pub fn set_time_point(&mut self, time: Rational) {
		let length = self.time_.length();
		self.set_time(TimeRange::new(time, time + length));
	}

	/// Whether another marker in the same list shares `time` as its in point.
	///
	/// De-Qt simplification: the marker has no parent pointer, so this always
	/// returns `false`; callers that need the answer use
	/// `TimelineMarkerList::get_marker_at_time` instead.
	pub fn has_sibling_at_time(&self, _t: Rational) -> bool {
		false
	}

	/// The marker's name.
	pub fn name(&self) -> &str {
		&self.name_
	}

	/// Set the marker's name.
	pub fn set_name(&mut self, name: &str) {
		self.name_ = name.to_string();
	}

	/// The marker's color index.
	pub fn color(&self) -> i32 {
		self.color_
	}

	/// Set the marker's color index.
	pub fn set_color(&mut self, c: i32) {
		self.color_ = c;
	}
}

/// `TimelineMarkerList` — an ordered collection of markers, sorted by time
/// (timelinemarker.h).
pub struct TimelineMarkerList {
	/// Markers, kept sorted by time.
	markers_: Vec<TimelineMarker>,
}

impl TimelineMarkerList {
	/// A new, empty list.
	pub fn new() -> Self {
		Self {
			markers_: Vec::new(),
		}
	}

	/// Whether the list is empty.
	pub fn empty(&self) -> bool {
		self.markers_.is_empty()
	}

	/// Number of markers.
	pub fn size(&self) -> usize {
		self.markers_.len()
	}

	/// Marker at `i`; `None` out of range.
	pub fn at(&self, i: usize) -> Option<&TimelineMarker> {
		self.markers_.get(i)
	}

	/// The last marker; `None` when empty.
	pub fn back(&self) -> Option<&TimelineMarker> {
		self.markers_.last()
	}

	/// The first marker; `None` when empty.
	pub fn front(&self) -> Option<&TimelineMarker> {
		self.markers_.first()
	}

	/// Insert a marker, keeping the list sorted by time (takes ownership).
	///
	/// Equal in points insert after the existing ones (`>` comparison,
	/// CPP-PARITY timelinemarker.h `insert_into_list`).
	pub fn add_marker(&mut self, marker: TimelineMarker) {
		for i in 0..self.markers_.len() {
			if self.markers_[i].time().in_() > marker.time().in_() {
				self.markers_.insert(i, marker);
				return;
			}
		}
		self.markers_.push(marker);
	}

	/// Detach `m` from the list, returning it; `None` if not present.
	pub fn remove_marker(&mut self, m: &TimelineMarker) -> Option<TimelineMarker> {
		for i in 0..self.markers_.len() {
			if std::ptr::eq(&self.markers_[i], m) {
				return Some(self.markers_.remove(i));
			}
		}
		None
	}

	/// First marker whose in point equals `t`; `None` if none.
	pub fn get_marker_at_time(&self, t: Rational) -> Option<&TimelineMarker> {
		self.markers_.iter().find(|m| m.time().in_() == t)
	}

	/// Marker closest to `t` (early-exits once the diff increases;
	/// CPP-PARITY timelinemarker.h).
	pub fn get_closest_marker_to_time(&self, t: Rational) -> Option<&TimelineMarker> {
		let mut closest: Option<&TimelineMarker> = None;
		let mut closest_diff = Rational::new(0, 1);
		for m in &self.markers_ {
			let this_diff = rational_abs(m.time().in_() - t);
			if closest.is_some() && this_diff > closest_diff {
				// Sorted by in point, so the distance is increasing from
				// here on.
				break;
			}
			if closest.is_none() || this_diff < closest_diff {
				closest = Some(m);
				closest_diff = this_diff;
			}
		}
		closest
	}

	/// Re-sort `m` after its time changed.
	pub fn resort(&mut self, m: &mut TimelineMarker) {
		if let Some(index) = self.markers_.iter().position(|x| std::ptr::eq(x, &*m)) {
			let marker = self.markers_.remove(index);
			self.add_marker(marker);
		}
	}

	/// Marker at `i`, mutably; `None` out of range.
	fn at_mut(&mut self, i: usize) -> Option<&mut TimelineMarker> {
		self.markers_.get_mut(i)
	}

	/// Remove the marker at `index` and re-insert it sorted; no-op when
	/// `index` is out of range. Used by the time-change command after
	/// mutating a marker in place.
	fn resort_at(&mut self, index: usize) {
		if index < self.markers_.len() {
			let marker = self.markers_.remove(index);
			self.add_marker(marker);
		}
	}
}

/// Absolute value of a rational (oakcore-rs has no `abs`).
fn rational_abs(r: Rational) -> Rational {
	if r < Rational::new(0, 1) {
		Rational::new(0, 1) - r
	} else {
		r
	}
}

// ---------------------------------------------------------------------------
// Marker undo commands. Each struct exposes prepare()/redo()/undo(); the
// FFI layer wraps it through bridge::undo's vtable (to_command()).
// ---------------------------------------------------------------------------

/// `MarkerAddCommand` (timelinemarker.h).
pub struct MarkerAddCommand {
	/// Target list.
	marker_list: crate::handle::CHandle,
	/// Marker range.
	range: TimeRange,
	/// Marker name.
	name: String,
	/// Marker color.
	color: i32,
	/// Whether the marker is currently in the list.
	added: bool,
}

impl MarkerAddCommand {
	/// Construct from range/name/color.
	pub fn new(
		marker_list: crate::handle::CHandle,
		range: TimeRange,
		name: &str,
		color: i32,
	) -> Self {
		Self {
			marker_list,
			range,
			name: name.to_string(),
			color,
			added: false,
		}
	}

	/// `redo`: add the marker, sorted.
	pub fn redo(&mut self) {
		if self.added {
			return;
		}
		let marker = TimelineMarker::with_time(self.color, self.range, &self.name);
		// SAFETY: the boxed value is a `TimelineMarkerList` created by
		// `make_owned`, and the command holds exclusive access to it.
		if let Some(list) = unsafe { get_mut::<TimelineMarkerList>(&self.marker_list) } {
			list.add_marker(marker);
			self.added = true;
		}
	}

	/// `undo`: remove the added marker.
	///
	/// Simplification: the marker is located by its in point
	/// (`get_marker_at_time`); when several markers share the in point, the
	/// oldest (first in the list) is removed.
	pub fn undo(&mut self) {
		if !self.added {
			return;
		}
		// SAFETY: as `redo`.
		if let Some(list) = unsafe { get_mut::<TimelineMarkerList>(&self.marker_list) } {
			if let Some(m) = list.get_marker_at_time(self.range.in_()) {
				// SAFETY: `m` borrows from `list`; `remove_marker` uses it
				// only for identity comparison before detaching.
				let mptr = m as *const TimelineMarker;
				let _ = list.remove_marker(unsafe { &*mptr });
			}
			self.added = false;
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> crate::handle::CHandle {
		box_command(self)
	}
}

impl Command for MarkerAddCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `MarkerRemoveCommand` (timelinemarker.h).
pub struct MarkerRemoveCommand {
	/// Target list.
	marker_list: crate::handle::CHandle,
	/// Index of the marker to remove.
	index: usize,
	/// Marker detached on `redo`, re-inserted by `undo`.
	removed: Option<TimelineMarker>,
}

impl MarkerRemoveCommand {
	/// Construct from list + index of the marker to remove.
	pub fn new(marker_list: crate::handle::CHandle, index: usize) -> Self {
		Self {
			marker_list,
			index,
			removed: None,
		}
	}

	/// `redo`: remove the marker.
	pub fn redo(&mut self) {
		if self.removed.is_some() {
			return;
		}
		// SAFETY: the boxed value is a `TimelineMarkerList` created by
		// `make_owned`, and the command holds exclusive access to it.
		if let Some(list) = unsafe { get_mut::<TimelineMarkerList>(&self.marker_list) } {
			if let Some(m) = list.at(self.index) {
				// SAFETY: `m` borrows from `list`; `remove_marker` uses it
				// only for identity comparison before detaching.
				let mptr = m as *const TimelineMarker;
				if let Some(removed) = list.remove_marker(unsafe { &*mptr }) {
					self.removed = Some(removed);
				}
			}
		}
	}

	/// `undo`: re-insert the marker, sorted.
	pub fn undo(&mut self) {
		// SAFETY: as `redo`.
		if let Some(list) = unsafe { get_mut::<TimelineMarkerList>(&self.marker_list) } {
			if let Some(marker) = self.removed.take() {
				list.add_marker(marker);
			}
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> crate::handle::CHandle {
		box_command(self)
	}
}

impl Command for MarkerRemoveCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `MarkerChangeColorCommand` (timelinemarker.h).
pub struct MarkerChangeColorCommand {
	/// Target list.
	marker_list: crate::handle::CHandle,
	/// Index of the marker to change.
	index: usize,
	/// Color before the change.
	old_color: i32,
	/// New color.
	new_color: i32,
}

impl MarkerChangeColorCommand {
	/// Construct from list + index + new color, capturing the current color
	/// as old.
	pub fn new(marker_list: crate::handle::CHandle, index: usize, new_color: i32) -> Self {
		// SAFETY: the boxed value is a `TimelineMarkerList` created by
		// `make_owned`; reading it here is the command's own handle.
		let old_color = unsafe { get::<TimelineMarkerList>(&marker_list) }
			.and_then(|l| l.at(index))
			.map(|m| m.color())
			.unwrap_or(0);
		Self {
			marker_list,
			index,
			old_color,
			new_color,
		}
	}

	/// `redo`: apply the new color.
	pub fn redo(&mut self) {
		// SAFETY: as `new`.
		if let Some(list) = unsafe { get_mut::<TimelineMarkerList>(&self.marker_list) } {
			if let Some(m) = list.at_mut(self.index) {
				m.set_color(self.new_color);
			}
		}
	}

	/// `undo`: restore the old color.
	pub fn undo(&mut self) {
		// SAFETY: as `new`.
		if let Some(list) = unsafe { get_mut::<TimelineMarkerList>(&self.marker_list) } {
			if let Some(m) = list.at_mut(self.index) {
				m.set_color(self.old_color);
			}
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> crate::handle::CHandle {
		box_command(self)
	}
}

impl Command for MarkerChangeColorCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `MarkerChangeNameCommand` (timelinemarker.h).
pub struct MarkerChangeNameCommand {
	/// Target list.
	marker_list: crate::handle::CHandle,
	/// Index of the marker to change.
	index: usize,
	/// Name before the change.
	old_name: String,
	/// New name.
	new_name: String,
}

impl MarkerChangeNameCommand {
	/// Construct from list + index + new name, capturing the current name as
	/// old.
	pub fn new(marker_list: crate::handle::CHandle, index: usize, name: &str) -> Self {
		// SAFETY: the boxed value is a `TimelineMarkerList` created by
		// `make_owned`; reading it here is the command's own handle.
		let old_name = unsafe { get::<TimelineMarkerList>(&marker_list) }
			.and_then(|l| l.at(index))
			.map(|m| m.name().to_string())
			.unwrap_or_default();
		Self {
			marker_list,
			index,
			old_name,
			new_name: name.to_string(),
		}
	}

	/// `redo`: apply the new name.
	pub fn redo(&mut self) {
		// SAFETY: as `new`.
		if let Some(list) = unsafe { get_mut::<TimelineMarkerList>(&self.marker_list) } {
			if let Some(m) = list.at_mut(self.index) {
				m.set_name(&self.new_name);
			}
		}
	}

	/// `undo`: restore the old name.
	pub fn undo(&mut self) {
		// SAFETY: as `new`.
		if let Some(list) = unsafe { get_mut::<TimelineMarkerList>(&self.marker_list) } {
			if let Some(m) = list.at_mut(self.index) {
				m.set_name(&self.old_name);
			}
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> crate::handle::CHandle {
		box_command(self)
	}
}

impl Command for MarkerChangeNameCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `MarkerChangeTimeCommand` (timelinemarker.h). The old range is captured at
/// construction when not supplied.
pub struct MarkerChangeTimeCommand {
	/// Target list.
	marker_list: crate::handle::CHandle,
	/// Index of the marker to change.
	index: usize,
	/// Time range before the change.
	old_time: TimeRange,
	/// New time range.
	new_time: TimeRange,
}

impl MarkerChangeTimeCommand {
	/// Construct from list + index + new time, capturing the current range as
	/// old.
	pub fn new(marker_list: crate::handle::CHandle, index: usize, time: TimeRange) -> Self {
		// SAFETY: the boxed value is a `TimelineMarkerList` created by
		// `make_owned`; reading it here is the command's own handle.
		let old_time = unsafe { get::<TimelineMarkerList>(&marker_list) }
			.and_then(|l| l.at(index))
			.map(|m| *m.time())
			.unwrap_or_else(|| TimeRange::new(Rational::new(0, 1), Rational::new(0, 1)));
		Self {
			marker_list,
			index,
			old_time,
			new_time: time,
		}
	}

	/// `redo`: apply the new time (resorts).
	pub fn redo(&mut self) {
		// SAFETY: as `new`.
		if let Some(list) = unsafe { get_mut::<TimelineMarkerList>(&self.marker_list) } {
			if let Some(m) = list.at_mut(self.index) {
				m.set_time(self.new_time);
			}
			list.resort_at(self.index);
		}
	}

	/// `undo`: restore the old time (resorts).
	pub fn undo(&mut self) {
		// SAFETY: as `new`.
		if let Some(list) = unsafe { get_mut::<TimelineMarkerList>(&self.marker_list) } {
			if let Some(m) = list.at_mut(self.index) {
				m.set_time(self.old_time);
			}
			list.resort_at(self.index);
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> crate::handle::CHandle {
		box_command(self)
	}
}

impl Command for MarkerChangeTimeCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

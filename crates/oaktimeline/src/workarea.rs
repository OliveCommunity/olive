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

//! Timeline work area (`src/timeline/src/timelineworkarea.h` +
//! `timelineundoworkarea.h`): the in/out range, its enabled flag, and the
//! `WorkareaSetEnabledCommand`/`WorkareaSetRangeCommand` undo commands.
//!
//! De-Qt: no QObject, no signals — change notifications are the facade's
//! job.

use oakcore_rs::{Rational, TimeRange};

/// The reset sentinel `k_reset_in` (timelineworkarea.h): 0/1. Exposed as a
/// function because `Rational::new` is not yet `const` in oakcore-rs.
pub fn reset_in() -> Rational {
	Rational::new(0, 1)
}
/// The reset sentinel `k_reset_out` (timelineworkarea.h): RATIONAL_MAX,
/// i.e. 2147483647/1.
pub fn reset_out() -> Rational {
	Rational::new(2147483647, 1)
}

/// `TimelineWorkArea` — the in/out range on a timeline (timelineworkarea.h).
pub struct TimelineWorkArea {
	/// Whether the work area is enabled.
	workarea_enabled_: bool,
	/// The in/out range.
	workarea_range_: TimeRange,
}

impl TimelineWorkArea {
	/// A new, disabled work area at `k_reset_in`..`k_reset_out`.
	pub fn new() -> Self {
		Self {
			workarea_enabled_: false,
			workarea_range_: TimeRange::new(reset_in(), reset_out()),
		}
	}

	/// Whether the work area is enabled.
	pub fn enabled(&self) -> bool {
		self.workarea_enabled_
	}

	/// Set enabled.
	pub fn set_enabled(&mut self, e: bool) {
		self.workarea_enabled_ = e;
	}

	/// The in point.
	pub fn in_(&self) -> Rational {
		self.workarea_range_.in_()
	}

	/// The out point.
	pub fn out(&self) -> Rational {
		self.workarea_range_.out()
	}

	/// The range length.
	pub fn length(&self) -> Rational {
		self.workarea_range_.length()
	}

	/// The full range.
	pub fn range(&self) -> &TimeRange {
		&self.workarea_range_
	}

	/// Set the range.
	pub fn set_range(&mut self, range: TimeRange) {
		self.workarea_range_ = range;
	}
}

/// `WorkareaSetEnabledCommand` (timelineundoworkarea.h).
pub struct WorkareaSetEnabledCommand {
	/// Target work area handle.
	points: crate::handle::CHandle,
	/// New enabled flag.
	new_enabled: bool,
	/// Enabled flag captured at construction, restored by `undo`.
	old_enabled: bool,
}

impl WorkareaSetEnabledCommand {
	/// Construct from work area + new enabled value (captures old at ctor).
	pub fn new(points: crate::handle::CHandle, enabled: bool) -> Self {
		let old_enabled = unsafe { crate::handle::get::<TimelineWorkArea>(&points) }
			.map(|wa| wa.enabled())
			.unwrap_or(false);
		Self {
			points,
			new_enabled: enabled,
			old_enabled,
		}
	}

	/// `redo`: set enabled.
	pub fn redo(&mut self) {
		if let Some(wa) = unsafe { crate::handle::get_mut::<TimelineWorkArea>(&self.points) } {
			wa.set_enabled(self.new_enabled);
		}
	}

	/// `undo`: restore old enabled.
	pub fn undo(&mut self) {
		if let Some(wa) = unsafe { crate::handle::get_mut::<TimelineWorkArea>(&self.points) } {
			wa.set_enabled(self.old_enabled);
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> crate::handle::CHandle {
		crate::undocommon::box_command(self)
	}
}

impl crate::undocommon::Command for WorkareaSetEnabledCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `WorkareaSetRangeCommand` (timelineundoworkarea.h). The old range is
/// captured at construction when not supplied.
pub struct WorkareaSetRangeCommand {
	/// Target work area handle.
	workarea: crate::handle::CHandle,
	/// New range.
	new_range: TimeRange,
	/// Range captured at construction, restored by `undo`.
	old_range: TimeRange,
}

impl WorkareaSetRangeCommand {
	/// Construct from work area + new range (captures current as old).
	pub fn new(workarea: crate::handle::CHandle, range: TimeRange) -> Self {
		let old_range = unsafe { crate::handle::get::<TimelineWorkArea>(&workarea) }
			.map(|wa| *wa.range())
			.unwrap_or(range);
		Self::new_with_old(workarea, range, old_range)
	}

	/// Construct from work area + new range + explicitly supplied old
	/// range (used by the FFI layer, which may have a previously captured
	/// range; `new` delegates here with the current range).
	pub fn new_with_old(
		workarea: crate::handle::CHandle,
		range: TimeRange,
		old_range: TimeRange,
	) -> Self {
		Self {
			workarea,
			new_range: range,
			old_range,
		}
	}

	/// `redo`: set the range.
	pub fn redo(&mut self) {
		if let Some(wa) = unsafe { crate::handle::get_mut::<TimelineWorkArea>(&self.workarea) } {
			wa.set_range(self.new_range);
		}
	}

	/// `undo`: restore the old range.
	pub fn undo(&mut self) {
		if let Some(wa) = unsafe { crate::handle::get_mut::<TimelineWorkArea>(&self.workarea) } {
			wa.set_range(self.old_range);
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> crate::handle::CHandle {
		crate::undocommon::box_command(self)
	}
}

impl crate::undocommon::Command for WorkareaSetRangeCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

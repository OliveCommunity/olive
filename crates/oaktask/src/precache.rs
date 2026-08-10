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

//! `PreCacheTask`, mirroring `src/task/src/precache/precachetask.h`.
//!
//! Renders a footage node (or the whole sequence) through
//! [`crate::render::RenderTask`] to fill the playback cache. Owns a deep copy
//! of the project (`OakNodeProject`) and borrows the source footage
//! (`OakNodeFootage`).
//!
//! **Simplifications over the C++**: the deep project copy / viewer wiring
//! is not built (the render drives the given viewer directly) and the
//! timeline work-area intersection is replaced by the full footage length.
//!
//! CPP-PARITY: src/task/src/precache/precachetask.h

use crate::bridge;
use crate::error::Result;
use crate::handle::CHandle;
use crate::render::{RenderTask, RenderTaskBehavior};
use crate::task::{Task, TaskBehavior};
use oakcore_rs::{Rational, TimeRange};

/// A pre-cache task: renders frames and audio of a footage node into the
/// playback cache without any output file.
pub struct PreCacheTask {
	/// The render base (itself a task).
	pub render: RenderTask,
	/// Owning deep copy of the project (borrowed `OakNodeProject`).
	pub project: CHandle,
	/// Borrowed source footage (borrowed `OakNodeFootage`).
	pub footage: CHandle,
	/// Frame index within the footage being cached.
	pub index: i32,
	/// The sequence node (borrowed `OakNodeSequence`).
	pub sequence: CHandle,
}

impl PreCacheTask {
	/// Create a pre-cache task for the given footage at `index` inside
	/// `sequence`.
	pub fn new(footage: CHandle, index: i32, sequence: CHandle) -> PreCacheTask {
		// Video params from the sequence (empty when unavailable).
		let mut video_params = CHandle::null();
		unsafe {
			bridge::node::oaknode_sequence_get_video_params(sequence, 0, &mut video_params);
		}

		let filename = footage_filename(footage);
		let title = format!("Pre-caching {filename}:{index}");
		let base = Task::new(&title, CHandle::null());
		let render = RenderTask::new(base, video_params, CHandle::null(), sequence, Default::default(), None);

		// A scratch project for the render color manager (simplified).
		let project = unsafe { bridge::node::oaknode_project_init() };

		PreCacheTask {
			render,
			project,
			footage,
			index,
			sequence,
		}
	}
}

impl TaskBehavior for PreCacheTask {
	fn run(&mut self, task: &mut Task) -> Result<()> {
		// Share the caller's cancellation atom with the inner render base.
		self.render.base.set_cancel_atom(task.get_cancel_atom());

		// The full footage length is the cache range (simplified: no
		// work-area intersection).
		let mut len_num = 0i64;
		let mut len_den = 1i64;
		unsafe {
			bridge::node::oaknode_footage_get_video_length(self.footage, &mut len_num, &mut len_den);
		}
		let range = TimeRange::new(Rational::new(0, 1), Rational::new(len_num, len_den));

		let mut color_manager = unsafe { bridge::node::oaknode_colormanager_init(self.project) };
		let mut cache = bridge::render::OakRenderCache::null();
		unsafe {
			bridge::node::oaknode_node_get_video_frame_cache(self.sequence, &mut cache);
		}

		self.render.set_render_inputs(color_manager, cache, 0 /* k_online */, false, range);

		// Drive the render with `self` as the subclass behavior (the C++
		// virtual dispatch receiver); the render is temporarily moved out to
		// avoid a self-referential borrow and put back before the handles are
		// released below.
		let mut render = std::mem::replace(&mut self.render, crate::render::RenderTask::placeholder());
		let result = render.render(task, self);
		self.render = render;

		if !cache.ctx.is_null() {
			unsafe {
				bridge::render::oakrender_cache_free(&mut cache);
			}
		}
		if !color_manager.ctx.is_null() {
			unsafe {
				bridge::node::oaknode_colormanager_free(&mut color_manager);
			}
		}

		result
	}
}

impl RenderTaskBehavior for PreCacheTask {
	fn frame_downloaded(&mut self, task: &mut Task, frame: CHandle) -> Result<()> {
		// Do nothing: pre-cache just fills the frame cache.
		let _ = (task, frame);
		Ok(())
	}

	fn audio_downloaded(&mut self, task: &mut Task, buffer: CHandle) -> Result<()> {
		// Pre-cache doesn't cache any audio.
		let _ = (task, buffer);
		Ok(())
	}

	fn encode_subtitle(&mut self, task: &mut Task, text: &str) -> Result<()> {
		let _ = (task, text);
		Ok(())
	}
}

/// Two-stage read of the footage filename.
fn footage_filename(footage: CHandle) -> String {
	let needed = unsafe { bridge::node::oaknode_footage_filename(footage, std::ptr::null_mut(), 0) };
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0i8; needed as usize];
	unsafe {
		bridge::node::oaknode_footage_filename(footage, buf.as_mut_ptr(), needed);
	}
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	unsafe { String::from_utf8_lossy(std::slice::from_raw_parts(buf.as_ptr() as *const u8, len)).into_owned() }
}

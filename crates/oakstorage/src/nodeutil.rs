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

//! oaknode project/serializer helpers (direct Rust calls into the
//! oaknode crate, single-lib unification; the former `bridge/node.rs`).
//!
//! The ove-xml/otio backends need oaknode's project + serializer
//! families. Graph (de)serialization stays oaknode's own serializer
//! (`load`/`save`); these helpers box the resulting `Arc<Mutex<Project>>`
//! into the canonical handle form the storage session API moves around.

use std::sync::{Arc, Mutex};

use crate::error::Result;
use crate::handle::CHandle;

/// The project payload boxed by project handles.
pub type ProjectArc = Arc<Mutex<oaknode::project::Project>>;

/// Box an existing project as an owned handle (refcount 1).
pub fn make_project_owned(project: ProjectArc) -> CHandle {
	oaknode::handle::make_owned(project)
}

/// Read the boxed project of a project handle.
///
/// # Safety
/// The handle must box `Arc<Mutex<Project>>` (created by this module or
/// oaknode's project helpers).
pub unsafe fn project_arc(h: &CHandle) -> Result<ProjectArc> {
	unsafe { oaknode::handle::get::<ProjectArc>(h) }
		.cloned()
		.ok_or(crate::error::Error::Invalid)
}

/// Load a project from XML text via the oaknode serializer.
pub fn serializer_load(xml: &str) -> Result<ProjectArc> {
	oaknode::serializer::load(xml).map_err(|e| crate::error::Error::Format(e.to_string()))
}

/// Serialize a project to XML text via the oaknode serializer.
pub fn serializer_save(project: &oaknode::project::Project) -> Result<String> {
	oaknode::serializer::save(project).map_err(|e| crate::error::Error::Format(e.to_string()))
}

/// Create a sequence node with its default track lists (video, audio,
/// subtitle) in `graph`. Returns `(sequence_id, [video, audio,
/// subtitle] list ids)`.
///
/// Mirrors the private `append_default_track_lists` wiring of the former
/// `oaknode_sequence_create` export so the otio backend can import
/// timelines into real projects.
pub fn create_sequence(
	graph: &mut oaknode::graph::Graph,
) -> (oaknode::id::NodeId, Vec<oaknode::id::NodeId>) {
	use oaknode::input::Input;
	use oaknode::node::NodeCore;
	use oaknode::sequence::SequenceBehavior;
	use oaknode::track::{TrackListBehavior, TrackType};
	use oaknode::value::{NodeValue, ValueType};

	let mut seq = SequenceBehavior::new();
	seq.set_default_parameters();
	let mut core = NodeCore::new();
	core.add_input(Input::new(
		oaknode::sequence::TEXTURE_INPUT,
		ValueType::Texture,
		NodeValue::None,
	));
	core.add_input(Input::new(
		oaknode::sequence::SAMPLES_INPUT,
		ValueType::Samples,
		NodeValue::None,
	));
	// One array input per track list (`track_in_%1`; the video list owns
	// track_in_0, audio track_in_1, subtitle track_in_2 — the C++
	// `Sequence::k_track_input_format` convention).
	for base in 0..3 {
		let mut track_input = Input::new(
			&oaknode::sequence::TRACK_INPUT_FORMAT.replace("%1", &base.to_string()),
			ValueType::None,
			NodeValue::None,
		);
		track_input.flags |= oaknode::input::flags::ARRAY;
		core.add_input(track_input);
	}
	let seq_id = graph.add_node(core, Box::new(seq));

	let mut lists = Vec::new();
	let mut base = 0i32;
	for kind in [TrackType::Video, TrackType::Audio, TrackType::Subtitle] {
		let mut behavior = TrackListBehavior::new(kind);
		behavior.sequence = Some(seq_id);
		behavior.array_base = base;
		let id = graph.add_node(NodeCore::new(), Box::new(behavior));
		lists.push(id);
		base += 1;
	}
	// Record the lists on the sequence behavior.
	if let Some(entry) = graph.get_mut(seq_id) {
		if let Some(s) = entry
			.behavior
			.as_any_mut()
			.and_then(|a| a.downcast_mut::<SequenceBehavior>())
		{
			s.track_lists = lists.clone();
		}
	}
	(seq_id, lists)
}

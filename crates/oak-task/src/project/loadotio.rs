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

//! `LoadOTIOTask`, mirroring `src/task/src/project/loadotio/loadotio.h`.
//!
//! Loads an OpenTimelineIO (`.otio`) or FCPXML (`.fcpxml`) file into a new
//! project, with a configurable import-confirmation callback. The format
//! is dispatched from the filename extension (see
//! [`crate::project::format`]); the document is parsed with the pure-Rust
//! `oakotio` binding (see `README` decision #6).
//!
//! **Single-lib note**: the project is built through the direct oaknode
//! domain operations in [`crate::nodeops`] (`Project::new()` +
//! `Project::initialize()`, factory-created sequence/folder/footage/
//! block/track nodes, graph connections) instead of the deleted oaknode /
//! oaktimeline C ABIs. Track creation uses the task-local
//! [`crate::nodeops::add_track_command`] (see its docs for the
//! oaktimeline-migration note). The loaded project is an
//! `Arc<Mutex<oak_node::project::Project>>` stored on the base task
//! (`take_project()`).
//!
//! CPP-PARITY: src/task/src/project/loadotio/loadotio.cpp

use std::collections::HashMap;
use std::path::Path;
use std::sync::Mutex;

use oak_node::id::NodeId;
use oak_node::track::TrackType;
use oak_otio::Serializable;

use crate::error::{Error, Result};
use crate::nodeops::{self, NodeRef, ProjectRef};
use crate::project::format::InterchangeFormat;
use crate::project::load::ProjectLoadBaseTask;
use crate::task::{Task, TaskBehavior};

/// Callback used to confirm whether the sequences of an OTIO document should
/// be imported, mirroring `ImportConfirmFn` in loadotio.h (receives the
/// sequence labels in order; return true to accept).
pub type ImportConfirmFn = Box<dyn FnMut(&[String]) -> bool + Send>;

/// Global import-confirmation callback, mirroring the C++ static
/// `LoadOTIOTask::confirm_callback_` (loadotio.h). When unset, everything is
/// imported (the headless default).
static CONFIRM_CALLBACK: Mutex<Option<ImportConfirmFn>> = Mutex::new(None);

/// Register (or clear, with `None`) the global import-confirmation callback.
///
/// CPP-PARITY: src/task/src/project/loadotio/loadotio.h
///   (`LoadOTIOTask::set_import_confirm_callback`)
pub fn set_import_confirm_callback(cb: Option<ImportConfirmFn>) {
	*CONFIRM_CALLBACK.lock().unwrap() = cb;
}

/// An OTIO project loader.
pub struct LoadOTIOTask {
	/// The base load task (owns the filename and yields the project).
	pub base: ProjectLoadBaseTask,
}

impl LoadOTIOTask {
	/// Create an OTIO loader for the given base load task.
	pub fn new(base: ProjectLoadBaseTask) -> LoadOTIOTask {
		LoadOTIOTask { base }
	}
}

impl TaskBehavior for LoadOTIOTask {
	/// Load and parse the document (`oakotio`), then convert it into the
	/// project stored on the base task.
	fn run(&mut self, task: &mut Task) -> Result<()> {
		// Dispatch the interchange format from the filename extension.
		// Format handling ends here: `parse_timelines` returns one
		// `oak_otio::Timeline` per sequence whatever the source format, so
		// the project building below is shared between `.otio` and
		// `.fcpxml` (C++ parity for the track/clip/footage code).
		let format = match InterchangeFormat::of(&self.base.filename) {
			Ok(f) => f,
			Err(e) => {
				if let Error::Failed(msg) = &e {
					task.set_error(msg);
				}
				return Err(e);
			}
		};

		let timelines = parse_timelines(task, &self.base.filename, format)?;

		// Build the project directly through the oaknode domain model
		// (the deleted `oaknode_project_init` stub is gone).
		let project: ProjectRef = oak_node::project::Project::new();
		{
			let mut guard = project.lock().unwrap_or_else(|e| e.into_inner());
			if let Err(e) = guard.initialize() {
				let _ = e;
				task.set_error("Failed to create project");
				return Err(Error::Failed("Failed to create project".to_string()));
			}
			guard.set_modified(true);
		}

		// Keep track of imported footage
		let mut imported_footage: HashMap<String, NodeRef> = HashMap::new();

		// Generate a list of sequences with the same names as the timelines.
		// Assumes each timeline has a unique name.
		let mut unnamed_sequence_count = 0;
		let mut sequences: Vec<NodeRef> = Vec::new();

		// Variables used for loading bar
		let mut number_of_clips: f64 = 0.0;

		for timeline in &timelines {
			let Some(sequence) = nodeops::sequence_create(&project) else {
				continue;
			};

			let label = if !timeline.name().is_empty() {
				timeline.name().to_string()
			} else {
				// If the otio timeline does not provide a name, create a
				// default one here.
				unnamed_sequence_count += 1;
				format!("Sequence {unnamed_sequence_count}")
			};
			nodeops::set_node_label(&project, sequence, &label);

			// Get number of clips for loading bar
			for track in timeline.tracks().children() {
				if let Some(otio_track) = track.as_track() {
					number_of_clips += otio_track.children().len() as f64;
				}
			}

			sequences.push((project.clone(), sequence));
		}
		if number_of_clips <= 0.0 {
			number_of_clips = 1.0;
		}

		// Ask the user which sequences to import (facade callback; headless
		// default accepts everything).
		let sequence_names: Vec<String> = sequences
			.iter()
			.map(|(p, s)| nodeops::node_label(p, *s))
			.collect();
		let mut confirm = CONFIRM_CALLBACK.lock().unwrap().take();
		let accepted = match confirm.as_mut() {
			Some(cb) => cb(&sequence_names),
			None => true,
		};

		if !accepted {
			// Cancel to indicate to caller that this task did not complete
			// and to simply dispose of it. The project is never handed to the
			// base task (the C++ base-task destructor does the same).
			task.cancel();
			return Ok(());
		}

		let root_folder = {
			let guard = project.lock().unwrap_or_else(|e| e.into_inner());
			guard.root
		};
		let mut clips_done = 0.0f64;

		for (timeline, (_, sequence)) in timelines.iter().zip(&sequences) {
			let sequence_node = *sequence;

			let mut add_seq = nodeops::folder_add_child_command(
				(project.clone(), root_folder),
				(project.clone(), sequence_node),
			);
			add_seq.redo_now();

			// Create a folder for this sequence's footage
			let sequence_footage = nodeops::folder_create(&project);
			if let Some(folder_id) = sequence_footage {
				nodeops::set_node_label(&project, folder_id, timeline.name());
				let mut add_folder = nodeops::folder_add_child_command(
					(project.clone(), root_folder),
					(project.clone(), folder_id),
				);
				add_folder.redo_now();
			}

			// Iterate through tracks
			for track_composable in timeline.tracks().children() {
				let Some(otio_track) = track_composable.as_track() else {
					continue;
				};

				// Determine what kind of track it is
				let track_type = match otio_track.kind() {
					"Video" => TrackType::Video,
					"Audio" => TrackType::Audio,
					other => {
						eprintln!("Found unknown track type: {other}");
						continue;
					}
				};

				// Create a new track
				let Some(track_list) =
					nodeops::sequence_track_list(&project, sequence_node, track_type)
				else {
					continue;
				};
				let mut add_track =
					nodeops::add_track_command(project.clone(), track_list);
				add_track.redo_now();

				let track_count = nodeops::tracklist_track_count(&project, track_list);
				let track = if track_count > 0 {
					nodeops::tracklist_track_at(&project, track_list, track_count - 1)
				} else {
					None
				};
				let Some(track) = track else {
					continue;
				};

				// Get clips from track
				let mut previous_block: Option<NodeId> = None;
				let mut prev_block_transition = false;

				for otio_block in otio_track.children() {
					if task.is_cancelled() {
						break;
					}

					let block_kind = match otio_block.schema_name() {
						"Clip" => nodeops::BlockKind::Clip,
						"Gap" => nodeops::BlockKind::Gap,
						"Transition" => {
							// Todo: Look into OTIO supported transitions and add
							// them to Oak.
							nodeops::BlockKind::Transition
						}
						other => {
							// We don't know what this is yet, just create a gap
							// for now so that *something* is there.
							eprintln!("Found unknown block type: {other}");
							nodeops::BlockKind::Gap
						}
					};
					let Some(block) = nodeops::block_create(&project, block_kind) else {
						continue;
					};
					let block_node = block;
					nodeops::set_node_label(&project, block_node, otio_block.name());
					nodeops::track_append_block(&project, track, block_node);

					if otio_block.schema_name() == "Clip" || otio_block.schema_name() == "Gap" {
						if let Some(source_range) = otio_block.source_range() {
							let start_seconds = source_range.start_time().to_seconds();
							let duration_seconds = source_range.duration().to_seconds();

							let start_time = oak_core::Rational::from_double(start_seconds);
							let duration = oak_core::Rational::from_double(duration_seconds);

							if otio_block.schema_name() == "Clip" {
								nodeops::clip_set_media_in(
									&project,
									block_node,
									start_time.numerator(),
									start_time.denominator(),
								);
							}
							nodeops::block_set_length_and_media_out(
								&project,
								block_node,
								duration.numerator(),
								duration.denominator(),
							);
						}
					}

					// If the previous block was a transition, connect the
					// current block to it.
					if prev_block_transition {
						if let Some(prev) = previous_block {
							nodeops::node_connect(
								&project,
								block_node,
								prev,
								nodeops::TRANSITION_IN_BLOCK_INPUT,
							);
						}
						prev_block_transition = false;
					}

					if let Some(otio_transition) = otio_block.as_transition() {
						// Set how far the transition eats into the previous
						// clip.
						let in_offset = otio_transition.in_offset().to_rational();
						let out_offset = otio_transition.out_offset().to_rational();
						nodeops::transition_set_offsets_and_length(
							&project,
							block_node,
							in_offset.numerator(),
							in_offset.denominator(),
							out_offset.numerator(),
							out_offset.denominator(),
						);

						if let Some(prev) = previous_block {
							nodeops::node_connect(
								&project,
								prev,
								block_node,
								nodeops::TRANSITION_OUT_BLOCK_INPUT,
							);
						}
						prev_block_transition = true;

						// Position transition in its own context.
						set_own_context_position(&project, block_node);
					}

					if otio_block.schema_name() == "Gap" {
						// Position gap in its own context.
						set_own_context_position(&project, block_node);
					}

					// Update this after it's used but before any continue
					// statements.
					previous_block = Some(block_node);

					if otio_block.schema_name() == "Clip" {
						let Some(otio_clip) = otio_block.as_clip() else {
							continue;
						};
						let Some(media_reference) = otio_clip.media_reference() else {
							continue;
						};
						if let Some(external) = media_reference.as_external_reference() {
							// Link footage
							let footage_url = external.target_url().to_string();

							let probed_item: Option<NodeRef> =
								if let Some(existing) = imported_footage.get(&footage_url) {
									Some(existing.clone())
								} else {
									let created =
										nodeops::footage_create(&project, Some(&footage_url));
									if let Some(created) = created {
										imported_footage.insert(
											footage_url.clone(),
											(project.clone(), created),
										);

										let label = Path::new(&footage_url)
											.file_name()
											.map(|n| n.to_string_lossy().into_owned())
											.unwrap_or_default();
										nodeops::set_node_label(&project, created, &label);

										if let Some(folder_id) = sequence_footage {
											let mut add_footage =
												nodeops::folder_add_child_command(
													(project.clone(), folder_id),
													(project.clone(), created),
												);
											add_footage.redo_now();
										}
									}
									created.map(|id| (project.clone(), id))
								};

							if let Some((_, probed_id)) = probed_item {
								// Position clip in its own context.
								set_own_context_position(&project, block_node);

								// Position footage in its context.
								nodeops::node_set_context_position(
									&project, block_node, probed_id, -2.0, 0.0, false,
								);

								// Record the clip-footage link in the domain
								// model (the C++ finds footage through the
								// input chain; the Rust clip records it).
								nodeops::clip_set_footage(&project, block_node, probed_id);

								if track_type == TrackType::Video {
									if let Some(transform) = factory_create(
										&project,
										nodeops::TRANSFORM_TYPE_ID,
									) {
										nodeops::node_connect(
											&project,
											probed_id,
											transform,
											"tex_in",
										);
										nodeops::node_connect(
											&project,
											transform,
											block_node,
											nodeops::CLIP_TEXTURE_INPUT,
										);
										nodeops::node_set_context_position(
											&project, block_node, transform, -1.0, 0.0, false,
										);
									}
								} else {
									if let Some(volume) =
										factory_create(&project, nodeops::VOLUME_TYPE_ID)
									{
										nodeops::node_connect(
											&project,
											probed_id,
											volume,
											"samples_in",
										);
										nodeops::node_connect(
											&project,
											volume,
											block_node,
											nodeops::CLIP_TEXTURE_INPUT,
										);
										nodeops::node_set_context_position(
											&project, block_node, volume, -1.0, 0.0, false,
										);
									}
								}
							}
						}
					}

					clips_done += 1.0;
					task.emit_progress(clips_done / number_of_clips);
				}
			}
		}

		self.base.store_project(project);
		Ok(())
	}
}

/// Create a node from the factory registry (`oaknode_factory_create_from_id`).
fn factory_create(project: &ProjectRef, type_id: &str) -> Option<NodeId> {
	let meta = oak_node::factory::Factory::global().find(type_id)?;
	let (core, behavior) = (meta.create)();
	let mut guard = project.lock().unwrap_or_else(|e| e.into_inner());
	Some(guard.graph.add_node(core, behavior))
}

/// Set the node's position in its own context (the C++
/// `set_own_context_position` helper in loadotio.cpp).
fn set_own_context_position(project: &ProjectRef, node: NodeId) {
	nodeops::node_set_context_position(project, node, node, 0.0, 0.0, false);
}

/// Parse the document into one `oak_otio::Timeline` per sequence, dispatching
/// on the filename extension:
///
/// - `.otio`: OpenTimelineIO JSON. A `Timeline` root yields one timeline, a
///   `SerializableCollection` yields one timeline per `Timeline` child, and
///   any other root is the C++ "Unknown OpenTimelineIO root element" error.
/// - `.fcpxml`: FCPXML (`oak_otio::fcpxml`), one timeline per `<sequence>`.
///
/// Format handling ends here — the caller builds the project from the
/// returned timelines regardless of the source format (C++ parity), so the
/// track/clip/footage code never forks.
fn parse_timelines(
	task: &mut Task,
	filename: &str,
	format: InterchangeFormat,
) -> Result<Vec<oak_otio::Timeline>> {
	match format {
		InterchangeFormat::OtioJson => {
			let root = oak_otio::from_json_file(filename).map_err(|e| {
				task.set_error(&format!(
					"Failed to load OpenTimelineIO from file \"{}\" \n\nOpenTimelineIO Error:\n\n{}",
					filename, e
				));
				Error::Failed("Failed to load OpenTimelineIO".to_string())
			})?;
			match &root {
				Serializable::SerializableCollection(collection) => Ok(collection
					.children()
					.iter()
					.filter_map(|child| child.as_timeline().cloned())
					.collect()),
				Serializable::Timeline(timeline) => Ok(vec![timeline.clone()]),
				_ => {
					task.set_error("Unknown OpenTimelineIO root element");
					Err(Error::Failed(
						"Unknown OpenTimelineIO root element".to_string(),
					))
				}
			}
		}
		InterchangeFormat::Fcpxml => oak_otio::from_fcpxml_file(filename).map_err(|e| {
			task.set_error(&format!(
				"Failed to load FCPXML from file \"{}\" \n\nFCPXML Error:\n\n{}",
				filename, e
			));
			Error::Failed("Failed to load FCPXML".to_string())
		}),
	}
}

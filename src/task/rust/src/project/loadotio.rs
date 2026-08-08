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
//! Loads an OpenTimelineIO file into a new `OakNodeProject`, with a
//! configurable import-confirmation callback. The OTIO document is parsed
//! with the pure-Rust `oakotio` binding (see `README` decision #6); the
//! project is built through the oaknode / oaktimeline C ABIs exactly like
//! the C++ task, so no OTIO type crosses the oaktask C ABI.
//!
//! CPP-PARITY: src/task/src/project/loadotio/loadotio.cpp

use std::collections::HashMap;
use std::path::Path;
use std::sync::Mutex;

use oakotio::Serializable;

use crate::bridge;
use crate::error::{Error, Result};
use crate::ffi::taskhandle::cstr;
use crate::handle::CHandle;
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
	/// Load and parse the OTIO file (`oakotio`), then convert it into the
	/// project stored on the base task.
	fn run(&mut self, task: &mut Task) -> Result<()> {
		let root = oakotio::from_json_file(&self.base.filename).map_err(|e| {
			task.set_error(&format!(
				"Failed to load OpenTimelineIO from file \"{}\" \n\nOpenTimelineIO Error:\n\n{}",
				self.base.filename, e
			));
			Error::Failed("Failed to load OpenTimelineIO".to_string())
		})?;

		let mut project = unsafe { bridge::node::oaknode_project_init() };
		if project.ctx.is_null() {
			task.set_error("Failed to create project");
			return Err(Error::Failed("Failed to create project".to_string()));
		}
		unsafe {
			bridge::node::oaknode_project_initialize(project);
			bridge::node::oaknode_project_set_modified(project, 1);
		}

		// Collect the timelines: a SerializableCollection holds several,
		// a Timeline root holds one, anything else is unsupported.
		let timelines: Vec<oakotio::Timeline> = match &root {
			Serializable::SerializableCollection(collection) => collection
				.children()
				.iter()
				.filter_map(|child| child.as_timeline().cloned())
				.collect(),
			Serializable::Timeline(timeline) => vec![timeline.clone()],
			_ => {
				task.set_error("Unknown OpenTimelineIO root element");
				if !project.ctx.is_null() {
					unsafe {
						bridge::node::oaknode_project_free(&mut project);
					}
				}
				return Err(Error::Failed("Unknown OpenTimelineIO root element".to_string()));
			}
		};

		// Keep track of imported footage
		let mut imported_footage: HashMap<String, CHandle> = HashMap::new();

		// Generate a list of sequences with the same names as the timelines.
		// Assumes each timeline has a unique name.
		let mut unnamed_sequence_count = 0;
		let mut sequences: Vec<CHandle> = Vec::new();

		// Variables used for loading bar
		let mut number_of_clips: f64 = 0.0;

		for timeline in &timelines {
			let sequence = unsafe { bridge::node::oaknode_sequence_create() };
			if sequence.ctx.is_null() {
				continue;
			}

			let label = if !timeline.name().is_empty() {
				timeline.name().to_string()
			} else {
				// If the otio timeline does not provide a name, create a
				// default one here.
				unnamed_sequence_count += 1;
				format!("Sequence {unnamed_sequence_count}")
			};
			unsafe {
				bridge::node::oaknode_node_set_label(bridge::node::oaknode_sequence_as_node(sequence), cstr(&label));
			}

			// Set default params incase they aren't edited.
			unsafe {
				bridge::node::oaknode_sequence_set_default_parameters(sequence);
			}

			// Get number of clips for loading bar
			for track in timeline.tracks().children() {
				if let Some(otio_track) = track.as_track() {
					number_of_clips += otio_track.children().len() as f64;
				}
			}

			sequences.push(sequence);
		}
		if number_of_clips <= 0.0 {
			number_of_clips = 1.0;
		}

		// Ask the user which sequences to import (facade callback; headless
		// default accepts everything).
		let sequence_names: Vec<String> = sequences
			.iter()
			.map(|s| node_label_of(unsafe { bridge::node::oaknode_sequence_as_node(*s) }))
			.collect();
		let mut confirm = CONFIRM_CALLBACK.lock().unwrap().take();
		let accepted = match confirm.as_mut() {
			Some(cb) => cb(&sequence_names),
			None => true,
		};

		if !accepted {
			// Cancel to indicate to caller that this task did not complete
			// and to simply dispose of it. The project is never handed to the
			// base task, so free it here (the C++ base-task destructor does
			// the same).
			task.cancel();
			for sequence in &mut sequences {
				unsafe {
					bridge::node::oaknode_sequence_free(sequence);
				}
			}
			if !project.ctx.is_null() {
				unsafe {
					bridge::node::oaknode_project_free(&mut project);
				}
			}
			return Ok(());
		}

		let root_folder = unsafe { bridge::node::oaknode_project_root(project) };
		let mut clips_done = 0.0f64;

		for (timeline, sequence) in timelines.iter().zip(&sequences) {
			let sequence_node = unsafe { bridge::node::oaknode_sequence_as_node(*sequence) };

			unsafe {
				bridge::node::oaknode_project_add_node(project, sequence_node);
			}
			let mut add_seq = unsafe { bridge::node::oaknode_command_create_folder_add_child(root_folder, sequence_node) };
			if !add_seq.ctx.is_null() {
				unsafe {
					bridge::undo::oakundo_command_redo_now(add_seq);
					bridge::undo::oakundo_command_free(&mut add_seq);
				}
			}

			// Create a folder for this sequence's footage
			let sequence_footage = unsafe { bridge::node::oaknode_folder_create(project) };
			if !sequence_footage.ctx.is_null() {
				unsafe {
					bridge::node::oaknode_node_set_label(
						bridge::node::oaknode_folder_as_node(sequence_footage),
						cstr(timeline.name()),
					);
				}
				let mut add_folder = unsafe {
					bridge::node::oaknode_command_create_folder_add_child(
						root_folder,
						bridge::node::oaknode_folder_as_node(sequence_footage),
					)
				};
				if !add_folder.ctx.is_null() {
					unsafe {
						bridge::undo::oakundo_command_redo_now(add_folder);
						bridge::undo::oakundo_command_free(&mut add_folder);
					}
				}
			}

			// Iterate through tracks
			for track_composable in timeline.tracks().children() {
				let Some(otio_track) = track_composable.as_track() else {
					continue;
				};

				// Determine what kind of track it is
				let track_type = match otio_track.kind() {
					"Video" => bridge::node::OAKNODE_TRACK_TYPE_VIDEO,
					"Audio" => bridge::node::OAKNODE_TRACK_TYPE_AUDIO,
					other => {
						eprintln!("Found unknown track type: {other}");
						continue;
					}
				};

				// Create a new track
				let mut track_list = CHandle::null();
				unsafe {
					bridge::node::oaknode_sequence_get_track_list(*sequence, track_type, &mut track_list);
				}
				let mut add_track = unsafe { bridge::timeline::oaktimeline_add_track_command(track_list) };
				if !add_track.ctx.is_null() {
					unsafe {
						bridge::undo::oakundo_command_redo_now(add_track);
						bridge::undo::oakundo_command_free(&mut add_track);
					}
				}

				let mut track = CHandle::null();
				let mut count = 0;
				unsafe {
					bridge::node::oaknode_tracklist_get_track_count(track_list, &mut count);
				}
				if count > 0 {
					unsafe {
						bridge::node::oaknode_tracklist_get_track_at(track_list, count - 1, &mut track);
					}
				}
				if track.ctx.is_null() {
					continue;
				}

				// Get clips from track
				let mut previous_block = CHandle::null();
				let mut prev_block_transition = false;

				for otio_block in otio_track.children() {
					if task.is_cancelled() {
						break;
					}

					let block = match otio_block.schema_name() {
						"Clip" => unsafe { bridge::node::oaknode_block_clip_create() },
						"Gap" => unsafe { bridge::node::oaknode_block_gap_create() },
						"Transition" => {
							// Todo: Look into OTIO supported transitions and add
							// them to Oak.
							unsafe {
								bridge::node::oaknode_block_transition_create(bridge::node::OAKNODE_TRANSITION_CROSS_DISSOLVE)
							}
						}
						other => {
							// We don't know what this is yet, just create a gap
							// for now so that *something* is there.
							eprintln!("Found unknown block type: {other}");
							unsafe { bridge::node::oaknode_block_gap_create() }
						}
					};
					if block.ctx.is_null() {
						continue;
					}

					let block_node = unsafe { bridge::node::oaknode_block_as_node(block) };
					unsafe {
						bridge::node::oaknode_project_add_node(project, block_node);
						bridge::node::oaknode_node_set_label(block_node, cstr(otio_block.name()));
						bridge::node::oaknode_track_append_block(track, block);
					}

					if otio_block.schema_name() == "Clip" || otio_block.schema_name() == "Gap" {
						if let Some(source_range) = otio_block.source_range() {
							let start_seconds = source_range.start_time().to_seconds();
							let duration_seconds = source_range.duration().to_seconds();

							let start_time = oakcore_rs::Rational::from_double(start_seconds);
							let duration = oakcore_rs::Rational::from_double(duration_seconds);

							if otio_block.schema_name() == "Clip" {
								unsafe {
									bridge::node::oaknode_clip_set_media_in(
										block,
										start_time.numerator() as i32,
										start_time.denominator() as i32,
									);
								}
							}
							unsafe {
								bridge::node::oaknode_block_set_length_and_media_out(
									block,
									duration.numerator() as i32,
									duration.denominator() as i32,
								);
							}
						}
					}

					// If the previous block was a transition, connect the
					// current block to it.
					if prev_block_transition {
						unsafe {
							bridge::node::oaknode_node_connect(
								block_node,
								bridge::node::oaknode_block_as_node(previous_block),
								cstr(bridge::node::OAKNODE_TRANSITION_IN_BLOCK_INPUT),
							);
						}
						prev_block_transition = false;
					}

					if let Some(otio_transition) = otio_block.as_transition() {
						// Set how far the transition eats into the previous
						// clip.
						let in_offset = otio_transition.in_offset().to_rational();
						let out_offset = otio_transition.out_offset().to_rational();
						unsafe {
							bridge::node::oaknode_transition_set_offsets_and_length(
								block,
								in_offset.numerator() as i32,
								in_offset.denominator() as i32,
								out_offset.numerator() as i32,
								out_offset.denominator() as i32,
							);
						}

						if !previous_block.ctx.is_null() {
							unsafe {
								bridge::node::oaknode_node_connect(
									bridge::node::oaknode_block_as_node(previous_block),
									block_node,
									cstr(bridge::node::OAKNODE_TRANSITION_OUT_BLOCK_INPUT),
								);
							}
						}
						prev_block_transition = true;

						// Position transition in its own context.
						unsafe {
							set_own_context_position(block_node);
						}
					}

					if otio_block.schema_name() == "Gap" {
						// Position gap in its own context.
						unsafe {
							set_own_context_position(block_node);
						}
					}

					// Update this after it's used but before any continue
					// statements.
					previous_block = block;

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

							let probed_item = if let Some(existing) = imported_footage.get(&footage_url) {
								*existing
							} else {
								let created = unsafe { bridge::node::oaknode_footage_create(project, cstr(&footage_url)) };
								if !created.ctx.is_null() {
									imported_footage.insert(footage_url.clone(), created);

									let label = Path::new(&footage_url)
										.file_name()
										.map(|n| n.to_string_lossy().into_owned())
										.unwrap_or_default();
									unsafe {
										bridge::node::oaknode_node_set_label(
											bridge::node::oaknode_footage_as_node(created),
											cstr(&label),
										);
									}

									if !sequence_footage.ctx.is_null() {
										let mut add_footage = unsafe {
											bridge::node::oaknode_command_create_folder_add_child(
												sequence_footage,
												bridge::node::oaknode_footage_as_node(created),
											)
										};
										if !add_footage.ctx.is_null() {
											unsafe {
												bridge::undo::oakundo_command_redo_now(add_footage);
												bridge::undo::oakundo_command_free(&mut add_footage);
											}
										}
									}
								}
								created
							};

							if !probed_item.ctx.is_null() {
								unsafe {
									// Position clip in its own context.
									set_own_context_position(block_node);

									// Position footage in its context.
									bridge::node::oaknode_node_set_context_position(
										block_node,
										bridge::node::oaknode_footage_as_node(probed_item),
										-2.0,
										0.0,
										0,
									);
								}

								if track_type == bridge::node::OAKNODE_TRACK_TYPE_VIDEO {
									let transform = unsafe {
										bridge::node::oaknode_factory_create_from_id(cstr(bridge::node::OAKNODE_TYPE_TRANSFORM))
									};
									if !transform.ctx.is_null() {
										unsafe {
											bridge::node::oaknode_project_add_node(project, transform);
											bridge::node::oaknode_node_connect(
												bridge::node::oaknode_footage_as_node(probed_item),
												transform,
												cstr("tex_in"),
											);
											bridge::node::oaknode_node_connect(transform, block_node, cstr("buffer_in"));
											bridge::node::oaknode_node_set_context_position(block_node, transform, -1.0, 0.0, 0);
										}
									}
								} else {
									let volume =
										unsafe { bridge::node::oaknode_factory_create_from_id(cstr(bridge::node::OAKNODE_TYPE_VOLUME)) };
									if !volume.ctx.is_null() {
										unsafe {
											bridge::node::oaknode_project_add_node(project, volume);
											bridge::node::oaknode_node_connect(
												bridge::node::oaknode_footage_as_node(probed_item),
												volume,
												cstr("samples_in"),
											);
											bridge::node::oaknode_node_connect(volume, block_node, cstr("buffer_in"));
											bridge::node::oaknode_node_set_context_position(block_node, volume, -1.0, 0.0, 0);
										}
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

/// Set the node's position in its own context (the C++
/// `set_own_context_position` helper in loadotio.cpp).
unsafe fn set_own_context_position(node: CHandle) {
	unsafe {
		bridge::node::oaknode_node_set_context_position(node, node, 0.0, 0.0, 0);
	}
}

/// Two-stage read of a node's label (the C++ `oaknode_node_get_label` usage).
fn node_label_of(node: CHandle) -> String {
	let needed = unsafe { bridge::node::oaknode_node_get_label(node, std::ptr::null_mut(), 0) };
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0i8; needed as usize];
	unsafe {
		bridge::node::oaknode_node_get_label(node, buf.as_mut_ptr(), needed);
	}
	crate::project::load::buf_to_string(&buf)
}

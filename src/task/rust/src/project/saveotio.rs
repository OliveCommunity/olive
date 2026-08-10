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

//! `SaveOTIOTask`, mirroring `src/task/src/project/saveotio/saveotio.h`.
//!
//! Serializes a borrowed `OakNodeProject` to an OpenTimelineIO (`.otio`) or
//! FCPXML (`.fcpxml`) file through the pure-Rust `oakotio` binding (see
//! `README` decision #6): the project's sequences become `OTIO::Timeline`s
//! (`serialize_timeline` / `serialize_track` / `serialize_track_list`),
//! exactly like the C++ `serialize_*` helpers. The format is dispatched
//! from the filename extension (see [`crate::project::format`]) at the
//! final write only — the serialization itself is shared. No OTIO or
//! FCPXML type crosses the oaktask C ABI.
//!
//! CPP-PARITY: src/task/src/project/saveotio/saveotio.cpp

use oakcore_rs::Rational;
use oakotio::{
	Clip, Composable, ExternalReference, Gap, MediaReference, RationalTime, Serializable, SerializableCollection, TimeRange,
	Timeline, Track, Transition,
};

use crate::bridge;
use crate::error::{Error, Result};
use crate::handle::CHandle;
use crate::project::format::InterchangeFormat;
use crate::project::load::buf_to_string;
use crate::task::{Task, TaskBehavior};

/// `Rational(INT_MIN)` — the initial "no track yet" maximum length
/// (`RATIONAL_MIN` in saveotio.cpp).
fn rational_min() -> Rational {
	Rational::new(-2147483647, 1)
}

/// An OTIO project-save task. Borrows its project.
pub struct SaveOTIOTask {
	/// The shared task base.
	pub base: Task,
	/// Borrowed project to save (borrowed `OakNodeProject`).
	pub project: CHandle,
	/// Output OTIO filename.
	pub filename: String,
}

impl SaveOTIOTask {
	/// Serialize one sequence into an `OTIO::Timeline`; `None` when the
	/// sequence has no usable frame rate or a track fails to serialize.
	///
	/// CPP-PARITY: saveotio.cpp (SaveOTIOTask::serialize_timeline)
	fn serialize_timeline(sequence: CHandle) -> Option<Timeline> {
		let mut otio_timeline = Timeline::new(node_label_of(unsafe { bridge::node::oaknode_sequence_as_node(sequence) }));

		let mut rate = 0.0f64;
		{
			let mut num = 0;
			let mut den = 1;
			let mut vp = CHandle::null();
			if unsafe { bridge::node::oaknode_sequence_get_video_params(sequence, 0, &mut vp) } == 0 {
				unsafe {
					bridge::common::oakcommon_videoparams_get_frame_rate(vp, &mut num, &mut den);
				}
				if den != 0 {
					rate = num as f64 / den as f64;
				}
				if !vp.ctx.is_null() {
					unsafe {
						bridge::common::oakcommon_videoparams_free(&mut vp);
					}
				}
			}
		}
		if rate.is_nan() || rate <= 0.0 {
			return None;
		}

		let mut video_list = CHandle::null();
		let mut audio_list = CHandle::null();
		unsafe {
			bridge::node::oaknode_sequence_get_track_list(sequence, bridge::node::OAKNODE_TRACK_TYPE_VIDEO, &mut video_list);
			bridge::node::oaknode_sequence_get_track_list(sequence, bridge::node::OAKNODE_TRACK_TYPE_AUDIO, &mut audio_list);
		}

		if !Self::serialize_track_list(video_list, &mut otio_timeline, rate)
			|| !Self::serialize_track_list(audio_list, &mut otio_timeline, rate)
		{
			return None;
		}

		Some(otio_timeline)
	}

	/// Serialize all tracks of one track list (video or audio) into the
	/// timeline. Each track is padded to the list's maximum track length.
	///
	/// CPP-PARITY: saveotio.cpp (SaveOTIOTask::serialize_track_list)
	fn serialize_track_list(list: CHandle, otio_timeline: &mut Timeline, sequence_rate: f64) -> bool {
		if list.ctx.is_null() {
			return true;
		}

		let mut max_track_length = rational_min();

		let mut track_count = 0;
		unsafe {
			bridge::node::oaknode_tracklist_get_track_count(list, &mut track_count);
		}

		for i in 0..track_count {
			let mut track = CHandle::null();
			unsafe {
				bridge::node::oaknode_tracklist_get_track_at(list, i, &mut track);
			}
			if !track.ctx.is_null() {
				let length = track_length_of(track);
				if length > max_track_length {
					max_track_length = length;
				}
			}
		}

		for i in 0..track_count {
			let mut track = CHandle::null();
			unsafe {
				bridge::node::oaknode_tracklist_get_track_at(list, i, &mut track);
			}
			if track.ctx.is_null() {
				continue;
			}

			let Some(otio_track) = Self::serialize_track(track, sequence_rate, max_track_length) else {
				return false;
			};

			otio_timeline.tracks_mut().append_child(Composable::Track(otio_track));
		}

		true
	}

	/// Serialize one track: a `Track` whose kind matches the native track
	/// type, one OTIO block per native block, plus a trailing `Gap` when the
	/// track is shorter than `max_track_length`.
	///
	/// CPP-PARITY: saveotio.cpp (SaveOTIOTask::serialize_track)
	fn serialize_track(track: CHandle, sequence_rate: f64, max_track_length: Rational) -> Option<Track> {
		let mut track_type = bridge::node::OAKNODE_TRACK_TYPE_NONE;
		unsafe {
			bridge::node::oaknode_track_get_type(track, &mut track_type);
		}

		let kind = match track_type {
			bridge::node::OAKNODE_TRACK_TYPE_VIDEO => "Video",
			bridge::node::OAKNODE_TRACK_TYPE_AUDIO => "Audio",
			other => {
				eprintln!("Don't know OTIO track kind for native type {other}");
				return None;
			}
		};
		let mut otio_track = Track::new(kind);

		let mut block_count = 0;
		unsafe {
			bridge::node::oaknode_track_get_block_count(track, &mut block_count);
		}

		for i in 0..block_count {
			let mut block = CHandle::null();
			unsafe {
				bridge::node::oaknode_track_get_block_at(track, i, &mut block);
			}
			if block.ctx.is_null() {
				continue;
			}

			let mut kind = bridge::node::OAKNODE_BLOCK_OTHER;
			unsafe {
				bridge::node::oaknode_block_get_kind(block, &mut kind);
			}

			let otio_block: Option<Composable> = match kind {
				bridge::node::OAKNODE_BLOCK_CLIP => {
					let mut otio_clip = Clip::new(node_label_of(unsafe { bridge::node::oaknode_block_as_node(block) }));

					otio_clip.set_source_range(TimeRange::new(
						RationalTime::from_rational(block_in_of(block), sequence_rate),
						RationalTime::from_rational(block_length_of(block), sequence_rate),
					));

					let mut media = CHandle::null();
					unsafe {
						bridge::node::oaknode_node_find_input_footage(bridge::node::oaknode_block_as_node(block), &mut media);
					}
					if !media.ctx.is_null() {
						let available_range = if track_type == bridge::node::OAKNODE_TRACK_TYPE_VIDEO {
							// OTIO ExternalReference uses the source clips
							// frame rate (or sample rate) as opposed to the
							// sequences rate.
							let mut source_frame_rate = 0.0f64;
							let mut duration = 0.0f64;
							let mut num = 0;
							let mut den = 1;
							let mut vp = CHandle::null();
							if unsafe { bridge::node::oaknode_footage_get_video_params(media, 0, &mut vp) } == 0 {
								unsafe {
									bridge::common::oakcommon_videoparams_get_frame_rate(vp, &mut num, &mut den);
								}
								if den != 0 {
									source_frame_rate = num as f64 / den as f64;
								}
								let mut dur = 0i64;
								unsafe {
									bridge::common::oakcommon_videoparams_get_duration(vp, &mut dur);
								}
								duration = dur as f64;
								if !vp.ctx.is_null() {
									unsafe {
										bridge::common::oakcommon_videoparams_free(&mut vp);
									}
								}
							}
							TimeRange::new(
								RationalTime::new(0.0, source_frame_rate),
								RationalTime::new(duration, source_frame_rate),
							)
						} else {
							TimeRange::new(RationalTime::new(0.0, 48000.0), RationalTime::new(0.0, 48000.0))
						};

						let media_url = footage_filename(media);
						if !media_url.is_empty() {
							otio_clip.set_media_reference(MediaReference::ExternalReference(ExternalReference::new(
								media_url,
								Some(available_range),
							)));
						}
					}

					Some(Composable::Clip(otio_clip))
				}
				bridge::node::OAKNODE_BLOCK_GAP => Some(Composable::Gap(Gap::new(
					TimeRange::new(
						RationalTime::from_rational(block_in_of(block), 24.0),
						RationalTime::from_rational(block_length_of(block), 24.0),
					),
					node_label_of(unsafe { bridge::node::oaknode_block_as_node(block) }),
				))),
				bridge::node::OAKNODE_BLOCK_TRANSITION => {
					let mut otio_transition = Transition::new(node_label_of(unsafe { bridge::node::oaknode_block_as_node(block) }));

					let (n, d) = transition_offset_of(block, true);
					otio_transition.set_in_offset(RationalTime::from_rational(Rational::new(n as i64, d as i64), 24.0));
					let (n, d) = transition_offset_of(block, false);
					otio_transition.set_out_offset(RationalTime::from_rational(Rational::new(n as i64, d as i64), 24.0));

					Some(Composable::Transition(otio_transition))
				}
				_ => None,
			};

			let Some(otio_block) = otio_block else {
				// We shouldn't ever get here, but catch without crashing if
				// we ever do.
				return None;
			};

			otio_track.append_child(otio_block);
		}

		// All OTIO tracks must have the same duration so we add a Gap to
		// fill the remaining time.
		let duration = track_duration(&otio_track);
		let duration_seconds = duration.clone().to_seconds();
		if duration_seconds < max_track_length.to_f64() {
			let time_left = max_track_length.to_f64() - duration_seconds;

			let gap = Gap::new(TimeRange::new(duration, RationalTime::new(time_left, 1.0)), "");
			otio_track.append_child(Composable::Gap(gap));
		}

		Some(otio_track)
	}
}

impl TaskBehavior for SaveOTIOTask {
	/// Serialize the project to OTIO/FCPXML via `oakotio` and write the file.
	fn run(&mut self, task: &mut Task) -> Result<()> {
		// Dispatch the interchange format from the filename extension.
		// Format handling ends at the write below — the serialization
		// produces `oakotio::Timeline`s and is shared between `.otio` and
		// `.fcpxml` (C++ parity for the serialize_* helpers).
		let format = match InterchangeFormat::of(&self.filename) {
			Ok(f) => f,
			Err(e) => {
				if let Error::Failed(msg) = &e {
					task.set_error(msg);
				}
				return Err(e);
			}
		};

		// Collect sequences from the root folder (non-recursive, matching
		// the original list_children_of_type behavior closely enough for
		// OTIO).
		let root = unsafe { bridge::node::oaknode_project_root(self.project) };
		if root.ctx.is_null() {
			task.set_error("Project contains no sequences to export.");
			return Err(Error::Failed("Project contains no sequences to export.".to_string()));
		}

		let mut sequences: Vec<CHandle> = Vec::new();
		let child_count = unsafe { bridge::node::oaknode_folder_child_count(root) };
		for i in 0..child_count {
			let child = unsafe { bridge::node::oaknode_folder_child_at(root, i) };
			if child.ctx.is_null() {
				continue;
			}
			if node_id_of(child) == bridge::node::OAKNODE_TYPE_SEQUENCE {
				// Borrowed sequence alias of the child node handle (all
				// oaknode handles share the same box layout).
				sequences.push(child);
			}
		}

		if sequences.is_empty() {
			task.set_error("Project contains no sequences to export.");
			return Err(Error::Failed("Project contains no sequences to export.".to_string()));
		}

		let mut serialized: Vec<Timeline> = Vec::with_capacity(sequences.len());
		for sequence in &sequences {
			match Self::serialize_timeline(*sequence) {
				Some(timeline) => serialized.push(timeline),
				None => {
					task.set_error(&format!(
						"Failed to serialize sequence \"{}\"",
						node_label_of(unsafe { bridge::node::oaknode_sequence_as_node(*sequence) })
					));
					return Err(Error::Failed("Failed to serialize sequence".to_string()));
				}
			}
		}

		// Write the serialized timelines, dispatching on the format: OTIO
		// JSON writes a `Timeline` root for one sequence or a
		// `SerializableCollection` for several; FCPXML writes all timelines
		// into one document (one `<project>` per timeline).
		let result: std::result::Result<(), String> = match format {
			InterchangeFormat::OtioJson => {
				if serialized.len() == 1 {
					serialized[0].to_json_file(&self.filename).map_err(|e| e.to_string())
				} else {
					// Serialize all into a SerializableCollection.
					let collection = SerializableCollection::new(
						"Sequences",
						serialized.into_iter().map(Serializable::Timeline).collect(),
					);
					Serializable::SerializableCollection(collection)
						.to_json_file(&self.filename)
						.map_err(|e| e.to_string())
				}
			}
			InterchangeFormat::Fcpxml => oakotio::to_fcpxml_file(&serialized, &self.filename).map_err(|e| e.to_string()),
		};

		result.map_err(|e| {
			let (label, what) = match format {
				InterchangeFormat::OtioJson => ("Failed to save OpenTimelineIO to file", "Failed to save OpenTimelineIO"),
				InterchangeFormat::Fcpxml => ("Failed to save FCPXML to file", "Failed to save FCPXML"),
			};
			task.set_error(&format!("{label} \"{}\": {e}", self.filename));
			Error::Failed(what.to_string())
		})
	}
}

/// Two-stage read of a node's label.
fn node_label_of(node: CHandle) -> String {
	let needed = unsafe { bridge::node::oaknode_node_get_label(node, std::ptr::null_mut(), 0) };
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0i8; needed as usize];
	unsafe {
		bridge::node::oaknode_node_get_label(node, buf.as_mut_ptr(), needed);
	}
	buf_to_string(&buf)
}

/// Two-stage read of a node's type id.
fn node_id_of(node: CHandle) -> String {
	let needed = unsafe { bridge::node::oaknode_node_get_id(node, std::ptr::null_mut(), 0) };
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0i8; needed as usize];
	unsafe {
		bridge::node::oaknode_node_get_id(node, buf.as_mut_ptr(), needed);
	}
	buf_to_string(&buf)
}

/// The block's in point as a `Rational` (default 0/1 when unset).
fn block_in_of(block: CHandle) -> Rational {
	let mut n = 0;
	let mut d = 1;
	unsafe {
		bridge::node::oaknode_block_get_in(block, &mut n, &mut d);
	}
	Rational::new(n as i64, d as i64)
}

/// The block's length as a `Rational` (default 0/1 when unset).
fn block_length_of(block: CHandle) -> Rational {
	let mut n = 0;
	let mut d = 1;
	unsafe {
		bridge::node::oaknode_block_get_length(block, &mut n, &mut d);
	}
	Rational::new(n as i64, d as i64)
}

/// The track's total length as a `Rational` (default 0/1 when unset).
fn track_length_of(track: CHandle) -> Rational {
	let mut n = 0;
	let mut d = 1;
	unsafe {
		bridge::node::oaknode_track_get_length(track, &mut n, &mut d);
	}
	Rational::new(n as i64, d as i64)
}

/// A transition's in (`in_offset == true`) or out offset as a `Rational`.
fn transition_offset_of(block: CHandle, in_offset: bool) -> (i32, i32) {
	let mut n = 0;
	let mut d = 1;
	if in_offset {
		unsafe {
			bridge::node::oaknode_transition_get_in_offset(block, &mut n, &mut d);
		}
	} else {
		unsafe {
			bridge::node::oaknode_transition_get_out_offset(block, &mut n, &mut d);
		}
	}
	(n, d)
}

/// Two-stage read of a footage's filename (empty when unset).
fn footage_filename(footage: CHandle) -> String {
	let needed = unsafe { bridge::node::oaknode_footage_filename(footage, std::ptr::null_mut(), 0) };
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0i8; needed as usize];
	unsafe {
		bridge::node::oaknode_footage_filename(footage, buf.as_mut_ptr(), needed);
	}
	buf_to_string(&buf)
}

/// The serialized track's duration as an `OTIO::RationalTime` — the sum of
/// its children's source-range durations, keeping the first child's rate
/// (the C++ `otio_track->duration(&es)` result for contiguous children).
fn track_duration(track: &Track) -> RationalTime {
	let mut acc: Option<RationalTime> = None;
	for child in track.children() {
		let duration = match child.as_clip().and_then(|c| c.source_range()).map(|r| r.duration())
			.or_else(|| child.as_gap().and_then(|g| g.source_range()).map(|r| r.duration()))
		{
			Some(d) => d,
			// Transitions occupy no time.
			None => RationalTime::new(0.0, 1.0),
		};
		match acc {
			None => acc = Some(duration),
			Some(a) => {
				let rate = a.rate();
				let value = a.value() + duration.rescaled_to(rate).value();
				acc = Some(RationalTime::new(value, rate));
			}
		}
	}
	acc.unwrap_or_else(RationalTime::invalid_time)
}

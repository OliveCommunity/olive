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

//! The `.otio` / `.fcpxml` backend (import/export via the native
//! `oakotio` crate).
//!
//! OTIO and FCPXML are **interchange formats** — the mapping is lossy by
//! design (M10 §2.2 "otio 后端（import 语义）"). What round-trips:
//! sequences → timelines, track lists → tracks (kind "Video" /
//! "Audio" / "Subtitle"), clip blocks → clips (name, source range,
//! media reference), gap blocks → gaps, transition blocks →
//! transitions. What is **not** carried across (documented lossy
//! items, bounded by the oakotio model and the current oaknode model):
//!
//! - Effect chains and their parameters: a clip's `tex_in` effect row
//!   (blur/opacity/... nodes) is not represented in OTIO's effect
//!   model (oakotio stores effects as opaque `Value` blobs), so effects
//!   are dropped on export.
//! - Keyframes and keyframe interpolation: clip parameters are not
//!   exported; OTIO has no per-clip parameter animation in the
//!   covered schema set.
//! - Exact rational timebases: times cross as seconds (oakotio
//!   `RationalTime`), so numeric numerators/denominators change while
//!   durations and offsets are preserved.
//! - Project bins (the folder tree), project settings, node labels and
//!   colors, and non-timeline nodes (math, generators, ...) are not
//!   part of the interchange.
//! - Clip ↔ footage linkage: exports the footage filename as an
//!   `ExternalReference.target_url`; imports create a footage node per
//!   clip. A clip without a resolvable footage reference exports a
//!   `MissingReference`.
//! - Nested OTIO stacks, markers and transitions-in-depth are imported
//!   approximately (a transition becomes a single transition block
//!   with its offsets; nested stacks are skipped).
//!
//! Load always builds a fresh project (`initialize()` + one sequence
//! per timeline); save exports every sequence of the project.

use std::collections::HashSet;

use oakcore_rs::Rational;
use oakotio::model::{
	Composable, ExternalReference, Gap, MediaReference, MissingReference, Serializable,
	SerializableCollection, Timeline, TimeRange, Track, Transition,
};
use oaknode::block::{ClipBlockBehavior, GapBlockBehavior, TransitionBlockBehavior};
use oaknode::footage::FootageBehavior;
use oaknode::graph::NodeEntry;
use oaknode::id::NodeId;
use oaknode::node::NodeCore;
use oaknode::project::Project;
use oaknode::sequence::SequenceBehavior;
use oaknode::track::{TrackBehavior, TrackListBehavior, TrackType};

use crate::backend::LoadResult;
use crate::nodeutil as node;
use crate::nodeutil::ProjectArc;
use crate::error::{Error, Result};
use crate::uri::StorageUri;

/// The otio backend (`file://` + `.otio` / `.fcpxml`).
pub struct OtioBackend;

/// Default frame rate used when a sequence has no video parameters.
const DEFAULT_FPS: f64 = 24.0;

impl OtioBackend {
	/// Construct.
	pub fn new() -> Self {
		OtioBackend
	}

	/// Save a project (already read out of its handle) to the URI. The
	/// Rust-typed inner path: the facade-facing trait `save` converts the
	/// project handle and forwards here.
	pub fn save_project(
		&self,
		project: &ProjectArc,
		uri: &StorageUri,
		_options: u32,
	) -> Result<()> {
		let path = uri.local_path().ok_or(Error::Invalid)?.to_string();
		let ext = uri.extension().ok_or(Error::Invalid)?;
		let guard = project.lock().map_err(|_| Error::State)?;
		let timelines = project_to_timelines(&guard);

		match ext.as_str() {
			"otio" => {
				let root = if timelines.len() == 1 {
					Serializable::Timeline(timelines.into_iter().next().unwrap())
				} else if timelines.is_empty() {
					// Nothing to export; a single empty timeline is the
					// friendliest shape for a fresh import.
					Serializable::Timeline(Timeline::new("Timeline"))
				} else {
					let children: Vec<Serializable> = timelines
						.into_iter()
						.map(Serializable::Timeline)
						.collect();
					Serializable::SerializableCollection(SerializableCollection::new(
						"oak",
						children,
					))
				};
				root.to_json_file(&path).map_err(|e| Error::Io(e.to_string()))
			}
			"fcpxml" => oakotio::to_fcpxml_file(&timelines, &path)
				.map_err(|e| Error::Io(e.to_string())),
			_ => Err(Error::Invalid),
		}
	}
}

impl Default for OtioBackend {
	fn default() -> Self {
		Self::new()
	}
}

impl crate::backend::StorageBackend for OtioBackend {
	fn name(&self) -> &'static str {
		"otio"
	}

	fn uri_scheme(&self) -> &'static str {
		"file"
	}

	fn can_handle(&self, uri: &StorageUri) -> bool {
		matches!(uri.extension().as_deref(), Some("otio") | Some("fcpxml"))
	}

	fn load(&self, uri: &StorageUri) -> Result<LoadResult> {
		let path = uri.local_path().ok_or(Error::Invalid)?.to_string();
		let ext = uri.extension().ok_or(Error::Invalid)?;

		let timelines: Vec<Timeline> = match ext.as_str() {
			"otio" => {
				let text = std::fs::read_to_string(&path).map_err(|e| Error::Io(e.to_string()))?;
				let root = oakotio::from_json_string(&text)
					.map_err(|e| Error::Format(e.to_string()))?;
				match root {
					Serializable::Timeline(t) => vec![t],
					Serializable::SerializableCollection(c) => c
						.children()
						.iter()
						.filter_map(|s| s.as_timeline().cloned())
						.collect(),
					Serializable::Raw(_) => {
						return Err(Error::Format(format!(
							"'{}' has no timeline or collection root",
							path
						)));
					}
				}
			}
			"fcpxml" => {
				oakotio::from_fcpxml_file(&path).map_err(|e| Error::Format(e.to_string()))?
			}
			_ => return Err(Error::Invalid),
		};

		let project = Project::new();
		{
			let mut guard = project.lock().map_err(|_| Error::State)?;
			guard
				.initialize()
				.map_err(|e| Error::Failed(e.to_string()))?;
			for timeline in &timelines {
				import_timeline(&mut guard, timeline)?;
			}
		}
		Ok(LoadResult::success(node::make_project_owned(project)))
	}

	fn save(
		&self,
		project: crate::handle::CHandle,
		uri: &StorageUri,
		options: u32,
	) -> Result<()> {
		// Facade boundary: convert the project handle to the boxed
		// project, then run the Rust-typed save.
		let arc = unsafe { node::project_arc(&project)? };
		self.save_project(&arc, uri, options)
	}
}

// ---------------------------------------------------------------------------
// Project -> oakotio
// ---------------------------------------------------------------------------

/// Walk the project graph and build one [`Timeline`] per sequence node.
fn project_to_timelines(project: &Project) -> Vec<Timeline> {
	let graph = &project.graph;
	let mut timelines = Vec::new();
	for id in graph.node_ids() {
		let entry = match graph.get(id) {
			Some(e) => e,
			None => continue,
		};
		let seq = match entry
			.behavior
			.as_any()
			.and_then(|a| a.downcast_ref::<SequenceBehavior>())
		{
			Some(s) => s,
			None => continue,
		};
		let fps = sequence_fps(seq);
		let name = if entry.core.label.is_empty() {
			"Sequence".to_string()
		} else {
			entry.core.label.clone()
		};
		let mut timeline = Timeline::new(name);

		for list_id in &seq.track_lists {
			let list = graph
				.get(*list_id)
				.and_then(|e| e.behavior.as_any())
				.and_then(|a| a.downcast_ref::<TrackListBehavior>());
			let list = match list {
				Some(l) => l,
				None => continue,
			};
			for track_id in &list.tracks {
				let track = graph
					.get(*track_id)
					.and_then(|e| e.behavior.as_any())
					.and_then(|a| a.downcast_ref::<TrackBehavior>());
				let track = match track {
					Some(t) => t,
					None => continue,
				};
				timeline.tracks_mut().append_child(export_track(graph, track, fps));
			}
		}
		timelines.push(timeline);
	}
	timelines
}

/// Export one track: its kind plus one composable per block, inserting
/// gaps for timeline discontinuities (oak blocks carry absolute
/// positions; OTIO items are placed sequentially).
fn export_track(graph: &oaknode::graph::Graph, track: &TrackBehavior, fps: f64) -> Composable {
	let mut out = Track::new(track_kind_str(track.kind));
	let mut pos = Rational::new(0, 1);
	for block_id in &track.blocks {
		let entry = match graph.get(*block_id) {
			Some(e) => e,
			None => continue,
		};
		let (kind, block) = classify_block(entry);
		let (block_in, block_out, media_in, length) = match block {
			Some(core) => (core.in_(), core.out(), core.media_in, core.length()),
			None => continue,
		};
		// A gap before this block, when the timeline position jumped.
		if block_in > pos {
			let gap_len = block_in - pos;
			out.append_child(Composable::Gap(Gap::new(
				TimeRange::new(to_rt(pos, fps), to_rt(gap_len, fps)),
				"Gap",
			)));
		}
		match kind {
			BlockKind::Clip => {
				let footage = clip_footage(graph, entry, *block_id);
				let mut clip = oakotio::model::Clip::new(clip_name(graph, footage));
				let reference = footage
					.and_then(|fid| {
						graph
							.get(fid)
							.and_then(|e| e.behavior.as_any())
							.and_then(|a| a.downcast_ref::<FootageBehavior>())
							.map(|f| f.filename.clone())
					})
					.map(|fname| media_ref::external(&fname))
					.unwrap_or_else(media_ref::missing);
				clip.set_media_reference(reference);
				clip.set_source_range(TimeRange::new(
					to_rt(media_in, fps),
					to_rt(length, fps),
				));
				out.append_child(Composable::Clip(clip));
			}
			BlockKind::Gap => {
				out.append_child(Composable::Gap(Gap::new(
					TimeRange::new(to_rt(block_in, fps), to_rt(length, fps)),
					"Gap",
				)));
			}
			BlockKind::Transition => {
				if let Some(tb) = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TransitionBlockBehavior>())
				{
					let mut t = Transition::new("Transition");
					t.set_in_offset(to_rt(tb.in_offset, fps));
					t.set_out_offset(to_rt(tb.out_offset, fps));
					out.append_child(Composable::Transition(t));
				}
			}
			BlockKind::Other => {}
		}
		pos = block_out;
	}
	Composable::Track(out)
}

/// Frame rate of a sequence (from its first video stream, or the
/// default).
fn sequence_fps(seq: &SequenceBehavior) -> f64 {
	seq.video_params
		.first()
		.map(|p| p.frame_rate)
		.filter(|r| !r.is_null() && r.denominator() != 0)
		.map(|r| r.numerator() as f64 / r.denominator() as f64)
		.unwrap_or(DEFAULT_FPS)
}

/// The clip's display name: its footage's label, else the footage
/// filename base, else "Clip".
fn clip_name(graph: &oaknode::graph::Graph, footage: Option<NodeId>) -> String {
	match footage {
		Some(fid) => {
			if let Some(e) = graph.get(fid) {
				if !e.core.label.is_empty() {
					return e.core.label.clone();
				}
				if let Some(f) = e
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<FootageBehavior>())
				{
					return file_base(&f.filename);
				}
			}
			"Clip".to_string()
		}
		None => "Clip".to_string(),
	}
}

/// Resolve the footage behind a clip block: the block's `footage`
/// field, else the first footage node reachable upstream (mirrors
/// `oaknode_node_find_input_footage`).
fn clip_footage(
	graph: &oaknode::graph::Graph,
	entry: &NodeEntry,
	clip_id: NodeId,
) -> Option<NodeId> {
	if let Some(be) = entry
		.behavior
		.as_any()
		.and_then(|a| a.downcast_ref::<ClipBlockBehavior>())
	{
		if let Some(f) = be.footage {
			if graph.is_valid(f) {
				return Some(f);
			}
		}
	}
	let mut visited = HashSet::new();
	let mut queue: Vec<NodeId> = graph.upstream(clip_id);
	while let Some(id) = queue.pop() {
		if !visited.insert(id) {
			continue;
		}
		if let Some(e) = graph.get(id) {
			let is_footage = e
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<FootageBehavior>())
				.is_some();
			if is_footage {
				return Some(id);
			}
		}
		queue.extend(graph.upstream(id));
	}
	None
}

/// Classify a block node into its kind plus its block core.
fn classify_block(
	entry: &NodeEntry,
) -> (BlockKind, Option<&oaknode::block::BlockCore>) {
	entry
		.behavior
		.as_any()
		.and_then(|a| {
			if let Some(c) = a.downcast_ref::<ClipBlockBehavior>() {
				return Some((BlockKind::Clip, Some(&c.core)));
			}
			if let Some(g) = a.downcast_ref::<GapBlockBehavior>() {
				return Some((BlockKind::Gap, Some(&g.core)));
			}
			if let Some(t) = a.downcast_ref::<TransitionBlockBehavior>() {
				return Some((BlockKind::Transition, Some(&t.core)));
			}
			None
		})
		.unwrap_or((BlockKind::Other, None))
}

/// Block node kinds.
enum BlockKind {
	Clip,
	Gap,
	Transition,
	Other,
}

/// TrackType -> OTIO kind string.
fn track_kind_str(kind: TrackType) -> &'static str {
	match kind {
		TrackType::Video => "Video",
		TrackType::Audio => "Audio",
		TrackType::Subtitle => "Subtitle",
	}
}

/// OTIO kind string -> TrackType (anything unknown is subtitle-ish).
fn track_kind_from_str(kind: &str) -> TrackType {
	match kind {
		"Video" => TrackType::Video,
		"Audio" => TrackType::Audio,
		_ => TrackType::Subtitle,
	}
}

/// Rational -> RationalTime at `fps` (seconds are preserved).
fn to_rt(r: Rational, fps: f64) -> oakotio::model::RationalTime {
	oakotio::model::RationalTime::from_rational(r, fps)
}

/// Local path -> `file://` URL (kept simple: no percent-encoding).
fn to_file_uri(path: &str) -> String {
	if path.starts_with("file://") {
		path.to_string()
	} else {
		format!("file://{path}")
	}
}

/// `file://` URL -> local path (strips the scheme).
fn from_file_uri(url: &str) -> String {
	url.strip_prefix("file://").unwrap_or(url).to_string()
}

/// Filename base (last path component, extension dropped).
fn file_base(path: &str) -> String {
	let name = path.rsplit('/').next().unwrap_or(path);
	match name.rsplit_once('.') {
		Some((base, _)) if !base.is_empty() => base.to_string(),
		_ => name.to_string(),
	}
}

/// Media-reference builders.
mod media_ref {
	use super::*;

	pub fn external(path: &str) -> MediaReference {
		MediaReference::ExternalReference(ExternalReference::new(to_file_uri(path), None))
	}

	pub fn missing() -> MediaReference {
		MediaReference::MissingReference(MissingReference::new())
	}
}

// ---------------------------------------------------------------------------
// oakotio -> Project
// ---------------------------------------------------------------------------

/// Import one timeline into `project` as a sequence with its track
/// lists, tracks, and blocks (footage nodes per clip).
fn import_timeline(project: &mut Project, timeline: &Timeline) -> Result<()> {
	let (seq_id, lists) = node::create_sequence(&mut project.graph);
	if !timeline.name().is_empty() {
		if let Some(entry) = project.graph.get_mut(seq_id) {
			entry.core.label = timeline.name().to_string();
		}
	}

	let mut track_count_by_list = [0i32, 0, 0];

	for composable in timeline.tracks().children() {
		let otio_track = match composable.as_track() {
			Some(t) => t,
			None => continue,
		};
		let kind = track_kind_from_str(otio_track.kind());
		let list_index = match kind {
			TrackType::Video => 0,
			TrackType::Audio => 1,
			TrackType::Subtitle => 2,
		};
		let list_id = lists[list_index];

		// Track node.
		let mut track_behavior = TrackBehavior::new(kind);
		track_behavior.track_list = Some(list_id);
		track_behavior.index = track_count_by_list[list_index];
		track_count_by_list[list_index] += 1;
		let track_id = project.graph.add_node(NodeCore::new(), Box::new(track_behavior));

		// Blocks (positions accumulate; gaps/offsets keep the span).
		let mut block_ids = Vec::new();
		let mut pos = Rational::new(0, 1);
		for child in otio_track.children() {
			match child.as_ref() {
				Composable::Clip(c) => {
					let len = c
						.source_range()
						.map(|r| r.duration().to_rational())
						.filter(|r| !r.is_null())
						.unwrap_or(Rational::new(1, 1));
					let media_in = c
						.source_range()
						.map(|r| r.start_time().to_rational())
						.unwrap_or(Rational::new(0, 1));
					let filename = c
						.media_reference()
						.and_then(|m| m.as_external_reference())
						.map(|e| from_file_uri(e.target_url()));
					let (core, mut behavior) = oaknode::block::clip_create();
					let block_id = {
						let clip = behavior
							.as_any_mut()
							.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
							.expect("clip_create returns a clip block");
						clip.core.range = oakcore_rs::TimeRange::new(pos, pos + len);
						clip.core.media_in = media_in;
						clip.core.track = Some(track_id);
						clip.footage = None;
						let id = project.graph.add_node(core, behavior);
						if let Some(fname) = filename {
							let foot_id = project.graph.add_node(
								NodeCore::new(),
								Box::new(FootageBehavior::new(&fname)),
							);
							if let Some(e) = project.graph.get_mut(id) {
								if let Some(c) = e
									.behavior
									.as_any_mut()
									.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
								{
									c.footage = Some(foot_id);
								}
							}
						}
						id
					};
					block_ids.push(block_id);
					pos = pos + len;
				}
				Composable::Gap(g) => {
					let len = g
						.source_range()
						.map(|r| r.duration().to_rational())
						.filter(|r| !r.is_null())
						.unwrap_or(Rational::new(1, 1));
					let (core, mut behavior) = oaknode::block::gap_create();
					let block_id = {
						let gap = behavior
							.as_any_mut()
							.and_then(|a| a.downcast_mut::<GapBlockBehavior>())
							.expect("gap_create returns a gap block");
						gap.core.range = oakcore_rs::TimeRange::new(pos, pos + len);
						gap.core.track = Some(track_id);
						project.graph.add_node(core, behavior)
					};
					block_ids.push(block_id);
					pos = pos + len;
				}
				Composable::Transition(t) => {
					// Approximate: a single transition block with its
					// offsets; it does not advance the timeline position.
					let (core, mut behavior) = oaknode::block::transition_create();
					let block_id = {
						let tr = behavior
							.as_any_mut()
							.and_then(|a| a.downcast_mut::<TransitionBlockBehavior>())
							.expect("transition_create returns a transition block");
						tr.in_offset = t.in_offset().to_rational();
						tr.out_offset = t.out_offset().to_rational();
						tr.core.range =
							oakcore_rs::TimeRange::new(pos, pos + Rational::new(1, 1));
						tr.core.track = Some(track_id);
						project.graph.add_node(core, behavior)
					};
					block_ids.push(block_id);
				}
				_ => {}
			}
		}

		// Wire the blocks into the track, and the track into its list.
		if let Some(entry) = project.graph.get_mut(track_id) {
			if let Some(tb) = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
			{
				tb.blocks = block_ids;
			}
		}
		if let Some(entry) = project.graph.get_mut(list_id) {
			if let Some(lb) = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackListBehavior>())
			{
				lb.tracks.push(track_id);
			}
		}
	}
	Ok(())
}

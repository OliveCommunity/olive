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

//! Domain helpers over the module graph (M14 R3).
//!
//! The real engine ([`crate::oakui::real`]) drives the oak* module crates
//! directly (no liboakengine C ABI). This module is the app's own assembly
//! layer for the project/timeline/storage domain: project lifecycle (new /
//! load / save over the oaknode serializer), sequence + track + clip
//! queries, footage import, the per-sequence marker-list / workarea
//! auxiliaries, and the project-library operations over oakstorage's
//! write-through backend.
//!
//! The functions mirror the semantics the facade's `oakengine_*` exports
//! had (the facade keeps its own copies for the frozen C ABI); the
//! composition mirrors `crates/oak-cli/src/engine.rs` (M14 R2), which
//! established the same mapping for the CLI.

use std::path::Path;
use std::sync::{Arc, Mutex, MutexGuard};

use oakcore_rs::{Rational, TimeRange};
use oaknode::block::ClipBlockBehavior;
use oaknode::footage::FootageBehavior;
use oaknode::graph::Graph;
use oaknode::id::NodeId;
use oaknode::project::Project;
use oaknode::sequence::SequenceBehavior;
use oaknode::track::{TrackBehavior, TrackListBehavior, TrackType};
use oaktimeline::handle::CHandle;
use oaktimeline::util::NodeRef;

use oakstorage::backend::StorageBackend;

use super::engine::LibraryProject;

/// The shared project reference (the modules' domain project handle).
pub type ProjectRef = Arc<Mutex<Project>>;

/// Lock a project, recovering from a poisoned lock (a panicking command
/// body must not wedge every later edit).
pub fn lock(p: &ProjectRef) -> MutexGuard<'_, Project> {
	p.lock().unwrap_or_else(|e| e.into_inner())
}

/// A timeline node reference (the oaktimeline command addressing).
pub fn node_ref(p: &ProjectRef, id: NodeId) -> NodeRef {
	NodeRef::new(p.clone(), id)
}

/// The node id whose stable identity is `identity` (`None` for the
/// sentinel). The app's widget-facing ids (clip ids, explorer entry ids)
/// ARE the node identities.
pub fn id_of(identity: u64) -> Option<NodeId> {
	NodeId::from_identity(identity)
}

// ---------------------------------------------------------------------------
// Behavior borrows
// ---------------------------------------------------------------------------

/// Borrow the sequence behavior at `id`.
pub fn sequence_behavior(g: &Graph, id: NodeId) -> Option<&SequenceBehavior> {
	g.get(id)?
		.behavior
		.as_any()?
		.downcast_ref::<SequenceBehavior>()
}

/// Borrow the track-list behavior at `id`.
pub fn track_list_behavior(g: &Graph, id: NodeId) -> Option<&TrackListBehavior> {
	g.get(id)?
		.behavior
		.as_any()?
		.downcast_ref::<TrackListBehavior>()
}

/// Borrow the track behavior at `id`.
pub fn track_behavior(g: &Graph, id: NodeId) -> Option<&TrackBehavior> {
	g.get(id)?
		.behavior
		.as_any()?
		.downcast_ref::<TrackBehavior>()
}

/// Borrow the clip behavior at `id`.
pub fn clip_behavior(g: &Graph, id: NodeId) -> Option<&ClipBlockBehavior> {
	g.get(id)?
		.behavior
		.as_any()?
		.downcast_ref::<ClipBlockBehavior>()
}

/// Borrow the footage behavior at `id`.
pub fn footage_behavior(g: &Graph, id: NodeId) -> Option<&FootageBehavior> {
	g.get(id)?
		.behavior
		.as_any()?
		.downcast_ref::<FootageBehavior>()
}

/// Whether `id` is a folder node.
pub fn is_folder(g: &Graph, id: NodeId) -> bool {
	g.get(id)
		.and_then(|e| e.behavior.as_any())
		.and_then(|a| a.downcast_ref::<oaknode::folder::FolderBehavior>())
		.is_some()
}

// ---------------------------------------------------------------------------
// Project lifecycle
// ---------------------------------------------------------------------------

/// Create a blank, initialized project (the facade's `project_create` +
/// `project_new`, minus the undo-stack clear and the storage bind — the
/// engine's adopt path owns those).
pub fn create_project() -> ProjectRef {
	let project = Project::new();
	lock(&project).initialize().ok();
	project
}

/// Load a `.ove` project file (plain XML; the module serializer ignores
/// the legacy compression flag). The filename is normalized to an absolute
/// path and the modified flag cleared, mirroring the facade's
/// `oakengine_project_load`.
pub fn load_ove(path: &Path) -> Result<ProjectRef, String> {
	let xml = std::fs::read_to_string(path)
		.map_err(|e| format!("failed to read \"{}\": {e}", path.display()))?;
	let project =
		oaknode::serializer::load(&xml).map_err(|e| format!("failed to parse: {e}"))?;
	let abs = if path.is_absolute() {
		path.to_path_buf()
	} else {
		std::env::current_dir()
			.map(|d| d.join(path))
			.unwrap_or_else(|_| path.to_path_buf())
	};
	let mut guard = lock(&project);
	guard.set_filename(&abs.to_string_lossy());
	guard.set_modified(false);
	drop(guard);
	Ok(project)
}

/// Write the project to `path` through the OVE serializer (the facade's
/// `oakengine_project_save` semantics: the target filename is recorded and
/// the modified flag cleared on success).
pub fn save_ove(project: &ProjectRef, path: &Path) -> Result<(), String> {
	let xml = {
		let guard = lock(project);
		oaknode::serializer::save(&guard).map_err(|e| format!("failed to serialize: {e}"))?
	};
	std::fs::write(path, &xml).map_err(|e| format!("failed to write \"{}\": {e}", path.display()))?;
	let mut guard = lock(project);
	guard.set_filename(&path.to_string_lossy());
	guard.set_modified(false);
	Ok(())
}

/// The display name (`Project::name`: filename base or "(untitled)").
pub fn project_name(p: &Project) -> String {
	p.name()
}

// ---------------------------------------------------------------------------
// Sequences
// ---------------------------------------------------------------------------

/// Every sequence node in the graph, in arena order.
pub fn sequence_ids(p: &Project) -> Vec<NodeId> {
	p.graph
		.node_ids()
		.into_iter()
		.filter(|&id| sequence_behavior(&p.graph, id).is_some())
		.collect()
}

/// Every footage node in the graph, in arena order.
pub fn footage_ids(p: &Project) -> Vec<NodeId> {
	p.graph
		.node_ids()
		.into_iter()
		.filter(|&id| footage_behavior(&p.graph, id).is_some())
		.collect()
}

/// Create a sequence node named `name` directly in the project's graph
/// (unlike the facade's `oakengine_sequence_new`, which kept the sequence
/// in a scratch project, the direct-rlib app keeps it in the project so
/// saves and the write-through library cover it).
pub fn create_sequence(project: &ProjectRef, name: &str) -> NodeId {
	let mut guard = lock(project);
	let (mut core, behavior) = SequenceBehavior::create();
	core.label = name.to_string();
	guard.graph.add_node(core, behavior)
}

/// The label of a node (`NodeCore::label`).
pub fn node_label(g: &Graph, id: NodeId) -> String {
	g.get(id).map(|e| e.core.label.clone()).unwrap_or_default()
}

/// The type id of a node (empty when the id is stale).
pub fn node_type_id(g: &Graph, id: NodeId) -> String {
	g.get(id)
		.map(|e| e.behavior.type_id().to_string())
		.unwrap_or_default()
}

/// The sequence's video format `(width, height, rate)` from its first
/// video stream.
pub fn sequence_video_params(g: &Graph, seq: NodeId) -> Option<(i32, i32, Rational)> {
	let v = sequence_behavior(g, seq)?.video_params.first()?;
	Some((v.width, v.height, v.frame_rate))
}

/// The sequence's frame duration as a `(num, den)` timebase pair (the
/// frame rate flipped; the facade's `seq_time_base`). `None` when the
/// sequence has no valid frame rate.
pub fn sequence_time_base(g: &Graph, seq: NodeId) -> Option<(i64, i64)> {
	let (_, _, rate) = sequence_video_params(g, seq)?;
	let num = rate.numerator();
	let den = rate.denominator();
	if num <= 0 || den <= 0 {
		return None;
	}
	Some((den, num))
}

/// The sequence content length (rational seconds): the longest track out
/// point across every track list (the module's `verify_length` overall).
pub fn sequence_length(g: &Graph, seq: NodeId) -> Rational {
	let mut best = Rational::new(0, 1);
	let Some(s) = sequence_behavior(g, seq) else {
		return best;
	};
	for &list_id in &s.track_lists {
		let Some(list) = track_list_behavior(g, list_id) else {
			continue;
		};
		for &track_id in &list.tracks {
			let Some(track) = track_behavior(g, track_id) else {
				continue;
			};
			for &block_id in &track.blocks {
				let Some(entry) = g.get(block_id) else {
					continue;
				};
				let Some(core) = block_core(entry) else {
					continue;
				};
				let out = core.out();
				if out > best {
					best = out;
				}
			}
		}
	}
	best
}

/// Borrow a block's core (any block kind).
fn block_core(entry: &oaknode::graph::NodeEntry) -> Option<&oaknode::block::BlockCore> {
	let any = entry.behavior.as_any()?;
	if let Some(c) = any.downcast_ref::<ClipBlockBehavior>() {
		return Some(&c.core);
	}
	if let Some(g) = any.downcast_ref::<oaknode::block::GapBlockBehavior>() {
		return Some(&g.core);
	}
	any.downcast_ref::<oaknode::block::TransitionBlockBehavior>()
		.map(|t| &t.core)
}

/// The sequence playhead (rational seconds).
pub fn sequence_playhead(g: &Graph, seq: NodeId) -> Rational {
	sequence_behavior(g, seq)
		.map(|s| s.playhead)
		.unwrap_or(Rational::new(0, 1))
}

/// Move the sequence playhead (rational seconds).
pub fn sequence_set_playhead(p: &ProjectRef, seq: NodeId, time: Rational) {
	let mut guard = lock(p);
	if let Some(s) = guard
		.graph
		.get_mut(seq)
		.and_then(|e| e.behavior.as_any_mut())
		.and_then(|a| a.downcast_mut::<SequenceBehavior>())
	{
		s.playhead = time;
	}
}

// ---------------------------------------------------------------------------
// Frame timestamps <-> rational seconds
// ---------------------------------------------------------------------------

/// Greatest common divisor (1 when both are zero).
fn gcd(a: i64, b: i64) -> i64 {
	let (mut a, mut b) = (a.abs(), b.abs());
	while b != 0 {
		let t = b;
		b = a % b;
		a = t;
	}
	if a == 0 {
		1
	} else {
		a
	}
}

/// Rational seconds -> timestamp in timebase units, rounding half away
/// from zero (the facade's `rational_to_ts`, `Timecode::k_round`).
pub fn rational_to_ts(r: Rational, tb: (i64, i64)) -> i64 {
	let (num, den) = (r.numerator(), r.denominator());
	if den == 0 || tb.0 == 0 || tb.1 == 0 {
		return 0;
	}
	let n = num as i128 * tb.1 as i128;
	let d = den as i128 * tb.0 as i128;
	let q = n / d;
	let r = n % d;
	let rr = if r < 0 { -r } else { r };
	let dd = if d < 0 { -d } else { d };
	if rr * 2 >= dd {
		(q + if n < 0 { -1 } else { 1 }) as i64
	} else {
		q as i64
	}
}

/// Timestamp -> reduced rational seconds (`time = ts * tb`).
pub fn ts_to_rational(ts: i64, tb: (i64, i64)) -> Rational {
	let num = ts as i128 * tb.0 as i128;
	let den = tb.1 as i128;
	let g = gcd((num % den) as i64, den as i64) as i128;
	Rational::new((num / g) as i64, (den / g) as i64)
}

// ---------------------------------------------------------------------------
// Tracks and clips
// ---------------------------------------------------------------------------

/// The sequence's track list of `kind`, when it exists.
pub fn track_list_of(g: &Graph, seq: NodeId, kind: TrackType) -> Option<NodeId> {
	for &list_id in &sequence_behavior(g, seq)?.track_lists {
		if track_list_behavior(g, list_id).map(|l| l.kind) == Some(kind) {
			return Some(list_id);
		}
	}
	None
}

/// Find (or create) the sequence's track list of `kind` (the facade's
/// `oaknode_sequence_get_track_list` find-or-create semantics; mirrors the
/// CLI's `find_or_create_track_list`).
pub fn find_or_create_track_list(p: &ProjectRef, seq: NodeId, kind: TrackType) -> Option<NodeId> {
	let mut guard = lock(p);
	if let Some(list) = track_list_of(&guard.graph, seq, kind) {
		return Some(list);
	}
	// Create it: a graph node owned by the sequence.
	let (core, behavior) = TrackListBehavior::create();
	let mut behavior = behavior;
	if let Some(a) = behavior.as_any_mut() {
		if let Some(list) = a.downcast_mut::<TrackListBehavior>() {
			list.kind = kind;
			list.array_base = sequence_behavior(&guard.graph, seq)?.track_lists.len() as i32;
		}
	}
	let list_id = guard.graph.add_node(core, behavior);
	if let Some(s) = guard
		.graph
		.get_mut(seq)
		.and_then(|e| e.behavior.as_any_mut())
		.and_then(|a| a.downcast_mut::<SequenceBehavior>())
	{
		s.track_lists.push(list_id);
	}
	if let Some(l) = guard
		.graph
		.get_mut(list_id)
		.and_then(|e| e.behavior.as_any_mut())
		.and_then(|a| a.downcast_mut::<TrackListBehavior>())
	{
		l.sequence = Some(seq);
	}
	Some(list_id)
}

/// The tracks of the sequence's `kind` list, in stack order.
pub fn track_ids(g: &Graph, seq: NodeId, kind: TrackType) -> Vec<NodeId> {
	track_list_of(g, seq, kind)
		.and_then(|list| track_list_behavior(g, list).map(|l| l.tracks.clone()))
		.unwrap_or_default()
}

/// The clip blocks of a track (gaps skipped), in timeline order.
pub fn clip_ids(g: &Graph, track: NodeId) -> Vec<NodeId> {
	track_behavior(g, track)
		.map(|t| {
			t.blocks
				.iter()
				.copied()
				.filter(|&b| clip_behavior(g, b).is_some())
				.collect()
		})
		.unwrap_or_default()
}

/// A clip's timeline range and media in-point (rational seconds).
pub fn clip_range(g: &Graph, clip: NodeId) -> Option<(Rational, Rational, Rational)> {
	let c = clip_behavior(g, clip)?;
	Some((c.core.in_(), c.core.out(), c.core.media_in))
}

/// The clip's owning track.
pub fn clip_track(g: &Graph, clip: NodeId) -> Option<NodeId> {
	clip_behavior(g, clip)?.core.track
}

/// The first footage node feeding `id` (upstream BFS over input edges),
/// mirroring the facade's `oaknode_node_find_input_footage`.
pub fn find_input_footage(g: &Graph, id: NodeId) -> Option<NodeId> {
	let mut frontier = vec![id];
	let mut visited: Vec<NodeId> = Vec::new();
	while !frontier.is_empty() {
		let mut next = Vec::new();
		for cur in frontier {
			if visited.contains(&cur) {
				continue;
			}
			visited.push(cur);
			let entry = g.get(cur)?;
			if entry.behavior.type_id() == "org.olivevideoeditor.Olive.footage" && cur != id {
				return Some(cur);
			}
			for (src, _, _) in g.input_connections(cur) {
				next.push(src);
			}
		}
		frontier = next;
	}
	None
}

/// The media filename feeding a clip (upstream BFS + footage behavior).
pub fn clip_media_filename(g: &Graph, clip: NodeId) -> Option<String> {
	let footage = find_input_footage(g, clip)?;
	Some(footage_behavior(g, footage)?.filename.clone())
}

// ---------------------------------------------------------------------------
// Footage
// ---------------------------------------------------------------------------

/// The footage's probed duration in seconds (`None` when unprobed).
pub fn footage_duration_seconds(g: &Graph, id: NodeId) -> Option<f64> {
	let d = footage_behavior(g, id)?.duration();
	let den = d.denominator();
	if den == 0 {
		return None;
	}
	let seconds = d.numerator() as f64 / den as f64;
	(seconds > 0.0).then_some(seconds)
}

/// Import a media file into the project's root folder (the facade's
/// `oakengine_project_import_footage`: the node is created in the graph
/// and one undoable "Import Footage" entry adds it to the root folder).
pub fn import_footage(project: &ProjectRef, path: &Path) -> Result<NodeId, String> {
	if !path.exists() {
		return Err(format!("file does not exist: {}", path.display()));
	}
	let filename = path.to_string_lossy().into_owned();
	let (root, id) = {
		let mut guard = lock(project);
		if !guard.root.valid() {
			return Err("the project has no root folder".to_string());
		}
		let (mut core, behavior) = FootageBehavior::create();
		core.set_standard_value("file_in", -1, oaknode::value::NodeValue::Text(filename.clone()));
		let id = guard.graph.add_node(core, behavior);
		if let Some(f) = guard
			.graph
			.get_mut(id)
			.and_then(|e| e.behavior.as_any_mut())
			.and_then(|a| a.downcast_mut::<FootageBehavior>())
		{
			f.filename = filename.clone();
			let _ = f.probe();
		}
		let label = path
			.file_name()
			.map(|f| f.to_string_lossy().into_owned())
			.unwrap_or_else(|| filename.clone());
		if let Some(e) = guard.graph.get_mut(id) {
			e.core.label = label;
		}
		(guard.root, id)
	};
	let cmd = oaktask::nodeops::folder_add_child_command(
		(project.clone(), root),
		(project.clone(), id),
	);
	oakundo::global::push(cmd, "Import Footage").map_err(|e| e.to_string())?;
	Ok(id)
}

// ---------------------------------------------------------------------------
// Marker list / workarea auxiliaries
//
// The module's sequences never initialize their own marker/workarea state
// (`SequenceBehavior` defaults both to empty handles), so the app — like
// the facade before it — materializes one of each per open sequence. The
// engine owns the handles and releases them before the project drops.
// ---------------------------------------------------------------------------

/// Create a marker-list handle (owned, refcount 1).
pub fn marker_list_create() -> CHandle {
	oaktimeline::handle::make_owned(oaktimeline::marker::TimelineMarkerList::new())
}

/// Create a workarea handle (owned, refcount 1).
pub fn workarea_create() -> CHandle {
	oaktimeline::handle::make_owned(oaktimeline::workarea::TimelineWorkArea::new())
}

/// Release an owned marker-list / workarea handle (NULL-safe; the handle
/// is dead afterwards).
pub fn release_handle(h: &mut CHandle) {
	if let Some(release) = h.release {
		// SAFETY: `h` is an owned handle from `marker_list_create` /
		// `workarea_create`; the release runs the box's own destructor once
		// per owned reference, and the caller drops the handle afterwards.
		unsafe { release(h.ctx) };
	}
	h.ctx = std::ptr::null_mut();
}

/// One marker as the timeline shows it: in-point, name, color index.
pub fn markers_of(list: &CHandle) -> Vec<(Rational, String, i32)> {
	if list.is_null() {
		return Vec::new();
	}
	// SAFETY: `list` boxes a `TimelineMarkerList` (created by
	// `marker_list_create`); the read is shared and brief.
	let Some(l) = (unsafe {
		oaktimeline::handle::get::<std::sync::Arc<std::sync::Mutex<oaktimeline::marker::TimelineMarkerList>>>(
			list,
		)
	}) else {
		return Vec::new();
	};
	let l = l.lock().unwrap_or_else(|e| e.into_inner());
	(0..l.size())
		.filter_map(|i| l.at(i))
		.map(|m| (m.time().in_(), m.name().to_string(), m.color()))
		.collect()
}

/// The index of the first marker whose in-point equals `time`.
pub fn marker_index_at(list: &CHandle, time: Rational) -> Option<usize> {
	if list.is_null() {
		return None;
	}
	// SAFETY: as `markers_of`.
	let l = unsafe {
		oaktimeline::handle::get::<std::sync::Arc<std::sync::Mutex<oaktimeline::marker::TimelineMarkerList>>>(
			list,
		)
	}?;
	let l = l.lock().unwrap_or_else(|e| e.into_inner());
	(0..l.size()).find(|&i| l.at(i).map(|m| m.time().in_()) == Some(time))
}

/// The workarea's `(enabled, range)`.
pub fn workarea_state(wa: &CHandle) -> Option<(bool, TimeRange)> {
	if wa.is_null() {
		return None;
	}
	// SAFETY: `wa` boxes a `TimelineWorkArea` (created by
	// `workarea_create`); the read is shared and brief.
	let w = unsafe {
		oaktimeline::handle::get::<std::sync::Arc<std::sync::Mutex<oaktimeline::workarea::TimelineWorkArea>>>(
			wa,
		)
	}?;
	let w = w.lock().unwrap_or_else(|e| e.into_inner());
	Some((w.enabled(), *w.range()))
}

/// Live (non-undoable) workarea write: enable flag plus range.
pub fn workarea_set(wa: &CHandle, enabled: bool, range: TimeRange) {
	if wa.is_null() {
		return;
	}
	// SAFETY: `wa` boxes a `TimelineWorkArea`; the engine writes it only
	// from the UI thread.
	if let Some(w) = unsafe {
		oaktimeline::handle::get_mut::<std::sync::Arc<std::sync::Mutex<oaktimeline::workarea::TimelineWorkArea>>>(
			wa,
		)
	} {
		let mut w = w.lock().unwrap_or_else(|e| e.into_inner());
		w.set_enabled(enabled);
		w.set_range(range);
	}
}

// ---------------------------------------------------------------------------
// Project library (M13 D4): the write-through database the manager browses
//
// The facade's library_* exports serialized rows to JSON only because they
// crossed the C ABI; the direct calls below return plain values.
// ---------------------------------------------------------------------------

/// The configured default library as a parsed URI; an error when the
/// write-through backend is disabled or the path does not resolve.
fn library() -> Result<oakstorage::uri::StorageUri, String> {
	if !oakstorage::writethrough::storage_enabled() {
		return Err("the project library is not configured".to_string());
	}
	let uri = oakstorage::writethrough::library_uri()
		.ok_or_else(|| "the project library path does not resolve".to_string())?;
	oakstorage::uri::StorageUri::parse(&uri).map_err(|e| e.to_string())
}

/// The library URI selecting one row (`…?project=<uuid>`).
fn project_uri(uuid: &str) -> Result<oakstorage::uri::StorageUri, String> {
	let uri = library()?;
	oakstorage::uri::StorageUri::parse(&format!("{}?project={uuid}", uri.to_uri_string()))
		.map_err(|e| e.to_string())
}

/// The library rows, most recently modified first (the project manager's
/// data source). With storage disabled the result is the empty list, not
/// an error (the facade contract).
pub fn library_list() -> Result<Vec<LibraryProject>, String> {
	if !oakstorage::writethrough::storage_enabled() {
		return Ok(Vec::new());
	}
	let uri = library()?;
	let backend = oakstorage::writethrough::backend();
	let infos = backend.list_projects(&uri).map_err(|e| e.to_string())?;
	Ok(infos
		.into_iter()
		.map(|info| {
			let stats = backend.project_stats(&uri, &info.uuid).unwrap_or_default();
			LibraryProject {
				uuid: info.uuid,
				name: info.name,
				created_at: info.created_at.and_utc().timestamp(),
				modified_at: info.modified_at.and_utc().timestamp(),
				duration_ms: stats.duration_ms,
				track_count: stats.track_count,
				clip_count: stats.clip_count,
				footage_count: stats.footage_count,
			}
		})
		.collect())
}

/// Create a blank project row named `name`; returns its uuid. The row
/// lands immediately (one `kind='import'` command), so the manager list
/// shows it before the first edit.
pub fn library_create(name: &str) -> Result<String, String> {
	if name.trim().is_empty() {
		return Err("invalid name".to_string());
	}
	let uri = library()?;
	let project = create_project();
	let uuid = {
		let mut guard = lock(&project);
		guard
			.settings
			.insert("projectname".to_string(), name.to_string());
		guard.uuid.clone()
	};
	let result = oakstorage::writethrough::backend()
		.save_project(&project, &uri, 0)
		.map_err(|e| e.to_string());
	result?;
	Ok(uuid)
}

/// Delete the library row `uuid` (cascades settings / snapshots /
/// journal).
pub fn library_delete(uuid: &str) -> Result<(), String> {
	if uuid.is_empty() {
		return Err("invalid uuid".to_string());
	}
	oakstorage::writethrough::backend()
		.delete_project(&library()?, uuid)
		.map_err(|e| e.to_string())
}

/// Rename the library row `uuid` (the manager's list name).
pub fn library_rename(uuid: &str, name: &str) -> Result<(), String> {
	if uuid.is_empty() || name.trim().is_empty() {
		return Err("invalid uuid or name".to_string());
	}
	oakstorage::writethrough::backend()
		.rename_project(&library()?, uuid, name.trim())
		.map_err(|e| e.to_string())
}

/// Duplicate the library row `uuid` (history included) under a fresh
/// uuid; returns the new row's uuid. `None` name defaults to
/// `<name> (copy)` backend-side.
pub fn library_duplicate(uuid: &str) -> Result<String, String> {
	if uuid.is_empty() {
		return Err("invalid uuid".to_string());
	}
	let info = oakstorage::writethrough::backend()
		.duplicate_project(&library()?, uuid, None)
		.map_err(|e| e.to_string())?;
	Ok(info.uuid)
}

/// Import a `.ove` / `.otio` / `.fcpxml` project file as a new library
/// row; returns the new row's uuid.
pub fn library_import(path: &Path) -> Result<String, String> {
	let file_uri = oakstorage::uri::StorageUri::parse(&path.to_string_lossy())
		.map_err(|e| e.to_string())?;
	oakstorage::writethrough::backend()
		.import_from_file(&library()?, &file_uri)
		.map_err(|e| e.to_string())
}

/// Export the library row `uuid` to `path`; the format is dispatched by
/// extension through the oakstorage registry.
pub fn library_export(uuid: &str, path: &Path) -> Result<(), String> {
	if uuid.is_empty() {
		return Err("invalid uuid".to_string());
	}
	let file_uri = oakstorage::uri::StorageUri::parse(&path.to_string_lossy())
		.map_err(|e| e.to_string())?;
	oakstorage::writethrough::backend()
		.export_to_file(&library()?, uuid, &file_uri)
		.map_err(|e| e.to_string())
}

/// Load the library row `uuid` as a fresh project (the modified flag is
/// cleared, mirroring the facade's `oakengine_project_load_library`; the
/// undo-stack clear and the storage bind are the engine adopt path's job).
pub fn library_open(uuid: &str) -> Result<ProjectRef, String> {
	if uuid.is_empty() {
		return Err("invalid uuid".to_string());
	}
	let result = oakstorage::writethrough::backend()
		.load(&project_uri(uuid)?)
		.map_err(|e| e.to_string())?;
	if result.project.is_null() {
		return Err(format!(
			"library load of {uuid} returned no project (info code {})",
			result.version_info
		));
	}
	let handle = result.project;
	let project = unsafe { oakstorage::nodeutil::project_arc(&handle) }
		.map_err(|e| e.to_string())?;
	lock(&project).set_modified(false);
	Ok(project)
}

// ---------------------------------------------------------------------------
// Write-through binding (the engine's per-project session state)
// ---------------------------------------------------------------------------

/// Bind `project` to the configured default library and return the handle
/// the binding was registered under (the caller keeps it for
/// [`storage_bound`] / [`storage_last_error`] queries and releases it with
/// [`storage_unbind`]). No-op (None still returned) semantics mirror the
/// facade: an unconfigured library leaves the project unbound but the
/// handle is still usable for queries.
pub fn storage_bind(project: &ProjectRef) -> CHandle {
	let handle = oakstorage::nodeutil::make_project_owned(project.clone());
	oakstorage::writethrough::bind_project(handle);
	handle
}

/// Whether the project behind `handle` is bound to the write-through
/// session (the status bar's write state).
pub fn storage_bound(handle: &CHandle) -> bool {
	oakstorage::writethrough::is_bound(*handle)
}

/// The last write-through / snapshot error of the project behind
/// `handle`, if any.
pub fn storage_last_error(handle: &CHandle) -> Option<String> {
	oakstorage::writethrough::last_error(*handle)
}

/// Flush the project's pending writes, drop its binding and release the
/// query handle (the engine's project-close path).
pub fn storage_unbind(handle: CHandle) {
	oakstorage::writethrough::unbind_project(handle);
	oakstorage::nodeutil::release_project(handle);
}

/// Flush every bound project and stop the snapshot thread (app exit).
pub fn storage_flush() {
	oakstorage::writethrough::flush_all();
}

// ---------------------------------------------------------------------------
// Undoable edit primitives
//
// The timeline edits the facade's `oakengine_sequence_*` / `oakengine_clip_*`
// exports performed, rebuilt over the oaktimeline command structs and the
// oakundo global stack's safe [`oakundo::global::push`]. Every entry point
// pushes exactly one undo row (composite edits assemble a multi command).
// ---------------------------------------------------------------------------

/// Push one command onto the global undo stack (redo then record).
pub fn push_command(cmd: oakundo::undocommand::UndoCommand, name: &str) -> Result<(), String> {
	oakundo::global::push(cmd, name).map_err(|e| e.to_string())
}

/// Assemble `children` into ONE multi command and push it (an empty set
/// is a no-op, mirroring the stack's empty-multi discard).
pub fn push_multi_command(
	children: Vec<oakundo::undocommand::UndoCommand>,
	name: &str,
) -> Result<(), String> {
	if children.is_empty() {
		return Ok(());
	}
	let mut multi = oakundo::undocommand::UndoCommand::multi();
	for child in children {
		multi.multi_add_child(child);
	}
	push_command(multi, name)
}

/// Push one command (private alias).
fn push(cmd: oakundo::undocommand::UndoCommand, name: &str) -> Result<(), String> {
	push_command(cmd, name)
}

/// Assemble and push a multi command (private alias).
fn push_multi(children: Vec<oakundo::undocommand::UndoCommand>, name: &str) -> Result<(), String> {
	push_multi_command(children, name)
}

/// An undoable edge add (the module's `oaknode_node_connect_undoable`
/// semantics): validated at creation (existence, connectability, not
/// already connected); the redo connects, the undo disconnects the input.
pub fn connect_command(
	p: &ProjectRef,
	from: NodeId,
	to: NodeId,
	input_id: &str,
) -> Result<oakundo::undocommand::UndoCommand, String> {
	{
		let g = lock(p);
		if !g.graph.is_valid(from) || !g.graph.is_valid(to) {
			return Err("connect: node not found".to_string());
		}
		let entry = g.graph.get(to).ok_or("connect: node not found")?;
		let input = entry
			.core
			.get_input(input_id)
			.ok_or_else(|| format!("connect: no input \"{input_id}\""))?;
		if !input.is_connectable() {
			return Err(format!("connect: input \"{input_id}\" is not connectable"));
		}
		if g.graph.connected_output(to, input_id, -1).is_some() {
			return Err(format!("connect: input \"{input_id}\" is already connected"));
		}
	}
	let (p1, p2) = (p.clone(), p.clone());
	let (id1, id2) = (input_id.to_string(), input_id.to_string());
	Ok(oakundo::undocommand::UndoCommand::from_closures(
		move || {
			let mut g = lock(&p1);
			let _ = g.graph.connect(from, to, &id1, -1);
		},
		move || {
			let mut g = lock(&p2);
			g.graph.disconnect_input(to, &id2, -1);
		},
	))
}

/// An undoable edge remove: succeeds even when nothing is connected (the
/// redo is then a no-op, mirroring the C++ command's redo swallowing). The
/// undo re-connects the edge captured at construction — the module's
/// `oaknode_node_disconnect_undoable` did not model this (documented
/// deviation); the app retains the source node, so its effect-chain edits
/// undo faithfully.
pub fn disconnect_command(
	p: &ProjectRef,
	to: NodeId,
	input_id: &str,
) -> Result<oakundo::undocommand::UndoCommand, String> {
	let source = {
		let g = lock(p);
		let has_input = g
			.graph
			.get(to)
			.map(|e| e.core.has_input(input_id))
			.unwrap_or(false);
		if !has_input {
			return Err(format!("disconnect: no input \"{input_id}\""));
		}
		g.graph.connected_output(to, input_id, -1)
	};
	let (p1, p2) = (p.clone(), p.clone());
	let (id1, id2) = (input_id.to_string(), input_id.to_string());
	Ok(oakundo::undocommand::UndoCommand::from_closures(
		move || {
			let mut g = lock(&p1);
			g.graph.disconnect_input(to, &id1, -1);
		},
		move || {
			if let Some(from) = source {
				let mut g = lock(&p2);
				let _ = g.graph.connect(from, to, &id2, -1);
			}
		},
	))
}

/// An undoable context-position set (the facade's
/// `oakengine_node_set_context_position`): the first entry is established
/// by the redo itself; the undo restores the previous position or removes
/// the entry it created.
pub fn set_context_position_command(
	p: &ProjectRef,
	node: NodeId,
	context: NodeId,
	x: f64,
	y: f64,
) -> Result<oakundo::undocommand::UndoCommand, String> {
	let old = {
		let g = lock(p);
		if !g.graph.is_valid(node) || !g.graph.is_valid(context) {
			return Err("set position: node not found".to_string());
		};
		g.graph
			.get(node)
			.and_then(|e| {
				e.core
					.context_positions
					.iter()
					.find(|(c, _, _)| *c == context)
					.map(|(_, pos, expanded)| (*pos, *expanded))
			})
	};
	let (p1, p2) = (p.clone(), p.clone());
	Ok(oakundo::undocommand::UndoCommand::from_closures(
		move || {
			let mut g = lock(&p1);
			if let Some(e) = g.graph.get_mut(node) {
				e.core.set_context_position(context, x, y, false);
			}
		},
		move || {
			let mut g = lock(&p2);
			if let Some(e) = g.graph.get_mut(node) {
				match old {
					Some(((ox, oy), expanded)) => {
						e.core.set_context_position(context, ox, oy, expanded);
					}
					None => {
						e.core.remove_from_context(context);
					}
				}
			}
		},
	))
}

/// Append a track of `kind` to the sequence (undoable "Add Track"; the
/// module's `TimelineAddTrackCommand`), returning the new track's index.
pub fn add_track(p: &ProjectRef, seq: NodeId, kind: TrackType) -> Result<usize, String> {
	let list = find_or_create_track_list(p, seq, kind)
		.ok_or_else(|| "sequence has no track list for this type".to_string())?;
	push(
		oaktimeline::undogeneral::TimelineAddTrackCommand::new(node_ref(p, list)).to_command(),
		"Add Track",
	)?;
	let g = lock(p);
	let n = track_list_behavior(&g.graph, list)
		.map(|l| l.tracks.len())
		.ok_or_else(|| "add track command produced no track list".to_string())?;
	if n == 0 {
		return Err("add track command produced no track".to_string());
	}
	Ok(n - 1)
}

/// Remove `track` from its list (undoable "Remove Track"; the module's
/// `TimelineRemoveTrackCommand` redo performs the full list+graph
/// removal, so no live compensation is needed).
pub fn remove_track(p: &ProjectRef, track: NodeId) -> Result<(), String> {
	push(
		oaktimeline::undogeneral::TimelineRemoveTrackCommand::new(node_ref(p, track)).to_command(),
		"Remove Track",
	)
}

/// A track's height in internal units (`None` when the id is stale).
pub fn track_height(p: &ProjectRef, track: NodeId) -> Option<f64> {
	let g = lock(p);
	track_behavior(&g.graph, track).map(|t| t.height)
}

/// A track's muted flag (`None` when the id is stale). Muted means
/// "silenced" on audio tracks and "hidden" on video/subtitle tracks
/// (Olive parity: one flag drives both).
pub fn track_muted(p: &ProjectRef, track: NodeId) -> Option<bool> {
	let g = lock(p);
	track_behavior(&g.graph, track).map(|t| t.muted)
}

/// A track's locked flag (`None` when the id is stale).
pub fn track_locked(p: &ProjectRef, track: NodeId) -> Option<bool> {
	let g = lock(p);
	track_behavior(&g.graph, track).map(|t| t.locked)
}

/// An undoable track-flag set (the closures capture the previous value, so
/// the undo restores it exactly). Shared by the mute/hide and lock toggles.
fn set_track_flag(
	p: &ProjectRef,
	track: NodeId,
	field: TrackFlag,
	value: bool,
	name: &str,
) -> Result<(), String> {
	let old = {
		let g = lock(p);
		let t = track_behavior(&g.graph, track)
			.ok_or_else(|| "set track flag: the node is not a track".to_string())?;
		match field {
			TrackFlag::Muted => t.muted,
			TrackFlag::Locked => t.locked,
		}
	};
	if old == value {
		return Ok(());
	}
	let (p1, p2) = (p.clone(), p.clone());
	push(
		oakundo::undocommand::UndoCommand::from_closures(
			move || {
				let mut g = lock(&p1);
				if let Some(t) = g
					.graph
					.get_mut(track)
					.and_then(|e| e.behavior.as_any_mut())
					.and_then(|a| a.downcast_mut::<TrackBehavior>())
				{
					match field {
						TrackFlag::Muted => t.muted = value,
						TrackFlag::Locked => t.locked = value,
					}
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(t) = g
					.graph
					.get_mut(track)
					.and_then(|e| e.behavior.as_any_mut())
					.and_then(|a| a.downcast_mut::<TrackBehavior>())
				{
					match field {
						TrackFlag::Muted => t.muted = old,
						TrackFlag::Locked => t.locked = old,
					}
				}
			},
		),
		name,
	)
}

/// The track flags the undoable setters cover.
#[derive(Clone, Copy)]
enum TrackFlag {
	/// Muted (audio) / hidden (video, subtitle).
	Muted,
	/// Locked against clip edits.
	Locked,
}

/// Set a track's muted flag (undoable "Set Track Muted"). On video and
/// subtitle tracks this is the visibility (show/hide) toggle.
pub fn set_track_muted(p: &ProjectRef, track: NodeId, muted: bool) -> Result<(), String> {
	set_track_flag(p, track, TrackFlag::Muted, muted, "Set Track Muted")
}

/// Set a track's locked flag (undoable "Set Track Locked"). Locked tracks
/// reject clip edits (the app layer refuses trim/move/split/delete).
pub fn set_track_locked(p: &ProjectRef, track: NodeId, locked: bool) -> Result<(), String> {
	set_track_flag(p, track, TrackFlag::Locked, locked, "Set Track Locked")
}

/// Set a track's height in internal units (NOT undoable, mirroring the
/// facade's `oakengine_track_set_height`).
pub fn set_track_height(p: &ProjectRef, track: NodeId, height: f64) {
	if height <= 0.0 {
		return;
	}
	let mut g = lock(p);
	if let Some(t) = g
		.graph
		.get_mut(track)
		.and_then(|e| e.behavior.as_any_mut())
		.and_then(|a| a.downcast_mut::<TrackBehavior>())
	{
		t.height = height;
	}
}

/// Place a clip of `footage` on track `track_index` of the sequence's
/// `kind` list (undoable "Add Clip", one row): the clip block is created
/// in the project, placed by the module's `TrackPlaceBlockCommand`, and
/// the footage is wired to the clip's `tex_in` — the facade's
/// `oakengine_sequence_add_footage_clip_ex` composition, minus the
/// scratch-project dance (the app keeps everything in one project).
///
/// `in_ts`/`out_ts`/`media_in_ts` are frame timestamps in the sequence's
/// frame-rate timebase. On undo the block is detached from the track but
/// left as an orphan node in the project graph (the module command's
/// documented behavior).
pub fn place_footage_clip(
	p: &ProjectRef,
	seq: NodeId,
	footage: NodeId,
	kind: TrackType,
	track_index: usize,
	in_ts: i64,
	out_ts: i64,
	media_in_ts: i64,
) -> Result<NodeId, String> {
	if in_ts < 0 || out_ts <= in_ts || media_in_ts < 0 {
		return Err("invalid clip range (need 0 <= in < out and media_in >= 0)".to_string());
	}
	if kind != TrackType::Video && kind != TrackType::Audio {
		return Err("clips are only supported on video and audio tracks".to_string());
	}
	let (tb, list) = {
		let g = lock(p);
		if footage_behavior(&g.graph, footage).is_none() {
			return Err("the footage node is not in the project".to_string());
		}
		let tb = sequence_time_base(&g.graph, seq)
			.ok_or_else(|| "sequence has no valid frame rate".to_string())?;
		let list = track_list_of(&g.graph, seq, kind)
			.ok_or_else(|| "sequence has no track list for this type".to_string())?;
		(tb, list)
	};
	let track_count = {
		let g = lock(p);
		track_list_behavior(&g.graph, list)
			.map(|l| l.tracks.len())
			.unwrap_or(0)
	};
	if track_index >= track_count {
		return Err(format!(
			"track index {track_index} out of range ({track_count} tracks)"
		));
	}

	let in_r = ts_to_rational(in_ts, tb);
	let out_r = ts_to_rational(out_ts, tb);
	let media_r = ts_to_rational(media_in_ts, tb);
	let length = out_r - in_r;

	// The clip block, positioned by media-in + length (the facade's
	// `oaknode_clip_set_media_in` + `oaknode_block_set_length_and_media_in`).
	let clip = {
		let mut g = lock(p);
		let (core, behavior) = oaknode::block::clip_create();
		let id = g.graph.add_node(core, behavior);
		if let Some(c) = g
			.graph
			.get_mut(id)
			.and_then(|e| e.behavior.as_any_mut())
			.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
		{
			c.core.media_in = media_r;
			c.core.set_length_and_media_in(length);
		}
		id
	};

	let place = oaktimeline::undopointer::TrackPlaceBlockCommand::new(
		node_ref(p, list),
		track_index as i32,
		node_ref(p, clip),
		in_r,
	)
	.to_command();
	let edge = connect_command(p, footage, clip, oaknode::block::clip_input::TEXTURE_INPUT)?;
	push_multi(vec![place, edge], "Add Clip")?;
	Ok(clip)
}

/// Split `clip` at `time_ts` (a frame timestamp strictly inside the
/// clip's range), undoable "Split Clip" (the module's
/// `BlockSplitCommand`).
pub fn split_clip(p: &ProjectRef, clip: NodeId, time_ts: i64) -> Result<(), String> {
	let (tb, in_r, out_r) = {
		let g = lock(p);
		let tb = clip_track(&g.graph, clip)
			.and_then(|t| track_behavior(&g.graph, t))
			.and_then(|t| t.track_list)
			.and_then(|l| track_list_behavior(&g.graph, l))
			.and_then(|l| l.sequence)
			.and_then(|s| sequence_time_base(&g.graph, s))
			.ok_or_else(|| "the clip's sequence has no valid frame rate".to_string())?;
		let (in_r, out_r, _) = clip_range(&g.graph, clip)
			.ok_or_else(|| "the node is not a clip".to_string())?;
		(tb, in_r, out_r)
	};
	let point = ts_to_rational(time_ts, tb);
	if point <= in_r || point >= out_r {
		return Err(format!("split time {time_ts} is not strictly inside the clip"));
	}
	push(
		oaktimeline::undosplit::BlockSplitCommand::new(node_ref(p, clip), point).to_command(),
		"Split Clip",
	)
}

/// Trim `clip`'s timeline range to `[new_in_ts, new_out_ts)` (undoable
/// "Trim Clip"; the facade's `oakengine_clip_trim` semantics: one end at
/// a time, trim-in anchors the OUT, trim-out anchors the IN — the
/// module's own `BlockTrimCommand` applies its length setters with
/// inverted semantics, so the closures carry the correct mapping).
pub fn trim_clip(p: &ProjectRef, clip: NodeId, new_in_ts: i64, new_out_ts: i64) -> Result<(), String> {
	use oaknode::block::BlockCore;
	if new_in_ts < 0 || new_out_ts <= new_in_ts {
		return Err("invalid trim range (need 0 <= new_in < new_out)".to_string());
	}
	let (tb, old_in, old_out, old_length) = {
		let g = lock(p);
		let tb = clip_track(&g.graph, clip)
			.and_then(|t| track_behavior(&g.graph, t))
			.and_then(|t| t.track_list)
			.and_then(|l| track_list_behavior(&g.graph, l))
			.and_then(|l| l.sequence)
			.and_then(|s| sequence_time_base(&g.graph, s))
			.ok_or_else(|| "the clip's sequence has no valid frame rate".to_string())?;
		let (in_r, out_r, _) =
			clip_range(&g.graph, clip).ok_or_else(|| "the node is not a clip".to_string())?;
		let length = out_r - in_r;
		(tb, in_r, out_r, length)
	};
	if new_in_ts == rational_to_ts(old_in, tb) && new_out_ts == rational_to_ts(old_out, tb) {
		return Ok(());
	}
	let new_in = ts_to_rational(new_in_ts, tb);
	let new_out = ts_to_rational(new_out_ts, tb);

	/// A trim closure command: `set` applies the length (in anchored or
	/// out anchored) for both redo and undo with the captured values.
	fn trim_cmd(
		p: &ProjectRef,
		clip: NodeId,
		out_anchored: bool,
		old: Rational,
		new: Rational,
	) -> oakundo::undocommand::UndoCommand {
		let (p1, p2) = (p.clone(), p.clone());
		oakundo::undocommand::UndoCommand::from_closures(
			move || {
				let mut g = lock(&p1);
				if let Some(c) = g
					.graph
					.get_mut(clip)
					.and_then(|e| e.behavior.as_any_mut())
					.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
				{
					if out_anchored {
						BlockCore::set_length_and_media_out(&mut c.core, new);
					} else {
						BlockCore::set_length_and_media_in(&mut c.core, new);
					}
				}
			},
			move || {
				let mut g = lock(&p2);
				if let Some(c) = g
					.graph
					.get_mut(clip)
					.and_then(|e| e.behavior.as_any_mut())
					.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
				{
					if out_anchored {
						BlockCore::set_length_and_media_out(&mut c.core, old);
					} else {
						BlockCore::set_length_and_media_in(&mut c.core, old);
					}
				}
			},
		)
	}

	let mut children = Vec::new();
	if new_in != old_in {
		// in-trim: length = block out - new in (out anchored).
		children.push(trim_cmd(p, clip, true, old_length, old_out - new_in));
	}
	if new_out != old_out {
		// out-trim: length = new out - new in (in anchored); the old
		// length is the post-in-trim length (out - new in) when both ends
		// move.
		let post_in_trim = old_out - new_in;
		children.push(trim_cmd(p, clip, false, post_in_trim, new_out - new_in));
	}
	push_multi(children, "Trim Clip")
}

/// Move `clip` within its track so its in point becomes `new_in_ts`
/// (undoable "Move Clip"; the module's `TrackMoveBlockCommand` — the old
/// spot becomes a gap, length and media-in are preserved).
pub fn move_clip(p: &ProjectRef, clip: NodeId, new_in_ts: i64) -> Result<(), String> {
	if new_in_ts < 0 {
		return Err("invalid move target".to_string());
	}
	let (tb, list, track_index) = {
		let g = lock(p);
		let track = clip_track(&g.graph, clip).ok_or_else(|| "the clip is not on a track".to_string())?;
		let list = track_behavior(&g.graph, track)
			.and_then(|t| t.track_list)
			.ok_or_else(|| "the clip's track has no list".to_string())?;
		let track_index = track_behavior(&g.graph, track).map(|t| t.index).unwrap_or(0);
		let tb = track_list_behavior(&g.graph, list)
			.and_then(|l| l.sequence)
			.and_then(|s| sequence_time_base(&g.graph, s))
			.ok_or_else(|| "the sequence has no valid frame rate".to_string())?;
		(tb, list, track_index)
	};
	push(
		oaktimeline::undopointer::TrackMoveBlockCommand::new(
			node_ref(p, list),
			track_index,
			node_ref(p, clip),
			ts_to_rational(new_in_ts, tb),
		)
		.to_command(),
		"Move Clip",
	)
}

/// Move `clip` to a different track at `new_in_ts` (undoable "Move Clip
/// to Track", one row): the source spot becomes a gap, the block's in
/// point is re-homed, and the clip is placed on the destination track
/// (the facade's `oakengine_sequence_move_clip_to_track` composition).
pub fn move_clip_to_track(
	p: &ProjectRef,
	clip: NodeId,
	dest_track: NodeId,
	new_in_ts: i64,
) -> Result<(), String> {
	if new_in_ts < 0 {
		return Err("invalid move target".to_string());
	}
	let (tb, list, dest_index, source_track) = {
		let g = lock(p);
		let source_track =
			clip_track(&g.graph, clip).ok_or_else(|| "the clip is not on a track".to_string())?;
		let list = track_behavior(&g.graph, dest_track)
			.and_then(|t| t.track_list)
			.ok_or_else(|| "the destination track has no list".to_string())?;
		let dest_index = track_behavior(&g.graph, dest_track)
			.map(|t| t.index)
			.unwrap_or(0);
		let tb = track_list_behavior(&g.graph, list)
			.and_then(|l| l.sequence)
			.and_then(|s| sequence_time_base(&g.graph, s))
			.ok_or_else(|| "the sequence has no valid frame rate".to_string())?;
		(tb, list, dest_index, source_track)
	};
	let in_r = ts_to_rational(new_in_ts, tb);

	let gap = oaktimeline::undogeneral::TrackReplaceBlockWithGapCommand::new(
		node_ref(p, source_track),
		node_ref(p, clip),
		true,
	)
	.to_command();
	// The module stores a block's position on the block itself, so the
	// place command alone would keep the old in point: re-home it between
	// the gap and the place (the facade's BlockInCmdData step).
	let old_in = {
		let g = lock(p);
		clip_range(&g.graph, clip)
			.map(|(in_r, _, _)| in_r)
			.ok_or_else(|| "the node is not a clip".to_string())?
	};
	let (p1, p2) = (p.clone(), p.clone());
	let rehome = oakundo::undocommand::UndoCommand::from_closures(
		move || {
			let mut g = lock(&p1);
			if let Some(c) = g
				.graph
				.get_mut(clip)
				.and_then(|e| e.behavior.as_any_mut())
				.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
			{
				c.core.set_in(in_r);
			}
		},
		move || {
			let mut g = lock(&p2);
			if let Some(c) = g
				.graph
				.get_mut(clip)
				.and_then(|e| e.behavior.as_any_mut())
				.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
			{
				c.core.set_in(old_in);
			}
		},
	);
	let place = oaktimeline::undopointer::TrackPlaceBlockCommand::new(
		node_ref(p, list),
		dest_index,
		node_ref(p, clip),
		in_r,
	)
	.to_command();
	push_multi(vec![gap, rehome, place], "Move Clip to Track")
}

/// Delete `clip` leaving a gap (undoable "Delete Clips"; the facade's
/// single-clip `oakengine_sequence_delete_clips` composition).
pub fn delete_clip(p: &ProjectRef, clip: NodeId) -> Result<(), String> {
	let source_track = {
		let g = lock(p);
		clip_track(&g.graph, clip).ok_or_else(|| "the clip is not on a track".to_string())?
	};
	let gap = oaktimeline::undogeneral::TrackReplaceBlockWithGapCommand::new(
		node_ref(p, source_track),
		node_ref(p, clip),
		true,
	)
	.to_command();
	let remove = oaktask::nodeops::remove_node_command(p.clone(), clip);
	push_multi(vec![gap, remove], "Delete Clips")
}

/// Delete `clip` and ripple the following content left (undoable
/// "Ripple Delete Clip"; the module's `TrackRippleRemoveAreaCommand`
/// over the clip's range).
pub fn ripple_delete_clip(p: &ProjectRef, clip: NodeId) -> Result<(), String> {
	let (track, range) = {
		let g = lock(p);
		let track =
			clip_track(&g.graph, clip).ok_or_else(|| "the clip is not on a track".to_string())?;
		let (in_r, out_r, _) =
			clip_range(&g.graph, clip).ok_or_else(|| "the node is not a clip".to_string())?;
		(track, TimeRange::new(in_r, out_r))
	};
	push(
		oaktimeline::undoripple::TrackRippleRemoveAreaCommand::new(node_ref(p, track), range)
			.to_command(),
		"Ripple Delete Clip",
	)
}

/// Undoable marker add ("Add Marker"). `time` is a rational seconds
/// in-point; a marker at the same time is rejected (the engine's marker
/// insertion asserts on duplicate times).
pub fn marker_add(markers: &CHandle, time: Rational, name: &str, color: i32) -> Result<(), String> {
	if marker_index_at(markers, time).is_some() {
		return Err("a marker already exists at that time".to_string());
	}
	push(
		oaktimeline::marker::MarkerAddCommand::new(
			*markers,
			TimeRange::new(time, time),
			name,
			color,
		)
		.to_command(),
		"Add Marker",
	)
}

/// Undoable marker remove ("Remove Marker"); `None`-equivalent when no
/// marker sits at `time` (the caller treats it as a benign no-op).
pub fn marker_remove(markers: &CHandle, time: Rational) -> Result<(), String> {
	let Some(index) = marker_index_at(markers, time) else {
		return Err("no marker at that time".to_string());
	};
	push(
		oaktimeline::marker::MarkerRemoveCommand::new(*markers, index).to_command(),
		"Remove Marker",
	)
}

/// Undoable workarea set ("Set Workarea"): the enabled flag plus the
/// in/out range as ONE entry. `old_range` is supplied by the caller (the
/// range read before the change — e.g. the drag-start range of a ruler
/// work-area drag); the enabled flag's previous value is captured by the
/// module command itself.
pub fn workarea_set_undoable(
	wa: &CHandle,
	enabled: bool,
	range: TimeRange,
	old_range: TimeRange,
) -> Result<(), String> {
	push_multi(
		vec![
			oaktimeline::workarea::WorkareaSetEnabledCommand::new(*wa, enabled).to_command(),
			oaktimeline::workarea::WorkareaSetRangeCommand::new_with_old(*wa, range, old_range)
				.to_command(),
		],
		"Set Workarea",
	)
}

/// Undoable removal of `node` from the project graph ("Remove Node"; the
/// module's remove command drops incident edges and restores the entry on
/// undo). The sequence node itself (the graph's output) cannot be
/// removed.
pub fn remove_node(p: &ProjectRef, node: NodeId) -> Result<(), String> {
	push(
		oaktimeline::undocommon::create_remove_command(&node_ref(p, node)),
		"Remove Node",
	)
}

// ---------------------------------------------------------------------------
// Test serialization
// ---------------------------------------------------------------------------

/// A process-wide test lock: the app's tests share the oakundo global
/// stack, the oakcommon config store and the codec decode sessions, so any
/// test touching them serializes on this lock.
#[cfg(test)]
pub fn test_lock() -> std::sync::MutexGuard<'static, ()> {
	static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
	LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
	use super::*;

	/// A project with one sequence holding one track of `kind`; returns the
	/// project, the sequence and the track's node id.
	fn project_with_track(kind: TrackType) -> (ProjectRef, NodeId, NodeId) {
		let project = create_project();
		let seq = create_sequence(&project, "Seq");
		let index = add_track(&project, seq, kind).expect("add a track");
		let track = {
			let g = lock(&project);
			track_ids(&g.graph, seq, kind)[index]
		};
		(project, seq, track)
	}

	/// The track flag setters flip the flag as ONE undoable entry each;
	/// undo restores the previous value and redo re-applies.
	#[test]
	fn track_flag_setters_toggle_and_undo() {
		let _g = test_lock();
		oakundo::global::clear().unwrap();
		let (project, _seq, track) = project_with_track(TrackType::Video);

		assert_eq!(track_muted(&project, track), Some(false));
		assert_eq!(track_locked(&project, track), Some(false));

		set_track_muted(&project, track, true).expect("mute the track");
		assert_eq!(track_muted(&project, track), Some(true));
		set_track_locked(&project, track, true).expect("lock the track");
		assert_eq!(track_locked(&project, track), Some(true));

		oakundo::global::undo().unwrap();
		assert_eq!(track_locked(&project, track), Some(false));
		oakundo::global::undo().unwrap();
		assert_eq!(track_muted(&project, track), Some(false));
		oakundo::global::redo().unwrap();
		assert_eq!(track_muted(&project, track), Some(true));
		oakundo::global::clear().unwrap();
	}

	/// Setting a flag to its current value pushes no undo row.
	#[test]
	fn track_flag_setter_noop_when_unchanged() {
		let _g = test_lock();
		oakundo::global::clear().unwrap();
		let (project, _seq, track) = project_with_track(TrackType::Audio);
		let before = oakundo::global::count().unwrap();

		set_track_muted(&project, track, false).expect("mute already false");
		set_track_locked(&project, track, false).expect("lock already false");
		assert_eq!(oakundo::global::count().unwrap(), before);
		oakundo::global::clear().unwrap();
	}

	/// A stale (non-track) id is rejected, not silently ignored.
	#[test]
	fn track_flag_setters_reject_non_tracks() {
		let _g = test_lock();
		oakundo::global::clear().unwrap();
		let (project, seq, _track) = project_with_track(TrackType::Video);
		assert!(set_track_muted(&project, seq, true).is_err());
		assert!(set_track_locked(&project, seq, true).is_err());
		oakundo::global::clear().unwrap();
	}
}

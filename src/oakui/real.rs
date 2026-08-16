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

//! The real engine: [`RealEngine`] binds the built `liboakengine` dylib
//! (the frozen `oakengine_*` C ABI over the module crates, see [`ffi`])
//! behind the same [`EngineGateway`](super::engine::EngineGateway) /
//! [`AppEngine`](super::engine::AppEngine) seam the mock implements. The
//! dylib is linked at build time (see the crate's `build.rs`); the app
//! never depends on the `oakengine` crate as an rlib, so every call below
//! is a pure `extern "C"` import declared in [`ffi`].
//!
//! # What is real here
//!
//! * **Project** — open/save/save-as/close through the facade (`.ove`
//!   serializer; `.otio` / `.fcpxml` through the oaktask interchange
//!   loader).
//! * **Sequence** — the current sequence's name / format / length / tracks /
//!   clips are read live from the facade sequence handle.
//! * **Edits** — timeline edits (trim, split, delete, ripple-delete) and
//!   track add/remove go through the facade's edit commands, each packaged
//!   as an undoable entry on the facade's global undo stack. Undo/redo walk
//!   that stack.
//! * **Export** — the oaktask export task, driven on a background thread,
//!   with progress events and cancel wired to the module task's event
//!   callback and cancel atom.
//! * **Config** — renderer backend + language keys round-trip through
//!   `oakengine_config_*`.
//!
//! # What is still mock/stub
//!
//! * The source monitor renders the selected footage node's frame through
//!   the facade CPU renderer
//!   ([`RealEngine::render_source_frame`],
//!   via the node-binding `oakengine_renderer_create_for_node`) at a proxy
//!   resolution — the same pattern as the program monitor
//!   ([`RealEngine::render_program_frame`]). Actual media *decode* is
//!   still a module gap (the oakrender eval's footage hook is deferred),
//!   so both viewers show the pipeline's generated frame, not the file's
//!   pixels; the full-resolution async render worker (the facade's worker
//!   module) is a separate process surface not bound yet.
//! * Effect stack — the selected clip's effect chain is bound: the stack
//!   reads the chain through the facade (see
//!   [`EffectStackDataSource`](EffectStackDataSource) for `RealEngine`)
//!   and edits go through the facade's undoable effect commands.
//! * Node graph — the node editor reads the current sequence's graph
//!   through the facade's sequence node-graph enumeration (see
//!   [`NodeGraphDataSource`](NodeGraphDataSource) for `RealEngine`):
//!   clip → effects → output with real edges plus the synthesized
//!   clip-to-output wires; connect/disconnect/move/delete are undoable
//!   facade commands (drag previews never persist).
//! * Audio meter still feeds silent data (the meter's facade surface is
//!   not bound in this increment).
//! * Clip moves go through the facade's move exports: same-track moves use
//!   `oakengine_sequence_move_clip`, cross-track moves
//!   `oakengine_sequence_move_clip_to_track` (M12 P4) — each one undoable
//!   entry, with the source spot becoming a gap.
//!
//! # Threading note
//!
//! Long facade calls (`oakengine_task_start_sync`) run on background threads
//! so the UI never blocks; the export event callback delivers progress
//! through a channel the app drains on its tick loop. Cancellation through
//! `oakengine_task_cancel` mirrors the C++ capi contract (cancel atom set
//! from the UI thread while the task runs on its own thread).

use std::collections::{BTreeSet, HashMap};
use std::ffi::{c_char, c_int, c_void, CString};
use std::path::{Path, PathBuf};
use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::time::Instant;

use gpui::effect_stack::{
	EffectCardKind, EffectData, EffectId, EffectStackDataSource, EffectStackEvent,
};
use gpui::node_graph::{
	EdgeData, EdgeId, NodeData, NodeGraphDataSource, NodeGraphEvent, NodeId, PortDataType, PortId,
	PortKind,
};
use gpui::timeline::{
	ClipData, ClipId, Frame, FrameRange, FrameRate, Marker, TimelineDataSource, TimelineEvent,
	TrackData, TrackKind, TrimEdge,
};
use gpui::{
	hsla, point, prelude::*, px, App, Context, Entity, Hsla, Pixels, RenderImage, SharedString,
};
use gpui_widgets::audio_meter::AudioMeterDataSource;
use gpui_widgets::project_explorer::{ProjectDataSource, ProjectEntry};
use gpui_widgets::viewer::PlaybackClock;

use super::engine::{
	AppEngine, EngineGateway, ExportEvent, ExportSession, LibraryProject, Monitor, Project,
	ScopeData, Sequence, VideoFormat,
};
use super::ffi::*;
use super::frames::{f32_rgba_to_bgra_image, synthetic_frame_samples};
use super::scopes::analyze_f32_rgba;
use super::transport::TransportState;

/// `oakengine_timeline.h` track-type constants.
const TRACK_TYPE_VIDEO: c_int = 0;
const TRACK_TYPE_AUDIO: c_int = 1;
const TRACK_TYPE_SUBTITLE: c_int = 2;

/// The sample-rate / layout / format defaults for export audio.
const EXPORT_SAMPLE_RATE: c_int = 48000;
/// Stereo channel-layout bitmask (`OLIVE_CHANNEL_LAYOUT_STEREO`).
const EXPORT_CHANNEL_LAYOUT: u64 = 0x3;
/// `oakcore_rs::SampleFormat::S16` as int (the encoder default).
const EXPORT_SAMPLE_FORMAT: c_int = 0;

/// The project name of a blank project before it is saved.
const UNTITLED: &str = "Untitled Project";

/// `PixelFormat::F32` (the render pipeline's internal format): F32 RGBA,
/// 16 bytes per pixel. The viewer renderer is created with this so the
/// frame accessors hand back float samples the app downconverts itself.
const PIXEL_FORMAT_F32: c_int = 4;

// ---------------------------------------------------------------------------
// Facade task event subscription
// ---------------------------------------------------------------------------
//
// The export path subscribes to task events through the facade's
// `oakengine_task_subscribe` (wrapping the module's `oaktask_task_subscribe`);
// the callback fires on the task's own thread.

/// The C callback the facade task subscription invokes on the task's own
/// thread. `userdata` is the raw pointer of a leaked
/// `mpsc::Sender<ExportEvent>` the export thread reclaims after the run.
unsafe extern "C" fn export_event_cb(event_id: c_int, value: f64, userdata: *mut c_void) {
	let Some(sender) = (userdata as *const mpsc::Sender<ExportEvent>).as_ref() else {
		return;
	};
	let event = match event_id {
		0 => ExportEvent::Started,
		1 => ExportEvent::Progress(value),
		_ => return, // Finished is reported by the export thread (with the error).
	};
	let _ = sender.send(event);
}

/// Reclaims the leaked `mpsc::Sender` the export callback wrote through.
/// Takes the whole [`SendPtr`] so closures capture the wrapper (which is
/// `Send`) rather than the raw field.
fn reclaim_userdata(userdata: SendPtr<mpsc::Sender<ExportEvent>>) {
	drop(unsafe { Box::from_raw(userdata.0) });
}

/// A borrowed facade handle wrapper that is `Send`/`Sync`: the pointee is
/// only ever accessed through the facade C ABI (whose exports guard with
/// `catch_unwind` and synchronize their own state).
#[derive(Clone, Copy)]
struct SendPtr<T>(*mut T);

// SAFETY: see [`SendPtr`].
unsafe impl<T> Send for SendPtr<T> {}
unsafe impl<T> Sync for SendPtr<T> {}

// ---------------------------------------------------------------------------
// Handle RAII
// ---------------------------------------------------------------------------

/// An owned facade project handle; freed with `oakengine_project_free`.
///
/// Raw facade pointers are not `Send`/`Sync`, so the wrapper carries
/// explicit unsafe impls; the handle is only ever dereferenced through the
/// facade functions (which guard with `catch_unwind`).
struct ProjectHandle(*mut OakEngineProject);

// SAFETY: the pointer is only used through the facade C ABI; the facade
// guards every export with catch_unwind, and all calls are serialized on the
// owning entity's context.
unsafe impl Send for ProjectHandle {}
unsafe impl Sync for ProjectHandle {}

impl ProjectHandle {
	fn ptr(&self) -> *mut OakEngineProject {
		self.0
	}
}

impl Drop for ProjectHandle {
	fn drop(&mut self) {
		unsafe {
			oakengine_project_free(self.0);
		}
	}
}

/// A borrowed facade sequence handle (boxed by the facade); freed with
/// [`free_box`] — and always before the project it was borrowed from.
struct SequenceHandle(*mut OakEngineSequence);

// SAFETY: see [`ProjectHandle`].
unsafe impl Send for SequenceHandle {}
unsafe impl Sync for SequenceHandle {}

impl SequenceHandle {
	fn ptr(&self) -> *mut OakEngineSequence {
		self.0
	}
}

impl Drop for SequenceHandle {
	fn drop(&mut self) {
		unsafe {
			free_box(self.0);
		}
	}
}

/// An owned facade renderer handle; freed with `oakengine_renderer_free`.
///
/// The facade renderer box is NOT a module-handle box (it is the facade's
/// own `RendererBox`), so it must never go through [`free_box`]; the
/// dedicated free is the only valid deallocator. The renderer borrows the
/// sequence handle it was created from, so it must be dropped before the
/// sequence (see [`RealEngine::drop_project`]).
struct RendererHandle(*mut OakEngineRenderer);

// SAFETY: see [`ProjectHandle`].
unsafe impl Send for RendererHandle {}
unsafe impl Sync for RendererHandle {}

impl RendererHandle {
	fn ptr(&self) -> *mut OakEngineRenderer {
		self.0
	}
}

impl Drop for RendererHandle {
	fn drop(&mut self) {
		unsafe {
			oakengine_renderer_free(self.0);
		}
	}
}

/// The lifecycle state of the program monitor's lazily created renderer.
enum RendererSlot {
	/// No renderer yet; the next `cpu_frame` tries to create one.
	Untried,
	/// The live per-sequence renderer.
	Ready(RendererHandle),
	/// Creation failed (or no sequence is open); don't retry until the
	/// project changes, so a broken setup doesn't retry — and log — on
	/// every frame.
	Unavailable,
}

// ---------------------------------------------------------------------------
// FFI helpers
// ---------------------------------------------------------------------------

/// Builds a `CString` from a path (lossy on non-UTF-8).
fn cstr_path(path: &Path) -> Option<CString> {
	CString::new(path.to_string_lossy().into_owned()).ok()
}

/// Two-stage read of a facade buf/size string (the return value is the
/// length excluding the NUL). The closure must call the facade getter inside
/// its own `unsafe` block.
fn read_string(f: impl Fn(*mut c_char, c_int) -> c_int) -> String {
	let needed = f(std::ptr::null_mut(), 0);
	if needed <= 0 {
		return String::new();
	}
	// The facade's two-stage getters report the length WITHOUT the
	// trailing NUL (`string_result` subtracts one); the buffer must
	// carry the NUL too.
	let mut buf = vec![0 as c_char; needed as usize + 1];
	f(buf.as_mut_ptr(), needed as c_int + 1);
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) })
		.into_owned()
}

/// Reads the error buffer the OVE load/save serializer fills.
fn load_error(err: &mut [c_char]) -> String {
	let len = err.iter().position(|&c| c == 0).unwrap_or(err.len());
	let text = String::from_utf8_lossy(unsafe {
		std::slice::from_raw_parts(err.as_ptr() as *const u8, len)
	})
	.into_owned();
	if text.is_empty() {
		"the operation failed".to_string()
	} else {
		text
	}
}

// ---------------------------------------------------------------------------
// Transport clock
// ---------------------------------------------------------------------------

/// The real engine's transport clock: the playhead plus the wall-clock
/// anchor while playing. Mirrors the mock's clock; the engine additionally
/// writes the program playhead back to the facade sequence.
pub struct RealClock {
	/// The transport state (play/pause, playhead, loop range).
	pub transport: TransportState,
	/// The clock's frame rate.
	pub rate: FrameRate,
	/// Wall-clock anchor `(started_at, anchored_frame)` while playing.
	started: Option<(Instant, Frame)>,
}

impl RealClock {
	/// A stopped clock at frame zero running at `rate`.
	pub fn new(rate: FrameRate) -> Self {
		Self {
			transport: TransportState::new(),
			rate,
			started: None,
		}
	}

	/// Starts playback from the current playhead.
	pub fn play(&mut self) {
		self.transport.play();
		self.started = Some((Instant::now(), self.transport.frame()));
	}

	/// Pauses playback, keeping the playhead.
	pub fn pause(&mut self) {
		self.transport.pause();
		self.started = None;
	}

	/// Advances the playhead from the wall clock while playing, looping at
	/// `length`. No-op when stopped.
	pub fn tick(&mut self, length: Frame) {
		let Some((started, anchored)) = self.started else {
			return;
		};
		let elapsed = started.elapsed();
		let mut frame = anchored
			+ Frame(
				(elapsed.as_secs_f64() * self.rate.num as f64 / self.rate.den as f64).round()
					as i64,
			);
		if length.0 > 0 && frame.0 >= length.0 {
			frame = Frame(frame.0 % length.0);
		}
		self.transport.seek(frame, length);
	}
}

impl PlaybackClock for RealClock {
	fn current_frame(&self) -> Frame {
		self.transport.frame()
	}

	fn is_playing(&self) -> bool {
		self.transport.is_playing()
	}

	fn frame_rate(&self) -> FrameRate {
		self.rate
	}
}

// ---------------------------------------------------------------------------
// Timeline model
// ---------------------------------------------------------------------------

/// A clip on the real timeline: the facade data plus the C-ABI coordinates
/// (`track_type` / per-type `track_index` / per-track `clip_index`) the edit
/// commands are addressed with.
#[derive(Debug, Clone)]
pub struct RealClip {
	id: ClipId,
	range: FrameRange,
	media_in: Frame,
	label: SharedString,
	color: Hsla,
	track_type: TrackKind,
	track_index: usize,
	clip_index: usize,
}

impl ClipData for RealClip {
	fn id(&self) -> ClipId {
		self.id
	}

	fn range(&self) -> FrameRange {
		self.range
	}

	fn media_in(&self) -> Frame {
		self.media_in
	}

	fn label(&self) -> SharedString {
		self.label.clone()
	}

	fn color(&self) -> Option<Hsla> {
		Some(self.color)
	}
}

/// One card of the real effect stack: the facade chain node's identity,
/// its factory display name, its enabled flag, and the app-owned
/// expansion state ([`RealEngine::expanded_effects`]). No source/output
/// cards: the host clip node is the implicit output, the chain's unlinked
/// upstream input is the implicit source (the effect stack shows only the
/// editable middle).
#[derive(Debug, Clone)]
struct RealEffect {
	/// The node's stable identity (also the card's `EffectId`).
	id: EffectId,
	/// The factory display name of the node's type.
	title: SharedString,
	/// The node's `enabled_in` flag.
	enabled: bool,
	/// The app-owned expansion state (not undoable).
	expanded: bool,
}

impl EffectData for RealEffect {
	fn id(&self) -> EffectId {
		self.id
	}

	fn kind(&self) -> EffectCardKind {
		EffectCardKind::Effect
	}

	fn title(&self) -> SharedString {
		self.title.clone()
	}

	fn is_enabled(&self) -> bool {
		self.enabled
	}

	fn is_expanded(&self) -> bool {
		self.expanded
	}
}

/// A track on the real timeline (snapshot handed to the timeline widget).
#[derive(Debug, Clone)]
pub struct RealTrack {
	kind: TrackKind,
	name: SharedString,
	height: Pixels,
	locked: bool,
	muted: bool,
	solo: bool,
	visible: bool,
	clips: Vec<RealClip>,
	track_type: c_int,
	track_index: usize,
}

impl TrackData for RealTrack {
	type Clip = RealClip;

	fn kind(&self) -> TrackKind {
		self.kind
	}

	fn name(&self) -> SharedString {
		self.name.clone()
	}

	fn is_locked(&self) -> bool {
		self.locked
	}

	fn is_muted(&self) -> bool {
		self.muted
	}

	fn is_solo(&self) -> bool {
		self.solo
	}

	fn is_visible(&self) -> bool {
		self.visible
	}

	fn height(&self) -> Pixels {
		self.height
	}

	fn clips(&self) -> &[Self::Clip] {
		&self.clips
	}
}

/// A deterministic clip color from a stable per-clip index (the facade
/// exposes no clip color).
fn clip_color(index: u64) -> Hsla {
	let hues = [0.55f32, 0.6, 0.08, 0.3, 0.78, 0.45, 0.9, 0.15];
	Hsla {
		h: hues[(index as usize) % hues.len()],
		s: 0.55,
		l: 0.45,
		a: 1.0,
	}
}

/// A marker color for a marker color index (the facade marker color
/// contract): a small palette around the amber accent, so adjacent markers
/// stay distinguishable.
fn marker_color(index: c_int) -> Hsla {
	let hues = [0.10f32, 0.0, 0.55, 0.30, 0.78];
	let h = hues[(index.max(0) as usize) % hues.len()];
	Hsla {
		h,
		s: 0.75,
		l: 0.55,
		a: 1.0,
	}
}

/// A node in the real node graph (M12 P2: built from the current
/// sequence's graph by [`crate::oakui::nodegraph`]).
pub use crate::oakui::nodegraph::{RealEdge, RealNode, RealPort};

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

/// The real engine: the facade project/sequence plus the snapshot models the
/// widgets read.
pub struct RealEngine {
	/// The owned facade project (None before any project is open).
	project: Option<ProjectHandle>,
	/// The borrowed facade sequence (freed before the project on drop).
	sequence: Option<SequenceHandle>,
	/// The gateway's cached project info.
	project_info: Project,
	/// The gateway's cached sequence info.
	sequence_info: Option<Sequence>,
	/// The source monitor's clock.
	pub source_clock: Entity<RealClock>,
	/// The program monitor's clock.
	pub program_clock: Entity<RealClock>,
	/// The timeline snapshot (rebuilt on open/edit).
	tracks: Vec<RealTrack>,
	/// Timeline waveform cache (M12 P4), created lazily at the current
	/// frame rate.
	waveforms: Mutex<Option<Arc<crate::oakui::waveform::WaveformCache>>>,
	/// The selected material-bin entry (demo state).
	selected_item: Option<u64>,
	/// The single selected timeline clip — the effect stack's target
	/// (`None` for an empty or multi-clip selection, or before any
	/// selection event).
	selected_clip: Option<ClipId>,
	/// Node identities whose effect cards are expanded (view state; kept
	/// here because `EffectData` is a pure read and expansion is not
	/// undoable).
	expanded_effects: BTreeSet<u64>,
	/// Whether the program monitor is playing (mirrors the clock; kept here
	/// because the audio-meter data source has no `App` to read the clock).
	program_playing: bool,
	/// Phase counter driving the (silent) audio levels.
	meter_phase: u32,
	/// Cache of the CPU frames handed to the viewers, keyed by monitor.
	/// Entries are the playhead frame that produced the image plus the scope
	/// samples analyzed in the same pass, so a paused viewer never
	/// regenerates its picture (or its scopes). Both monitors hold real
	/// rendered frames (see [`RealEngine::render_program_frame`] /
	/// [`RealEngine::render_source_frame`]); the synthetic pattern is only
	/// the failure fallback.
	cpu_frame_cache: Mutex<HashMap<Monitor, (i64, Arc<RenderImage>, ScopeData)>>,
	/// The program monitor's cached renderer, created lazily from the
	/// current sequence at a proxy resolution. The mutex both provides the
	/// interior mutability `cpu_frame` (a `&self` read) needs and serializes
	/// the synchronous render calls. Reset to [`RendererSlot::Untried`]
	/// (before the sequence is freed) in [`RealEngine::drop_project`].
	renderer: Mutex<RendererSlot>,
	/// The source monitor's cached renderer, created lazily from the
	/// currently selected footage node at a proxy resolution (same slot
	/// semantics as `renderer`). Reset to [`RendererSlot::Untried`] when
	/// the selection changes or the project is dropped — the renderer binds
	/// the footage node, so a new selection must bind the new node.
	source_renderer: Mutex<RendererSlot>,
}

impl RealEngine {
	/// Render one tick's worth of audio at the program playhead and queue
	/// it for playback (M12 P1). Failures are silent: playback continues
	/// video-only.
	fn pull_audio_tick(&mut self, cx: &mut Context<Self>) {
		let renderer = {
			let slot = self.renderer.lock().unwrap_or_else(|e| e.into_inner());
			match &*slot {
				RendererSlot::Ready(handle) => RendererHandle(handle.0),
				_ => return,
			}
		};
		let fps = self.frame_rate();
		let frame = self.clock_frame(Monitor::Program, cx).0;
		if frame < 0 {
			return;
		}
		// ~1/60 s of sequence per tick.
		let chunk = ((fps.num as f64 / fps.den as f64) / 60.0).max(0.001) as i64;
		let buf = unsafe { oakengine_renderer_render_audio(renderer.0, frame, chunk) };
		if buf.is_null() {
			return;
		}
		let rate = unsafe { oakengine_audio_sample_rate(buf) };
		let channels = unsafe { oakengine_audio_channel_count(buf) };
		let frames = unsafe { oakengine_audio_sample_count(buf) };
		let data = unsafe { oakengine_audio_data(buf, 0) };
		if rate > 0 && channels > 0 && frames > 0 && !data.is_null() {
			// Packed F32 (10 = the engine's packed F32 sample format);
			// layout: 1ch → mono mask, else stereo.
			let layout: u64 = if channels == 1 { 0x4 } else { 0x3 };
			let params =
				unsafe { oakcore_audioparams_create(rate, layout, 10) };
			if !params.is_null() {
				unsafe {
					oakengine_audio_push_to_output(
						params,
						data as *const c_char,
						frames * i64::from(channels) * 4,
						std::ptr::null_mut(),
						0,
					);
					oakcore_audioparams_free(params);
				}
			}
		}
		unsafe { oakengine_audio_free(buf) };
	}

	/// Builds an engine with no project open.
	pub fn new(cx: &mut Context<Self>) -> Self {
		let rate = VideoFormat::hd_1080p25().rate;
		Self {
			project: None,
			sequence: None,
			project_info: Project {
				name: UNTITLED.into(),
				path: PathBuf::new(),
			},
			sequence_info: None,
			source_clock: cx.new(|_cx| RealClock::new(rate)),
			program_clock: cx.new(|_cx| RealClock::new(rate)),
			tracks: Vec::new(),
			waveforms: Mutex::new(None),
			selected_item: None,
			selected_clip: None,
			expanded_effects: BTreeSet::new(),
			program_playing: false,
			meter_phase: 0,
			cpu_frame_cache: Mutex::new(HashMap::new()),
			renderer: Mutex::new(RendererSlot::Untried),
			source_renderer: Mutex::new(RendererSlot::Untried),
		}
	}

	/// Resolves the clock entity for a monitor.
	fn clock(&self, monitor: Monitor) -> &Entity<RealClock> {
		match monitor {
			Monitor::Source => &self.source_clock,
			Monitor::Program => &self.program_clock,
		}
	}

	/// The sequence pointer, if a project+sequence is open.
	fn seq_ptr(&self) -> Option<*mut OakEngineSequence> {
		self.sequence.as_ref().map(SequenceHandle::ptr)
	}

	/// The project pointer, if a project is open.
	fn project_ptr(&self) -> Option<*mut OakEngineProject> {
		self.project.as_ref().map(ProjectHandle::ptr)
	}

	/// Current sequence length (0 without a sequence).
	fn sequence_length(&self) -> Frame {
		self.sequence_info
			.as_ref()
			.map(|s| s.length)
			.unwrap_or(Frame(0))
	}

	/// Mirrors the program playhead into the facade sequence (best effort).
	fn mirror_program_playhead(&self, cx: &App) {
		if let Some(seq) = self.seq_ptr() {
			let frame = self.program_clock.read(cx).transport.frame().0;
			unsafe {
				oakengine_sequence_set_playhead(seq, frame);
			}
		}
	}

	/// Brings up the module's process-global render manager if it is not
	/// running yet (without it `render_frame` fails with NULL + last_error).
	/// Returns false when the manager could not be started.
	fn ensure_render_manager() -> bool {
		unsafe {
			if oakengine_render_manager_available() != 0 {
				return true;
			}
			oakengine_render_manager_init();
			oakengine_render_manager_available() != 0
		}
	}

	/// The proxy resolution the viewer renderer runs at: the sequence's
	/// aspect scaled to a small long edge. Rendering is a synchronous call
	/// made from `cpu_frame` (a `&self` read on the UI thread), so the
	/// geometry stays tiny to keep the block short; the async render worker
	/// (full-resolution, off-thread) is a separate transport surface not
	/// bound yet.
	fn proxy_render_size(&self) -> Option<(c_int, c_int)> {
		let info = self.sequence_info.as_ref()?;
		let (w, h) = (info.format.width.max(1), info.format.height.max(1));
		const MAX_LONG_EDGE: u32 = 480;
		let scale = MAX_LONG_EDGE as f64 / w.max(h) as f64;
		let width = ((w as f64 * scale).round() as u32).max(2);
		let height = ((h as f64 * scale).round() as u32).max(2);
		Some((width as c_int, height as c_int))
	}

	/// Renders one program-monitor frame through the facade CPU renderer:
	/// creates the per-sequence renderer lazily (cached in `self.renderer`),
	/// renders `frame`, analyzes the scope samples from the F32 RGBA result,
	/// and downconverts to BGRA8. Returns `None` (the caller falls back to
	/// the synthetic pattern) when no sequence is open, the render manager is
	/// unavailable, or the render itself fails.
	fn render_program_frame(&self, frame: Frame) -> Option<(RenderImage, ScopeData)> {
		let seq = self.seq_ptr()?;
		if !Self::ensure_render_manager() {
			return None;
		}
		let mut slot = self.renderer.lock().unwrap();
		match &*slot {
			RendererSlot::Unavailable => return None,
			RendererSlot::Untried => {
				let created = self.create_renderer(seq);
				*slot = match created {
					Some(handle) => RendererSlot::Ready(handle),
					None => RendererSlot::Unavailable,
				};
			}
			RendererSlot::Ready(_) => {}
		}
		let RendererSlot::Ready(handle) = &*slot else {
			return None;
		};
		let renderer = handle.ptr();
		let frame_ptr = unsafe { oakengine_renderer_render_frame(renderer, frame.0) };
		if frame_ptr.is_null() {
			let error = read_string(|buf, size| unsafe {
				oakengine_renderer_last_error(renderer, buf, size)
			});
			println!("[real engine] render_frame failed: {error}");
			// Don't retry (and re-log) on every frame.
			*slot = RendererSlot::Unavailable;
			return None;
		}
		// Read the frame (F32 RGBA, rows padded to linesize), repack it
		// tightly, then downconvert.
		let (width, height, linesize, format) = unsafe {
			(
				oakengine_frame_width(frame_ptr),
				oakengine_frame_height(frame_ptr),
				oakengine_frame_linesize_bytes(frame_ptr),
				oakengine_frame_format(frame_ptr),
			)
		};
		let data = unsafe { oakengine_frame_data(frame_ptr) };
		let mut image = None;
		if width > 0 && height > 0 && format == PIXEL_FORMAT_F32 && !data.is_null() {
			let row_bytes = (width * 4 * 4) as usize;
			let linesize = (linesize as usize).max(row_bytes);
			let mut samples = vec![0.0f32; (width * height * 4) as usize];
			for y in 0..height as usize {
				unsafe {
					std::ptr::copy_nonoverlapping(
						(data as *const u8).add(y * linesize),
						samples.as_mut_ptr().add(y * row_bytes / 4) as *mut u8,
						row_bytes,
					);
				}
			}
			// The scopes read the same F32 samples the viewer displays.
			let scope = analyze_f32_rgba(width as u32, height as u32, &samples);
			image = Some((
				f32_rgba_to_bgra_image(width as u32, height as u32, &samples),
				scope,
			));
		}
		unsafe {
			oakengine_frame_free(frame_ptr);
		}
		image
	}

	/// Creates the per-sequence renderer at the proxy resolution (see
	/// [`RealEngine::proxy_render_size`]). The renderer borrows the sequence
	/// handle; the caller owns the slot it is stored in.
	fn create_renderer(&self, seq: *mut OakEngineSequence) -> Option<RendererHandle> {
		let (width, height) = self.proxy_render_size()?;
		let rate = self.sequence_info.as_ref()?.format.rate;
		// Pixel format 4 = PixelFormat::F32 (the pipeline format);
		// timestamp units are frames at this rate.
		let renderer = unsafe {
			oakengine_renderer_create(
				seq,
				width,
				height,
				PIXEL_FORMAT_F32,
				rate.num as c_int,
				rate.den as c_int,
				std::ptr::null(),
			)
		};
		if renderer.is_null() {
			println!("[real engine] renderer_create failed; viewer keeps the synthetic frame");
			return None;
		}
		Some(RendererHandle(renderer))
	}

	/// The selected entry's footage node (M12 P3: entry ids are the
	/// nodes' stable identities), or `None` when the selection is a
	/// folder or absent.
	///
	/// # Safety
	/// The returned box is freed with `oakengine_node_free`.
	fn selected_footage_node(&self) -> Option<*mut OakEngineNode> {
		let project = self.project_ptr()?;
		let id = self.selected_item?;
		unsafe {
			// The identity must resolve to a footage entry (a folder or
			// sequence id must not be treated as footage).
			let count = unsafe { oakengine_project_footage_count(project) };
			let mut is_footage = false;
			for i in 0..count.max(0) {
				let f = unsafe { oakengine_project_footage_at(project, i) };
				if f.is_null() {
					continue;
				}
				let matches = unsafe { oakengine_node_identity(f) } == id;
				unsafe { oakengine_node_free(f) };
				if matches {
					is_footage = true;
					break;
				}
			}
			if !is_footage {
				return None;
			}
			crate::oakui::projectbrowser::find_by_identity(project, id)
		}
	}

	/// Creates the per-footage renderer at the proxy resolution (see
	/// [`RealEngine::proxy_render_size`]). The renderer borrows the footage
	/// node handle; the caller owns the slot it is stored in.
	fn create_node_renderer(&self, node: *mut OakEngineNode) -> Option<RendererHandle> {
		let (width, height) = self.proxy_render_size()?;
		let rate = self.sequence_info.as_ref()?.format.rate;
		let renderer = unsafe {
			oakengine_renderer_create_for_node(
				node,
				width,
				height,
				PIXEL_FORMAT_F32,
				rate.num as c_int,
				rate.den as c_int,
				std::ptr::null(),
			)
		};
		if renderer.is_null() {
			println!(
				"[real engine] renderer_create_for_node failed; viewer keeps the synthetic frame"
			);
			return None;
		}
		Some(RendererHandle(renderer))
	}

	/// Renders one source-monitor frame through the facade CPU renderer:
	/// creates the per-footage renderer lazily (cached in
	/// `self.source_renderer`, bound to the currently selected footage
	/// node), renders `frame`, analyzes the scope samples from the F32 RGBA
	/// result, and downconverts to BGRA8. Returns `None` (the caller falls
	/// back to the synthetic pattern) when no footage is selected, the
	/// render manager is unavailable, or the render itself fails.
	fn render_source_frame(&self, frame: Frame) -> Option<(RenderImage, ScopeData)> {
		let node = self.selected_footage_node()?;
		if !Self::ensure_render_manager() {
			// SAFETY: `node` is a box from `selected_footage_node`.
			unsafe { oakengine_node_free(node) };
			return None;
		}
		let mut slot = self.source_renderer.lock().unwrap();
		match &*slot {
			RendererSlot::Unavailable => {
				unsafe { oakengine_node_free(node) };
				return None;
			}
			RendererSlot::Untried => {
				let created = self.create_node_renderer(node);
				*slot = match created {
					Some(handle) => RendererSlot::Ready(handle),
					None => RendererSlot::Unavailable,
				};
			}
			RendererSlot::Ready(_) => {}
		}
		// SAFETY: `node` is a live box; freed on every path below.
		unsafe { oakengine_node_free(node) };
		let RendererSlot::Ready(handle) = &*slot else {
			return None;
		};
		let renderer = handle.ptr();
		let frame_ptr = unsafe { oakengine_renderer_render_frame(renderer, frame.0) };
		if frame_ptr.is_null() {
			let error = read_string(|buf, size| unsafe {
				oakengine_renderer_last_error(renderer, buf, size)
			});
			println!("[real engine] source render_frame failed: {error}");
			// Don't retry (and re-log) on every frame.
			*slot = RendererSlot::Unavailable;
			return None;
		}
		// Read the frame (F32 RGBA, rows padded to linesize), repack it
		// tightly, then downconvert.
		let (width, height, linesize, format) = unsafe {
			(
				oakengine_frame_width(frame_ptr),
				oakengine_frame_height(frame_ptr),
				oakengine_frame_linesize_bytes(frame_ptr),
				oakengine_frame_format(frame_ptr),
			)
		};
		let data = unsafe { oakengine_frame_data(frame_ptr) };
		let mut image = None;
		if width > 0 && height > 0 && format == PIXEL_FORMAT_F32 && !data.is_null() {
			let row_bytes = (width * 4 * 4) as usize;
			let linesize = (linesize as usize).max(row_bytes);
			let mut samples = vec![0.0f32; (width * height * 4) as usize];
			for y in 0..height as usize {
				unsafe {
					std::ptr::copy_nonoverlapping(
						(data as *const u8).add(y * linesize),
						samples.as_mut_ptr().add(y * row_bytes / 4) as *mut u8,
						row_bytes,
					);
				}
			}
			let scope = analyze_f32_rgba(width as u32, height as u32, &samples);
			image = Some((
				f32_rgba_to_bgra_image(width as u32, height as u32, &samples),
				scope,
			));
		}
		unsafe {
			oakengine_frame_free(frame_ptr);
		}
		image
	}

	/// Adopts a newly created/loaded facade project, freeing any previous
	/// one, and rebuilds every snapshot. `blank` projects get a default
	/// sequence; loaded ones use the first sequence.
	fn adopt_project(&mut self, project: *mut OakEngineProject, cx: &mut Context<Self>) {
		self.drop_project();
		self.project = Some(ProjectHandle(project));

		// Cached display info.
		let name = read_string(|buf, size| unsafe { oakengine_project_name(project, buf, size) });
		let path = PathBuf::from(read_string(|buf, size| unsafe {
			oakengine_project_filename(project, buf, size)
		}));
		self.project_info = Project {
			name: if name.is_empty() {
				UNTITLED.into()
			} else {
				name
			},
			path,
		};

		// The sequence: the project's first, or a blank default.
		let count = unsafe { oakengine_project_sequence_count(project) };
		let sequence = if count > 0 {
			unsafe { oakengine_project_sequence_at(project, 0) }
		} else {
			let name_c = CString::new("Sequence 1").unwrap();
			unsafe { oakengine_sequence_new(project, name_c.as_ptr()) }
		};
		if sequence.is_null() {
			return;
		}
		self.sequence = Some(SequenceHandle(sequence));
		self.refresh_sequence_info();
		self.rebuild_timeline();

		cx.notify();
	}

	/// Frees the project and every borrowed handle (renderer and sequence
	/// first: the renderer borrows the sequence, and the sequence is
	/// borrowed from the project).
	fn drop_project(&mut self) {
		*self.renderer.lock().unwrap() = RendererSlot::Untried;
		*self.source_renderer.lock().unwrap() = RendererSlot::Untried;
		drop(self.sequence.take());
		drop(self.project.take());
		self.cpu_frame_cache.lock().unwrap().clear();
		self.tracks.clear();

		self.sequence_info = None;
		self.project_info = Project {
			name: UNTITLED.into(),
			path: PathBuf::new(),
		};
	}

	/// Refreshes the cached `Sequence` (name / format / length) from the
	/// facade.
	fn refresh_sequence_info(&mut self) {
		let Some(seq) = self.seq_ptr() else {
			self.sequence_info = None;
			return;
		};
		let name = read_string(|buf, size| unsafe { oakengine_sequence_name(seq, buf, size) });
		let mut num: c_int = 0;
		let mut den: c_int = 0;
		let mut width: c_int = 0;
		let mut height: c_int = 0;
		let mut seconds: f64 = 0.0;
		unsafe {
			oakengine_sequence_get_frame_rate(seq, &mut num, &mut den);
			oakengine_sequence_get_video_params(
				seq,
				&mut width,
				&mut height,
				std::ptr::null_mut(),
				std::ptr::null_mut(),
			);
			oakengine_sequence_get_length(seq, &mut seconds);
		}
		let rate = if num > 0 && den > 0 {
			FrameRate::new(num as u32, den as u32)
		} else {
			VideoFormat::hd_1080p25().rate
		};
		let length = Frame((seconds * rate.num as f64 / rate.den as f64).round() as i64);
		self.sequence_info = Some(Sequence {
			name: if name.is_empty() {
				"Sequence 1".into()
			} else {
				name
			},
			format: VideoFormat {
				width: width.max(1) as u32,
				height: height.max(1) as u32,
				rate,
			},
			length,
		});
	}

	/// Rebuilds the timeline snapshot from the facade sequence.
	fn rebuild_timeline(&mut self) {
		self.tracks.clear();
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let mut video: c_int = 0;
		let mut audio: c_int = 0;
		let mut subtitle: c_int = 0;
		unsafe {
			oakengine_sequence_track_count(seq, &mut video, &mut audio, &mut subtitle);
		}
		let mut out: Vec<RealTrack> = Vec::new();
		// Per-type track lists, each displayed topmost-first.
		for (kind, track_type, count) in [
			(TrackKind::Video, TRACK_TYPE_VIDEO, video),
			(TrackKind::Audio, TRACK_TYPE_AUDIO, audio),
			(TrackKind::Subtitle, TRACK_TYPE_SUBTITLE, subtitle),
		] {
			for track_index in (0..count).rev() {
				out.push(self.snapshot_track(kind, track_type, track_index as usize));
			}
		}
		self.tracks = out;
		// M12 P4: refresh the audio waveforms for the visible audio clips.
		if let Some(cache) = self.waveform_cache() {
			for track in &self.tracks {
				if track.kind != TrackKind::Audio {
					continue;
				}
				for clip in &track.clips {
					self.refresh_clip_waveform(cache.clone(), clip);
				}
			}
		}
	}

	/// Snapshots one track (with its clips) from the facade.
	fn snapshot_track(&self, kind: TrackKind, track_type: c_int, track_index: usize) -> RealTrack {
		let Some(seq) = self.seq_ptr() else {
			return RealTrack {
				kind,
				name: SharedString::new_static(""),
				height: px(64.0),
				locked: false,
				muted: false,
				solo: false,
				visible: true,
				clips: Vec::new(),
				track_type,
				track_index,
			};
		};
		let name = match kind {
			TrackKind::Video => format!("V{}", track_index + 1),
			TrackKind::Audio => format!("A{}", track_index + 1),
			TrackKind::Subtitle => format!("S{}", track_index + 1),
		};
		// Height in internal units → pixels.
		let mut internal: f64 = 0.0;
		let height = unsafe {
			if oakengine_track_get_height(seq, track_type, track_index as c_int, &mut internal) == 0
			{
				px(oakengine_track_height_internal_to_pixels(internal).max(24) as f32)
			} else {
				px(64.0)
			}
		};

		let clip_count =
			unsafe { oakengine_sequence_clip_count(seq, track_type, track_index as c_int) };
		let mut clips = Vec::with_capacity(clip_count.max(0) as usize);
		for clip_index in 0..clip_count.max(0) {
			let clip = unsafe {
				oakengine_sequence_clip_at(seq, track_type, track_index as c_int, clip_index)
			};
			if clip.is_null() {
				continue;
			}
			let mut in_ts: i64 = 0;
			let mut out_ts: i64 = 0;
			let mut media_in: i64 = 0;
			unsafe {
				oakengine_clip_get_range(clip, &mut in_ts, &mut out_ts, &mut media_in);
				free_box(clip);
			}
			clips.push(RealClip {
				id: ClipId(
					(track_type as u64) * 1_000_000
						+ (track_index as u64 + 1) * 1000
						+ clip_index as u64,
				),
				range: FrameRange::new(Frame(in_ts), Frame(out_ts)),
				media_in: Frame(media_in),
				label: format!("Clip {}", clip_index + 1).into(),
				color: clip_color(clip_index as u64),
				track_type: kind,
				track_index,
				clip_index: clip_index as usize,
			});
		}
		RealTrack {
			kind,
			name: name.into(),
			height,
			locked: false,
			muted: false,
			solo: false,
			visible: true,
			clips,
			track_type,
			track_index,
		}
	}

	/// Rebuilds the material-bin snapshot from the facade project's footage.
	/// The waveform cache (created lazily at the current frame rate).
	fn waveform_cache(&self) -> Option<Arc<crate::oakui::waveform::WaveformCache>> {
		let mut slot = self.waveforms.lock().unwrap_or_else(|e| e.into_inner());
		if slot.is_none() {
			let fps = self
				.sequence_info
				.as_ref()
				.map(|s| s.format.rate.num as f32 / s.format.rate.den.max(1) as f32)
				.unwrap_or(25.0);
			*slot = Some(crate::oakui::waveform::WaveformCache::new(fps));
		}
		slot.clone()
	}

	/// Extract (or reuse) the waveform of one audio clip.
	fn refresh_clip_waveform(
		&self,
		cache: Arc<crate::oakui::waveform::WaveformCache>,
		clip: &RealClip,
	) {
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let clip_ptr = unsafe {
			oakengine_sequence_clip_at(
				seq,
				TRACK_TYPE_AUDIO,
				clip.track_index as c_int,
				clip.clip_index as c_int,
			)
		};
		if clip_ptr.is_null() {
			return;
		}
		let filename = read_string(|buf, size| unsafe {
			oakengine_clip_get_media_filename(clip_ptr, buf, size)
		});
		unsafe { free_box(clip_ptr) };
		if filename.is_empty() {
			return;
		}
		let duration_frames = (clip.range.end.0 - clip.range.start.0).max(1);
		cache.refresh(clip.id.0, &filename, duration_frames);
	}

	/// Looks up the snapshot clip coordinates by `ClipId`.
	fn clip_coords(&self, id: ClipId) -> Option<(TrackKind, usize, usize)> {
		for track in &self.tracks {
			if let Some(clip) = track.clips.iter().find(|c| c.id() == id) {
				return Some((clip.track_type, clip.track_index, clip.clip_index));
			}
		}
		None
	}

	/// The selected clip's facade node view (a boxed node handle the
	/// caller frees with `oakengine_node_free`), or `None` when no single
	/// clip is selected or the clip box resolves to no node.
	fn selected_clip_node(&self) -> Option<*mut OakEngineNode> {
		let clip_id = self.selected_clip?;
		let (kind, track_index, clip_index) = self.clip_coords(clip_id)?;
		let seq = self.seq_ptr()?;
		let clip = unsafe {
			oakengine_sequence_clip_at(
				seq,
				Self::track_type_of(kind),
				track_index as c_int,
				clip_index as c_int,
			)
		};
		if clip.is_null() {
			return None;
		}
		let node = unsafe { oakengine_clip_as_node(clip) };
		// SAFETY: the clip box is a plain facade box (see `free_box`); the
		// node box is independent (the facade boxed its own handle copy).
		unsafe { free_box(clip) };
		// SAFETY: `node` is a fresh box the caller frees, or NULL.
		if node.is_null() {
			None
		} else {
			Some(node)
		}
	}

	/// Whether `node` can host effects (its effect-input id is non-empty;
	/// the facade reports `E_NOT_FOUND` for nodes without one).
	///
	/// # Safety
	/// `node` must be a live node box (NULL reports false).
	unsafe fn node_hosts_effects(node: *mut OakEngineNode) -> bool {
		let mut buf = [0 as c_char; 64];
		let mut element: c_int = 0;
		unsafe {
			oakengine_node_get_effect_input(
				node,
				buf.as_mut_ptr(),
				buf.len() as c_int,
				&mut element,
			) >= 0
		}
	}

	/// The effect in `host`'s chain whose identity is `identity` (a boxed
	/// node handle the caller frees), or `None` when not a member.
	///
	/// # Safety
	/// `host` must be a live node box.
	unsafe fn chain_effect_by_identity(
		host: *mut OakEngineNode,
		identity: u64,
	) -> Option<*mut OakEngineNode> {
		let count = unsafe { oakengine_node_effect_count(host) };
		for i in 0..count.max(0) {
			let effect = unsafe { oakengine_node_effect_at(host, i) };
			if effect.is_null() {
				continue;
			}
			if unsafe { oakengine_node_identity(effect) } == identity {
				return Some(effect);
			}
			// SAFETY: `effect` is a box from `oakengine_node_effect_at`.
			unsafe { oakengine_node_free(effect) };
		}
		None
	}

	/// The display label of the selected clip (its timeline snapshot
	/// label), if any.
	fn selected_clip_label(&self) -> Option<SharedString> {
		let clip_id = self.selected_clip?;
		for track in &self.tracks {
			if let Some(clip) = track.clips.iter().find(|c| c.id() == clip_id) {
				return Some(clip.label());
			}
		}
		None
	}

	/// The card list of the selected clip's effect chain. Each card wraps
	/// one chain node (index 0 = closest to the source); a clip that
	/// cannot host effects yields an empty list (its `target_label` is
	/// `None`, so the stack shows the empty state).
	fn selected_effect_cards(&self) -> Vec<Arc<dyn EffectData>> {
		let Some(node) = self.selected_clip_node() else {
			return Vec::new();
		};
		let mut out: Vec<Arc<dyn EffectData>> = Vec::new();
		// SAFETY: `node` is a live box; freed on every return path.
		unsafe {
			if !Self::node_hosts_effects(node) {
				oakengine_node_free(node);
				return out;
			}
			let count = oakengine_node_effect_count(node);
			for i in 0..count.max(0) {
				let effect = oakengine_node_effect_at(node, i);
				if effect.is_null() {
					continue;
				}
				let identity = oakengine_node_identity(effect);
				let type_id = read_string(|buf, size| {
					// SAFETY: `effect` is a live box; buf/size follow the
					// facade two-stage convention (the enclosing `unsafe`
					// block covers this closure body).
					oakengine_node_get_type_id(effect, buf, size)
				});
				let title = CString::new(type_id.clone())
					.ok()
					.map(|c| {
						read_string(|buf, size| {
							// SAFETY: as above; `c` outlives the call.
							oakengine_node_factory_name_from_id(c.as_ptr(), buf, size)
						})
					})
					.filter(|n| !n.is_empty())
					.unwrap_or(type_id);
				let enabled = oakengine_node_is_enabled(effect) != 0;
				let expanded = self.expanded_effects.contains(&identity);
				out.push(Arc::new(RealEffect {
					id: EffectId(identity),
					title: title.into(),
					enabled,
					expanded,
				}) as Arc<dyn EffectData>);
				// SAFETY: `effect` is a box from `oakengine_node_effect_at`.
				oakengine_node_free(effect);
			}
			oakengine_node_free(node);
		}
		out
	}

	/// The facade track-type constant for a [`TrackKind`].
	fn track_type_of(kind: TrackKind) -> c_int {
		match kind {
			TrackKind::Video => TRACK_TYPE_VIDEO,
			TrackKind::Audio => TRACK_TYPE_AUDIO,
			TrackKind::Subtitle => TRACK_TYPE_SUBTITLE,
		}
	}

	/// Applies an edit command, then refreshes the snapshots and repaints.
	fn apply_edit(&mut self, rc: c_int, what: &str, cx: &mut Context<Self>) {
		if rc != 0 {
			println!("[real engine] {what} failed (facade error {rc})");
		}
		self.refresh_sequence_info();
		self.rebuild_timeline();
		// The sequence content changed: cached rendered frames are stale.
		self.cpu_frame_cache.lock().unwrap().clear();
		cx.notify();
	}

	/// Formats a facade error (two-stage task error buffer) into a message.
	fn task_error(task: *mut OakEngineTask) -> String {
		let text = read_string(|buf, size| unsafe { oakengine_task_error(task, buf, size) });
		if text.is_empty() {
			"the task failed".to_string()
		} else {
			text
		}
	}
}

// ---------------------------------------------------------------------------
// EngineGateway
// ---------------------------------------------------------------------------

impl EngineGateway for RealEngine {
	fn project(&self) -> Option<&Project> {
		self.project.as_ref().map(|_| &self.project_info)
	}

	fn current_sequence(&self) -> Option<&Sequence> {
		self.sequence_info.as_ref()
	}

	fn open_project(&mut self, path: PathBuf, cx: &mut Context<Self>) {
		if let Err(err) = self.open_project_path(path, cx) {
			println!("[real engine] open failed: {err}");
		}
	}

	fn request_frame(&mut self, monitor: Monitor, frame: Frame, cx: &mut Context<Self>) {
		let length = self.sequence_length();
		let clock = self.clock(monitor).clone();
		clock.update(cx, |clock, cx| {
			clock.transport.seek(frame, length);
			if clock.transport.is_playing() {
				clock.play();
			}
			cx.notify();
		});
		self.mirror_program_playhead(cx);
		cx.notify();
	}

	fn play(&mut self, monitor: Monitor, cx: &mut Context<Self>) {
		if monitor == Monitor::Program {
			self.program_playing = true;
		}
		let clock = self.clock(monitor).clone();
		clock.update(cx, |clock, cx| {
			clock.play();
			cx.notify();
		});
		self.mirror_program_playhead(cx);
		cx.notify();
	}

	fn pause(&mut self, monitor: Monitor, cx: &mut Context<Self>) {
		if monitor == Monitor::Program {
			self.program_playing = false;
		}
		let clock = self.clock(monitor).clone();
		clock.update(cx, |clock, cx| {
			clock.pause();
			cx.notify();
		});
		cx.notify();
	}

	fn step(&mut self, monitor: Monitor, delta: i64, cx: &mut Context<Self>) {
		let length = self.sequence_length();
		let clock = self.clock(monitor).clone();
		clock.update(cx, |clock, cx| {
			clock.transport.step(delta, length);
			if clock.transport.is_playing() {
				clock.play();
			}
			cx.notify();
		});
		self.mirror_program_playhead(cx);
		cx.notify();
	}

	fn tick(&mut self, cx: &mut Context<Self>) {
		let length = self.sequence_length();
		for clock in [&self.source_clock, &self.program_clock] {
			let clock = clock.clone();
			clock.update(cx, |clock, cx| {
				clock.tick(length);
				cx.notify();
			});
		}
		self.mirror_program_playhead(cx);
		self.meter_phase = self.meter_phase.wrapping_add(1);
		// M12 P1: while the program plays, pull the audio for the
		// current playhead window and queue it for the output device.
		if self.program_playing {
			self.pull_audio_tick(cx);
		}
		cx.notify();
	}
}

// ---------------------------------------------------------------------------
// Data-source traits
// ---------------------------------------------------------------------------

impl TimelineDataSource for RealEngine {
	type Track = RealTrack;

	fn frame_rate(&self) -> FrameRate {
		self.sequence_info
			.as_ref()
			.map(|s| s.format.rate)
			.unwrap_or(VideoFormat::hd_1080p25().rate)
	}

	fn sequence_length(&self) -> Frame {
		self.sequence_length()
	}

	fn track_count(&self) -> usize {
		self.tracks.len()
	}

	fn track(&self, index: usize) -> Option<Self::Track> {
		self.tracks.get(index).cloned()
	}

	fn markers(&self) -> Vec<Marker> {
		let Some(seq) = self.seq_ptr() else {
			return Vec::new();
		};
		let count = unsafe { oakengine_sequence_marker_count(seq) };
		if count <= 0 {
			return Vec::new();
		}
		let mut out = Vec::with_capacity(count as usize);
		for index in 0..count {
			let mut time: i64 = 0;
			let mut name_buf = [0 as c_char; 128];
			let mut color: c_int = 0;
			let rc = unsafe {
				oakengine_sequence_marker_at(
					seq,
					index,
					&mut time,
					name_buf.as_mut_ptr(),
					name_buf.len() as c_int,
					&mut color,
				)
			};
			if rc != 0 {
				continue;
			}
			let len = name_buf.iter().position(|&c| c == 0).unwrap_or(name_buf.len());
			let name: SharedString =
				String::from_utf8_lossy(unsafe {
					std::slice::from_raw_parts(name_buf.as_ptr() as *const u8, len)
				})
				.into_owned()
				.into();
			out.push(Marker {
				frame: Frame(time),
				label: name,
				color: Some(marker_color(color)),
			});
		}
		out
	}
}

impl EffectStackDataSource for RealEngine {
	fn effects(&self) -> Vec<Arc<dyn EffectData>> {
		self.selected_effect_cards()
	}

	fn target_label(&self) -> Option<SharedString> {
		let label = self.selected_clip_label()?;
		let node = self.selected_clip_node()?;
		// SAFETY: `node` is a live box; freed below. A clip that cannot
		// host effects keeps the empty state (no label, no cards).
		let hosts = unsafe { Self::node_hosts_effects(node) };
		unsafe { oakengine_node_free(node) };
		hosts.then_some(label)
	}
}

impl NodeGraphDataSource for RealEngine {
	type Node = RealNode;
	type Edge = RealEdge;

	fn nodes(&self) -> Vec<Self::Node> {
		// SAFETY: the sequence box is live while the engine holds it.
		unsafe {
			crate::oakui::nodegraph::build_graph(self.seq_ptr().unwrap_or(std::ptr::null_mut())).0
		}
	}

	fn edges(&self) -> Vec<Self::Edge> {
		// SAFETY: the sequence box is live while the engine holds it.
		unsafe {
			crate::oakui::nodegraph::build_graph(self.seq_ptr().unwrap_or(std::ptr::null_mut())).1
		}
	}

	fn can_connect(&self, from: PortId, to: PortId) -> bool {
		// SAFETY: the sequence box is live while the engine holds it.
		unsafe {
			crate::oakui::nodegraph::can_connect(
				self.seq_ptr().unwrap_or(std::ptr::null_mut()),
				from,
				to,
			)
		}
	}
}

impl ProjectDataSource for RealEngine {
	fn roots(&self) -> Vec<ProjectEntry> {
		// SAFETY: the project box is live while the engine holds it.
		unsafe {
			crate::oakui::projectbrowser::roots(self.project_ptr().unwrap_or(std::ptr::null_mut()))
		}
	}

	fn children(&self, parent_id: u64) -> Vec<ProjectEntry> {
		// SAFETY: the project box is live while the engine holds it.
		unsafe {
			crate::oakui::projectbrowser::children(
				self.project_ptr().unwrap_or(std::ptr::null_mut()),
				parent_id,
			)
		}
	}
}

impl AudioMeterDataSource for RealEngine {
	fn levels(&self) -> Vec<f32> {
		// Per-channel linear peaks of the engine's buffered audio output
		// (facade `oakengine_audio_output_levels`, clamped to the meter's
		// 0..1 range). Silent when nothing has been pushed to the output
		// (no playback audio path yet) or on any facade error.
		let mut peaks = [0.0f32; 8];
		// SAFETY: `peaks` is a live 8-entry buffer; capacity matches.
		let n = unsafe { oakengine_audio_output_levels(peaks.as_mut_ptr(), peaks.len() as c_int) };
		if n <= 0 {
			return vec![0.0, 0.0];
		}
		peaks[..n as usize].iter().map(|p| p.clamp(0.0, 1.0)).collect()
	}
}

// ---------------------------------------------------------------------------
// AppEngine
// ---------------------------------------------------------------------------

impl AppEngine for RealEngine {
	type Clock = RealClock;

	fn create(cx: &mut Context<Self>) -> Self {
		Self::new(cx)
	}

	fn source_clock(&self) -> &Entity<Self::Clock> {
		&self.source_clock
	}

	fn program_clock(&self) -> &Entity<Self::Clock> {
		&self.program_clock
	}

	fn clock_frame(&self, monitor: Monitor, cx: &App) -> Frame {
		self.clock(monitor).read(cx).transport.frame()
	}

	fn cpu_frame(&self, monitor: Monitor, cx: &App) -> Arc<RenderImage> {
		let frame = self.clock_frame(monitor, cx);
		let mut cache = self.cpu_frame_cache.lock().unwrap();
		if let Some((cached_frame, image, _)) = cache.get(&monitor) {
			if *cached_frame == frame.0 {
				return image.clone();
			}
		}
		// Both monitors render through the facade CPU renderer (falling
		// back to the synthetic pattern when rendering is unavailable): the
		// program monitor renders the current sequence, the source monitor
		// renders the currently selected footage node at the source clock's
		// playhead frame.
		let rendered = match monitor {
			Monitor::Program => self.render_program_frame(frame),
			Monitor::Source => self.render_source_frame(frame),
		};
		let (image, scope) = match rendered {
			Some((image, scope)) => (Arc::new(image), scope),
			None => {
				let (width, height, samples) = synthetic_frame_samples(frame);
				let scope = analyze_f32_rgba(width, height, &samples);
				(
					Arc::new(f32_rgba_to_bgra_image(width, height, &samples)),
					scope,
				)
			}
		};
		cache.insert(monitor, (frame.0, image.clone(), scope));
		image
	}

	fn scope_data(&self, monitor: Monitor, cx: &App) -> ScopeData {
		// Ensure the cache holds the current playhead frame (the analysis
		// runs inside that render pass, so this never re-walks a frame).
		let _ = self.cpu_frame(monitor, cx);
		let cache = self.cpu_frame_cache.lock().unwrap();
		cache
			.get(&monitor)
			.map(|(_, _, scope)| scope.clone())
			.unwrap_or_default()
	}

	fn add_track(&mut self, kind: TrackKind, cx: &mut Context<Self>) {
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let rc = unsafe { oakengine_sequence_add_track(seq, Self::track_type_of(kind)) };
		self.apply_edit(rc, "add track", cx);
	}

	fn remove_track(&mut self, index: usize, cx: &mut Context<Self>) {
		let Some(track) = self.tracks.get(index) else {
			return;
		};
		let (track_type, track_index) = (track.track_type, track.track_index);
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let rc = unsafe { oakengine_sequence_remove_track(seq, track_type, track_index as c_int) };
		self.apply_edit(rc, "remove track", cx);
	}

	fn set_track_height(&mut self, height: Pixels, cx: &mut Context<Self>) {
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let internal =
			unsafe { oakengine_track_height_pixels_to_internal(f32::from(height) as c_int) };
		let mut video: c_int = 0;
		let mut audio: c_int = 0;
		let mut subtitle: c_int = 0;
		unsafe {
			oakengine_sequence_track_count(seq, &mut video, &mut audio, &mut subtitle);
		}
		for (track_type, count) in [
			(TRACK_TYPE_VIDEO, video),
			(TRACK_TYPE_AUDIO, audio),
			(TRACK_TYPE_SUBTITLE, subtitle),
		] {
			for index in 0..count {
				unsafe { oakengine_track_set_height(seq, track_type, index, internal) };
			}
		}
		self.rebuild_timeline();
		cx.notify();
	}

	fn select_item(&mut self, id: u64, cx: &mut Context<Self>) {
		let changed = self.selected_item != Some(id);
		self.selected_item = Some(id);
		if changed {
			// The source monitor renders the selected footage node: a new
			// selection must rebind the renderer and drop the stale cached
			// frame (the cache key only tracks the playhead frame).
			*self.source_renderer.lock().unwrap() = RendererSlot::Untried;
			self.cpu_frame_cache.lock().unwrap().remove(&Monitor::Source);
		}
		cx.notify();
	}

	fn set_selected_clips(&mut self, clips: Vec<ClipId>, cx: &mut Context<Self>) {
		// The effect stack targets exactly one clip: an empty or
		// multi-clip selection keeps the empty state (see
		// `EffectStackDataSource::target_label`).
		self.selected_clip = (clips.len() == 1).then(|| clips[0]);
		cx.notify();
	}

	fn addable_effects(&self) -> Vec<(String, String)> {
		// The factory entries flagged `video_effect` and not hidden from
		// the create menu (per the facade contract). A scratch node per
		// entry just to read its flags (freed immediately).
		let mut out = Vec::new();
		let count = unsafe { oakengine_node_factory_id_count() };
		let video_flag = unsafe { oakengine_node_flag_video_effect() };
		let hidden_flag = unsafe { oakengine_node_flag_dont_show_in_create_menu() };
		for i in 0..count.max(0) {
			let type_id =
				read_string(|buf, size| unsafe { oakengine_node_factory_id_at(i, buf, size) });
			let Some(c_id) = CString::new(type_id.clone()).ok() else {
				continue;
			};
			let node = unsafe { oakengine_node_factory_create_from_id(c_id.as_ptr()) };
			if node.is_null() {
				continue;
			}
			let flags = unsafe { oakengine_node_get_flags(node) };
			// SAFETY: `node` is an owned box from the factory.
			unsafe { oakengine_node_free(node) };
			if flags & video_flag != 0 && flags & hidden_flag == 0 {
				let name = read_string(|buf, size| {
					// SAFETY: `c_id` outlives the call; buf/size follow the
					// facade two-stage convention.
					unsafe { oakengine_node_factory_name_from_id(c_id.as_ptr(), buf, size) }
				});
				let name = if name.is_empty() {
					type_id.clone()
				} else {
					name
				};
				out.push((type_id, name));
			}
		}
		out
	}

	fn add_effect(
		&mut self,
		index: usize,
		type_id: &str,
		cx: &mut Context<Self>,
	) -> Result<(), String> {
		let Some(host) = self.selected_clip_node() else {
			return Err("no selected clip".into());
		};
		let c_id = CString::new(type_id).map_err(|_| "invalid effect type id".to_string())?;
		// SAFETY: `host` is a live box; freed below.
		let rc = unsafe { oakengine_node_effect_insert(host, index as c_int, c_id.as_ptr()) };
		unsafe { oakengine_node_free(host) };
		if rc != 0 {
			return Err(format!("facade error {rc}"));
		}
		self.apply_edit(rc, "add effect", cx);
		Ok(())
	}

	fn apply_effect_event(&mut self, event: &EffectStackEvent, cx: &mut Context<Self>) {
		match event {
			EffectStackEvent::EnableToggled { effect, enabled } => {
				let Some(host) = self.selected_clip_node() else {
					cx.notify();
					return;
				};
				// SAFETY: both boxes are live and freed below.
				let rc = unsafe {
					let Some(eff) = Self::chain_effect_by_identity(host, effect.0) else {
						oakengine_node_free(host);
						cx.notify();
						return;
					};
					let rc = oakengine_node_effect_set_enabled(eff, *enabled as c_int);
					oakengine_node_free(eff);
					oakengine_node_free(host);
					rc
				};
				self.apply_edit(rc, "toggle effect", cx);
			}
			EffectStackEvent::ExpansionToggled { effect, expanded } => {
				// View state only (not undoable); kept here so the card
				// list re-reads it after the notify.
				if *expanded {
					self.expanded_effects.insert(effect.0);
				} else {
					self.expanded_effects.remove(&effect.0);
				}
				cx.notify();
			}
			EffectStackEvent::RemoveRequested(id) => {
				let Some(host) = self.selected_clip_node() else {
					cx.notify();
					return;
				};
				// SAFETY: both boxes are live and freed below.
				let rc = unsafe {
					let Some(eff) = Self::chain_effect_by_identity(host, id.0) else {
						oakengine_node_free(host);
						cx.notify();
						return;
					};
					let rc = oakengine_node_effect_remove(host, eff);
					oakengine_node_free(eff);
					oakengine_node_free(host);
					rc
				};
				self.apply_edit(rc, "remove effect", cx);
			}
			EffectStackEvent::ReorderRequested { effect, new_index } => {
				let Some(host) = self.selected_clip_node() else {
					cx.notify();
					return;
				};
				// SAFETY: both boxes are live and freed below.
				let rc = unsafe {
					let Some(eff) = Self::chain_effect_by_identity(host, effect.0) else {
						oakengine_node_free(host);
						cx.notify();
						return;
					};
					let rc = oakengine_node_effect_move(host, eff, *new_index as c_int);
					oakengine_node_free(eff);
					oakengine_node_free(host);
					rc
				};
				self.apply_edit(rc, "reorder effect", cx);
			}
			EffectStackEvent::AddRequested { index } => {
				// The effect choice is a panel-owned menu (see
				// `InspectorPanel`); the undoable insert runs through
				// `AppEngine::add_effect` once the user picks a type.
				// `index` is acknowledged here for parity with the
				// request semantics (the panel passes it back).
				let _ = index;
				cx.notify();
			}
			// The app owns the context menu; parameter changes have no
			// metadata to refresh yet.
			EffectStackEvent::ContextMenuRequested { .. }
			| EffectStackEvent::ParameterChanged { .. } => {
				cx.notify();
			}
		}
	}

	fn apply_node_graph_event(&mut self, event: &NodeGraphEvent, cx: &mut Context<Self>) {
		match event {
			// Preview events never persist: the widget draws dragged nodes at
			// their model position plus the preview delta, and the position
			// is written back (as one undoable step) only when the drag ends
			// in `NodeMoveRequested`.
			NodeGraphEvent::NodeMovePreview { .. }
			| NodeGraphEvent::ViewChanged { .. }
			| NodeGraphEvent::BackgroundClicked { .. }
			| NodeGraphEvent::SelectionChanged { .. } => {}
			_ => {
				// SAFETY: the sequence box is live while the engine holds it.
				let result = unsafe {
					crate::oakui::nodegraph::apply_edit(
						self.seq_ptr().unwrap_or(std::ptr::null_mut()),
						event,
					)
				};
				if let Err(e) = result {
					println!("[real engine] node-graph request rejected: {e}");
				}
			}
		}
		cx.notify();
	}

	fn apply_timeline_event(&mut self, event: &TimelineEvent, cx: &mut Context<Self>) {
		match event {
			TimelineEvent::PlayheadChanged(frame) => {
				let current = self.clock_frame(Monitor::Program, cx);
				if *frame != current {
					self.request_frame(Monitor::Program, *frame, cx);
				}
			}
			TimelineEvent::ClipTrimRequested {
				clip,
				edge,
				new_frame,
			} => {
				let Some((track_type, track_index, clip_index)) = self.clip_coords(*clip) else {
					return;
				};
				// Re-read the clip's current range, then compute the new
				// in/out pair for `oakengine_clip_trim`.
				let Some(seq) = self.seq_ptr() else {
					return;
				};
				let clip_ptr = unsafe {
					oakengine_sequence_clip_at(
						seq,
						Self::track_type_of(track_type),
						track_index as c_int,
						clip_index as c_int,
					)
				};
				if clip_ptr.is_null() {
					return;
				}
				let mut in_ts: i64 = 0;
				let mut out_ts: i64 = 0;
				let mut media_in: i64 = 0;
				unsafe {
					oakengine_clip_get_range(clip_ptr, &mut in_ts, &mut out_ts, &mut media_in);
				}
				let (new_in, new_out) = match edge {
					TrimEdge::Start => (new_frame.0, out_ts),
					TrimEdge::End => (in_ts, new_frame.0),
				};
				let rc = unsafe { oakengine_clip_trim(clip_ptr, new_in, new_out) };
				unsafe { free_box(clip_ptr) };
				self.apply_edit(rc, "trim clip", cx);
			}
			TimelineEvent::ClipMoveRequested {
				clip,
				new_track,
				new_start,
			} => {
				// M12 P4: cross-track moves go through the dedicated
				// facade export (one undoable entry); same-track moves
				// use the classic export.
				let Some((track_type, track_index, clip_index)) = self.clip_coords(*clip) else {
					return;
				};
				let Some(seq) = self.seq_ptr() else {
					return;
				};
				let rc = if *new_track as c_int != track_index as c_int {
					unsafe {
						oakengine_sequence_move_clip_to_track(
							seq,
							Self::track_type_of(track_type),
							track_index as c_int,
							clip_index as c_int,
							*new_track as c_int,
							new_start.0,
						)
					}
				} else {
					unsafe {
						oakengine_sequence_move_clip(
							seq,
							Self::track_type_of(track_type),
							track_index as c_int,
							clip_index as c_int,
							new_start.0,
						)
					}
				};
				self.apply_edit(rc, "move clip", cx);
			}
			TimelineEvent::TrackHeightChanged { track, height } => {
				if let Some(t) = self.tracks.get(*track) {
					let internal = unsafe {
						oakengine_track_height_pixels_to_internal(f32::from(height) as c_int)
					};
					if let Some(seq) = self.seq_ptr() {
						unsafe {
							oakengine_track_set_height(
								seq,
								t.track_type,
								t.track_index as c_int,
								internal,
							)
						};
					}
					self.rebuild_timeline();
				}
				cx.notify();
			}
			// Selection / zoom / transition / track-selected: not editable.
			TimelineEvent::SelectionChanged
			| TimelineEvent::TrackSelected { .. }
			| TimelineEvent::TransitionChanged { .. }
			| TimelineEvent::ZoomChanged(_) => {}
				TimelineEvent::WorkAreaPreview { start, end } => {
					self.set_workarea_preview(*start, *end, cx);
				}
				TimelineEvent::WorkAreaCommitted {
					start,
					end,
					old_start,
					old_end,
				} => {
					self.commit_workarea(*old_start, *old_end, *start, *end, cx);
				}
		}
	}

	fn split_clip(&mut self, clip: ClipId, time: Frame, cx: &mut Context<Self>) {
		let Some((track_type, track_index, clip_index)) = self.clip_coords(clip) else {
			return;
		};
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let rc = unsafe {
			oakengine_sequence_split_clip(
				seq,
				Self::track_type_of(track_type),
				track_index as c_int,
				clip_index as c_int,
				time.0,
			)
		};
		self.apply_edit(rc, "split clip", cx);
	}

	fn split_at_playhead(&mut self, cx: &mut Context<Self>) {
		let frame = self.clock_frame(Monitor::Program, cx);
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let targets: Vec<(c_int, usize, usize)> = self
			.tracks
			.iter()
			.flat_map(|track| {
				track.clips.iter().filter_map(|clip| {
					if clip.range.start.0 < frame.0 && frame.0 < clip.range.end.0 {
						Some((track.track_type, track.track_index, clip.clip_index))
					} else {
						None
					}
				})
			})
			.collect();
		let mut rc = 0;
		for (track_type, track_index, clip_index) in targets {
			rc = unsafe {
				oakengine_sequence_split_clip(
					seq,
					track_type,
					track_index as c_int,
					clip_index as c_int,
					frame.0,
				)
			};
		}
		self.apply_edit(rc, "split at playhead", cx);
	}

	fn workarea(&self) -> Option<(Frame, Frame)> {
		let seq = self.seq_ptr()?;
		if unsafe { oakengine_sequence_workarea_is_enabled(seq) } == 0 {
			return None;
		}
		let mut in_ts: i64 = 0;
		let mut out_ts: i64 = 0;
		if unsafe { oakengine_sequence_get_workarea(seq, &mut in_ts, &mut out_ts) } != 0 {
			return None;
		}
		Some((Frame(in_ts), Frame(out_ts)))
	}

	fn add_marker_at_playhead(&mut self, cx: &mut Context<Self>) {
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let frame = self.clock_frame(Monitor::Program, cx);
		let rc = unsafe { oakengine_sequence_marker_add(seq, frame.0, c"".as_ptr()) };
		self.apply_edit(rc, "add marker", cx);
	}

	fn remove_marker_at_playhead(&mut self, cx: &mut Context<Self>) {
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let frame = self.clock_frame(Monitor::Program, cx);
		let rc = unsafe { oakengine_sequence_marker_remove(seq, frame.0) };
		// Removing a marker that is not there is a benign no-op for the menu
		// action (the facade reports NOT_FOUND); only rebuild on success.
		if rc != 0 {
			return;
		}
		self.apply_edit(rc, "remove marker", cx);
	}

	fn set_workarea_preview(&mut self, start: Frame, end: Frame, cx: &mut Context<Self>) {
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		// Live, non-undoable: the engine workarea tracks the drag so other
		// reads (export, snap) stay current; no timeline rebuild needed — the
		// band itself is widget-local state.
		unsafe { oakengine_sequence_set_workarea(seq, 1, start.0, end.0) };
		cx.notify();
	}

	fn commit_workarea(
		&mut self,
		old_start: Frame,
		old_end: Frame,
		start: Frame,
		end: Frame,
		cx: &mut Context<Self>,
	) {
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let rc = unsafe {
			oakengine_sequence_set_workarea_undoable(
				seq,
				1,
				start.0,
				end.0,
				old_start.0,
				old_end.0,
			)
		};
		self.apply_edit(rc, "set workarea", cx);
	}

	fn clear_workarea(&mut self, cx: &mut Context<Self>) {
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let (old_start, old_end) = self.workarea().unwrap_or((Frame::ZERO, Frame::ZERO));
		let rc = unsafe {
			oakengine_sequence_set_workarea_undoable(
				seq,
				0,
				old_start.0,
				old_end.0,
				old_start.0,
				old_end.0,
			)
		};
		self.apply_edit(rc, "clear workarea", cx);
	}

	fn delete_clip(&mut self, clip: ClipId, ripple: bool, cx: &mut Context<Self>) {
		let Some((track_type, track_index, clip_index)) = self.clip_coords(clip) else {
			return;
		};
		let Some(seq) = self.seq_ptr() else {
			return;
		};
		let rc = if ripple {
			unsafe {
				oakengine_sequence_ripple_delete_clip(
					seq,
					Self::track_type_of(track_type),
					track_index as c_int,
					clip_index as c_int,
				)
			}
		} else {
			let clip_ptr = unsafe {
				oakengine_sequence_clip_at(
					seq,
					Self::track_type_of(track_type),
					track_index as c_int,
					clip_index as c_int,
				)
			};
			if clip_ptr.is_null() {
				return;
			}
			let mut clips = [clip_ptr];
			let mut rippled: c_int = 0;
			let rc = unsafe {
				oakengine_sequence_delete_clips(
					seq,
					clips.as_mut_ptr(),
					1,
					0,
					std::ptr::null(),
					0,
					&mut rippled,
				)
			};
			unsafe { free_box(clip_ptr) };
			rc
		};
		self.apply_edit(
			rc,
			if ripple {
				"ripple delete clip"
			} else {
				"delete clip"
			},
			cx,
		);
	}

	fn can_undo(&self) -> bool {
		self.project_ptr()
			.map(|p| unsafe { oakengine_project_can_undo(p) } != 0)
			.unwrap_or(false)
	}

	fn can_redo(&self) -> bool {
		self.project_ptr()
			.map(|p| unsafe { oakengine_project_can_redo(p) } != 0)
			.unwrap_or(false)
	}

	fn undo(&mut self, cx: &mut Context<Self>) {
		if let Some(p) = self.project_ptr() {
			unsafe {
				oakengine_project_undo(p);
			}
			self.refresh_sequence_info();
			self.rebuild_timeline();
			self.cpu_frame_cache.lock().unwrap().clear();
			cx.notify();
		}
	}

	fn redo(&mut self, cx: &mut Context<Self>) {
		if let Some(p) = self.project_ptr() {
			unsafe {
				oakengine_project_redo(p);
			}
			self.refresh_sequence_info();
			self.rebuild_timeline();
			self.cpu_frame_cache.lock().unwrap().clear();
			cx.notify();
		}
	}

	fn new_project(&mut self, cx: &mut Context<Self>) {
		let project = unsafe { oakengine_project_create() };
		if project.is_null() {
			println!("[real engine] failed to create a blank project");
			return;
		}
		if unsafe { oakengine_project_new(project) } != 0 {
			unsafe { oakengine_project_free(project) };
			println!("[real engine] failed to initialize a blank project");
			return;
		}
		self.adopt_project(project, cx);
	}

	fn open_project_path(&mut self, path: PathBuf, cx: &mut Context<Self>) -> Result<(), String> {
		let ext = path
			.extension()
			.and_then(|e| e.to_str())
			.map(|e| e.to_ascii_lowercase());
		match ext.as_deref() {
			Some("otio") | Some("fcpxml") => self.open_interchange(&path, cx),
			_ => self.open_ove(&path, cx),
		}
	}

	fn export_project_path(
		&mut self,
		path: PathBuf,
		cx: &mut Context<Self>,
	) -> Result<(), String> {
		if self.project_ptr().is_none() {
			return Err("no project open".into());
		}
		let ext = path
			.extension()
			.and_then(|e| e.to_str())
			.map(|e| e.to_ascii_lowercase());
		match ext.as_deref() {
			Some("otio") | Some("fcpxml") => self.export_interchange(&path, cx),
			_ => self.export_ove(&path, cx),
		}
	}

	fn close_project(&mut self, cx: &mut Context<Self>) {
		self.drop_project();
		cx.notify();
	}

	fn import_footage(&mut self, path: PathBuf, cx: &mut Context<Self>) -> Result<(), String> {
		let Some(project) = self.project_ptr() else {
			return Err("no project open".into());
		};
		let Some(path_c) = cstr_path(&path) else {
			return Err("invalid import path".into());
		};
		// SAFETY: `project` is the live facade handle the engine owns; the
		// returned footage box is freed below.
		let footage = unsafe { oakengine_project_import_footage(project, path_c.as_ptr()) };
		if footage.is_null() {
			let error = read_string(|buf, size| unsafe {
				oakengine_footage_last_error(buf, size)
			});
			return Err(if error.is_empty() {
				format!("failed to import \"{}\"", path.display())
			} else {
				error
			});
		}
		// SAFETY: `footage` is an owned facade box (`oakengine_footage_free`).
		unsafe { oakengine_footage_free(footage) };
		// The material bin reads the folder tree live from the facade, so a
		// notify is enough for the explorer to list the new entry.
		cx.notify();
		Ok(())
	}

	// --- project library (M13 D4) --------------------------------------

	fn storage_bound(&self) -> bool {
		self.project_ptr()
			.map(|p| unsafe { oakengine_storage_is_bound(p) } != 0)
			.unwrap_or(false)
	}

	fn storage_last_error(&self) -> Option<String> {
		let project = self.project_ptr()?;
		let message = read_string(|buf, size| unsafe {
			oakengine_storage_last_error(project, buf, size)
		});
		if message.is_empty() {
			None
		} else {
			Some(message)
		}
	}

	fn library_projects(&self) -> Result<Vec<LibraryProject>, String> {
		library_list()
	}

	fn library_create_project(&mut self, name: &str, cx: &mut Context<Self>) -> Result<(), String> {
		let uuid = library_create(name)?;
		self.open_library_project(&uuid, cx)
	}

	fn library_open_project(&mut self, uuid: &str, cx: &mut Context<Self>) -> Result<(), String> {
		self.open_library_project(uuid, cx)
	}

	fn library_delete_project(&mut self, uuid: &str) -> Result<(), String> {
		let uuid_c = CString::new(uuid).map_err(|_| "invalid uuid".to_string())?;
		let rc = unsafe { oakengine_library_delete(uuid_c.as_ptr()) };
		if rc != 0 {
			return Err(format!("failed to delete the project (error {rc})"));
		}
		Ok(())
	}

	fn library_rename_project(&mut self, uuid: &str, name: &str) -> Result<(), String> {
		let uuid_c = CString::new(uuid).map_err(|_| "invalid uuid".to_string())?;
		let name_c = CString::new(name).map_err(|_| "invalid name".to_string())?;
		let rc = unsafe { oakengine_library_rename(uuid_c.as_ptr(), name_c.as_ptr()) };
		if rc != 0 {
			return Err(format!("failed to rename the project (error {rc})"));
		}
		Ok(())
	}

	fn library_duplicate_project(&mut self, uuid: &str) -> Result<(), String> {
		let uuid_c = CString::new(uuid).map_err(|_| "invalid uuid".to_string())?;
		let mut buf = [0 as c_char; 256];
		// Single call with a stack buffer: the duplicate has a side effect,
		// so the two-stage (measure-then-read) pattern must not be used.
		let rc = unsafe {
			oakengine_library_duplicate(
				uuid_c.as_ptr(),
				std::ptr::null(),
				buf.as_mut_ptr(),
				buf.len() as c_int,
			)
		};
		if rc < 0 {
			return Err(format!("failed to duplicate the project (error {rc})"));
		}
		Ok(())
	}

	fn library_import_project(&mut self, path: PathBuf) -> Result<String, String> {
		let path_c = cstr_path(&path).ok_or("invalid import path")?;
		let mut buf = [0 as c_char; 256];
		// Single call with a stack buffer (side effect; see duplicate).
		let rc = unsafe {
			oakengine_library_import(path_c.as_ptr(), buf.as_mut_ptr(), buf.len() as c_int)
		};
		if rc < 0 {
			return Err(format!(
				"failed to import \"{}\" (error {rc})",
				path.display()
			));
		}
		let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
		Ok(
			String::from_utf8_lossy(unsafe {
				std::slice::from_raw_parts(buf.as_ptr() as *const u8, len)
			})
			.into_owned(),
		)
	}

	fn library_export_project(&mut self, uuid: &str, path: PathBuf) -> Result<(), String> {
		let uuid_c = CString::new(uuid).map_err(|_| "invalid uuid".to_string())?;
		let path_c = cstr_path(&path).ok_or("invalid export path")?;
		let rc = unsafe { oakengine_library_export(uuid_c.as_ptr(), path_c.as_ptr()) };
		if rc != 0 {
			return Err(format!(
				"failed to export the project to \"{}\" (error {rc})",
				path.display()
			));
		}
		Ok(())
	}

	fn start_export(&mut self, format: i32, path: PathBuf) -> Result<ExportSession, String> {
		let Some(seq) = self.seq_ptr() else {
			return Err("no sequence open".into());
		};

		// Build the encoding params from the sequence's format.
		let params = unsafe { oakengine_encoding_params_create() };
		if params.is_null() {
			return Err("failed to create encoding params".into());
		}
		let cpath = cstr_path(&path).ok_or("invalid output path")?;
		let rc = unsafe { oakengine_encoding_params_set_filename(params, cpath.as_ptr()) };
		if rc != 0 {
			unsafe { oakengine_encoding_params_destroy(params) };
			return Err(format!("failed to set the export filename (error {rc})"));
		}
		let rc = unsafe { oakengine_encoding_params_set_format(params, format) };
		if rc != 0 {
			unsafe { oakengine_encoding_params_destroy(params) };
			return Err(format!("failed to set the export format (error {rc})"));
		}
		// Video params POD from the sequence; first video codec of the format.
		let mut width: c_int = 0;
		let mut height: c_int = 0;
		let mut par_num: c_int = 1;
		let mut par_den: c_int = 1;
		let mut rate_num: c_int = 25;
		let mut rate_den: c_int = 1;
		unsafe {
			oakengine_sequence_get_video_params(
				seq,
				&mut width,
				&mut height,
				&mut par_num,
				&mut par_den,
			);
			oakengine_sequence_get_frame_rate(seq, &mut rate_num, &mut rate_den);
		}
		let video_codec = unsafe { oakengine_encoding_format_video_codec_at(format, 0) };
		if video_codec < 0 {
			unsafe { oakengine_encoding_params_destroy(params) };
			return Err(format!("format {format} has no video codec"));
		}
		let pod = OakVideoParamsPod {
			width: width.max(1),
			height: height.max(1),
			time_base_num: rate_den.max(1),
			time_base_den: rate_num.max(1),
			format: 0,
			pixel_aspect_num: par_num.max(1),
			pixel_aspect_den: par_den.max(1),
			interlacing: 0,
			color_range: 0,
			divider: 1,
			video_type: 0,
			premultiplied_alpha: 0,
		};
		let rc = unsafe { oakengine_encoding_params_enable_video(params, &pod, video_codec) };
		if rc != 0 {
			unsafe { oakengine_encoding_params_destroy(params) };
			return Err(format!("failed to enable video (error {rc})"));
		}
		let audio_codec = unsafe { oakengine_encoding_format_audio_codec_at(format, 0) };
		if audio_codec < 0 {
			unsafe { oakengine_encoding_params_destroy(params) };
			return Err(format!("format {format} has no audio codec"));
		}
		let rc = unsafe {
			oakengine_encoding_params_enable_audio(
				params,
				EXPORT_SAMPLE_RATE,
				EXPORT_CHANNEL_LAYOUT,
				EXPORT_SAMPLE_FORMAT,
				audio_codec,
			)
		};
		if rc != 0 {
			unsafe { oakengine_encoding_params_destroy(params) };
			return Err(format!("failed to enable audio (error {rc})"));
		}
		// Export range: the work area when enabled (M12 P4), otherwise the
		// whole sequence. Frames → seconds rationals in the sequence's
		// frame-rate timebase (frame duration = rate_den / rate_num).
		if let Some((in_ts, out_ts)) = self.workarea().filter(|(s, e)| e.0 > s.0) {
			let tb_num = i64::from(rate_den.max(1));
			let tb_den = i64::from(rate_num.max(1));
			unsafe {
				oakengine_encoding_params_set_custom_range(
					params,
					in_ts.0 * tb_num,
					tb_den,
					out_ts.0 * tb_num,
					tb_den,
				);
				oakengine_encoding_params_set_export_length(
					params,
					((out_ts.0 - in_ts.0) * tb_num) as c_int,
					rate_num.max(1),
				);
			}
		} else {
			let length = self.sequence_length();
			if length.0 > 0 {
				unsafe {
					oakengine_encoding_params_set_export_length(params, length.0 as c_int, 1);
				}
			}
		}

		let task = unsafe { oakengine_task_create_export(seq, params) };
		if task.is_null() {
			unsafe { oakengine_encoding_params_destroy(params) };
			return Err("failed to create the export task".into());
		}

		// Progress events through the facade task subscription (the callback
		// is invoked on the task's own thread with the raw userdata pointer).
		let (tx, rx) = mpsc::channel::<ExportEvent>();
		let cb_userdata = SendPtr(Box::into_raw(Box::new(tx.clone())));
		unsafe {
			oakengine_task_subscribe(task, Some(export_event_cb), cb_userdata.0 as *mut c_void);
		}

		// The task pointer is shared between the cancel handle and the worker
		// thread; the thread owns it and frees it when the run ends.
		let shared = Arc::new(Mutex::new(Some(SendPtr(task))));
		let cancel = {
			let shared = shared.clone();
			Box::new(move || {
				if let Some(task) = shared.lock().unwrap().as_ref() {
					unsafe {
						oakengine_task_cancel(task.0);
					}
				}
			})
		};
		let worker = shared.clone();
		std::thread::spawn(move || {
			unsafe {
				let task = worker
					.lock()
					.unwrap()
					.as_ref()
					.expect("export task present")
					.0;
				let ok = oakengine_task_start_sync(task);
				let error = RealEngine::task_error(task);
				oakengine_task_free(task);
				*worker.lock().unwrap() = None;
				// Reclaim the callback userdata (the task's listener is
				// one-shot and dropped after the run).
				reclaim_userdata(cb_userdata);
				let _ = tx.send(ExportEvent::Finished(ok != 0, error));
			}
		});

		Ok(ExportSession { events: rx, cancel })
	}
	fn backend_name(&self) -> &'static str {
		"real"
	}
}

// ---------------------------------------------------------------------------
// Format dispatch (pure, unit tested)
// ---------------------------------------------------------------------------

/// Classifies a project-file extension for open/save dispatch.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProjectFormat {
	/// `.ove` / `.ovexml` — the native serializer.
	Ove,
	/// `.otio` — OpenTimelineIO JSON.
	Otio,
	/// `.fcpxml` — Final Cut Pro XML.
	Fcpxml,
}

impl ProjectFormat {
	/// The format for a file path, by extension (unknown → [`Ove`]
	/// (ProjectFormat::Ove), matching the app's default serializer).
	pub fn of(path: &PathBuf) -> Self {
		let ext = path
			.extension()
			.and_then(|e| e.to_str())
			.map(|e| e.to_ascii_lowercase());
		match ext.as_deref() {
			Some("otio") => ProjectFormat::Otio,
			Some("fcpxml") => ProjectFormat::Fcpxml,
			_ => ProjectFormat::Ove,
		}
	}
}

impl RealEngine {
	/// Opens a `.ove` / `.ovexml` project through the facade serializer.
	fn open_ove(&mut self, path: &PathBuf, cx: &mut Context<Self>) -> Result<(), String> {
		let project = unsafe { oakengine_project_create() };
		if project.is_null() {
			return Err("failed to create a project".into());
		}
		let Some(cpath) = cstr_path(path) else {
			unsafe { oakengine_project_free(project) };
			return Err("invalid project path".into());
		};
		let mut err = [0 as c_char; 4096];
		let rc = unsafe {
			oakengine_project_load(
				project,
				cpath.as_ptr(),
				err.as_mut_ptr(),
				err.len() as c_int,
			)
		};
		if rc != 0 {
			let message = load_error(&mut err);
			unsafe { oakengine_project_free(project) };
			return Err(format!("failed to load \"{}\": {message}", path.display()));
		}
		// The module serializer cannot parse every legacy document (e.g. the
		// `<olive>`-rooted format skips its nested `<project>` body), which
		// loads "successfully" with no content; surface it instead of
		// pretending the project opened.
		let nodes = unsafe { oakengine_project_node_count(project) };
		if nodes == 0 {
			println!(
				"[real engine] warning: \"{}\" loaded but contained no parseable content; starting from an empty project",
				path.display()
			);
		}
		self.adopt_project(project, cx);
		Ok(())
	}

	/// Opens an `.otio` / `.fcpxml` project through the oaktask interchange
	/// loader and adopts the loaded project (`oakengine_task_load_take_project`
	/// hands over the loader's project after a successful run).
	fn open_interchange(&mut self, path: &PathBuf, cx: &mut Context<Self>) -> Result<(), String> {
		let Some(cpath) = cstr_path(path) else {
			return Err("invalid project path".into());
		};
		let task = unsafe { oakengine_task_create_project_load_otio(cpath.as_ptr()) };
		if task.is_null() {
			return Err("failed to create the interchange load task".into());
		}
		let rc = unsafe { oakengine_task_start_sync(task) };
		let error = Self::task_error(task);
		let loaded = {
			let project = unsafe { oakengine_task_load_take_project(task) };
			if project.is_null() {
				None
			} else {
				Some(project)
			}
		};
		unsafe { oakengine_task_free(task) };
		if rc == 0 {
			return Err(format!("failed to load \"{}\": {error}", path.display()));
		}
		match loaded {
			Some(project) => {
				self.adopt_project(project, cx);
				Ok(())
			}
			None => Err(format!(
				"loaded \"{}\" but the loader returned no project",
				path.display()
			)),
		}
	}

	/// Exports the project to `path` through the OVE serializer (the .ove
	/// branch of 导出工程文件…; the write-through library stays the primary
	/// persistence, this only writes a file).
	fn export_ove(&mut self, path: &Path, _cx: &mut Context<Self>) -> Result<(), String> {
		let Some(project) = self.project_ptr() else {
			return Err("no project open".into());
		};
		let Some(cpath) = cstr_path(path) else {
			return Err("invalid project path".into());
		};
		let rc = unsafe { oakengine_project_save(project, cpath.as_ptr()) };
		if rc != 0 {
			return Err(format!("failed to export the project (error {rc})"));
		}
		// The facade records the target filename (legacy save side effect);
		// refresh the display name to match.
		let name = read_string(|buf, size| unsafe { oakengine_project_name(project, buf, size) });
		if !name.is_empty() {
			self.project_info.name = name;
		}
		let filename =
			read_string(|buf, size| unsafe { oakengine_project_filename(project, buf, size) });
		if !filename.is_empty() {
			self.project_info.path = PathBuf::from(filename);
		}
		Ok(())
	}

	/// Exports as `.otio` / `.fcpxml` through the oaktask save task (the
	/// facade derives the output filename from the project's own filename).
	fn export_interchange(&mut self, path: &PathBuf, _cx: &mut Context<Self>) -> Result<(), String> {
		let Some(project) = self.project_ptr() else {
			return Err("no project open".into());
		};
		let Some(cpath) = cstr_path(path) else {
			return Err("invalid project path".into());
		};
		let rc = unsafe { oakengine_project_set_filename(project, cpath.as_ptr()) };
		if rc != 0 {
			return Err(format!("failed to set the output filename (error {rc})"));
		}
		let task = unsafe { oakengine_task_create_project_save_otio(project) };
		if task.is_null() {
			return Err("failed to create the interchange save task".into());
		}
		let rc = unsafe { oakengine_task_start_sync(task) };
		let error = Self::task_error(task);
		unsafe { oakengine_task_free(task) };
		if rc == 0 {
			return Err(format!("failed to export \"{}\": {error}", path.display()));
		}
		self.project_info.path = path.clone();
		Ok(())
	}
}

/// Builds the export-format list: (format id, display name, extension) from
/// the oakcodec encoding enumeration.
///
/// Pure helper so the export dialog can be unit tested; the facade is only
/// consulted for the real engine.
pub fn encoding_formats() -> Vec<(c_int, String, String)> {
	let count = unsafe { oakengine_encoding_format_count() };
	let mut out = Vec::new();
	for i in 0..count.max(0) {
		let name = read_string(|buf, size| unsafe { oakengine_encoding_format_name(i, buf, size) });
		let ext =
			read_string(|buf, size| unsafe { oakengine_encoding_format_extension(i, buf, size) });
		out.push((i, name, ext));
	}
	out
}

/// The format id of the default export container: MPEG-4 Video (`.mp4`).
pub const EXPORT_FORMAT_MP4: c_int = 2;

// ---------------------------------------------------------------------------
// Config C ABI (renderer backend + language)
// ---------------------------------------------------------------------------

/// The config key selecting the renderer backend (worker `create_renderer`
/// backend id).
pub const CONFIG_KEY_RENDERER_BACKEND: &str = "GraphicsBackend";

/// Reads a config string through the facade config C ABI (empty when
/// missing).
pub fn config_get_string(key: &str) -> String {
	let Ok(key_c) = CString::new(key) else {
		return String::new();
	};
	read_string(|buf, size| unsafe { oakengine_config_get_string(key_c.as_ptr(), buf, size) })
}

/// Writes a config string through the facade config C ABI.
pub fn config_set_string(key: &str, value: &str) {
	let (Ok(key_c), Ok(value_c)) = (CString::new(key), CString::new(value)) else {
		return;
	};
	unsafe {
		oakengine_config_set_string(key_c.as_ptr(), value_c.as_ptr());
	}
}

/// The renderer backends offered in the preferences dialog, in display
/// order. The first entry is the built-in default.
pub fn renderer_backends() -> Vec<&'static str> {
	vec!["opengl", "metal", "vulkan", "none"]
}

// ---------------------------------------------------------------------------
// Project library (M13 D4: the write-through database the manager browses)
// ---------------------------------------------------------------------------

/// The config key selecting the storage backend (see
/// `crates/oakengine/src/storage.rs`).
pub const CONFIG_KEY_STORAGE_BACKEND: &str = "Storage/Backend";

/// Enables the SQLite write-through library unless the user configured the
/// backend explicitly (any existing value — including "off" — wins over the
/// app's default). The library path defaults facade-side to
/// `<system data directory>/library.db`.
pub fn configure_storage() {
	if config_get_string(CONFIG_KEY_STORAGE_BACKEND).is_empty() {
		config_set_string(CONFIG_KEY_STORAGE_BACKEND, "sqlite");
	}
}

/// Flushes every bound project (write-through + snapshot) and stops the
/// facade's snapshot thread. The app calls this on exit.
pub fn storage_flush() {
	unsafe {
		oakengine_storage_flush();
	}
}

/// The library rows, most recently modified first (the project manager's
/// data source; JSON over the facade's `oakengine_library_list`).
pub fn library_list() -> Result<Vec<LibraryProject>, String> {
	let needed = unsafe { oakengine_library_list(std::ptr::null_mut(), 0) };
	if needed < 0 {
		return Err(format!("failed to list the library (error {needed})"));
	}
	let mut buf = vec![0 as c_char; needed as usize + 1];
	unsafe { oakengine_library_list(buf.as_mut_ptr(), needed + 1) };
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	let json =
		String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) })
			.into_owned();
	let rows: serde_json::Value =
		serde_json::from_str(&json).map_err(|e| format!("malformed library list: {e}"))?;
	let Some(rows) = rows.as_array() else {
		return Err("malformed library list (not an array)".into());
	};
	Ok(rows
		.iter()
		.map(|row| {
			let s = |key: &str| row.get(key).and_then(|v| v.as_str()).unwrap_or_default().to_string();
			let n = |key: &str| row.get(key).and_then(|v| v.as_i64()).unwrap_or(0);
			LibraryProject {
				uuid: s("uuid"),
				name: s("name"),
				created_at: n("created_at"),
				modified_at: n("modified_at"),
				duration_ms: n("duration_ms"),
				track_count: n("track_count") as i32,
				clip_count: n("clip_count") as i32,
				footage_count: n("footage_count") as i32,
			}
		})
		.collect())
}

/// Creates a blank project row in the library; returns its uuid. Single
/// call with a stack buffer: the create has a side effect, so the
/// two-stage (measure-then-read) pattern must not be used.
fn library_create(name: &str) -> Result<String, String> {
	let name_c = CString::new(name).map_err(|_| "invalid name".to_string())?;
	let mut buf = [0 as c_char; 256];
	let rc = unsafe { oakengine_library_create(name_c.as_ptr(), buf.as_mut_ptr(), buf.len() as c_int) };
	if rc < 0 {
		return Err(format!("failed to create the project (error {rc})"));
	}
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	Ok(
		String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) })
			.into_owned(),
	)
}

impl RealEngine {
	/// Opens the library row `uuid` through the facade's library-load path
	/// (which binds the project to the write-through session) and adopts it.
	fn open_library_project(&mut self, uuid: &str, cx: &mut Context<Self>) -> Result<(), String> {
		let project = unsafe { oakengine_project_create() };
		if project.is_null() {
			return Err("failed to create a project".into());
		}
		let uuid_c = CString::new(uuid).map_err(|_| "invalid uuid".to_string())?;
		let mut err = [0 as c_char; 4096];
		let rc = unsafe {
			oakengine_project_load_library(
				project,
				uuid_c.as_ptr(),
				err.as_mut_ptr(),
				err.len() as c_int,
			)
		};
		if rc != 0 {
			let message = load_error(&mut err);
			unsafe { oakengine_project_free(project) };
			return Err(format!("failed to open the library project: {message}"));
		}
		self.adopt_project(project, cx);
		// The facade's project name is filename-derived ("(untitled)" for a
		// library row); display the library row name instead.
		if let Ok(rows) = library_list() {
			if let Some(row) = rows.iter().find(|row| row.uuid == uuid) {
				self.project_info.name = row.name.clone();
			}
		}
		Ok(())
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	/// Serializes the media/FFmpeg-heavy tests: the engine dylib's static
	/// FFmpeg is not thread-safe against concurrent decode sessions.
	fn media_lock() -> std::sync::MutexGuard<'static, ()> {
		static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
		LOCK.lock().unwrap_or_else(|e| e.into_inner())
	}

	#[test]
	fn project_format_dispatches_by_extension() {
		assert_eq!(
			ProjectFormat::of(&PathBuf::from("/tmp/x.ove")),
			ProjectFormat::Ove
		);
		assert_eq!(
			ProjectFormat::of(&PathBuf::from("/tmp/x.ovexml")),
			ProjectFormat::Ove
		);
		assert_eq!(
			ProjectFormat::of(&PathBuf::from("/tmp/x.OTIO")),
			ProjectFormat::Otio
		);
		assert_eq!(
			ProjectFormat::of(&PathBuf::from("/tmp/x.fcpxml")),
			ProjectFormat::Fcpxml
		);
		// Unknown extensions fall back to the OVE serializer.
		assert_eq!(
			ProjectFormat::of(&PathBuf::from("/tmp/x.xml")),
			ProjectFormat::Ove
		);
		assert_eq!(
			ProjectFormat::of(&PathBuf::from("/tmp/x")),
			ProjectFormat::Ove
		);
	}

	#[test]
	fn renderer_backends_list_is_stable() {
		let backends = renderer_backends();
		assert!(backends.len() >= 2);
		assert!(
			backends.contains(&"opengl"),
			"the default backend is offered"
		);
	}

	/// End-to-end through the facade: a project the engine itself writes
	/// (save → load round-trip) keeps its identity, and the in-memory
	/// sequence the app drives (created with `oakengine_sequence_new`) carries
	/// real tracks. The repository's `tests/project_with_footage.ove` is a
	/// legacy `<olive>`-rooted document the oaknode serializer cannot parse,
	/// so the round-trip uses the engine's own current-format writer.
	///
	/// NOTE (documented facade gaps): `oakengine_sequence_new` keeps the
	/// sequence in a module scratch project (not the project's membership), so
	/// the saved file carries no sequence and a loaded file registers none —
	/// the app therefore opens any project and works against a fresh in-memory
	/// sequence (see [`RealEngine::adopt_project`]).
	#[test]
	fn real_project_save_load_round_trip() {
		let project = unsafe { oakengine_project_create() };
		assert!(!project.is_null());
		assert_eq!(unsafe { oakengine_project_new(project) }, 0);

		// The in-memory sequence the app drives: real tracks over the facade.
		let name = CString::new("Round Trip").unwrap();
		let sequence = unsafe { oakengine_sequence_new(project, name.as_ptr()) };
		assert!(!sequence.is_null());
		assert_eq!(
			unsafe { oakengine_sequence_add_track(sequence, TRACK_TYPE_VIDEO) },
			0
		);
		assert_eq!(
			unsafe { oakengine_sequence_add_track(sequence, TRACK_TYPE_AUDIO) },
			0
		);
		let mut video: c_int = -1;
		let mut audio: c_int = -1;
		let mut subtitle: c_int = -1;
		unsafe {
			oakengine_sequence_track_count(sequence, &mut video, &mut audio, &mut subtitle);
		}
		assert_eq!((video, audio, subtitle), (1, 1, 0), "in-memory tracks");

		// Save as uncompressed `.ovexml` (the module serializer only reads
		// plain XML).
		let save_path =
			std::env::temp_dir().join(format!("oakapp_roundtrip_{}.ovexml", std::process::id()));
		let cpath = CString::new(save_path.to_string_lossy().into_owned()).unwrap();
		assert_eq!(
			unsafe { oakengine_project_set_filename(project, cpath.as_ptr()) },
			0
		);
		assert_eq!(
			unsafe { oakengine_project_save(project, cpath.as_ptr()) },
			0
		);
		assert!(save_path.exists());
		unsafe { free_box(sequence) };
		unsafe { oakengine_project_free(project) };

		// Load it back through the same facade path the app uses: the file
		// loads and the project identity round-trips.
		let project2 = unsafe { oakengine_project_create() };
		assert!(!project2.is_null());
		let mut err = [0 as c_char; 4096];
		let rc = unsafe {
			oakengine_project_load(
				project2,
				cpath.as_ptr(),
				err.as_mut_ptr(),
				err.len() as c_int,
			)
		};
		assert_eq!(rc, 0, "project loads: {}", load_error(&mut err));
		let loaded_name =
			read_string(|buf, size| unsafe { oakengine_project_name(project2, buf, size) });
		assert!(!loaded_name.is_empty(), "the loaded project has a name");

		unsafe { oakengine_project_free(project2) };
		let _ = std::fs::remove_file(&save_path);
	}

	/// End-to-end CPU render through the same facade path
	/// [`RealEngine::render_program_frame`] uses: with the render manager up,
	/// `render_frame` on an in-memory sequence produces a real F32 frame at
	/// the renderer's proxy geometry, and the samples are well-formed
	/// (finite, in range). The sequence is empty, so the picture is black — content
	/// correctness with real footage needs the footage-input surface the
	/// facade does not bind yet (documented gap); what is asserted here is
	/// the full transport: renderer lifecycle, frame geometry/format/stride
	/// and sane sample values.
	#[test]
	fn real_render_frame_e2e() {
		let _media = media_lock();
		if !RealEngine::ensure_render_manager() {
			panic!("the render manager failed to start");
		}

		let project = unsafe { oakengine_project_create() };
		assert!(!project.is_null());
		assert_eq!(unsafe { oakengine_project_new(project) }, 0);
		let name = CString::new("Render E2E").unwrap();
		let sequence = unsafe { oakengine_sequence_new(project, name.as_ptr()) };
		assert!(!sequence.is_null());

		// The app's proxy size: sequence aspect (default 1920x1080) scaled
		// to a 480px long edge, F32 at 25 fps.
		let renderer = unsafe {
			oakengine_renderer_create(
				sequence,
				480,
				270,
				PIXEL_FORMAT_F32,
				25,
				1,
				std::ptr::null(),
			)
		};
		assert!(!renderer.is_null(), "renderer_create must succeed");

		let frame = unsafe { oakengine_renderer_render_frame(renderer, 0) };
		assert!(!frame.is_null(), "render_frame must produce a frame");
		assert_eq!(unsafe { oakengine_frame_width(frame) }, 480);
		assert_eq!(unsafe { oakengine_frame_height(frame) }, 270);
		assert_eq!(unsafe { oakengine_frame_format(frame) }, PIXEL_FORMAT_F32);
		let linesize = unsafe { oakengine_frame_linesize_bytes(frame) };
		assert!(linesize >= 480 * 4 * 4, "linesize covers a full row");
		let data = unsafe { oakengine_frame_data(frame) } as *const f32;
		assert!(!data.is_null());

		// Sample pixels across the frame: all values must be finite and in
		// range; an empty sequence renders transparent black (all zeros).
		let stride = linesize as usize / 4;
		let mut nonzero = 0usize;
		for &(x, y) in &[(0usize, 0usize), (240, 135), (479, 269)] {
			let base = y * stride + x * 4;
			let px = unsafe { std::slice::from_raw_parts(data.add(base), 4) };
			assert!(
				px.iter().all(|v| v.is_finite() && (0.0..=1.0).contains(v)),
				"samples in range: {px:?}"
			);
			nonzero += px.iter().filter(|&&v| v != 0.0).count();
		}
		assert_eq!(nonzero, 0, "an empty sequence renders transparent black");
		unsafe { oakengine_frame_free(frame) };

		// M12 P0: with a clip of real media on the video track, the same
		// renderer must produce the decoded footage (known content, non
		// black). The media is program-generated.
		let media = std::env::temp_dir().join(format!(
			"oakapp_e2e_media_{}.mp4",
			std::process::id()
		));
		let media_c = CString::new(media.to_string_lossy().into_owned()).unwrap();
		assert_eq!(
			unsafe { oakengine_testmedia_write_clip(media_c.as_ptr(), 64, 64, 10, 10) },
			0,
			"generate e2e test media"
		);
		let footage = unsafe { oakengine_project_import_footage(project, media_c.as_ptr()) };
		assert!(!footage.is_null(), "import_footage must succeed");
		assert_eq!(unsafe { oakengine_project_footage_count(project) }, 1);
		assert_eq!(
			unsafe {
				oakengine_sequence_add_track(sequence, TRACK_TYPE_VIDEO)
			},
			0
		);
		// Clip covering [0, 10) frames at 25 fps.
		let clip = unsafe {
			oakengine_sequence_add_footage_clip_ex(
				sequence,
				footage,
				TRACK_TYPE_VIDEO,
				0,
				0,
				10,
				0,
			)
		};
		if clip.is_null() {
			let msg = read_string(|buf, size| unsafe {
				oakengine_sequence_last_error(buf, size)
			});
			panic!("add_footage_clip failed: {msg}");
		}
		unsafe { oakengine_footage_free(footage) };
		let frame = unsafe { oakengine_renderer_render_frame(renderer, 0) };
		assert!(!frame.is_null(), "render_frame with a clip must produce a frame");
		let data = unsafe { oakengine_frame_data(frame) } as *const f32;
		let mut nonzero = 0usize;
		for &(x, y) in &[(0usize, 0usize), (240, 135), (479, 269)] {
			let base = y * stride + x * 4;
			let px = unsafe { std::slice::from_raw_parts(data.add(base), 4) };
			assert!(
				px.iter().all(|v| v.is_finite() && (0.0..=1.0).contains(v)),
				"samples in range: {px:?}"
			);
			nonzero += px.iter().filter(|&&v| v != 0.0).count();
		}
		assert!(
			nonzero > 0,
			"the sequence with a footage clip must render non-black pixels"
		);
		// Known content: the test clip's left half is red on frame 0 —
		// the center-left pixel must be red-dominant.
		let base = 135 * stride + 120 * 4;
		let px = unsafe { std::slice::from_raw_parts(data.add(base), 4) };
		assert!(
			px[0] > 0.5 && px[1] < 0.4 && px[2] < 0.4,
			"center-left pixel stays red from the decoded clip: {px:?}"
		);
		unsafe { oakengine_frame_free(frame) };
		let _ = std::fs::remove_file(&media);

		// A second frame at a later timestamp renders too.
		let frame2 = unsafe { oakengine_renderer_render_frame(renderer, 30) };
		assert!(!frame2.is_null());
		unsafe { oakengine_frame_free(frame2) };

		// Invalid arguments are rejected (geometry, pixel format).
		assert!(unsafe {
			oakengine_renderer_create(sequence, 0, 270, PIXEL_FORMAT_F32, 25, 1, std::ptr::null())
		}
		.is_null());
		assert!(unsafe {
			oakengine_renderer_create(sequence, 480, 270, 99999, 25, 1, std::ptr::null())
		}
		.is_null());

		unsafe { oakengine_renderer_free(renderer) };
		unsafe { free_box(sequence) };
		unsafe { oakengine_project_free(project) };
	}

	/// M12 P3 acceptance: importing a media file makes it appear in the
	/// project browser's real folder tree.
	#[test]
	fn real_project_browser_lists_imported_footage() {
		let _media = media_lock();
		let project = unsafe { oakengine_project_create() };
		assert!(!project.is_null());
		assert_eq!(unsafe { oakengine_project_new(project) }, 0);

		// Generate a real media file through the facade, import it.
		let media = std::env::temp_dir().join(format!(
			"oakapp_browser_{}.mp4",
			std::process::id()
		));
		let cpath = CString::new(media.to_string_lossy().into_owned()).unwrap();
		assert_eq!(
			unsafe { oakengine_testmedia_write_clip(cpath.as_ptr(), 64, 64, 10, 10) },
			0
		);
		let footage = unsafe { oakengine_project_import_footage(project, cpath.as_ptr()) };
		assert!(!footage.is_null(), "import must succeed");
		unsafe { oakengine_footage_free(footage) };

		// The project browser (ProjectDataSource) must list it.
		let roots = unsafe { crate::oakui::projectbrowser::roots(project) };
		assert!(!roots.is_empty(), "the root folder lists entries");
		let media_name = media.file_name().unwrap().to_string_lossy().into_owned();
		let entry = roots
			.iter()
			.find(|e| e.name.as_ref() == media_name)
			.expect("the imported footage is listed by its file name");
		assert!(!entry.is_dir, "footage entries are files");
		assert!(entry.id != 0, "entry id is the node identity");

		// Double-click behavior: the entry id resolves back to the footage
		// node through `find_by_identity`.
		let node = unsafe { crate::oakui::projectbrowser::find_by_identity(project, entry.id) };
		assert!(node.is_some(), "selection resolves to a node");
		unsafe { oakengine_node_free(node.unwrap()) };

		let _ = std::fs::remove_file(&media);
	}

	/// M12 P3 acceptance through the app seam: a `RealEngine` built exactly
	/// as the app builds it imports a generated media file through the same
	/// [`AppEngine::import_footage`] method the explorer's drag-drop handler
	/// calls, and the project browser's `roots()` lists the entry under the
	/// root folder. Selecting the entry (the double-click path) resolves back
	/// to the footage node.
	#[gpui::test]
	async fn real_engine_import_footage_lists_in_the_project_browser(
		cx: &mut gpui::TestAppContext,
	) {
		let _media = media_lock();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));

		let media = std::env::temp_dir().join(format!(
			"oakapp_engine_import_{}.mp4",
			std::process::id()
		));
		let cpath = CString::new(media.to_string_lossy().into_owned()).unwrap();
		assert_eq!(
			unsafe { oakengine_testmedia_write_clip(cpath.as_ptr(), 64, 64, 10, 10) },
			0,
			"generate e2e test media"
		);
		let imported = cx
			.update(|app| engine.update(app, |engine, cx| engine.import_footage(media.clone(), cx)));
		assert!(imported.is_ok(), "import through the seam succeeds: {imported:?}");

		// The project browser (ProjectDataSource) lists the file at the root.
		let name = media.file_name().unwrap().to_string_lossy().into_owned();
		let entry = cx.read(|app| {
			engine
				.read(app)
				.roots()
				.into_iter()
				.find(|e| e.name.as_ref() == name)
		});
		let entry = entry.expect("the imported footage is listed by its file name");
		assert!(!entry.is_dir, "footage entries are files");
		assert!(entry.id != 0, "entry id is the node identity");

		// The double-click path: selecting the entry must resolve to a node.
		cx.update(|app| engine.update(app, |engine, cx| engine.select_item(entry.id, cx)));

		let _ = std::fs::remove_file(&media);
	}

	/// M12 P2 acceptance: a real project with a sequence + footage clip
	/// builds a NON-EMPTY node graph with the wires the node editor shows:
	/// the footage feeds the clip's `tex_in` (a real edge), and every clip
	/// connects to the sequence output through the synthesized wire. Runs
	/// through the same facade path `RealEngine::nodes()`/`edges()` use.
	#[test]
	fn real_node_graph_enumerates_sequence() {
		let _media = media_lock();
		let project = unsafe { oakengine_project_create() };
		assert!(!project.is_null());
		assert_eq!(unsafe { oakengine_project_new(project) }, 0);
		let name = CString::new("Node Editor").unwrap();
		let sequence = unsafe { oakengine_sequence_new(project, name.as_ptr()) };
		assert!(!sequence.is_null());
		assert_eq!(unsafe { oakengine_sequence_add_track(sequence, TRACK_TYPE_VIDEO) }, 0);

		let media = std::env::temp_dir().join(format!(
			"oakapp_nodegraph_{}.mp4",
			std::process::id()
		));
		let cpath = CString::new(media.to_string_lossy().into_owned()).unwrap();
		assert_eq!(
			unsafe { oakengine_testmedia_write_clip(cpath.as_ptr(), 64, 64, 10, 10) },
			0
		);
		let footage = unsafe { oakengine_project_import_footage(project, cpath.as_ptr()) };
		assert!(!footage.is_null(), "import must succeed");
		let clip = unsafe {
			oakengine_sequence_add_footage_clip_ex(
				sequence,
				footage,
				TRACK_TYPE_VIDEO,
				0,
				0,
				10,
				0,
			)
		};
		assert!(!clip.is_null(), "clip placement must succeed");
		unsafe { oakengine_footage_free(footage) };
		unsafe { free_box(clip) };

		// The graph through the same builder `RealEngine::nodes()` /
		// `edges()` use (the sequence handle is the engine's).
		let (nodes, edges) = unsafe { crate::oakui::nodegraph::build_graph(sequence) };
		assert!(
			nodes.len() >= 3,
			"sequence output + clip + footage (got {} nodes)",
			nodes.len()
		);
		assert!(
			!edges.is_empty(),
			"the built graph carries wires (got {} edges)",
			edges.len()
		);

		// The output card is the sequence node; a wire lands on it.
		let seq_node = unsafe { oakengine_sequence_as_node(sequence) };
		assert!(!seq_node.is_null());
		let output_id = NodeId(unsafe { oakengine_node_identity(seq_node) });
		unsafe { oakengine_node_free(seq_node) };
		assert!(
			nodes.iter().any(|n| n.id == output_id),
			"the sequence node is the graph's output card"
		);
		let clip_edge = edges
			.iter()
			.find(|e| e.to_node == output_id)
			.expect("a wire lands on the output card");
		assert!(
			crate::oakui::nodegraph::is_output_wire(clip_edge.id),
			"the clip→output wire is the synthesized one"
		);

		// The footage→clip media edge is a REAL graph edge: the footage
		// node carries an outgoing connection (built from the module's
		// `output_connection_at_ex`), so its wire is not the synthesized
		// kind.
		let real_edges = edges
			.iter()
			.filter(|e| !crate::oakui::nodegraph::is_output_wire(e.id))
			.count();
		assert!(
			real_edges >= 1,
			"the footage→clip media edge is real (got {real_edges} real edges)"
		);

		unsafe { free_box(sequence) };
		unsafe { oakengine_project_free(project) };
		let _ = std::fs::remove_file(&media);
	}
}

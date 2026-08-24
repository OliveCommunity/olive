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

//! The real engine: [`RealEngine`] drives the oak* module crates directly
//! (M14 R3 — no `liboakengine` dylib, no C ABI) behind the same
//! [`EngineGateway`](super::engine::EngineGateway) /
//! [`AppEngine`](super::engine::AppEngine) seam the mock implements.
//!
//! The engine owns the domain project (`Arc<Mutex<oak_node::project::Project>>`)
//! and the current sequence's `NodeId`; every read is a direct graph walk
//! and every edit is an oaktimeline/oakundo command pushed onto the
//! process-wide undo stack ([`oak_undo::global`]). The assembly-layer
//! helpers live in [`super::graphops`] (project/timeline/storage),
//! [`super::effectchain`] (effect chains) and [`super::renderops`]
//! (montage resolution, ticket rendering, the export driver); the
//! node-graph and project-browser snapshots are [`super::nodegraph`] and
//! [`super::projectbrowser`].
//!
//! # What is real here
//!
//! * **Project** — open/save-as/close through the oaknode serializer
//!   (`.ove`; `.otio` / `.fcpxml` through the oaktask interchange tasks).
//! * **Sequence** — the current sequence's name / format / length / tracks /
//!   clips are read live from the graph.
//! * **Edits** — timeline edits (trim, split, delete, ripple-delete) and
//!   track add/remove go through the modules' edit commands, each packaged
//!   as one undoable entry on the global undo stack. Undo/redo walk that
//!   stack.
//! * **Export** — the oaktask export task, driven on a background thread,
//!   with progress events and cancel wired to the module task's event
//!   listener and cancel atom.
//! * **Config** — the preferences (renderer backend, language, theme,
//!   cache dir, proxy policy, snapshot interval, default transition,
//!   audio devices) round-trip through the oakcommon config store; the
//!   audio device selection additionally applies live through oakaudio's
//!   manager.
//! * **Storage** — the write-through library binds every opened project
//!   through [`oak_storage::writethrough`]; the manager window's library
//!   operations call the oakstorage database backend directly.
//!
//! # Threading note
//!
//! Long renders (full-resolution fills, exports) run on background
//! threads; the export task's event listener delivers progress through a
//! channel the app drains on its tick loop. The background full-res
//! worker holds the project's `Arc`, so a project drop mid-render is a
//! non-event (the drained frame is discarded by the generation check).

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet, VecDeque};
use std::path::{Path, PathBuf};
use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::time::Instant;

use gpui::effect_stack::{
	EffectCardKind, EffectData, EffectId, EffectStackDataSource, EffectStackEvent,
};
use gpui::node_graph::{NodeGraphDataSource, NodeGraphEvent};
use gpui::timeline::{
	ClipData, ClipId, Frame, FrameRange, FrameRate, Marker, TimelineDataSource, TimelineEvent,
	TrackData, TrackHeaderEvent, TrackKind, TrimEdge,
};
use gpui::{prelude::*, px, App, Context, Entity, Hsla, Pixels, RenderImage, SharedString};
use gpui_widgets::audio_meter::AudioMeterDataSource;
use gpui_widgets::project_explorer::{ProjectDataSource, ProjectEntry};
use gpui_widgets::viewer::PlaybackClock;

use oak_node::id::NodeId;
use oak_node::track::TrackType;
use oak_render::manager::RenderManager;
use oak_render::procpool::{bgra8_to_rgba8, ShmFrameRef};
use oak_timeline::handle::CHandle;
use oak_timeline::util::NodeRef;

use super::engine::{
	AppEngine, EngineGateway, ExportSession, LibraryProject, Monitor, MulticamState, Project,
	ScopeData, Sequence, VideoFormat,
};
use super::frames::{bgra_bytes_to_render_image, f32_rgba_to_bgra_image, synthetic_frame_samples};
use super::graphops::{self, ProjectRef};
use super::scopes::{analyze_bgra8, analyze_f32_rgba};
use super::transport::TransportState;

/// The project name of a blank project before it is saved.
const UNTITLED: &str = "Untitled Project";

// ---------------------------------------------------------------------------
// Handle wrappers
// ---------------------------------------------------------------------------

/// An owned module handle (the write-through query handle, the
/// per-sequence marker list and workarea) that is `Send`/`Sync`.
struct AuxHandle(CHandle);

// SAFETY: the pointee is a refcounted module box only ever touched from
// the UI thread through its own accessor functions; the `Send`/`Sync`
// impls exist so the gpui entity can carry it.
unsafe impl Send for AuxHandle {}
unsafe impl Sync for AuxHandle {}

/// The lifecycle state of a monitor's render path (the renders are
/// stateless ticket submissions; the slot only remembers whether the path
/// ever failed so a broken setup doesn't retry — and re-log — on every
/// frame).
enum RendererSlot {
	/// No render attempted yet; the next `cpu_frame` tries one.
	Untried,
	/// The render path works for this monitor.
	Ready,
	/// The last render failed; don't retry until the project changes.
	Unavailable,
}

// ---------------------------------------------------------------------------
// Full-resolution frame cache (M12 P5a)
// ---------------------------------------------------------------------------
//
// Each monitor displays the small proxy frame immediately and lets a
// background thread fill the same frame at the sequence's native size;
// when the fill lands it replaces the proxy in the display path. The cache
// below is the UI-thread-owned state of that schedule: the two cached
// frames plus the one in-flight background job per monitor.

/// The proxy frame cached for one monitor: the image plus the scope samples
/// analyzed in the same render pass (a paused viewer never regenerates
/// either).
struct ProxyEntry {
	/// The playhead frame that produced the frame.
	frame: i64,
	/// The viewer image.
	image: Arc<RenderImage>,
	/// The scope samples of the same render.
	scope: ScopeData,
}

/// A full-resolution frame the background worker filled in. No scope data:
/// the scopes keep reading the proxy pass (same content, lower resolution),
/// so a full-res fill costs no extra analysis and the scopes tab behaves
/// exactly as before.
struct FullResEntry {
	/// The playhead frame the frame was rendered for.
	frame: i64,
	/// The viewer image at the sequence's native size.
	image: Arc<RenderImage>,
}

/// One monitor's display cache: the proxy frame plus the full-resolution
/// fill, and the identity of the in-flight background job.
#[derive(Default)]
struct MonitorFrameCache {
	/// The last proxy render (immediate display path).
	proxy: Option<ProxyEntry>,
	/// The last full-resolution fill (replaces the proxy when its frame
	/// matches the playhead).
	full: Option<FullResEntry>,
	/// The in-flight background job's `(frame, generation)`, or None. Only
	/// one job runs per monitor; the drain re-schedules when the playhead
	/// moves while a job is in flight.
	pending: Option<(i64, u64)>,
}

impl MonitorFrameCache {
	/// The image to display for `frame`: the full-resolution fill when the
	/// worker has landed it, else the proxy frame, else None.
	fn image_for(&self, frame: i64) -> Option<&Arc<RenderImage>> {
		if let Some(full) = &self.full {
			if full.frame == frame {
				return Some(&full.image);
			}
		}
		if let Some(proxy) = &self.proxy {
			if proxy.frame == frame {
				return Some(&proxy.image);
			}
		}
		None
	}

	/// The scope samples matching [`MonitorFrameCache::image_for`] (always
	/// the proxy pass; see [`FullResEntry`]).
	fn scope_for(&self, frame: i64) -> Option<&ScopeData> {
		let proxy = self.proxy.as_ref()?;
		(proxy.frame == frame).then_some(&proxy.scope)
	}

	/// Whether a background full-resolution render should be started for
	/// `frame`: the playhead is not moving (`playing`), the frame is not
	/// already cached full-res, and no job is in flight for this monitor.
	fn needs_full_res(&self, frame: i64, playing: bool) -> bool {
		// Proxy stays primary while playing (a full-res render would fight
		// the moving playhead); a cached fill or an in-flight job mean no
		// new job (the drain re-schedules once the job lands).
		!playing
			&& self.pending.is_none()
			&& !self.full.as_ref().is_some_and(|f| f.frame == frame)
	}

	/// Installs a completed full-res frame. Returns false — and keeps the
	/// cache untouched — when the completion is stale (the pending job it
	/// belongs to no longer matches, i.e. an edit, a selection change or a
	/// project drop happened while it was in flight).
	fn install_full_res(
		&mut self,
		frame: i64,
		generation: u64,
		image: Arc<RenderImage>,
	) -> bool {
		if self.pending != Some((frame, generation)) {
			return false;
		}
		self.pending = None;
		self.full = Some(FullResEntry { frame, image });
		true
	}
}

/// What a background full-res job renders: the program monitor's sequence
/// or the source monitor's selected footage node. The request also carries
/// the project's `Arc`, which keeps the graph alive regardless of what the
/// UI thread does with the engine's own reference.
enum FullResTarget {
	/// The program monitor's sequence.
	Sequence(NodeId),
	/// The source monitor's selected footage node.
	Footage(NodeId),
}

// ---------------------------------------------------------------------------
// Playback pre-render window (M15 S2)
// ---------------------------------------------------------------------------
//
// During playback the engine feeds the forward window (default 120 frames,
// config `PlaybackPreRenderFrames`) to the `PreviewScheduler` through the
// process dispatcher: the scheduler interleaves the frames across the
// workers, so by the time the playhead reaches a frame its pixels are
// already sitting in a shm slot. `cpu_frame` hits this slot cache first
// (zero copy — build the display image from the slot bytes, then release
// the slot); the synchronous render path is the miss fallback. Frames
// that fall out of the window (or whose params were invalidated) release
// their slots back to the workers.

/// The default forward pre-render window (frames).
const DEFAULT_PREVIEW_WINDOW_FORWARD: i64 = 120;
/// Config key for the forward pre-render window size (frames).
const CONFIG_KEY_PREVIEW_WINDOW: &str = "PlaybackPreRenderFrames";

/// One monitor's playback pre-render state (UI-thread-owned; the completions
/// delivered by the dispatcher's poll run on the UI thread too).
#[derive(Default)]
struct PreviewWindow {
	/// The node identity the window renders (sequence / footage).
	sequence: u64,
	/// The render-params generation the window was built for (bumped on
	/// invalidation; a mismatch cancels the old requests and rebuilds).
	generation: u64,
	/// Frames already submitted to the scheduler (pending or in flight).
	submitted: BTreeSet<i64>,
	/// Rendered frames held in shm slots, keyed by frame number. A frame
	/// is consumed by `cpu_frame` (slot released after the display image
	/// is built) or released when it falls out of the window.
	slots: BTreeMap<i64, ShmFrameRef>,
}

// ---------------------------------------------------------------------------
// Playback audio prefetch (M15 S3)
// ---------------------------------------------------------------------------
//
// Real-time audio is pulled by the UI tick; rendering it through the worker
// pool adds one IPC round trip and can wait behind a busy render worker. To
// avoid dropouts the chunks are rendered AHEAD asynchronously (tickets
// complete on the dispatcher's poll) and buffered here; the pull never
// blocks on a worker. `AUDIO_PREFETCH_CHUNKS` ahead ≈ 64 ms of audio at
// 60 fps — enough to cover the delivery latency while keeping at most a
// handful of audio slots in flight (the dispatcher's credit flow control
// caps them per worker).

/// How many audio chunks are kept rendered ahead of the playhead (M15 S3).
/// Each chunk is one sequence frame of audio; 4 frames ahead ≈ 66 ms at
/// 60 fps and ≈ 160 ms at 25 fps — enough to cover the delivery latency
/// while keeping at most a handful of audio slots in flight (the
/// dispatcher's credit flow control caps them per worker).
const AUDIO_PREFETCH_CHUNKS: i64 = 4;

/// The playback-audio prefetch buffer: rendered chunks ordered by start
/// timestamp, plus the submission cursor. UI-thread-only (guarded by the
/// engine's `audio_prefetch` mutex so the engine stays `Sync`).
struct AudioPrefetch {
	/// Sequence frame of the first buffered chunk (or `next_submit` when
	/// the buffer is empty).
	front_ts: i64,
	/// Sequence frame of the next chunk to submit.
	next_submit: i64,
	/// Chunk length (sequence frames per tick) the buffer is built with.
	chunk: i64,
	/// Rendered chunks in start-ts order.
	buffered: VecDeque<(i64, super::renderops::RenderedAudio)>,
}

impl AudioPrefetch {
	/// An empty, uninitialized prefetch.
	fn new() -> Self {
		Self {
			front_ts: i64::MIN,
			next_submit: i64::MIN,
			chunk: 1,
			buffered: VecDeque::new(),
		}
	}

	/// True when `frame` lies inside the submitted window
	/// `[front_ts, next_submit)` — the playhead is being served.
	fn covers(&self, frame: i64) -> bool {
		self.front_ts != i64::MIN && self.front_ts <= frame && frame < self.next_submit
	}

	/// Reset for a (re)start at `frame`: drop every buffered chunk and
	/// restart the submission cursor there (a seek or a new project).
	fn reset(&mut self, frame: i64, chunk: i64) {
		self.front_ts = frame;
		self.next_submit = frame;
		self.chunk = chunk.max(1);
		self.buffered.clear();
	}

	/// Insert a rendered chunk; stale arrivals (outside the submitted
	/// window — a seek raced the render) are dropped.
	fn insert(&mut self, ts: i64, data: super::renderops::RenderedAudio) {
		if ts < self.front_ts || ts >= self.next_submit {
			return;
		}
		if self.buffered.iter().any(|(t, _)| *t == ts) {
			return;
		}
		let pos = self
			.buffered
			.iter()
			.position(|(t, _)| *t > ts)
			.unwrap_or(self.buffered.len());
		self.buffered.insert(pos, (ts, data));
	}

	/// Pop the chunk at `frame` (dropping any stale leading chunks).
	/// Returns `None` when the chunk has not been rendered yet.
	fn pop_at(&mut self, frame: i64) -> Option<super::renderops::RenderedAudio> {
		while let Some((ts, _)) = self.buffered.front() {
			if *ts < frame {
				let ts = self.buffered.pop_front().unwrap().0;
				self.front_ts = ts + self.chunk;
			} else {
				break;
			}
		}
		let (ts, data) = self.buffered.pop_front()?;
		if ts == frame {
			self.front_ts = ts + self.chunk;
			Some(data)
		} else {
			// Gap: the chunk at `frame` is still rendering. Re-insert and
			// report nothing (the output device zero-fills this tick).
			self.buffered.push_front((ts, data));
			None
		}
	}
}

/// One background full-resolution render request (built on the UI thread
/// at schedule time; the worker thread owns it from there).
struct FullResRequest {
	/// The monitor the frame belongs to.
	monitor: Monitor,
	/// The playhead frame to render.
	frame: i64,
	/// The engine's full-res generation when the job was scheduled (stale
	/// completions are discarded by the drain).
	generation: u64,
	/// The project the target lives in (keeps the graph alive).
	project: ProjectRef,
	/// The sequence or footage node to render.
	target: FullResTarget,
	/// Output width (the sequence's native size).
	width: i32,
	/// Output height.
	height: i32,
	/// The sequence's timebase (frame duration = `tb.0 / tb.1` seconds).
	tb: (i64, i64),
}

/// A completed full-res frame, delivered through the completion channel.
struct FullResEvent {
	/// The monitor the frame belongs to.
	monitor: Monitor,
	/// The playhead frame that was rendered.
	frame: i64,
	/// The job's generation (see [`FullResRequest`]).
	generation: u64,
	/// The rendered viewer image.
	image: Arc<RenderImage>,
}

// ---------------------------------------------------------------------------
// Material-bin thumbnails
// ---------------------------------------------------------------------------
//
// The icon view shows a PNG of each footage's first frame. The PNGs are
// generated lazily on background threads (the render is a ticket wait and
// must not block the UI thread) into a shared directory keyed by the media
// filename; completions are drained on the app tick like full-res frames.

/// The thumbnail render size (twice the icon view's 72x48 slot).
const THUMBNAIL_WIDTH: i32 = 144;
/// The thumbnail render height (see [`THUMBNAIL_WIDTH`]).
const THUMBNAIL_HEIGHT: i32 = 96;

/// A completed thumbnail, delivered through the completion channel.
struct ThumbEvent {
	/// The footage node's stable identity (the bin entry's id).
	identity: u64,
	/// The job's generation (stale jobs are discarded by the drain).
	generation: u64,
	/// The PNG file on disk.
	path: PathBuf,
}

/// The thumbnail cache: finished PNG paths keyed by footage identity, plus
/// the identities with a generation job in flight (never re-scheduled).
#[derive(Default)]
struct ThumbnailState {
	/// Finished thumbnails.
	done: HashMap<u64, PathBuf>,
	/// Identities with a job in flight (or already attempted).
	pending: HashSet<u64>,
}

/// The project settings key of the OCIO config override (the 项目属性
/// color tab).
pub const PROJECT_SETTING_OCIO_CONFIG: &str = "ocioconfig";

/// The shared directory holding generated footage thumbnails.
fn thumbnail_dir() -> PathBuf {
	std::env::temp_dir().join("oak-thumbnails")
}

/// The PNG path of a footage's thumbnail: an FNV-1a hash of the media
/// filename, so the same file always hits the same cached PNG.
/// `cache_dir` is the project's cache-location override (the 项目属性
/// disk-cache setting); `None` uses the shared default directory.
fn thumbnail_path(cache_dir: Option<&str>, filename: &str) -> PathBuf {
	let mut h: u64 = 0xcbf29ce484222325;
	for b in filename.as_bytes() {
		h ^= u64::from(*b);
		h = h.wrapping_mul(0x100000001b3);
	}
	let dir = cache_dir
		.map(|d| PathBuf::from(d).join("thumbnails"))
		.unwrap_or_else(thumbnail_dir);
	dir.join(format!("{h:016x}.png"))
}

// ---------------------------------------------------------------------------
// Multicam angle frames (M15 S2)
// ---------------------------------------------------------------------------
//
// The Multicam panel draws one cell per angle (= the source sequence's
// track `i` at the playhead). The engine renders those frames on background
// threads — the same ticket path as the viewers, one single-track montage
// per angle — and caches them keyed by (multicam node, source) with an LRU
// cap, so a paused panel never re-renders a cell and playback refreshes
// cells round-robin (the panel throttles its requests; the engine only ever
// has one in-flight render per source).

/// The grid cell render size: the sequence's aspect scaled to a 320px long
/// edge (the panel grid cells are roughly this size; keeping the tickets
/// small bounds the 9-angle burst cost).
const MULTICAM_ANGLE_LONG_EDGE: u32 = 320;

/// A completed multicam angle frame, delivered through the completion
/// channel (drained on the app tick, like full-res frames). `None` image =
/// the render failed; the drain still clears the in-flight marker so the
/// cell can be retried on the next invalidation.
struct MulticamAngleEvent {
	/// The multicam node identity the frame belongs to.
	node_id: u64,
	/// The source index rendered.
	source: i32,
	/// The playhead frame the frame was rendered for.
	playhead: i64,
	/// The rendered display image (`None` when the render failed).
	image: Option<Arc<RenderImage>>,
}

/// One background multicam angle render request (UI-thread-built; the
/// worker thread owns it from there).
struct MulticamAngleRequest {
	/// The multicam node identity (the cache key's node half).
	node_id: u64,
	/// The source index.
	source: i32,
	/// The playhead frame to render.
	playhead: i64,
	/// The project (keeps the graph alive while the worker renders).
	project: ProjectRef,
	/// The source sequence node.
	seq: NodeId,
	/// The track whose clip makes up this angle.
	track: NodeId,
	/// Output width.
	width: i32,
	/// Output height.
	height: i32,
	/// The sequence's timebase.
	tb: (i64, i64),
}

/// The multicam angle-frame cache: rendered frames keyed by
/// `(multicam node, source)` with the playhead they were rendered for,
/// LRU-capped, plus the in-flight sources per node.
#[derive(Default)]
struct MulticamFrameCache {
	/// `(node_id, source) -> (rendered playhead, image)`, insertion-ordered
	/// (the LRU eviction drops the head).
	frames: Vec<((u64, i32), (i64, Arc<RenderImage>))>,
	/// `(node_id, source)` renders currently in flight (never re-scheduled).
	pending: HashSet<(u64, i32)>,
}

impl MulticamFrameCache {
	/// The cached image for `(node, source)` rendered at exactly `playhead`
	/// (a playhead change makes the frame stale).
	fn lookup(&self, node: u64, source: i32, playhead: i64) -> Option<Arc<RenderImage>> {
		self.frames
			.iter()
			.find(|(k, v)| k == &(node, source) && v.0 == playhead)
			.map(|(_, (_, img))| img.clone())
	}

	/// The most recent image for `(node, source)` regardless of playhead
	/// (the panel's stale-OK fallback during playback).
	fn last(&self, node: u64, source: i32) -> Option<Arc<RenderImage>> {
		self.frames
			.iter()
			.rev()
			.find(|(k, _)| k == &(node, source))
			.map(|(_, (_, img))| img.clone())
	}

	/// Store a freshly rendered frame, evicting the LRU head past the cap.
	fn insert(&mut self, node: u64, source: i32, playhead: i64, image: Arc<RenderImage>) {
		self.frames.retain(|(k, _)| k != &(node, source));
		self.frames.push(((node, source), (playhead, image)));
		const CAP: usize = 24;
		if self.frames.len() > CAP {
			let excess = self.frames.len() - CAP;
			self.frames.drain(0..excess);
		}
	}
}

// ---------------------------------------------------------------------------
// Frame conversion
// ---------------------------------------------------------------------------

/// Repack one in-process F32 RGBA rendered frame (rows padded to
/// linesize) into tightly packed samples. Returns `(width, height,
/// samples)` when the frame is the in-process F32 variant (the shm
/// variant is BGRA8 and is read as bytes instead).
fn read_f32_frame(frame: &super::renderops::RenderedFrame) -> Option<(u32, u32, Vec<f32>)> {
	let super::renderops::RenderedFrame::CpuF32 {
		width,
		height,
		linesize,
		data,
	} = frame
	else {
		return None;
	};
	if *width <= 0 || *height <= 0 {
		return None;
	}
	let row_bytes = (*width * 4 * 4) as usize;
	let linesize = (*linesize as usize).max(row_bytes);
	if data.len() < linesize * *height as usize {
		return None;
	}
	let mut samples = vec![0.0f32; (*width * *height * 4) as usize];
	for y in 0..*height as usize {
		let row = &data[y * linesize..y * linesize + row_bytes];
		for (i, px) in row.chunks_exact(4).enumerate() {
			let v = f32::from_ne_bytes([px[0], px[1], px[2], px[3]]);
			samples[y * (*width as usize) * 4 + i] = v;
		}
	}
	Some((*width as u32, *height as u32, samples))
}

/// Release the shm slot a rendered frame holds, if any (M15 S2: the
/// zero-copy onscreen path builds the display image from the slot bytes,
/// then returns the slot to its worker — slot release = cache eviction).
/// No-op for in-process frames and when the manager is down.
fn release_rendered_frame(rendered: &super::renderops::RenderedFrame) {
	if let super::renderops::RenderedFrame::Shm(frame) = rendered {
		if let Some(m) = RenderManager::global() {
			m.release_frame(frame);
		}
	}
}

/// Build an owned viewer image from a rendered frame for a long-lived
/// cache (full-res fills, thumbnails). The shm variant copies the slot
/// bytes out once (`slot_to_vec` — the counted copy path, necessary
/// because the image must outlive the slot), then the caller releases the
/// slot; the in-process variant converts F32→BGRA8.
fn rendered_to_owned_image(rendered: &super::renderops::RenderedFrame) -> Option<Arc<RenderImage>> {
	match rendered {
		super::renderops::RenderedFrame::Shm(f) => {
			let meta = &f.meta;
			let (w, h) = (meta.width.max(0) as u32, meta.height.max(0) as u32);
			let pixels = f.shm.slot_to_vec(f.slot);
			let data = pixels.get(..meta.data_size.max(0) as usize)?;
			bgra_bytes_to_render_image(w, h, data).map(Arc::new)
		}
		super::renderops::RenderedFrame::CpuF32 { .. } => {
			let (w, h, samples) = read_f32_frame(rendered)?;
			Some(Arc::new(f32_rgba_to_bgra_image(w, h, &samples)))
		}
	}
}

// ---------------------------------------------------------------------------
// Transport clock
// ---------------------------------------------------------------------------

/// The real engine's transport clock: the playhead plus the wall-clock
/// anchor while playing. Mirrors the mock's clock; the engine additionally
/// writes the program playhead back to the sequence.
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
	/// `length`. No-op when stopped. The advance is clamped per tick: a
	/// long stall (the first render after pressing play, a disk stall)
	/// must not teleport the playhead past the pre-render window — the
	/// dropped time is re-anchored away instead (NLEs drop frames during
	/// stalls; they never jump the playhead over rendered content).
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
		// At 60 Hz ticks this allows up to 120 fps of advance — normal
		// playback rates are unaffected; only stall teleporting is clamped.
		const MAX_ADVANCE_PER_TICK: i64 = 2;
		let current = self.transport.frame();
		if frame.0 > current.0 + MAX_ADVANCE_PER_TICK {
			frame = Frame(current.0 + MAX_ADVANCE_PER_TICK);
			self.started = Some((Instant::now(), frame));
		}
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

/// A clip on the real timeline: the widget snapshot plus the graph
/// addressing (the block's `NodeId`; the widget-facing id is its stable
/// identity).
#[derive(Debug, Clone)]
pub struct RealClip {
	id: ClipId,
	range: FrameRange,
	media_in: Frame,
	label: SharedString,
	color: Hsla,
	/// The block node in the project graph.
	block: NodeId,
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

/// A running proxy transcode the engine drains on the tick loop (the
/// task thread reports through the event channel; completion applies the
/// footage's proxy fields and invalidates the rendered frames).
struct ProxyRun {
	/// The footage node.
	footage: NodeId,
	/// The task's display label (the status bar's proxy segment).
	label: String,
	/// Last reported progress (`0.0..=1.0`).
	progress: f64,
	/// The task thread's event channel.
	events: mpsc::Receiver<super::engine::ExportEvent>,
}

/// One card of the real effect stack: the chain node's identity, its
/// factory display name, its enabled flag, and the app-owned expansion
/// state ([`RealEngine::expanded_effects`]). No source/output cards: the
/// host clip node is the implicit output, the chain's unlinked upstream
/// input is the implicit source (the effect stack shows only the editable
/// middle).
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
	/// Optional secondary line (the "OpenFX" tag for plugin nodes).
	subtitle: Option<SharedString>,
	/// The persistent-message badge count (plugin nodes only; `None`
	/// otherwise).
	badge: Option<usize>,
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

	fn subtitle(&self) -> Option<SharedString> {
		self.subtitle.clone()
	}

	fn is_enabled(&self) -> bool {
		self.enabled
	}

	fn is_expanded(&self) -> bool {
		self.expanded
	}

	fn badge_count(&self) -> Option<usize> {
		self.badge
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
	/// The track node in the project graph.
	track: NodeId,
	/// The track's per-type index (the facade's clip coordinates; edit
	/// commands address nodes directly now, but the drop policy and the
	/// widget's cross-track moves still speak display/per-type indices).
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

/// A deterministic clip color from a stable per-clip index (the module
/// graph exposes no clip color).
fn clip_color(index: u64) -> Hsla {
	let hues = [0.55f32, 0.6, 0.08, 0.3, 0.78, 0.45, 0.9, 0.15];
	Hsla {
		h: hues[(index as usize) % hues.len()],
		s: 0.55,
		l: 0.45,
		a: 1.0,
	}
}

/// A marker color for a marker color index (a small palette around the
/// amber accent, so adjacent markers stay distinguishable).
fn marker_color(index: i32) -> Hsla {
	let hues = [0.10f32, 0.0, 0.55, 0.30, 0.78];
	let h = hues[(index.max(0) as usize) % hues.len()];
	Hsla {
		h,
		s: 0.75,
		l: 0.55,
		a: 1.0,
	}
}

/// The [`TrackKind`] of a module track type.
fn track_kind_of(kind: TrackType) -> TrackKind {
	match kind {
		TrackType::Video => TrackKind::Video,
		TrackType::Audio => TrackKind::Audio,
		TrackType::Subtitle => TrackKind::Subtitle,
	}
}

/// The module track type of a [`TrackKind`].
fn track_type_of(kind: TrackKind) -> TrackType {
	match kind {
		TrackKind::Video => TrackType::Video,
		TrackKind::Audio => TrackType::Audio,
		TrackKind::Subtitle => TrackType::Subtitle,
	}
}

/// A node in the real node graph (M12 P2: built from the current
/// sequence's graph by [`crate::oakui::nodegraph`]).
pub use crate::oakui::nodegraph::{RealEdge, RealNode, RealPort};

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

/// The real engine: the domain project/sequence plus the snapshot models
/// the widgets read.
pub struct RealEngine {
	/// The domain project (None before any project is open).
	project: Option<ProjectRef>,
	/// The current sequence node.
	sequence: Option<NodeId>,
	/// The write-through binding's query handle (bound at adopt, released
	/// at drop).
	storage: Option<AuxHandle>,
	/// The current sequence's marker list (the module's sequences carry
	/// none; the app materializes one per open sequence, like the facade).
	markers: Option<AuxHandle>,
	/// The current sequence's work area (see `markers`).
	workarea: Option<AuxHandle>,
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
	/// The selected material-bin entry (a node identity).
	selected_item: Option<u64>,
	/// The single selected timeline clip — the effect stack's target
	/// (`None` for an empty or multi-clip selection, or before any
	/// selection event). Also drives the node graph's scope: while a single
	/// clip is selected, the editor shows that clip's context chain only.
	selected_clip: Option<ClipId>,
	/// The node-graph selection mirror (`None` = no single node selected):
	/// set when the user clicks a node in the node editor (or an effect
	/// card in the inspector) so the inspector and the node graph share one
	/// selection. A timeline clip selection also writes its block node here.
	selected_graph_node: Option<u64>,
	/// The engine clipboard for Cut/Copy/Paste (graphops::ClipboardClip
	/// entries in timeline order).
	clipboard: Vec<graphops::ClipboardClip>,
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
	/// Each monitor holds its proxy frame (rendered synchronously on the UI
	/// thread, with the scope samples analyzed in the same pass) plus the
	/// full-resolution fill the background worker lands when the playhead
	/// rests (see [`MonitorFrameCache`]). Both monitors hold real rendered
	/// frames (see [`RealEngine::render_program_frame`] /
	/// [`RealEngine::render_source_frame`]); the synthetic pattern is only
	/// the failure fallback.
	cpu_frame_cache: Mutex<HashMap<Monitor, MonitorFrameCache>>,
	/// The display-color transform generation the frame cache was built
	/// against (a change drops every cached image — they were produced
	/// with the stale transform).
	display_color_gen: std::cell::Cell<u64>,
	/// Bumped whenever the rendered content can change underneath an
	/// in-flight background full-res job (an edit, a selection change or a
	/// project drop); completions tagged with a stale generation are
	/// discarded by the drain.
	full_res_generation: u64,
	/// M15 S2 playback pre-render state, keyed by monitor (see
	/// [`PreviewWindow`]). An `Arc` so the ticket completions (which fire
	/// from the dispatcher's poll on the UI thread) can reach the cache;
	/// the mutex keeps the engine `Sync`.
	preview_windows: Arc<Mutex<HashMap<Monitor, PreviewWindow>>>,
	/// Bumped whenever the preview content can change (edit / proxy
	/// toggle / selection / project drop); the pre-render window rebuilds
	/// against the new generation.
	preview_generation: u64,
	/// The channel background full-res jobs report finished frames through;
	/// drained on the app tick. The mutex keeps the engine `Sync` (the
	/// channel is only ever touched on the UI thread).
	full_res_rx: Mutex<mpsc::Receiver<FullResEvent>>,
	/// The sending half of `full_res_rx` (cloned into every job).
	full_res_tx: Mutex<mpsc::Sender<FullResEvent>>,
	/// The program monitor's render-path state (see [`RendererSlot`]).
	/// Reset to [`RendererSlot::Untried`] in [`RealEngine::drop_project`].
	renderer: Mutex<RendererSlot>,
	/// The source monitor's render-path state (same slot semantics as
	/// `renderer`). Reset when the selection changes or the project is
	/// dropped — the render binds the footage node, so a new selection
	/// must re-render the new node.
	source_renderer: Mutex<RendererSlot>,
	/// Material-bin thumbnail cache (finished PNGs + in-flight jobs).
	thumbnails: Mutex<ThumbnailState>,
	/// Generation counter invalidating in-flight thumbnail jobs on project
	/// drop (same semantics as `full_res_generation`).
	thumb_generation: u64,
	/// The channel background thumbnail jobs report finished PNGs through;
	/// drained on the app tick. The mutex keeps the engine `Sync`.
	thumb_rx: Mutex<mpsc::Receiver<ThumbEvent>>,
	/// The sending half of `thumb_rx` (cloned into every job).
	thumb_tx: Mutex<mpsc::Sender<ThumbEvent>>,
	proxy_runs: Vec<ProxyRun>,
	/// The multicam angle-frame cache (rendered grid cells keyed by
	/// (multicam node, source), LRU-capped). An `Arc` so the background
	/// angle workers' completions can reach it; the mutex keeps the engine
	/// `Sync`.
	multicam_frames: Arc<Mutex<MulticamFrameCache>>,
	/// The channel background multicam angle workers report finished frames
	/// through; drained on the app tick.
	multicam_rx: Mutex<mpsc::Receiver<MulticamAngleEvent>>,
	/// The sending half of `multicam_rx` (cloned into every worker).
	multicam_tx: Mutex<mpsc::Sender<MulticamAngleEvent>>,
	/// M15 S3: async playback-audio prefetch — the process dispatcher
	/// renders audio chunks ahead (completions arrive on the UI tick's
	/// poll); the channel delivers `(start_ts, samples)` and the prefetch
	/// state reorders them for the real-time pull.
	audio_rx: Mutex<mpsc::Receiver<(i64, super::renderops::RenderedAudio)>>,
	/// The sending half of `audio_rx` (cloned into every audio ticket).
	audio_tx: Mutex<mpsc::Sender<(i64, super::renderops::RenderedAudio)>>,
	/// The audio prefetch buffer (see [`AudioPrefetch`]).
	audio_prefetch: Mutex<AudioPrefetch>,
}

impl RealEngine {
	/// Render one tick's worth of audio at the program playhead and queue
	/// it for playback (M12 P1; M15 S3: async worker-pool prefetch). The
	/// audio chunks are rendered AHEAD in oak-worker (tickets posted
	/// through the process dispatcher; completions arrive on the UI tick's
	/// poll) and buffered by [`AudioPrefetch`], so the real-time pull never
	/// blocks the UI thread on a busy render worker. Failures degrade to
	/// silence; when the manager is down the channel stays empty and
	/// playback continues video-only.
	fn pull_audio_tick(&mut self, cx: &mut Context<Self>) {
		let (Some(project), Some(seq)) = (self.project.clone(), self.sequence) else {
			return;
		};
		let Some(tb) = self.time_base() else {
			return;
		};
		let frame = self.clock_frame(Monitor::Program, cx).0;
		if frame < 0 {
			return;
		}
		// One sequence frame of audio per chunk, keyed by the playhead
		// frame. A per-tick wall-clock heuristic (~1/60 s) truncates to zero
		// frames for sub-60 fps sequences, so render exactly one frame per
		// playhead frame instead — the output device consumes one frame's
		// audio per frame advance regardless of the rate.
		let chunk: i64 = 1;

		let mut st = self.audio_prefetch.lock().unwrap_or_else(|e| e.into_inner());
		// Reset on seek / (re)start: the playhead must lie inside the
		// submitted window [front_ts, next_submit).
		if !st.covers(frame) {
			st.reset(frame, chunk);
		}
		// Submit chunks to cover [next_submit, frame + PREFETCH ahead). In
		// steady state the window moves by one chunk per tick, so exactly
		// one new ticket is posted; the rest are already buffered.
		let tx = self.audio_tx.lock().unwrap_or_else(|e| e.into_inner()).clone();
		let target = frame + chunk * AUDIO_PREFETCH_CHUNKS;
		while st.next_submit < target {
			let ts = st.next_submit;
			let p = project.clone();
			if super::renderops::submit_audio_chunk(&p, seq, ts, chunk, tb, tx.clone()).is_err() {
				break;
			}
			st.next_submit += chunk;
		}
		// Drain completed chunks. For the inline fallback backend the
		// submit above already ran them synchronously into the channel; for
		// the process backend they arrived on this tick's earlier poll.
		let rx = self.audio_rx.lock().unwrap_or_else(|e| e.into_inner());
		while let Ok((ts, data)) = rx.try_recv() {
			st.insert(ts, data);
		}
		drop(rx);
		// Push the chunk at the current playhead to the output device.
		let Some(buf) = st.pop_at(frame) else {
			return;
		};
		if buf.sample_rate <= 0 || buf.channel_count <= 0 || buf.data.is_empty() {
			return;
		}
		// Packed F32; layout: 1ch → mono mask, else stereo.
		let layout: u64 = if buf.channel_count == 1 { 0x4 } else { 0x3 };
		let bytes: Vec<u8> = buf.data.iter().flat_map(|v| v.to_ne_bytes()).collect();
		if let Some(mut manager) = oak_audio::manager::instance() {
			let _ = manager.push_to_output(
				oak_audio::params::AudioParams {
					sample_rate: buf.sample_rate,
					channel_layout: layout,
					format: oak_core::SampleFormat::F32,
				},
				&bytes,
				&mut [],
			);
		}
	}

	/// Builds an engine with no project open.
	pub fn new(cx: &mut Context<Self>) -> Self {
		let rate = VideoFormat::hd_1080p25().rate;
		let (full_res_tx, full_res_rx) = mpsc::channel::<FullResEvent>();
		let (thumb_tx, thumb_rx) = mpsc::channel::<ThumbEvent>();
		let (multicam_tx, multicam_rx) = mpsc::channel::<MulticamAngleEvent>();
		let (audio_tx, audio_rx) = mpsc::channel::<(i64, super::renderops::RenderedAudio)>();
		Self {
			project: None,
			sequence: None,
			storage: None,
			markers: None,
			workarea: None,
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
			selected_graph_node: None,
			clipboard: Vec::new(),
			expanded_effects: BTreeSet::new(),
			program_playing: false,
			meter_phase: 0,
			cpu_frame_cache: Mutex::new(HashMap::new()),
			display_color_gen: std::cell::Cell::new(0),
			full_res_generation: 0,
			preview_windows: Arc::new(Mutex::new(HashMap::new())),
			preview_generation: 0,
			full_res_rx: Mutex::new(full_res_rx),
			full_res_tx: Mutex::new(full_res_tx),
			renderer: Mutex::new(RendererSlot::Untried),
			source_renderer: Mutex::new(RendererSlot::Untried),
			thumbnails: Mutex::new(ThumbnailState::default()),
			thumb_generation: 0,
			thumb_rx: Mutex::new(thumb_rx),
			thumb_tx: Mutex::new(thumb_tx),
			proxy_runs: Vec::new(),
			multicam_frames: Arc::new(Mutex::new(MulticamFrameCache::default())),
			multicam_rx: Mutex::new(multicam_rx),
			multicam_tx: Mutex::new(multicam_tx),
			audio_rx: Mutex::new(audio_rx),
			audio_tx: Mutex::new(audio_tx),
			audio_prefetch: Mutex::new(AudioPrefetch::new()),
		}
	}

	/// Resolves the clock entity for a monitor.
	fn clock(&self, monitor: Monitor) -> &Entity<RealClock> {
		match monitor {
			Monitor::Source => &self.source_clock,
			Monitor::Program => &self.program_clock,
		}
	}

	/// The open project reference, if any.
	fn project_ref(&self) -> Option<&ProjectRef> {
		self.project.as_ref()
	}

	/// The sequence's timebase `(rate_den, rate_num)`, if a sequence with
	/// a valid frame rate is open.
	fn time_base(&self) -> Option<(i64, i64)> {
		let project = self.project_ref()?;
		let guard = graphops::lock(project);
		graphops::sequence_time_base(&guard.graph, self.sequence?)
	}

	/// The selected footage's duration in frames at the current rate
	/// (0 when nothing is selected or the footage was not probed).
	fn source_length(&self) -> Frame {
		let (Some(project), Some(identity)) = (self.project_ref(), self.selected_item) else {
			return Frame(0);
		};
		let Some(id) = graphops::id_of(identity) else {
			return Frame(0);
		};
		let guard = graphops::lock(project);
		let Some(seconds) = graphops::footage_duration_seconds(&guard.graph, id) else {
			return Frame(0);
		};
		let rate = self.frame_rate();
		let fps = rate.num as f64 / rate.den.max(1) as f64;
		Frame((seconds * fps).round().max(1.0) as i64)
	}

	/// Current sequence length (0 without a sequence).
	fn sequence_length(&self) -> Frame {
		self.sequence_info
			.as_ref()
			.map(|s| s.length)
			.unwrap_or(Frame(0))
	}

	/// Mirrors the program playhead into the sequence (best effort).
	fn mirror_program_playhead(&self, cx: &App) {
		let (Some(project), Some(seq)) = (self.project_ref(), self.sequence) else {
			return;
		};
		let Some(tb) = self.time_base() else {
			return;
		};
		let frame = self.program_clock.read(cx).transport.frame().0;
		graphops::sequence_set_playhead(project, seq, graphops::ts_to_rational(frame, tb));
	}

	/// The proxy resolution the viewer renders at: the sequence's aspect
	/// scaled to a small long edge. Rendering is a synchronous call made
	/// from `cpu_frame` (a `&self` read on the UI thread), so the geometry
	/// stays tiny to keep the block short; the full-resolution frame is
	/// rendered off-thread at the sequence's native size by the background
	/// job (M12 P5a, see [`RealEngine::schedule_full_res`]).
	fn proxy_render_size(&self) -> Option<(i32, i32)> {
		let info = self.sequence_info.as_ref()?;
		let (w, h) = (info.format.width.max(1), info.format.height.max(1));
		// The playback resolution divider (the C++ viewer `Playback
		// Resolution ▸` menu): render the preview at 480/divider long edge
		// so slower machines (or debug builds) can still play in real time.
		let divider = self.playback_divider().max(1) as u32;
		let max_long_edge = 480 / divider;
		let scale = max_long_edge as f64 / w.max(h) as f64;
		let width = ((w as f64 * scale).round() as u32).max(2);
		let height = ((h as f64 * scale).round() as u32).max(2);
		Some((width as i32, height as i32))
	}

	/// Renders one program-monitor frame through the oakrender ticket
	/// arena: builds the sequence's montage at `frame`, renders at the
	/// proxy geometry, and produces the viewer display image plus the
	/// scope samples (M15 S2: the process backend delivers a BGRA8 shm
	/// slot; the display image is built straight from the slot bytes —
	/// the GPU-upload staging copy — and the slot released). Returns
	/// `None` (the caller falls back to the synthetic pattern) when no
	/// sequence is open, the render manager is unavailable, or the render
	/// itself fails.
	fn render_program_frame(&self, frame: Frame) -> Option<(RenderImage, ScopeData)> {
		let project = self.project_ref()?.clone();
		let seq = self.sequence?;
		let tb = self.time_base()?;
		if !super::renderops::ensure_render_manager() {
			return None;
		}
		let mut slot = self.renderer.lock().unwrap();
		if matches!(*slot, RendererSlot::Unavailable) {
			return None;
		}
		let (width, height) = self.proxy_render_size()?;
		match super::renderops::render_sequence_frame(&project, seq, frame.0, tb, width, height) {
			Ok(rendered) => {
				*slot = RendererSlot::Ready;
				let out = rendered.to_display();
				release_rendered_frame(&rendered);
				out
			}
			Err(error) => {
				println!("[real engine] render_frame failed: {error}");
				// Don't retry (and re-log) on every frame.
				*slot = RendererSlot::Unavailable;
				None
			}
		}
	}

	/// The selected entry's footage node (M12 P3: entry ids are the
	/// nodes' stable identities), or `None` when the selection is a folder
	/// or absent.
	fn selected_footage_node(&self) -> Option<NodeId> {
		let project = self.project_ref()?;
		let id = graphops::id_of(self.selected_item?)?;
		let guard = graphops::lock(project);
		graphops::footage_behavior(&guard.graph, id).map(|_| id)
	}

	/// Renders one source-monitor frame through the ticket arena: the
	/// currently selected footage node decoded at the proxy geometry (same
	/// pipeline as the program monitor, M15 S2 shm slot + release).
	/// Returns `None` (the caller falls back to the synthetic pattern)
	/// when no footage is selected, the render manager is unavailable, or
	/// the render itself fails.
	fn render_source_frame(&self, frame: Frame) -> Option<(RenderImage, ScopeData)> {
		let project = self.project_ref()?.clone();
		let node = self.selected_footage_node()?;
		let tb = self.time_base()?;
		if !super::renderops::ensure_render_manager() {
			return None;
		}
		let mut slot = self.source_renderer.lock().unwrap();
		if matches!(*slot, RendererSlot::Unavailable) {
			return None;
		}
		let (width, height) = self.proxy_render_size()?;
		match super::renderops::render_footage_frame(&project, node, frame.0, tb, width, height) {
			Ok(rendered) => {
				*slot = RendererSlot::Ready;
				let out = rendered.to_display();
				release_rendered_frame(&rendered);
				out
			}
			Err(error) => {
				println!("[real engine] source render_frame failed: {error}");
				*slot = RendererSlot::Unavailable;
				None
			}
		}
	}

	/// Builds the background full-res job for `monitor` at `frame` (the
	/// program monitor's sequence or the source monitor's selected footage
	/// node) at the sequence's native size. Returns None when there is
	/// nothing to render (no sequence open, no footage selected).
	fn build_full_res_request(&self, monitor: Monitor, frame: i64) -> Option<FullResRequest> {
		let info = self.sequence_info.as_ref()?;
		let target = match monitor {
			Monitor::Program => FullResTarget::Sequence(self.sequence?),
			Monitor::Source => FullResTarget::Footage(self.selected_footage_node()?),
		};
		Some(FullResRequest {
			monitor,
			frame,
			generation: self.full_res_generation,
			project: self.project_ref()?.clone(),
			target,
			width: info.format.width.max(1) as i32,
			height: info.format.height.max(1) as i32,
			tb: self.time_base()?,
		})
	}

	/// Runs one background full-resolution render (the worker thread of
	/// the full-res path) and reports the finished frame through `tx`. The
	/// request owns a project `Arc`, so the render stays valid even when
	/// the engine's project is dropped mid-flight (the drain discards the
	/// stale completion).
	fn full_res_worker(request: FullResRequest, tx: mpsc::Sender<FullResEvent>) {
		let FullResRequest {
			monitor,
			frame,
			generation,
			project,
			target,
			width,
			height,
			tb,
		} = request;
		let mut event = None;
		if super::renderops::ensure_render_manager() {
			let rendered = match target {
				FullResTarget::Sequence(seq) => {
					super::renderops::render_sequence_frame(&project, seq, frame, tb, width, height)
				}
				FullResTarget::Footage(node) => {
					super::renderops::render_footage_frame(&project, node, frame, tb, width, height)
				}
			};
			if let Ok(rendered) = rendered {
				// M15 S2: the process backend delivers a shm slot — copy the
				// pixels out once (counted, long-lived cache) and release the
				// slot.
				let image = rendered_to_owned_image(&rendered);
				release_rendered_frame(&rendered);
				if let Some(image) = image {
					event = Some(FullResEvent {
						monitor,
						frame,
						generation,
						image,
					});
				}
			}
		}
		if let Some(event) = event {
			let _ = tx.send(event);
		}
	}

	/// Schedules a background full-resolution render for `monitor`'s current
	/// playhead when the policy says so: the playhead is resting, the frame
	/// is not already cached full-res, and no job is in flight for this
	/// monitor (M12 P5a). The job runs on its own thread, so the UI thread
	/// never blocks.
	fn schedule_full_res(&mut self, monitor: Monitor, cx: &mut Context<Self>) {
		let frame = self.clock_frame(monitor, cx).0;
		if frame < 0 {
			return;
		}
		let clock = self.clock(monitor).clone();
		let playing = clock.read(cx).transport.is_playing();
		let schedule = {
			let cache = self.cpu_frame_cache.lock().unwrap();
			cache
				.get(&monitor)
				.is_none_or(|entry| entry.needs_full_res(frame, playing))
		};
		if !schedule {
			return;
		}
		let Some(request) = self.build_full_res_request(monitor, frame) else {
			return;
		};
		self.cpu_frame_cache.lock().unwrap().entry(monitor).or_default().pending =
			Some((frame, request.generation));
		let tx = self.full_res_tx.lock().unwrap().clone();
		std::thread::spawn(move || Self::full_res_worker(request, tx));
	}

	/// Installs completed full-res frames into the cache, discarding stale
	/// completions (a job that outlived an edit, a selection change or a
	/// project drop — its generation no longer matches the pending job).
	fn drain_full_res(&mut self) {
		let mut cache = self.cpu_frame_cache.lock().unwrap();
		let rx = self.full_res_rx.lock().unwrap();
		while let Ok(event) = rx.try_recv() {
			cache
				.entry(event.monitor)
				.or_default()
				.install_full_res(event.frame, event.generation, event.image);
		}
	}

	// -----------------------------------------------------------------------
	// Multi-camera (the Multicam panel grid + the timeline Multi-Cam menu)
	// -----------------------------------------------------------------------

	/// The program playhead as a sequence-frame timestamp (the angle render
	/// time; 0 without a sequence). The sequence's stored playhead is
	/// mirrored from the program clock on every seek/tick.
	fn program_playhead_ts(&self) -> i64 {
		let Some(project) = self.project_ref() else { return 0 };
		let Some(seq) = self.sequence else { return 0 };
		let Some(tb) = self.time_base() else { return 0 };
		let time = graphops::sequence_playhead(&graphops::lock(project).graph, seq);
		graphops::rational_to_ts(time, tb)
	}

	/// The multicam state the panel displays (the C++ viewer's
	/// `detect_multicam_node`): the selected clip's multicam, falling back
	/// to the clip under the program playhead on the video tracks. The
	/// detection runs on demand, so the panel always reads a fresh answer;
	/// the node-graph-selection level of the C++ is not ported (the Rust
	/// node editor has no multicam selection).
	fn multicam_state_internal(&self) -> Option<MulticamState> {
		let project = self.project_ref()?;
		let seq = self.sequence?;
		if let Some(clip) = self.selected_clip_node() {
			if let Some(state) = super::multicam::multicam_state_for_clip(project, clip) {
				return Some(state);
			}
		}
		let time = graphops::sequence_playhead(&graphops::lock(project).graph, seq);
		let clip = super::multicam::clip_at_playhead_with_multicam(project, seq, time)?;
		super::multicam::multicam_state_for_clip(project, clip)
	}

	/// Renders one multicam angle on a background thread (the same ticket
	/// path as the viewers, one single-track montage per angle) and reports
	/// the finished frame through `tx`.
	fn multicam_angle_worker(request: MulticamAngleRequest, tx: mpsc::Sender<MulticamAngleEvent>) {
		let MulticamAngleRequest {
			node_id,
			source,
			playhead,
			project,
			seq,
			track,
			width,
			height,
			tb,
		} = request;
		let mut image = None;
		if super::renderops::ensure_render_manager() {
			if let Ok(rendered) = super::renderops::render_multicam_angle_frame(
				&project, seq, track, playhead, tb, width, height,
			) {
				image = rendered_to_owned_image(&rendered);
				release_rendered_frame(&rendered);
			}
		}
		// Always report (also on failure) so the in-flight marker clears.
		let _ = tx.send(MulticamAngleEvent {
			node_id,
			source,
			playhead,
			image,
		});
	}

	/// The engine's [`AppEngine::multicam_angle_frame`]: returns the cached
	/// angle frame for the current playhead when present, otherwise
	/// schedules a background render (deduplicated per source) and returns
	/// `None`. The panel shows its last image until the frame lands.
	fn multicam_angle_frame_internal(&mut self, source: i32) -> Option<Arc<RenderImage>> {
		let Some(state) = self.multicam_state_internal() else {
			return None;
		};
		if source < 0 || source >= state.source_count {
			return None;
		}
		let Some(project) = self.project.clone() else { return None };
		let Some(seq) = self.sequence else { return None };
		let Some(tb) = self.time_base() else { return None };
		let playhead = self.program_playhead_ts();
		// Exact-playhead cache hit.
		if let Some(img) = self
			.multicam_frames
			.lock()
			.unwrap()
			.lookup(state.node_id, source, playhead)
		{
			return Some(img);
		}
		let Some(mc) = graphops::id_of(state.node_id) else {
			return None;
		};
		let Some(track) = super::multicam::multicam_source_track(&project, mc, source) else {
			return None;
		};
		// The panel may outlive a stale node (a multicam removed under it):
		// treat a node mismatch as a fresh cache.
		let mut cache = self.multicam_frames.lock().unwrap();
		if cache.pending.contains(&(state.node_id, source)) {
			return cache.last(state.node_id, source);
		}
		let (width, height) = {
			let info = self.sequence_info.as_ref()?;
			let (w, h) = (info.format.width.max(1), info.format.height.max(1));
			let scale = MULTICAM_ANGLE_LONG_EDGE as f64 / w.max(h) as f64;
			(((w as f64 * scale).round() as u32).max(2) as i32, ((h as f64 * scale).round() as u32).max(2) as i32)
		};
		cache.pending.insert((state.node_id, source));
		let request = MulticamAngleRequest {
			node_id: state.node_id,
			source,
			playhead,
			project,
			seq,
			track,
			width,
			height,
			tb,
		};
		let tx = self.multicam_tx.lock().unwrap().clone();
		std::thread::spawn(move || Self::multicam_angle_worker(request, tx));
		cache.last(state.node_id, source)
	}

	/// Installs completed multicam angle frames into the cache and repaints
	/// (the panel re-reads the fresh cell images on the next render).
	fn drain_multicam_frames(&mut self, cx: &mut Context<Self>) {
		let rx = self.multicam_rx.lock().unwrap();
		let mut any = false;
		while let Ok(event) = rx.try_recv() {
			any = true;
			let mut cache = self.multicam_frames.lock().unwrap();
			cache.pending.remove(&(event.node_id, event.source));
			if let Some(image) = event.image {
				cache.insert(event.node_id, event.source, event.playhead, image);
			}
		}
		if any {
			cx.notify();
		}
	}

	/// Clears the multicam angle cache (project drop / edit invalidation).
	fn clear_multicam_frames(&mut self) {
		self.multicam_frames.lock().unwrap().frames.clear();
		self.multicam_frames.lock().unwrap().pending.clear();
	}

	// -----------------------------------------------------------------------
	// M15 S2: playback pre-render window
	// -----------------------------------------------------------------------

	/// Feeds the playback pre-render window for `monitor` into the
	/// scheduler (M15 S2): while playing, the forward window's frames are
	/// submitted at Playback priority (ordered by playhead distance), so
	/// workers render them ahead of the playhead into shm slots. The
	/// window rebuilds when the node or the render-params generation
	/// changed; slots that fell behind the playhead are released (credit
	/// returns to the workers).
	fn update_preview_window(&mut self, monitor: Monitor, cx: &mut Context<Self>) {
		// Only during playback: a paused viewer uses the synchronous miss
		// path (and the resting full-res fill).
		let playing = self.clock(monitor).read(cx).transport.is_playing();
		if !playing {
			return;
		}
		let Some(project) = self.project.clone() else { return };
		let Some(tb) = self.time_base() else { return };
		let Some((width, height)) = self.proxy_render_size() else { return };
		let Some(node) = (match monitor {
			Monitor::Program => self.sequence,
			Monitor::Source => self.selected_footage_node(),
		}) else {
			return;
		};
		let node_id = node.identity();
		let playhead = self.clock_frame(monitor, cx).0;
		if playhead < 0 {
			return;
		}
		let length = match monitor {
			Monitor::Program => self.sequence_length().0,
			Monitor::Source => self.source_length().0,
		};
		let Some(m) = RenderManager::global() else { return };

		let forward = config_get_int(CONFIG_KEY_PREVIEW_WINDOW, DEFAULT_PREVIEW_WINDOW_FORWARD)
			.clamp(8, 1200);
		// Cap the window to the pool's slot headroom: a window that can
		// hold every slot starves interactive (this frame's synchronous
		// render) and audio tickets of credit, and since the slot-releasing
		// cleanup runs on this same UI thread, a synchronous wait then
		// deadlocks playback. Keep one slot per worker in reserve.
		let forward = match m.dispatch.preview_window_capacity() {
			Some(capacity) => forward.min(capacity as i64).max(1),
			None => forward,
		};
		let end = length.max(playhead).min(playhead + forward);

		// Reset / rebuild when the node changed or an invalidation bumped the
		// generation (the old pending/claimed requests are cancelled).
		let mut windows = self.preview_windows.lock().unwrap_or_else(|e| e.into_inner());
		let window = windows.entry(monitor).or_default();
		// Cancel/release calls fire completions synchronously and those
		// completions lock `preview_windows`, so they must run AFTER this
		// guard is dropped (calling them here self-deadlocks the UI thread).
		let mut stale_sequences: Vec<u64> = Vec::new();
		let mut stale_keys: Vec<(u64, i64, u64)> = Vec::new();
		let mut stale_slots: Vec<ShmFrameRef> = Vec::new();
		if window.sequence != node_id || window.generation != self.preview_generation {
			stale_sequences.push(window.sequence);
			stale_slots.extend(std::mem::take(&mut window.slots).into_values());
			window.submitted.clear();
			window.sequence = node_id;
			window.generation = self.preview_generation;
		}
		// Release slots that fell behind the playhead (frames passed without
		// being displayed); the just-displayed frame was already consumed by
		// `cpu_frame`. Prune the submitted set the same way.
		let keep_from = (playhead - 2).max(0);
		let stale: Vec<i64> = window
			.slots
			.keys()
			.copied()
			.filter(|f| *f < keep_from)
			.collect();
		for f in stale {
			if let Some(slot) = window.slots.remove(&f) {
				stale_slots.push(slot);
			}
		}
		window
			.submitted
			.retain(|f| *f >= keep_from || (*f >= playhead && *f < end));
		// Cancel in-flight/pending frames the playhead has already passed:
		// when they complete they can never be displayed, but they still
		// occupy worker time. Dropping them keeps the workers on frames
		// around the playhead, so a stall-deferred window converges back
		// instead of grinding through ancient history forever (the
		// "picture frozen while the playhead moves" regression).
		let stale_pending: Vec<i64> = window
			.submitted
			.iter()
			.copied()
			.filter(|f| *f < keep_from && !window.slots.contains_key(f))
			.collect();
		for f in stale_pending {
			stale_keys.push((window.sequence, f, window.generation));
			window.submitted.remove(&f);
		}
		let new_frames: Vec<i64> = (playhead.max(0)..end)
			.filter(|f| !window.submitted.contains(f))
			.collect();
		drop(windows);

		// Fire the deferred cancels/releases outside the `preview_windows`
		// lock (see the comment at the guard above). Cancels come before the
		// new submissions below: a sequence cancel drops every pending
		// request of that sequence regardless of version.
		for sequence in stale_sequences {
			m.cancel_preview_sequence(sequence);
		}
		for (sequence, frame, version) in stale_keys {
			m.dispatch.cancel_preview_frame(sequence, frame, version);
		}
		for slot in &stale_slots {
			m.release_frame(slot);
		}

		for frame in new_frames {
			let params = match monitor {
				Monitor::Program => super::renderops::sequence_frame_params(
					&project,
					node,
					frame,
					tb,
					width,
					height,
				),
				Monitor::Source => {
					super::renderops::footage_frame_params(&project, node, frame, tb, width, height)
				}
			};
			let Ok(params) = params else { continue };
			let distance = frame.saturating_sub(playhead).abs();
			let version = self.preview_generation;
			let preview_windows = self.preview_windows.clone();
			let done: oak_render::ticket::Completion = Box::new(move |result| {
				let mut windows =
					preview_windows.lock().unwrap_or_else(|e| e.into_inner());
				let window = windows.entry(monitor).or_default();
				match result {
					Ok(oak_render::ticket::TicketPayload::ShmFrame(slot)) => {
						// The rendered frame is cached in its shm slot until
						// the playhead reaches it (cpu_frame) or it falls out
						// of the window.
						window.slots.insert(frame, slot);
					}
					_ => {
						// Render failed / cancelled: allow a re-request.
						window.submitted.remove(&frame);
					}
				}
			});
			m.tickets.submit_playback(params, frame, distance, version, done);
			self.preview_windows
				.lock()
				.unwrap_or_else(|e| e.into_inner())
				.entry(monitor)
				.or_default()
				.submitted
				.insert(frame);
		}
	}

	/// Looks a frame up in the pre-rendered playback window's shm slot
	/// cache (M15 S2): builds the viewer display image + scope samples
	/// straight from the slot bytes (the GPU-upload staging copy) and
	/// releases the slot. Returns `None` when the frame is not cached.
	fn preview_slot_frame(
		&self,
		monitor: Monitor,
		frame: Frame,
	) -> Option<(Arc<RenderImage>, ScopeData)> {
		let node = match monitor {
			Monitor::Program => self.sequence?,
			Monitor::Source => self.selected_footage_node()?,
		};
		let node_id = node.identity();
		let mut windows = self.preview_windows.lock().unwrap_or_else(|e| e.into_inner());
		let window = windows.get_mut(&monitor)?;
		if window.sequence != node_id || window.generation != self.preview_generation {
			return None;
		}
		let slot = window.slots.remove(&frame.0)?;
		let out = {
			let meta = &slot.meta;
			let (w, h) = (meta.width.max(0) as u32, meta.height.max(0) as u32);
			let data = slot
				.shm
				.slot_bytes(slot.slot)
				.get(..meta.data_size.max(0) as usize)?;
			let image = bgra_bytes_to_render_image(w, h, data)?;
			let scope = analyze_bgra8(w, h, data);
			(Arc::new(image), scope)
		};
		if let Some(m) = RenderManager::global() {
			m.release_frame(&slot);
		}
		Some(out)
	}

	/// Cancels every monitor's pre-render window and releases its held
	/// slots (edit / selection change / project drop / preview-media
	/// invalidation).
	fn cancel_preview_windows(&mut self) {
		// Collect the teardown work under the lock, then run it outside:
		// `cancel_preview_sequence` fires completions synchronously and those
		// completions lock `preview_windows` (self-deadlock otherwise).
		let mut pending: Vec<(u64, Vec<ShmFrameRef>)> = Vec::new();
		{
			let mut windows = self.preview_windows.lock().unwrap_or_else(|e| e.into_inner());
			for window in windows.values_mut() {
				let slots: Vec<ShmFrameRef> =
					std::mem::take(&mut window.slots).into_values().collect();
				pending.push((window.sequence, slots));
				window.submitted.clear();
			}
		}
		if let Some(m) = RenderManager::global() {
			for (sequence, slots) in pending {
				m.cancel_preview_sequence(sequence);
				for slot in &slots {
					m.release_frame(slot);
				}
			}
		}
	}

	/// Cancels one monitor's pre-render window (source selection change:
	/// the old footage's window is stale even though the program window is
	/// untouched).
	fn cancel_preview_window(&mut self, monitor: Monitor) {
		// Same lock-order rule as `cancel_preview_windows`: run the cancel
		// and releases outside the `preview_windows` guard.
		let pending = {
			let mut windows = self.preview_windows.lock().unwrap_or_else(|e| e.into_inner());
			let Some(window) = windows.get_mut(&monitor) else {
				return;
			};
			let slots: Vec<ShmFrameRef> =
				std::mem::take(&mut window.slots).into_values().collect();
			window.submitted.clear();
			(window.sequence, slots)
		};
		if let Some(m) = RenderManager::global() {
			m.cancel_preview_sequence(pending.0);
			for slot in &pending.1 {
				m.release_frame(slot);
			}
		}
	}

	/// Invalidates every cached/rendered preview frame: the CPU cache is
	/// cleared, the full-res and pre-render-window generations bumped, and
	/// the pre-render windows cancelled (pending/claimed requests and held
	/// slots released). Shared by edits, undo-stack changes, project drops
	/// and preview-media invalidation.
	fn invalidate_rendered_frames(&mut self) {
		self.cpu_frame_cache.lock().unwrap().clear();
		self.full_res_generation = self.full_res_generation.wrapping_add(1);
		self.preview_generation = self.preview_generation.wrapping_add(1);
		self.cancel_preview_windows();
		self.clear_multicam_frames();
	}

	/// Attaches cached thumbnails to the bin entries, spawning a background
	/// generation job for every footage that has none yet. Entries without a
	/// renderable frame keep the widget's placeholder.
	fn attach_thumbnails(&self, entries: Vec<ProjectEntry>) -> Vec<ProjectEntry> {
		let Some(project) = self.project.clone() else {
			return entries;
		};
		entries
			.into_iter()
			.map(|entry| {
				if entry.is_dir {
					return entry;
				}
				let mut thumbs = self.thumbnails.lock().unwrap();
				if let Some(path) = thumbs.done.get(&entry.id) {
					return entry.with_thumbnail(path.to_string_lossy().into_owned());
				}
				if thumbs.pending.insert(entry.id) {
					let tx = self.thumb_tx.lock().unwrap().clone();
					let project = project.clone();
					let identity = entry.id;
					let generation = self.thumb_generation;
					std::thread::spawn(move || {
						Self::thumbnail_worker(project, identity, generation, tx)
					});
				}
				entry
			})
			.collect()
	}

	/// Renders one footage's first frame to a PNG on a background thread
	/// and reports the file through `tx`. The request owns a project `Arc`,
	/// so the render stays valid when the engine drops the project mid-flight
	/// (the drain discards the stale completion).
	fn thumbnail_worker(
		project: ProjectRef,
		identity: u64,
		generation: u64,
		tx: mpsc::Sender<ThumbEvent>,
	) {
		let Some(path) = Self::render_thumbnail(&project, identity) else {
			return;
		};
		let _ = tx.send(ThumbEvent {
			identity,
			generation,
			path,
		});
	}

	/// Renders `identity`'s footage first frame into the thumbnail
	/// directory and returns the PNG path (`None` when the entry is not a
	/// renderable footage or the render fails). The directory follows the
	/// project's disk-cache location when it overrides the default (the
	/// 项目属性 disk-cache setting's live consumer).
	fn render_thumbnail(project: &ProjectRef, identity: u64) -> Option<PathBuf> {
		let node = graphops::id_of(identity)?;
		let (filename, cache_dir) = {
			let guard = graphops::lock(project);
			let filename = graphops::footage_behavior(&guard.graph, node)
				.map(|f| f.filename.clone())
				.filter(|f| !f.is_empty())?;
			let dir = (guard.cache_location_setting != 0).then(|| guard.cache_path());
			(filename, dir)
		};
		let path = thumbnail_path(cache_dir.as_deref(), &filename);
		if path.exists() {
			return Some(path);
		}
		if !super::renderops::ensure_render_manager() {
			return None;
		}
		// Frame zero: the timebase only scales the timestamp, so any valid
		// pair produces time 0. M15 S2: the process backend delivers a shm
		// slot — copy the pixels out once (counted, the PNG must outlive the
		// slot), convert BGRA→RGBA, release the slot.
		let rendered = super::renderops::render_footage_frame(
			project,
			node,
			0,
			(1, 1000),
			THUMBNAIL_WIDTH,
			THUMBNAIL_HEIGHT,
		)
		.ok()?;
		let (width, height, bytes) = match &rendered {
			super::renderops::RenderedFrame::Shm(f) => {
				let meta = &f.meta;
				let (w, h) = (meta.width.max(0) as u32, meta.height.max(0) as u32);
				let pixels = f.shm.slot_to_vec(f.slot);
				let data = pixels.get(..meta.data_size.max(0) as usize)?;
				(w, h, bgra8_to_rgba8(data))
			}
			super::renderops::RenderedFrame::CpuF32 { .. } => {
				let (w, h, samples) = read_f32_frame(&rendered)?;
				let bytes: Vec<u8> = samples
					.iter()
					.map(|v| (v.clamp(0.0, 1.0) * 255.0).round() as u8)
					.collect();
				(w, h, bytes)
			}
		};
		release_rendered_frame(&rendered);
		let image = image::RgbaImage::from_raw(width, height, bytes)?;
		std::fs::create_dir_all(path.parent()?).ok()?;
		// Write aside then rename so readers never see a partial file. The
		// temp name ends in `.part` (not `.png`), so the encoder is given
		// explicitly — `Image::save` infers the format from the extension
		// and would reject it.
		let tmp = path.with_extension("part");
		{
			let file = std::fs::File::create(&tmp).ok()?;
			image
				.write_to(&mut std::io::BufWriter::new(file), image::ImageFormat::Png)
				.ok()?;
		}
		std::fs::rename(&tmp, &path).ok()?;
		Some(path)
	}

	/// Installs completed thumbnails, discarding stale completions from a
	/// dropped project's generation.
	fn drain_thumbnails(&mut self) {
		let rx = self.thumb_rx.lock().unwrap();
		let mut thumbs = self.thumbnails.lock().unwrap();
		while let Ok(event) = rx.try_recv() {
			if event.generation != self.thumb_generation {
				continue;
			}
			thumbs.pending.remove(&event.identity);
			thumbs.done.insert(event.identity, event.path);
		}
	}

	/// Invalidates every monitor's rendered frames (the preview media
	/// changed — proxy toggled, generated or deleted). No timeline rebuild
	/// is needed — the montage resolution reads the proxy switch on every
	/// pull.
	fn invalidate_preview_frames(&mut self, cx: &mut Context<Self>) {
		self.invalidate_rendered_frames();
		cx.notify();
	}

	/// The disk cache directory the proxy files live in (the config
	/// override, else the platform default).
	fn proxy_cache_path() -> String {
		let configured = config_get_string(CONFIG_KEY_DISK_CACHE_PATH);
		if configured.trim().is_empty() {
			oak_common::filefunctions::default_disk_cache_path()
		} else {
			configured
		}
	}

	/// The footage node behind a project-explorer entry id, when it is a
	/// footage node of the open project.
	fn footage_of(&self, id: u64) -> Option<NodeId> {
		let project = self.project.as_ref()?;
		let node = graphops::id_of(id)?;
		let guard = graphops::lock(project);
		graphops::footage_behavior(&guard.graph, node)?;
		Some(node)
	}

	/// Drains the in-flight proxy transcodes (called from the tick loop):
	/// progress events update the status-bar segment, completion applies
	/// the footage's proxy fields and invalidates the rendered frames.
	fn drain_proxy_runs(&mut self, cx: &mut Context<Self>) {
		if self.proxy_runs.is_empty() {
			return;
		}
		let mut finished: Vec<(NodeId, bool)> = Vec::new();
		let mut changed = false;
		for run in self.proxy_runs.iter_mut() {
			while let Ok(event) = run.events.try_recv() {
				match event {
					super::engine::ExportEvent::Started => {}
					super::engine::ExportEvent::Progress(value) => {
						run.progress = value.clamp(0.0, 1.0);
						changed = true;
					}
					super::engine::ExportEvent::Finished(ok, _) => {
						finished.push((run.footage, ok));
					}
				}
			}
		}
		self.proxy_runs.retain(|run| {
			!finished.iter().any(|(footage, _)| *footage == run.footage)
		});
		if let Some(project) = self.project.clone() {
			for (footage, ok) in &finished {
				let mut guard = graphops::lock(&project);
				if let Some(f) = guard
					.graph
					.get_mut(*footage)
					.and_then(|e| e.behavior.as_any_mut())
					.and_then(|a| a.downcast_mut::<oak_node::footage::FootageBehavior>())
				{
					if *ok {
						// The transcode wrote the final file: mark the
						// proxy ready and usable (state 2, enabled).
						f.proxy_state = 2;
						f.proxy_enabled = true;
					} else {
						f.proxy_state = 3;
					}
				}
			}
		}
		// Only a FINISHED run may change any preview media (the proxy
		// engages then); progress events only repaint the status bar.
		// Invalidating the rendered frames on every progress tick would
		// keep the playback cache permanently cold while a proxy
		// generates.
		if !finished.is_empty() {
			self.invalidate_preview_frames(cx);
		} else if changed {
			cx.notify();
		}
	}

	/// The proxy lifecycle state of one footage row: an in-flight run
	/// wins, then the disk state of the recorded proxy path, with a
	/// recorded failure (state 3) preserved while no file is on disk.
	fn proxy_state_of(
		&self,
		f: &oak_node::footage::FootageBehavior,
		node: NodeId,
	) -> super::engine::ProxyMediaState {
		use super::engine::ProxyMediaState;
		if self.proxy_runs.iter().any(|run| run.footage == node) {
			return ProxyMediaState::Generating;
		}
		if f.proxy.is_empty() {
			return ProxyMediaState::Missing;
		}
		match oak_codec::proxymanager::ProxyManager::get_proxy_state(&f.proxy) {
			oak_codec::proxymanager::ProxyState::Ready => ProxyMediaState::Ready,
			oak_codec::proxymanager::ProxyState::Generating => ProxyMediaState::Generating,
			_ => {
				if f.proxy_state == 3 {
					ProxyMediaState::Failed
				} else {
					ProxyMediaState::Missing
				}
			}
		}
	}

	/// Synchronize the selected clips by their footage's source start
	/// timecode (the C++ `synchronize_selected_clips_by_source_time`):
	/// the clip whose source head (start time + media in) is earliest is
	/// the reference, the earliest selected in point is the anchor, and
	/// every clip is re-placed so its source head lines up with the
	/// reference's at the anchor (one multi-undo).
	fn sync_clips_by_source_time_internal(&mut self, clips: &[ClipId]) {
		use oak_audio::synchronizer::{place_by_source_time, SourceClip};

		let Some(project) = self.project.clone() else {
			return;
		};

		struct SourceTarget {
			node: NodeId,
			track: NodeId,
			list: NodeId,
			track_index: i32,
			source: SourceClip,
			source_head: oak_core::Rational,
			block_in: oak_core::Rational,
		}

		let mut targets: Vec<SourceTarget> = Vec::new();
		{
			let guard = graphops::lock(&project);
			for clip in clips {
				let Some(node) = graphops::id_of(clip.0) else {
					continue;
				};
				let Some((block_in, _, media_in)) = graphops::clip_range(&guard.graph, node)
				else {
					continue;
				};
				let Some(track) = graphops::clip_track(&guard.graph, node) else {
					continue;
				};
				let Some(t) = graphops::track_behavior(&guard.graph, track) else {
					continue;
				};
				let Some(list) = t.track_list else {
					continue;
				};
				let Some(f) = graphops::find_input_footage(&guard.graph, node)
					.and_then(|f| graphops::footage_behavior(&guard.graph, f))
				else {
					continue;
				};
				if !f.has_source_start_time {
					continue;
				}
				targets.push(SourceTarget {
					node,
					track,
					list,
					track_index: t.index,
					source_head: f.source_start_time + media_in,
					block_in,
					source: SourceClip {
						source_start_time: f.source_start_time,
						media_in,
						has_source_start_time: true,
					},
				});
			}
		}
		if targets.len() < 2 {
			return;
		}

		let mut reference = &targets[0];
		let mut anchor_in = targets[0].block_in;
		for target in &targets[1..] {
			if target.source_head < reference.source_head {
				reference = target;
			}
			if target.block_in < anchor_in {
				anchor_in = target.block_in;
			}
		}

		// (node, track, list, track index, placement) for every valid
		// placement (the reference itself lands on the anchor).
		let mut placements: Vec<(NodeId, NodeId, NodeId, i32, oak_core::Rational)> =
			Vec::new();
		for target in &targets {
			let placement = place_by_source_time(&reference.source, &target.source, anchor_in);
			if placement.valid {
				placements.push((
					target.node,
					target.track,
					target.list,
					target.track_index,
					placement.timeline_in,
				));
			}
		}
		if placements.len() < 2 {
			return;
		}

		let mut children: Vec<oak_undo::undocommand::UndoCommand> = Vec::new();
		for (node, track, _, _, _) in &placements {
			children.push(
				oak_timeline::undogeneral::TrackReplaceBlockWithGapCommand::new(
					graphops::node_ref(&project, *track),
					graphops::node_ref(&project, *node),
					false,
				)
				.to_command(),
			);
		}
		for (node, _, list, track_index, timeline_in) in &placements {
			children.push(
				oak_timeline::undopointer::TrackPlaceBlockCommand::new(
					graphops::node_ref(&project, *list),
					*track_index,
					graphops::node_ref(&project, *node),
					*timeline_in,
				)
				.to_command(),
			);
		}
		let _ = graphops::push_multi_command(children, "Synchronize Clips by Source Time");
	}

	/// Synchronize the selected clips by waveform correlation (the C++
	/// `synchronize_selected_clips_by_waveform_internal`): the leftmost
	/// clip is the reference, every other clip is shifted by the
	/// estimated envelope offset; with `allow_speed`, an inconclusive
	/// offset triggers a rate search whose winner also rescales the clip
	/// speed (one multi-undo).
	fn sync_clips_by_waveform_internal(&mut self, clips: &[ClipId], allow_speed: bool) {
		use oak_audio::synchronizer::place_by_waveform_offset;
		use oak_audio::waveformsync::{estimate_envelope_offset_valid, estimate_stretch_and_offset};

		let Some(cache) = self.waveform_cache() else {
			return;
		};
		let Some(project) = self.project.clone() else {
			return;
		};

		struct WaveTarget {
			node: NodeId,
			track: NodeId,
			list: NodeId,
			track_index: i32,
			block_in: oak_core::Rational,
			speed: f64,
			media_in_s: f64,
			media_len_s: f64,
			waveform: Arc<crate::oakui::waveform::ClipWaveform>,
		}

		let mut targets: Vec<WaveTarget> = Vec::new();
		{
			let guard = graphops::lock(&project);
			for clip in clips {
				let Some(node) = graphops::id_of(clip.0) else {
					continue;
				};
				let Some((block_in, block_out, media_in)) =
					graphops::clip_range(&guard.graph, node)
				else {
					continue;
				};
				let Some(behavior) = graphops::clip_behavior(&guard.graph, node) else {
					continue;
				};
				let Some(track) = graphops::clip_track(&guard.graph, node) else {
					continue;
				};
				let Some(t) = graphops::track_behavior(&guard.graph, track) else {
					continue;
				};
				let Some(list) = t.track_list else {
					continue;
				};
				let Some(waveform) = cache.get(clip.0) else {
					continue;
				};
				// The C++ media range is media_in + timeline length
				// (speed/reverse ignored there); the Rust cache covers the
				// whole file from sample 0.
				let media_len_s = (block_out - block_in).to_f64();
				if !super::waveformsync::waveform_sync_eligible(&waveform, media_len_s) {
					continue;
				}
				targets.push(WaveTarget {
					node,
					track,
					list,
					track_index: t.index,
					block_in,
					speed: behavior.core.speed,
					media_in_s: media_in.to_f64(),
					media_len_s,
					waveform,
				});
			}
		}
		if targets.len() < 2 {
			return;
		}

		let mut ref_index = 0usize;
		for (i, target) in targets.iter().enumerate() {
			if target.block_in < targets[ref_index].block_in {
				ref_index = i;
			}
		}
		let sample_rate = targets[ref_index].waveform.sample_rate;
		if sample_rate <= 0 {
			return;
		}
		let window_samples = (sample_rate / 20).max(1) as usize;
		let max_offset_windows = (i64::from(sample_rate) * 600) / window_samples as i64;

		let reference = &targets[ref_index];
		let (ref_envelope, ref_valid) = super::waveformsync::extract_cache_envelope(
			&reference.waveform,
			reference.media_in_s,
			reference.media_len_s,
			window_samples,
		);

		// (node, track, list, track index, placement, speed, old speed).
		let mut placements: Vec<(
			NodeId,
			NodeId,
			NodeId,
			i32,
			oak_core::Rational,
			f64,
			f64,
		)> = vec![(
			reference.node,
			reference.track,
			reference.list,
			reference.track_index,
			reference.block_in,
			1.0,
			reference.speed,
		)];

		for (i, target) in targets.iter().enumerate() {
			if i == ref_index {
				continue;
			}
			let (cand_envelope, cand_valid) = super::waveformsync::extract_cache_envelope(
				&target.waveform,
				target.media_in_s,
				target.media_len_s,
				window_samples,
			);
			let mut offset = estimate_envelope_offset_valid(
				&ref_envelope,
				&cand_envelope,
				&ref_valid,
				&cand_valid,
				window_samples,
				max_offset_windows,
			);
			let mut speed = 1.0;
			if allow_speed && (!offset.valid || offset.confidence < 0.6) {
				// Inconclusive: the clips may run at different speeds —
				// search a rate range with a tighter offset radius.
				let radius = max_offset_windows.min(
					(i64::from(sample_rate) * 30) / window_samples as i64,
				);
				let stretch = estimate_stretch_and_offset(
					&ref_envelope,
					&cand_envelope,
					&ref_valid,
					&cand_valid,
					window_samples,
					radius,
					0.75,
					1.34,
					0.005,
				);
				if stretch.valid
					&& stretch.confidence > (if offset.valid { offset.confidence } else { 0.0 })
				{
					speed = stretch.rate;
					offset.valid = true;
					offset.confidence = stretch.confidence;
					offset.offset_samples = stretch.offset_samples;
				}
			}
			if !offset.valid {
				continue;
			}
			let placement =
				place_by_waveform_offset(reference.block_in, offset.offset_samples, sample_rate);
			if placement.valid && placement.timeline_in >= oak_core::Rational::new(0, 1) {
				placements.push((
					target.node,
					target.track,
					target.list,
					target.track_index,
					placement.timeline_in,
					speed,
					target.speed,
				));
			}
		}
		if placements.len() < 2 {
			return;
		}

		let mut children: Vec<oak_undo::undocommand::UndoCommand> = Vec::new();
		for (node, track, _, _, _, speed, old_speed) in &placements {
			children.push(
				oak_timeline::undogeneral::TrackReplaceBlockWithGapCommand::new(
					graphops::node_ref(&project, *track),
					graphops::node_ref(&project, *node),
					false,
				)
				.to_command(),
			);
			if (*speed - 1.0).abs() > f64::EPSILON {
				// The C++ multiplies the clip's current speed by the
				// estimated rate (`clip_speed * placement.speed`); the
				// Rust clip keeps its speed on the block core.
				let new_speed = *old_speed * speed;
				let old_speed = *old_speed;
				let node = *node;
				let (p1, p2) = (project.clone(), project.clone());
				children.push(oak_undo::undocommand::UndoCommand::from_closures(
					move || {
						let mut g = graphops::lock(&p1);
						if let Some(c) = g
							.graph
							.get_mut(node)
							.and_then(|e| e.behavior.as_any_mut())
							.and_then(|a| a.downcast_mut::<oak_node::block::ClipBlockBehavior>())
						{
							c.core.speed = new_speed;
						}
					},
					move || {
						let mut g = graphops::lock(&p2);
						if let Some(c) = g
							.graph
							.get_mut(node)
							.and_then(|e| e.behavior.as_any_mut())
							.and_then(|a| a.downcast_mut::<oak_node::block::ClipBlockBehavior>())
						{
							c.core.speed = old_speed;
						}
					},
				));
			}
		}
		for (node, _, list, track_index, timeline_in, _, _) in &placements {
			children.push(
				oak_timeline::undopointer::TrackPlaceBlockCommand::new(
					graphops::node_ref(&project, *list),
					*track_index,
					graphops::node_ref(&project, *node),
					*timeline_in,
				)
				.to_command(),
			);
		}
		let _ = graphops::push_multi_command(children, "Synchronize Clips by Waveform");
	}

	/// Adopts a newly created/loaded project, dropping any previous one,
	/// and rebuilds every snapshot. The undo stack is cleared (a project
	/// switch starts a fresh history, mirroring the facade's
	/// project_new/load) and the project is bound to the write-through
	/// library. Projects without a sequence get a blank default.
	fn adopt_project(&mut self, project: ProjectRef, cx: &mut Context<Self>) {
		self.drop_project();
		oak_undo::global::clear().ok();

		// Cached display info.
		let (name, path, first_sequence) = {
			let guard = graphops::lock(&project);
			(
				guard.name(),
				PathBuf::from(guard.filename().to_string()),
				graphops::sequence_ids(&guard).first().copied(),
			)
		};
		self.project_info = Project {
			name: if name.is_empty() || name == "(untitled)" {
				UNTITLED.into()
			} else {
				name
			},
			path,
		};
		self.project = Some(project.clone());
		self.storage = Some(AuxHandle(graphops::storage_bind(&project)));

		// Footage loaded from a file may lack stream metadata (C++ projects
		// have no `<streams>` segment; older Rust saves predate the probe
		// recording them): reprobe so durations, drop track kinds and the
		// source monitor's playback length come back (the facade's load
		// probe cascade).
		graphops::reprobe_unprobed_footage(&project);

		// The sequence: the project's first, or a blank default.
		let seq = first_sequence
			.unwrap_or_else(|| graphops::create_sequence(&project, "Sequence 1"));
		self.sequence = Some(seq);
		self.markers = Some(AuxHandle(graphops::marker_list_create()));
		self.workarea = Some(AuxHandle(graphops::workarea_create()));
		self.refresh_sequence_info();
		self.rebuild_timeline();

		// The project's stored OCIO override (if any) drives the display
		// color pipeline from here on.
		Self::apply_project_color_config(Some(&project));

		cx.notify();
	}

	/// Drops the project and every auxiliary handle. The undo stack is
	/// cleared FIRST: its commands reference the marker/workarea
	/// auxiliaries, so they must be gone before those handles release.
	fn drop_project(&mut self) {
		*self.renderer.lock().unwrap() = RendererSlot::Untried;
		*self.source_renderer.lock().unwrap() = RendererSlot::Untried;
		oak_undo::global::clear().ok();
		if let Some(mut markers) = self.markers.take() {
			graphops::release_handle(&mut markers.0);
		}
		if let Some(mut workarea) = self.workarea.take() {
			graphops::release_handle(&mut workarea.0);
		}
		if let Some(storage) = self.storage.take() {
			graphops::storage_unbind(storage.0);
		}
		self.project = None;
		self.sequence = None;
		// The audio prefetch belongs to the dropped project's sequence time:
		// invalidate it so the next playback restarts the submission cursor.
		self.audio_prefetch
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.reset(0, 1);
		// The sequence an in-flight full-res job may still be rendering is
		// gone (the job holds its own project `Arc`, so it stays valid, but
		// its frame belongs to the dropped project): mark it stale.
		self.invalidate_rendered_frames();
		// Same for in-flight thumbnail jobs; the cache is per-project too
		// (identities are only unique within one graph).
		self.thumb_generation = self.thumb_generation.wrapping_add(1);
		*self.thumbnails.lock().unwrap() = ThumbnailState::default();
		self.tracks.clear();

		self.sequence_info = None;
		self.project_info = Project {
			name: UNTITLED.into(),
			path: PathBuf::new(),
		};
		// The dropped project's OCIO override leaves with it: the display
		// color pipeline returns to the app default config.
		Self::apply_project_color_config(None);
	}

	/// Applies a project's stored OCIO override (`Some(project)`) or
	/// restores the app default config (`None`, or a project without an
	/// override). A config that fails to load falls back to the app default
	/// (the file may have moved since the project was saved).
	fn apply_project_color_config(project: Option<&ProjectRef>) {
		let stored = project.and_then(|p| {
			graphops::lock(p)
				.settings
				.get(PROJECT_SETTING_OCIO_CONFIG)
				.cloned()
		});
		let applied = match stored.as_deref() {
			None => oak_render::color::set_up_default_config(),
			Some(path) => oak_render::color::set_up_default_config_from(Some(path)),
		};
		if let Err(e) = applied {
			println!("[real engine] project OCIO config apply failed: {e}");
			let _ = oak_render::color::set_up_default_config();
		}
		super::displaycolor::invalidate();
	}

	/// Refreshes the cached `Sequence` (name / format / length) from the
	/// graph.
	fn refresh_sequence_info(&mut self) {
		let (Some(project), Some(seq)) = (self.project.clone(), self.sequence) else {
			self.sequence_info = None;
			return;
		};
		let guard = graphops::lock(&project);
		let name = graphops::node_label(&guard.graph, seq);
		let rate = graphops::sequence_video_params(&guard.graph, seq)
			.map(|(_, _, r)| {
				FrameRate::new(r.numerator().max(1) as u32, r.denominator().max(1) as u32)
			})
			.unwrap_or(VideoFormat::hd_1080p25().rate);
		let (width, height) = graphops::sequence_video_params(&guard.graph, seq)
			.map(|(w, h, _)| (w.max(1) as u32, h.max(1) as u32))
			.unwrap_or((1920, 1080));
		let seconds = graphops::sequence_length(&guard.graph, seq);
		let seconds = seconds.numerator() as f64 / seconds.denominator().max(1) as f64;
		let length = Frame((seconds * rate.num as f64 / rate.den as f64).round() as i64);
		self.sequence_info = Some(Sequence {
			name: if name.is_empty() {
				"Sequence 1".into()
			} else {
				name
			},
			format: VideoFormat {
				width,
				height,
				rate,
			},
			length,
		});
		// Stage 6b: keep the OFX normalised-coordinate default conversion in
		// sync with the current sequence's extent.
		crate::oakui::ofx::update_project_extent(width as f64, height as f64);
	}

	/// Stage 6b: refreshes the OFX timeline-suite fallback time snapshot
	/// from the program monitor's playhead (seconds) and the sequence
	/// length (the range bounds). Called on seek and on every tick.
	fn update_ofx_viewer_time(&self, cx: &App) {
		let fps = self.frame_rate().as_f64();
		if fps <= 0.0 {
			return;
		}
		let frame = self.clock_frame(Monitor::Program, cx).0;
		let time = frame as f64 / fps;
		let length = self.sequence_length().0 as f64 / fps;
		crate::oakui::ofx::update_viewer_time(time, 0.0, length);
	}

	/// Rebuilds the timeline snapshot from the graph.
	fn rebuild_timeline(&mut self) {
		self.tracks.clear();
		let (Some(project), Some(seq)) = (self.project_ref(), self.sequence) else {
			return;
		};
		let tb = self.time_base();
		let mut out: Vec<RealTrack> = Vec::new();
		{
			let guard = graphops::lock(project);
			// Per-type track lists, each displayed topmost-first. NLE
			// growth direction: video/subtitle tracks grow upward (the
			// list is displayed reversed, so an appended track lands on
			// top), audio tracks grow downward (list order = display
			// order, so an appended track lands at the bottom).
			for kind in [TrackType::Video, TrackType::Audio, TrackType::Subtitle] {
				let tracks = graphops::track_ids(&guard.graph, seq, kind);
				let ordered: Box<dyn Iterator<Item = (usize, &NodeId)>> = match kind {
					TrackType::Audio => Box::new(tracks.iter().enumerate()),
					_ => Box::new(tracks.iter().enumerate().rev()),
				};
				for (track_index, &track_id) in ordered {
					out.push(Self::snapshot_track(&guard.graph, track_id, kind, track_index, tb));
				}
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

	/// The shared refresh after any undo-stack change (undo / redo /
	/// history jump): the sequence info, the timeline snapshot and the
	/// frame caches all follow the reverted or re-applied state.
	fn apply_stack_change(&mut self, cx: &mut Context<Self>) {
		self.refresh_sequence_info();
		self.rebuild_timeline();
		self.invalidate_rendered_frames();
		cx.notify();
	}

	/// Snapshots one track (with its clips) from the graph.
	fn snapshot_track(
		graph: &oak_node::graph::Graph,
		track: NodeId,
		kind: TrackType,
		track_index: usize,
		tb: Option<(i64, i64)>,
	) -> RealTrack {
		let name = match kind {
			TrackType::Video => format!("V{}", track_index + 1),
			TrackType::Audio => format!("A{}", track_index + 1),
			TrackType::Subtitle => format!("S{}", track_index + 1),
		};
		// Height in internal units → pixels.
		let height = graphops::track_behavior(graph, track)
			.map(|t| {
				px(oak_node::track::internal_height_to_pixel_height(t.height).max(24) as f32)
			})
			.unwrap_or(px(64.0));
		let clips = graphops::clip_ids(graph, track)
			.iter()
			.enumerate()
			.filter_map(|(clip_index, &block)| {
				let (in_r, out_r, media_r) = graphops::clip_range(graph, block)?;
				let to_ts = |r: oak_core::Rational| tb.map(|tb| graphops::rational_to_ts(r, tb)).unwrap_or(0);
				Some(RealClip {
					id: ClipId(block.identity()),
					range: FrameRange::new(Frame(to_ts(in_r)), Frame(to_ts(out_r))),
					media_in: Frame(to_ts(media_r)),
					label: format!("Clip {}", clip_index + 1).into(),
					color: clip_color(clip_index as u64),
					block,
				})
			})
			.collect();
		// The muted flag doubles as the video/subtitle visibility toggle
		// (Olive parity: the eye button flips `muted`).
		let (muted, locked) = graphops::track_behavior(graph, track)
			.map(|t| (t.muted, t.locked))
			.unwrap_or((false, false));
		RealTrack {
			kind: track_kind_of(kind),
			name: name.into(),
			height,
			locked,
			muted,
			solo: false,
			visible: !muted,
			clips,
			track,
			track_index,
		}
	}

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
		let Some(project) = self.project_ref() else {
			return;
		};
		let filename = {
			let guard = graphops::lock(project);
			graphops::clip_media_filename(&guard.graph, clip.block)
		};
		let Some(filename) = filename else {
			return;
		};
		let duration_frames = (clip.range.end.0 - clip.range.start.0).max(1);
		cache.refresh(clip.id.0, &filename, duration_frames);
	}

	/// Looks up the snapshot clip's block node by `ClipId` (the id IS the
	/// block's stable identity).
	fn clip_block(&self, id: ClipId) -> Option<NodeId> {
		let block = graphops::id_of(id.0)?;
		let project = self.project_ref()?;
		let guard = graphops::lock(project);
		graphops::clip_behavior(&guard.graph, block)?;
		// The clip must be on the CURRENT timeline (a stale id of a
		// removed clip must not resolve).
		self.tracks
			.iter()
			.any(|t| t.clips.iter().any(|c| c.id == id))
			.then_some(block)
	}

	/// Whether the track hosting clip `block` is locked (locked tracks
	/// reject every clip edit: trim, move, split, delete).
	fn clip_track_locked(&self, block: NodeId) -> bool {
		let Some(project) = self.project_ref() else {
			return false;
		};
		let guard = graphops::lock(project);
		graphops::clip_track(&guard.graph, block)
			.and_then(|track| graphops::track_behavior(&guard.graph, track))
			.map(|t| t.locked)
			.unwrap_or(false)
	}

	/// Resolves the node-graph selection to the inspector's view: which
	/// clip's stack to show, and which effect card (if any) to highlight.
	/// A single selected node that names a clip block on the current
	/// timeline selects that clip's stack; one that names an effect of some
	/// clip's chain selects that clip's stack and returns the matching
	/// card. Without a node-graph selection the timeline-selected clip
	/// (the existing behavior) is the target.
	fn inspector_selection(&self) -> (Option<ClipId>, Option<EffectId>) {
		let Some(project) = self.project_ref() else {
			return (self.selected_clip, None);
		};
		let Some(ident) = self.selected_graph_node else {
			return (self.selected_clip, None);
		};
		let Some(node) = graphops::id_of(ident) else {
			return (self.selected_clip, None);
		};
		let guard = graphops::lock(project);
		// A clip block node on the current timeline: its stack is the target.
		if graphops::clip_behavior(&guard.graph, node).is_some()
			&& self.tracks.iter().any(|t| t.clips.iter().any(|c| c.id.0 == ident))
		{
			return (Some(ClipId(ident)), None);
		}
		// An effect node of some clip's chain: that clip's stack, with the
		// matching card highlighted.
		for track in &self.tracks {
			for clip in &track.clips {
				let Some(block) = graphops::id_of(clip.id.0) else {
					continue;
				};
				if graphops::clip_behavior(&guard.graph, block).is_none() {
					continue;
				}
				if let Some(effect) = super::effectchain::chain(&guard.graph, block)
					.iter()
					.find(|n| n.identity() == ident)
				{
					return (Some(clip.id()), Some(EffectId(effect.identity())));
				}
			}
		}
		(self.selected_clip, None)
	}

	/// The selected clip's block node, or `None` when no single clip is
	/// selected. The inspector's stack target follows the node-graph
	/// selection (see [`inspector_selection`](Self::inspector_selection)).
	fn selected_clip_node(&self) -> Option<NodeId> {
		self.clip_block(self.inspector_selection().0?)
	}

	/// The display label of the selected clip (its timeline snapshot
	/// label), if any.
	fn selected_clip_label(&self) -> Option<SharedString> {
		let clip_id = self.inspector_selection().0?;
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
		let Some(project) = self.project_ref() else {
			return Vec::new();
		};
		let Some(host) = self.selected_clip_node() else {
			return Vec::new();
		};
		let guard = graphops::lock(project);
		let mut out: Vec<Arc<dyn EffectData>> = Vec::new();
		for node in super::effectchain::chain(&guard.graph, host) {
			let identity = node.identity();
			let type_id = graphops::node_type_id(&guard.graph, node);
			// `name_of` covers both static (built-in) and dynamic (OpenFX
			// plugin) factory entries.
			let title = oak_node::factory::Factory::global()
				.name_of(&type_id)
				.filter(|n| !n.is_empty())
				.unwrap_or(type_id);
			let plugin_handle = super::effectchain::plugin_instance_handle(&guard.graph, node);
			// The OpenFX plugin badge: the persistent-message count (the
			// simplified 徽标/计数 of stage 6b). Built-in effects show none.
			let badge = plugin_handle.and_then(|handle| {
				let count =
					oak_plugin::suites::message::persistent_message_count(handle as usize);
				(count > 0).then_some(count)
			});
			let subtitle = plugin_handle.map(|_| {
				// A muted secondary line identifying the plugin effect as an
				// OpenFX entry.
				crate::i18n::tr("inspector.badge.openfx").to_string()
			});
			out.push(Arc::new(RealEffect {
				id: EffectId(identity),
				title: title.into(),
				subtitle: subtitle.map(Into::into),
				enabled: super::effectchain::is_enabled(&guard.graph, node),
				expanded: self.expanded_effects.contains(&identity),
				badge,
			}) as Arc<dyn EffectData>);
		}
		out
	}

	/// Applies an edit command's result, then refreshes the snapshots and
	/// repaints.
	fn apply_edit(&mut self, result: Result<(), String>, what: &str, cx: &mut Context<Self>) {
		if let Err(error) = result {
			println!("[real engine] {what} failed: {error}");
		}
		self.refresh_sequence_info();
		self.rebuild_timeline();
		// The sequence content changed: cached rendered frames are stale,
		// and so are any in-flight full-res renders and pre-render windows
		// (M12 P5a / M15 S2).
		self.invalidate_rendered_frames();
		cx.notify();
	}
}

// ---------------------------------------------------------------------------
// EngineGateway
// ---------------------------------------------------------------------------

/// The clip's timeline length in seconds — the media-range length the C++
/// waveform sync extracts its envelope over (`media_in + length`, speed
/// and reverse ignored there; parity with
/// `oakengine_clip_get_media_range_rational`).
fn clip_media_length_seconds(g: &oak_node::graph::Graph, node: NodeId) -> f64 {
	match graphops::clip_range(g, node) {
		Some((in_r, out_r, _)) => (out_r - in_r).to_f64(),
		None => 0.0,
	}
}

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
		self.update_ofx_viewer_time(cx);
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
		// Each clock loops at its own monitor's length: the program at the
		// sequence length, the source at the selected footage's duration
		// (the sequence length is wrong for footage playback — an empty
		// project's length 0 used to freeze the source playhead at 0).
		let length = self.sequence_length();
		let source_length = self.source_length();
		for (clock, len) in [
			(&self.source_clock, source_length),
			(&self.program_clock, length),
		] {
			let clock = clock.clone();
			clock.update(cx, |clock, cx| {
				clock.tick(len);
				cx.notify();
			});
		}
		self.mirror_program_playhead(cx);
		self.update_ofx_viewer_time(cx);
		self.meter_phase = self.meter_phase.wrapping_add(1);
		// M15 S2: pump the process dispatcher — ticket completions (the
		// pre-render window, full-res fills, synchronous renders) are
		// delivered from its poll loop, which must run on the UI tick.
		// Then feed the playback pre-render windows before draining the
		// completion channels.
		if let Some(m) = RenderManager::global() {
			m.poll();
		}
		self.update_preview_window(Monitor::Source, cx);
		self.update_preview_window(Monitor::Program, cx);
		// M12 P1: while the program plays, pull the audio for the
		// current playhead window and queue it for the output device.
		if self.program_playing {
			self.pull_audio_tick(cx);
		}
		// M12 P5a: install finished full-resolution frames and schedule
		// the next fills for the resting playheads (the schedule skips
		// playing monitors, so playback keeps the proxy path).
		self.drain_full_res();
		self.drain_thumbnails();
		self.drain_proxy_runs(cx);
		self.drain_multicam_frames(cx);
		self.schedule_full_res(Monitor::Source, cx);
		self.schedule_full_res(Monitor::Program, cx);
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
		let Some(markers) = &self.markers else {
			return Vec::new();
		};
		let Some(tb) = self.time_base() else {
			return Vec::new();
		};
		graphops::markers_of(&markers.0)
			.into_iter()
			.map(|(time, name, color)| Marker {
				frame: Frame(graphops::rational_to_ts(time, tb)),
				label: name.into(),
				color: Some(marker_color(color)),
			})
			.collect()
	}
}

impl EffectStackDataSource for RealEngine {
	fn effects(&self) -> Vec<Arc<dyn EffectData>> {
		self.selected_effect_cards()
	}

	fn target_label(&self) -> Option<SharedString> {
		let label = self.selected_clip_label()?;
		let project = self.project_ref()?;
		let host = self.selected_clip_node()?;
		let guard = graphops::lock(project);
		// A clip that cannot host effects keeps the empty state (no label,
		// no cards).
		super::effectchain::effect_input_of(&guard.graph, host)?;
		Some(label)
	}

	fn selected_effect(&self) -> Option<EffectId> {
		self.inspector_selection().1
	}
}

impl NodeGraphDataSource for RealEngine {
	type Node = RealNode;
	type Edge = RealEdge;

	fn nodes(&self) -> Vec<Self::Node> {
		let (Some(project), Some(seq)) = (self.project_ref(), self.sequence) else {
			return Vec::new();
		};
		match self.selected_clip {
			// While one clip is selected the editor shows that clip's
			// context chain (footage → effects → clip) instead of the whole
			// sequence graph.
			Some(clip) => crate::oakui::nodegraph::build_graph_for_clip(project, seq, clip.0).0,
			None => crate::oakui::nodegraph::build_graph(project, seq).0,
		}
	}

	fn edges(&self) -> Vec<Self::Edge> {
		let (Some(project), Some(seq)) = (self.project_ref(), self.sequence) else {
			return Vec::new();
		};
		match self.selected_clip {
			Some(clip) => crate::oakui::nodegraph::build_graph_for_clip(project, seq, clip.0).1,
			None => crate::oakui::nodegraph::build_graph(project, seq).1,
		}
	}

	fn can_connect(&self, from: gpui::node_graph::PortId, to: gpui::node_graph::PortId) -> bool {
		let Some(project) = self.project_ref() else {
			return false;
		};
		crate::oakui::nodegraph::can_connect(project, from, to)
	}
}

impl ProjectDataSource for RealEngine {
	fn roots(&self) -> Vec<ProjectEntry> {
		let Some(project) = self.project_ref() else {
			return Vec::new();
		};
		self.attach_thumbnails(crate::oakui::projectbrowser::roots(project))
	}

	fn children(&self, parent_id: u64) -> Vec<ProjectEntry> {
		let Some(project) = self.project_ref() else {
			return Vec::new();
		};
		self.attach_thumbnails(crate::oakui::projectbrowser::children(project, parent_id))
	}
}

impl AudioMeterDataSource for RealEngine {
	fn levels(&self) -> Vec<f32> {
		// Per-channel linear peaks of the engine's buffered audio output
		// (oakaudio's manager, clamped to the meter's 0..1 range). Silent
		// when nothing has been pushed to the output (no playback audio
		// path yet) or without an AudioManager instance.
		let Some(manager) = oak_audio::manager::instance() else {
			return vec![0.0, 0.0];
		};
		let mut peaks = [0.0f32; 8];
		let n = manager.output_levels(&mut peaks).unwrap_or(0);
		if n <= 0 {
			return vec![0.0, 0.0];
		}
		peaks[..n as usize].iter().map(|p| p.clamp(0.0, 1.0)).collect()
	}
}

// ---------------------------------------------------------------------------
// AppEngine
// ---------------------------------------------------------------------------

impl Drop for RealEngine {
	/// The pre-render windows hold worker shm slots; a dropped engine must
	/// hand them back. `ShmFrameRef` has no self-release on drop, so without
	/// this every closed project (and every test engine) permanently shrank
	/// the shared slot pool until later playback windows starved (the
	/// full-suite `playback_window_supplies_playhead_frames` failure).
	fn drop(&mut self) {
		self.cancel_preview_windows();
	}
}

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
		// A display-color transform change (mode / ICC / content space)
		// invalidates every cached image: they were produced with the old
		// transform.
		let gen = super::displaycolor::generation();
		if gen != self.display_color_gen.get() {
			self.display_color_gen.set(gen);
			cache.clear();
		}
		// The full-resolution fill replaces the proxy when its frame matches
		// the playhead; otherwise the proxy frame is displayed (rendered
		// synchronously below on a cache miss, filled by the background
		// worker once the playhead rests).
		if let Some(image) = cache.entry(monitor).or_default().image_for(frame.0) {
			return image.clone();
		}
		// M15 S2: try the pre-rendered playback window first — the frame's
		// pixels are already in a worker shm slot (zero copy: build the
		// display image from the slot bytes, then release the slot).
		if let Some((image, scope)) = self.preview_slot_frame(monitor, frame) {
			cache.entry(monitor).or_default().proxy = Some(ProxyEntry {
				frame: frame.0,
				image: image.clone(),
				scope,
			});
			return image;
		}
		// During playback a cache miss must NOT block the UI thread on a
		// synchronous render: the wait starves the tick loop that feeds
		// the pre-render window, and the seek-priority ticket steals
		// worker capacity from it, so the window never catches up (every
		// painted frame blocked in `TicketArena::wait` — the choppy
		// playback regression). Show the last displayed frame while the
		// window warms up; the workers catch up within a few frames and
		// the window then serves every playhead frame.
		if self.clock(monitor).read(cx).transport.is_playing() {
			if let Some(image) = cache
				.get(&monitor)
				.and_then(|e| e.proxy.as_ref().map(|p| p.image.clone()))
			{
				return image;
			}
		}
		// Both monitors render through the oakrender ticket arena (falling
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
		cache.entry(monitor).or_default().proxy = Some(ProxyEntry {
			frame: frame.0,
			image: image.clone(),
			scope,
		});
		image
	}

	fn scope_data(&self, monitor: Monitor, cx: &App) -> ScopeData {
		// Ensure the cache holds the current playhead frame (the analysis
		// runs inside that render pass, so this never re-walks a frame).
		let _ = self.cpu_frame(monitor, cx);
		let frame = self.clock_frame(monitor, cx);
		let cache = self.cpu_frame_cache.lock().unwrap();
		cache
			.get(&monitor)
			.and_then(|entry| entry.scope_for(frame.0))
			.cloned()
			.unwrap_or_default()
	}

	fn add_track(&mut self, kind: TrackKind, cx: &mut Context<Self>) {
		let (Some(project), Some(seq)) = (self.project.clone(), self.sequence) else {
			return;
		};
		let result = graphops::add_track(&project, seq, track_type_of(kind)).map(|_| ());
		self.apply_edit(result, "add track", cx);
	}

	fn remove_track(&mut self, index: usize, cx: &mut Context<Self>) {
		let Some(track) = self.tracks.get(index) else {
			return;
		};
		let track_id = track.track;
		let Some(project) = self.project.clone() else {
			return;
		};
		self.apply_edit(graphops::remove_track(&project, track_id), "remove track", cx);
	}

	fn set_track_height(&mut self, height: Pixels, cx: &mut Context<Self>) {
		let (Some(project), Some(seq)) = (self.project.clone(), self.sequence) else {
			return;
		};
		let internal = oak_node::track::pixel_height_to_internal_height(f32::from(height) as i32);
		// Collect the track ids under the graph lock, then release it:
		// graphops::set_track_height locks the project itself, so calling
		// it with the guard held self-deadlocks (the Cmd+= freeze).
		let ids: Vec<NodeId> = {
			let guard = graphops::lock(&project);
			[TrackType::Video, TrackType::Audio, TrackType::Subtitle]
				.into_iter()
				.flat_map(|kind| graphops::track_ids(&guard.graph, seq, kind))
				.collect()
		};
		for track in ids {
			graphops::set_track_height(&project, track, internal);
		}
		self.rebuild_timeline();
		cx.notify();
	}

	fn select_item(&mut self, id: u64, cx: &mut Context<Self>) {
		let changed = self.selected_item != Some(id);
		self.selected_item = Some(id);
		if changed {
			// The source monitor renders the selected footage node: a new
			// selection must drop the stale cached frame (the cache key only
			// tracks the playhead frame), and any in-flight full-res job for
			// the old selection is stale. The source pre-render window (M15
			// S2) rebuilds against the new footage.
			*self.source_renderer.lock().unwrap() = RendererSlot::Untried;
			self.cpu_frame_cache.lock().unwrap().remove(&Monitor::Source);
			self.full_res_generation = self.full_res_generation.wrapping_add(1);
			self.cancel_preview_window(Monitor::Source);
		}
		cx.notify();
	}

	fn set_selected_clips(&mut self, clips: Vec<ClipId>, cx: &mut Context<Self>) {
		// The effect stack targets exactly one clip: an empty or
		// multi-clip selection keeps the empty state (see
		// `EffectStackDataSource::target_label`). The node graph follows:
		// while one clip is selected it shows (and highlights) that clip's
		// context chain, so its block node becomes the graph selection.
		self.selected_clip = (clips.len() == 1).then(|| clips[0]);
		self.selected_graph_node = self.selected_clip.map(|clip| clip.0);
		cx.notify();
	}

	fn selected_graph_node(&self) -> Option<u64> {
		self.selected_graph_node
	}

	fn addable_effects(&self) -> Vec<crate::oakui::engine::EffectEntry> {
		super::effectchain::addable_effects()
	}

	fn add_effect(
		&mut self,
		index: usize,
		type_id: &str,
		cx: &mut Context<Self>,
	) -> Result<(), String> {
		let Some(project) = self.project.clone() else {
			return Err("no project open".into());
		};
		let Some(host) = self.selected_clip_node() else {
			return Err("no selected clip".into());
		};
		let result = super::effectchain::insert(&project, host, index, type_id).map(|_| ());
		self.apply_edit(result.clone(), "add effect", cx);
		result
	}

	fn effect_params(&self, effect: EffectId) -> Option<Vec<crate::oakui::engine::EffectParam>> {
		let project = self.project_ref()?;
		let node = graphops::id_of(effect.0)?;
		let guard = graphops::lock(project);
		super::effectchain::effect_params(&guard.graph, node)
	}

	fn set_effect_param(
		&mut self,
		effect: EffectId,
		input_id: &str,
		value: oak_node::value::NodeValue,
		cx: &mut Context<Self>,
	) -> Result<(), String> {
		let Some(project) = self.project.clone() else {
			return Err("no project open".into());
		};
		let Some(node) = graphops::id_of(effect.0) else {
			return Err("effect node not found".into());
		};
		let result = super::effectchain::set_input_value(&project, node, input_id, value);
		self.apply_edit(result.clone(), "set parameter", cx);
		result
	}

	fn effect_push_button(
		&mut self,
		effect: EffectId,
		input_id: &str,
		cx: &mut Context<Self>,
	) -> Result<(), String> {
		let Some(project) = self.project_ref() else {
			return Err("no project open".into());
		};
		let Some(node) = graphops::id_of(effect.0) else {
			return Err("effect node not found".into());
		};
		let guard = graphops::lock(project);
		let Some(instance) = super::effectchain::plugin_instance_handle(&guard.graph, node) else {
			return Err("not a plugin effect".into());
		};
		drop(guard);
		if !oak_plugin::node_factory::push_button_clicked(instance, input_id) {
			return Err(format!("push button \"{input_id}\" not found"));
		}
		// A button press can change the plugin's other parameters; refresh
		// the snapshots and repaint.
		cx.notify();
		Ok(())
	}

	fn ofx_interact_target(&self, _cx: &App) -> Option<u64> {
		let Some(project) = self.project_ref() else {
			return None;
		};
		let Some(host) = self.selected_clip_node() else {
			return None;
		};
		let guard = graphops::lock(project);
		for node in super::effectchain::chain(&guard.graph, host) {
			// The inspector's current selection: the first *expanded*
			// OFX plugin card (its parameter UI is on screen, so its
			// custom interact drives the viewer overlay).
			if !self.expanded_effects.contains(&node.identity()) {
				continue;
			}
			return super::effectchain::plugin_instance_handle(&guard.graph, node);
		}
		None
	}

	fn apply_effect_event(&mut self, event: &EffectStackEvent, cx: &mut Context<Self>) {
		match event {
			EffectStackEvent::EnableToggled { effect, enabled } => {
				let (Some(project), Some(_host)) =
					(self.project.clone(), self.selected_clip_node())
				else {
					cx.notify();
					return;
				};
				let Some(node) = graphops::id_of(effect.0) else {
					cx.notify();
					return;
				};
				let result = super::effectchain::set_enabled(&project, node, *enabled);
				self.apply_edit(result, "toggle effect", cx);
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
				let (Some(project), Some(host)) = (self.project.clone(), self.selected_clip_node())
				else {
					cx.notify();
					return;
				};
				let Some(node) = graphops::id_of(id.0) else {
					cx.notify();
					return;
				};
				let result = super::effectchain::remove(&project, host, node);
				self.apply_edit(result, "remove effect", cx);
			}
			EffectStackEvent::ReorderRequested { effect, new_index } => {
				let (Some(project), Some(host)) = (self.project.clone(), self.selected_clip_node())
				else {
					cx.notify();
					return;
				};
				let Some(node) = graphops::id_of(effect.0) else {
					cx.notify();
					return;
				};
				let result = super::effectchain::move_effect(&project, host, node, *new_index);
				self.apply_edit(result, "reorder effect", cx);
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
			EffectStackEvent::AddTypeRequested { index, type_id } => {
				// A drag-and-drop add from the effect library: the type is
				// already chosen (normally handled by the inspector panel;
				// applied here too so direct event drives work).
				let (index, type_id) = (*index, type_id.clone());
				let result = self.add_effect(index, &type_id, cx);
				if let Err(err) = result {
					println!("[real engine] drop-add effect failed: {err}");
					cx.notify();
				}
			}
			EffectStackEvent::CardSelected { effect } => {
				// The inspector card click selects the effect's node in the
				// node editor (the bidirectional node↔inspector link). The
				// node editor panel observes the engine and pushes the
				// highlight into the graph widget.
				self.selected_graph_node = Some(effect.0);
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
			// The node editor panel answers the right-click itself (it owns
			// the popup); the engine has nothing to apply.
			| NodeGraphEvent::NodeContextMenuRequested { .. } => {}
			NodeGraphEvent::SelectionChanged { nodes } => {
				// The node-graph selection is the inspector's shared
				// selection: a single selected node is mirrored into
				// `selected_graph_node`, and when it names an effect of the
				// targeted clip's chain its card is expanded too (so the
				// inspector shows that effect's params).
				let prev = self.selected_graph_node;
				self.selected_graph_node = (nodes.len() == 1).then(|| {
					(*nodes
						.iter()
						.next()
						.expect("a one-element set always yields an item"))
					.0
				});
				if prev != self.selected_graph_node {
					if let Some(effect) = self.selected_effect() {
						self.expanded_effects.insert(effect.0);
					}
				}
			}
			_ => {
				if let (Some(project), Some(seq)) = (self.project.clone(), self.sequence) {
					let result = crate::oakui::nodegraph::apply_edit(&project, seq, event);
					if let Err(e) = result {
						println!("[real engine] node-graph request rejected: {e}");
					}
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
				let Some(block) = self.clip_block(*clip) else {
					return;
				};
				if self.clip_track_locked(block) {
					return;
				}
				let Some(project) = self.project.clone() else {
					return;
				};
				// Re-read the clip's current range, then compute the new
				// in/out pair for the trim.
				let Some(tb) = self.time_base() else {
					return;
				};
				let (in_ts, out_ts) = {
					let guard = graphops::lock(&project);
					let Some((in_r, out_r, _)) = graphops::clip_range(&guard.graph, block) else {
						return;
					};
					(
						graphops::rational_to_ts(in_r, tb),
						graphops::rational_to_ts(out_r, tb),
					)
				};
				let (new_in, new_out) = match edge {
					TrimEdge::Start => (new_frame.0, out_ts),
					TrimEdge::End => (in_ts, new_frame.0),
				};
				let result = graphops::trim_clip(&project, block, new_in, new_out);
				self.apply_edit(result, "trim clip", cx);
			}
			TimelineEvent::ClipMoveRequested {
				clip,
				new_track,
				new_start,
			} => {
				// The dragged clip moves to `new_track`/`new_start`; every
				// clip linked to it (the A/V pair dropped from one file)
				// follows in lockstep, each staying on its own track and
				// shifting by the same frame offset. All of it lands as ONE
				// undoable entry.
				let Some(block) = self.clip_block(*clip) else {
					return;
				};
				if self.clip_track_locked(block) {
					return;
				}
				let Some(project) = self.project.clone() else {
					return;
				};
				// Linked clips on locked tracks are left in place.
				let linked: Vec<NodeId> = {
					let guard = graphops::lock(&project);
					guard
						.graph
						.links_of(block)
						.into_iter()
						.filter(|&other| graphops::clip_behavior(&guard.graph, other).is_some())
						.collect()
				};
				let linked: Vec<NodeId> = linked
					.into_iter()
					.filter(|&other| !self.clip_track_locked(other))
					.collect();
				let current_track = self
					.tracks
					.iter()
					.position(|t| t.clips.iter().any(|c| c.block == block));
				// Cross-track moves go through the gap + re-home + place
				// composition; same-track moves use the plain move command.
				let dest_track = match (current_track, self.tracks.get(*new_track)) {
					(Some(current), _) if current == *new_track => None,
					(_, Some(dest)) => Some(dest.track),
					_ => {
						self.apply_edit(
							Err("move clip: destination track out of range".to_string()),
							"move clip",
							cx,
						);
						return;
					}
				};
				let result = graphops::move_clip_with_links(
					&project,
					block,
					dest_track,
					new_start.0,
					&linked,
				);
				self.apply_edit(result, "move clip", cx);
			}
			TimelineEvent::TrackHeightChanged { track, height } => {
				if let (Some(t), Some(project)) = (self.tracks.get(*track), self.project.clone()) {
					let internal =
						oak_node::track::pixel_height_to_internal_height(f32::from(*height) as i32);
					graphops::set_track_height(&project, t.track, internal);
					self.rebuild_timeline();
				}
				cx.notify();
			}
			// Selection / zoom / transition / track-selected / context-menu:
			// not editable (the right-click is answered by the panel's popup).
			TimelineEvent::SelectionChanged
			| TimelineEvent::TrackSelected { .. }
			| TimelineEvent::TransitionChanged { .. }
			| TimelineEvent::ContextMenuRequested { .. }
			| TimelineEvent::ZoomChanged(_) => {}
			TimelineEvent::TrackToggleRequested { track, toggle } => {
				// The header toggles map onto the undoable track flag
				// setters. The muted flag doubles as the video/subtitle
				// visibility toggle (Olive parity); the model has no solo
				// flag yet, so solo requests are inert.
				let (Some(t), Some(project)) =
					(self.tracks.get(*track), self.project.clone())
				else {
					return;
				};
				let result = match toggle {
					TrackHeaderEvent::ToggleLock => {
						graphops::set_track_locked(&project, t.track, !t.locked)
					}
					TrackHeaderEvent::ToggleMute => {
						graphops::set_track_muted(&project, t.track, !t.muted)
					}
					TrackHeaderEvent::ToggleVisibility => {
						graphops::set_track_muted(&project, t.track, t.visible)
					}
					TrackHeaderEvent::ToggleSolo => Ok(()),
				};
				self.apply_edit(result, "toggle track flag", cx);
			}
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
		let (Some(block), Some(project)) = (self.clip_block(clip), self.project.clone()) else {
			return;
		};
		if self.clip_track_locked(block) {
			return;
		}
		let result = graphops::split_clip(&project, block, time.0);
		self.apply_edit(result, "split clip", cx);
	}

	fn split_at_playhead(&mut self, cx: &mut Context<Self>) {
		let frame = self.clock_frame(Monitor::Program, cx);
		let Some(project) = self.project.clone() else {
			return;
		};
		let targets: Vec<NodeId> = self
			.tracks
			.iter()
			.filter(|track| !track.locked)
			.flat_map(|track| {
				track.clips.iter().filter_map(|clip| {
					if clip.range.start.0 < frame.0 && frame.0 < clip.range.end.0 {
						Some(clip.block)
					} else {
						None
					}
				})
			})
			.collect();
		let mut result = Ok(());
		for block in targets {
			result = graphops::split_clip(&project, block, frame.0);
		}
		self.apply_edit(result, "split at playhead", cx);
	}

	fn workarea(&self) -> Option<(Frame, Frame)> {
		let wa = self.workarea.as_ref()?;
		let tb = self.time_base()?;
		let (enabled, range) = graphops::workarea_state(&wa.0)?;
		if !enabled {
			return None;
		}
		Some((
			Frame(graphops::rational_to_ts(range.in_(), tb)),
			Frame(graphops::rational_to_ts(range.out(), tb)),
		))
	}

	fn add_marker_at_playhead(&mut self, cx: &mut Context<Self>) {
		let (Some(markers), Some(tb)) = (&self.markers, self.time_base()) else {
			return;
		};
		let frame = self.clock_frame(Monitor::Program, cx);
		let time = graphops::ts_to_rational(frame.0, tb);
		let result = graphops::marker_add(&markers.0, time, "", 0);
		self.apply_edit(result, "add marker", cx);
	}

	fn remove_marker_at_playhead(&mut self, cx: &mut Context<Self>) {
		let (Some(markers), Some(tb)) = (&self.markers, self.time_base()) else {
			return;
		};
		let frame = self.clock_frame(Monitor::Program, cx);
		let time = graphops::ts_to_rational(frame.0, tb);
		// Removing a marker that is not there is a benign no-op for the menu
		// action; only rebuild on success.
		let Ok(()) = graphops::marker_remove(&markers.0, time) else {
			return;
		};
		self.apply_edit(Ok(()), "remove marker", cx);
	}

	fn set_workarea_preview(&mut self, start: Frame, end: Frame, cx: &mut Context<Self>) {
		let (Some(wa), Some(tb)) = (&self.workarea, self.time_base()) else {
			return;
		};
		// Live, non-undoable: the engine workarea tracks the drag so other
		// reads (export, snap) stay current; no timeline rebuild needed — the
		// band itself is widget-local state.
		graphops::workarea_set(
			&wa.0,
			true,
			oak_core::TimeRange::new(
				graphops::ts_to_rational(start.0, tb),
				graphops::ts_to_rational(end.0, tb),
			),
		);
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
		let (Some(wa), Some(tb)) = (&self.workarea, self.time_base()) else {
			return;
		};
		let result = graphops::workarea_set_undoable(
			&wa.0,
			true,
			oak_core::TimeRange::new(
				graphops::ts_to_rational(start.0, tb),
				graphops::ts_to_rational(end.0, tb),
			),
			oak_core::TimeRange::new(
				graphops::ts_to_rational(old_start.0, tb),
				graphops::ts_to_rational(old_end.0, tb),
			),
		);
		self.apply_edit(result, "set workarea", cx);
	}

	fn clear_workarea(&mut self, cx: &mut Context<Self>) {
		let (Some(wa), Some(tb)) = (&self.workarea, self.time_base()) else {
			return;
		};
		let (old_start, old_end) = self.workarea().unwrap_or((Frame::ZERO, Frame::ZERO));
		let result = graphops::workarea_set_undoable(
			&wa.0,
			false,
			oak_core::TimeRange::new(
				graphops::ts_to_rational(old_start.0, tb),
				graphops::ts_to_rational(old_end.0, tb),
			),
			oak_core::TimeRange::new(
				graphops::ts_to_rational(old_start.0, tb),
				graphops::ts_to_rational(old_end.0, tb),
			),
		);
		self.apply_edit(result, "clear workarea", cx);
	}

	fn delete_clip(&mut self, clip: ClipId, ripple: bool, cx: &mut Context<Self>) {
		let (Some(block), Some(project)) = (self.clip_block(clip), self.project.clone()) else {
			return;
		};
		if self.clip_track_locked(block) {
			return;
		}
		let result = if ripple {
			graphops::ripple_delete_clip(&project, block)
		} else {
			graphops::delete_clip(&project, block)
		};
		self.apply_edit(
			result,
			if ripple {
				"ripple delete clip"
			} else {
				"delete clip"
			},
			cx,
		);
	}

	/// Copy the selected clips to the engine clipboard (C++ Copy): the
	/// clipboard holds footage/range/speed/track-kind per clip, in
	/// timeline order.
	fn clipboard_copy(&mut self, clips: Vec<ClipId>, _cx: &mut Context<Self>) {
		let Some(project) = self.project.clone() else {
			return;
		};
		let blocks: Vec<NodeId> = clips
			.iter()
			.filter_map(|id| graphops::id_of(id.0))
			.collect();
		self.clipboard = graphops::copy_clips(&project, &blocks);
	}

	/// Copy then gap-delete the selected clips (C++ Cut: copy + Delete,
	/// leaving gaps — ripple deletion is the separate Ripple Delete
	/// action).
	fn clipboard_cut(&mut self, clips: Vec<ClipId>, cx: &mut Context<Self>) {
		self.clipboard_copy(clips.clone(), cx);
		for id in clips {
			self.delete_clip(id, false, cx);
		}
	}

	/// Paste the clipboard at the program playhead (C++ Paste): one
	/// undoable entry, clips keep their relative offsets and links.
	fn clipboard_paste(&mut self, cx: &mut Context<Self>) {
		let (Some(project), Some(seq)) = (self.project.clone(), self.sequence) else {
			return;
		};
		if self.clipboard.is_empty() {
			return;
		}
		let playhead = self.clock_frame(Monitor::Program, cx).0.max(0);
		let items = self.clipboard.clone();
		let result = graphops::paste_clips(&project, seq, &items, playhead).map(|_| ());
		self.apply_edit(result, "paste", cx);
	}

	fn can_undo(&self) -> bool {
		self.project.is_some() && oak_undo::global::undoable()
	}

	fn can_redo(&self) -> bool {
		self.project.is_some() && oak_undo::global::redoable()
	}

	fn undo(&mut self, cx: &mut Context<Self>) {
		if self.project.is_some() {
			oak_undo::global::undo().ok();
			self.apply_stack_change(cx);
		}
	}

	fn redo(&mut self, cx: &mut Context<Self>) {
		if self.project.is_some() {
			oak_undo::global::redo().ok();
			self.apply_stack_change(cx);
		}
	}

	fn history_entries(&self) -> Vec<super::HistoryEntry> {
		if self.project.is_none() {
			return Vec::new();
		}
		// The C++ `HistoryModel` lists every row of the engine stack (done
		// first, then the redoable tail); labels are read through the same
		// two-stage contract the C++ `oakengine_undo_command_text` uses.
		let count = oak_undo::global::count().unwrap_or(0);
		(0..count)
			.map(|row| super::HistoryEntry {
				name: oak_undo::global::command_name(row).unwrap_or_default(),
				done: oak_undo::global::command_done(row).unwrap_or(false),
			})
			.collect()
	}

	fn history_index(&self) -> i64 {
		if self.project.is_some() {
			oak_undo::global::index().unwrap_or(0)
		} else {
			0
		}
	}

	fn jump_history(&mut self, index: i64, cx: &mut Context<Self>) {
		if self.project.is_some() {
			oak_undo::global::jump(index).ok();
			self.apply_stack_change(cx);
		}
	}

	fn new_project(&mut self, cx: &mut Context<Self>) {
		let project = graphops::create_project();
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
		if self.project.is_none() {
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
		let Some(project) = self.project.clone() else {
			return Err("no project open".into());
		};
		graphops::import_footage(&project, &path)?;
		// The material bin reads the folder tree live from the graph, so a
		// notify is enough for the explorer to list the new entry.
		cx.notify();
		Ok(())
	}

	fn entry_path(&self, id: u64) -> Option<PathBuf> {
		let project = self.project.clone()?;
		let footage = graphops::id_of(id)?;
		let guard = graphops::lock(&project);
		if !guard.graph.is_valid(footage) {
			return None;
		}
		let behavior = graphops::footage_behavior(&guard.graph, footage)?;
		if behavior.filename.is_empty() {
			return None;
		}
		Some(PathBuf::from(&behavior.filename))
	}

	fn replace_footage(
		&mut self,
		id: u64,
		path: PathBuf,
		cx: &mut Context<Self>,
	) -> Result<(), String> {
		if !path.is_file() {
			return Err(format!("file does not exist: {}", path.display()));
		}
		let Some(project) = self.project.clone() else {
			return Err("no project open".into());
		};
		let Some(footage) = graphops::id_of(id) else {
			return Err("unknown project entry".into());
		};
		{
			let mut guard = graphops::lock(&project);
			if !guard.graph.is_valid(footage) {
				return Err("unknown project entry".into());
			}
			let Some(f) = guard
				.graph
				.get_mut(footage)
				.and_then(|e| e.behavior.as_any_mut())
				.and_then(|a| a.downcast_mut::<oak_node::footage::FootageBehavior>())
			else {
				return Err("entry is not footage".into());
			};
			f.filename = path.to_string_lossy().into_owned();
			// Re-probe in place; a failed probe leaves the footage invalid,
			// matching the import-time rejection behavior. (Not undoable
			// yet — the C++ replace is a single command.)
			f.probe()
				.map_err(|e| format!("failed to probe \"{}\": {e}", path.display()))?;
		}
		cx.notify();
		Ok(())
	}

	fn drop_footage(
		&mut self,
		id: u64,
		_track_kind: TrackKind,
		track_index: usize,
		time: Frame,
		cx: &mut Context<Self>,
	) {
		let (Some(project), Some(seq)) = (self.project.clone(), self.sequence) else {
			return;
		};
		// The explorer's entry id IS the footage node's stable identity
		// (`projectbrowser`).
		let Some(footage) = graphops::id_of(id) else {
			println!("[real engine] drop footage: entry {id} is not a footage node");
			return;
		};
		let (filename, video_streams, total_streams, seconds) = {
			let guard = graphops::lock(&project);
			let Some(f) = graphops::footage_behavior(&guard.graph, footage) else {
				println!("[real engine] drop footage: entry {id} is not a footage node");
				return;
			};
			(
				f.filename.clone(),
				f.video_stream_count(),
				f.total_stream_count(),
				graphops::footage_duration_seconds(&guard.graph, footage),
			)
		};
		// Media type from the probed stream list (the probe is real since
		// the import fills it); fall back to the extension only when no
		// streams were recorded (legacy projects loaded without a probe).
		let (has_video, has_audio) = if total_streams > 0 {
			(video_streams > 0, total_streams > video_streams)
		} else if crate::oakui::filename_is_audio(&filename) {
			(false, true)
		} else {
			(true, false)
		};
		// Track policy (see the `AppEngine::drop_footage` docs): use the
		// pointed display track when its kind matches, otherwise auto-select
		// the topmost track of the footage's kind; reject when there is none.
		// A video-with-audio file needs BOTH a video and an audio track —
		// missing kinds are created (the NLE convention — Premiere
		// auto-creates on drop).
		let mut ensure_track = |this: &mut Self, kind: TrackKind, cx: &mut Context<Self>| {
			if let Some(track) = this.tracks.get(track_index) {
				if track.kind == kind {
					return Some(track_index);
				}
			}
			if let Some(index) = this.tracks.iter().position(|t| t.kind == kind) {
				return Some(index);
			}
			this.add_track(kind, cx);
			match this.tracks.iter().position(|t| t.kind == kind) {
				Some(index) => Some(index),
				None => {
					println!(
						"[real engine] drop footage: could not add a {:?} track for \"{}\"",
						kind, filename
					);
					None
				}
			}
		};
		let (kind, track_index_facade) = if has_video && has_audio {
			let Some(video_target) = ensure_track(self, TrackKind::Video, cx) else {
				return;
			};
			let Some(audio_target) = ensure_track(self, TrackKind::Audio, cx) else {
				return;
			};
			let (video_index, audio_index) = (
				self.tracks[video_target].track_index,
				self.tracks[audio_target].track_index,
			);
			// Clip length: the footage's probed duration when available;
			// otherwise a 10-second default.
			let fps = self.frame_rate();
			let fps_f = fps.num as f64 / fps.den.max(1) as f64;
			let length = match seconds {
				Some(s) => (s * fps_f).round().max(1.0) as i64,
				None => (10.0 * fps_f).round().max(1.0) as i64,
			};
			let in_ts = time.0.max(0);
			// One undoable "Add Clip" entry: the video clip plus its linked
			// audio clip at the same range (the NLE A/V drop).
			let result = graphops::place_footage_clips_linked(
				&project,
				seq,
				footage,
				&[
					(TrackType::Video, video_index as usize),
					(TrackType::Audio, audio_index as usize),
				],
				in_ts,
				in_ts + length,
				0,
			)
			.map(|_| ());
			self.apply_edit(result, "drop footage", cx);
			return;
		} else {
			let footage_kind = if has_video { TrackKind::Video } else { TrackKind::Audio };
			let Some(target) = ensure_track(self, footage_kind, cx) else {
				return;
			};
			let track = &self.tracks[target];
			(track_type_of(track.kind), track.track_index)
		};
		// Clip length: the footage's probed duration when available;
		// otherwise a 10-second default.
		let fps = self.frame_rate();
		let fps_f = fps.num as f64 / fps.den.max(1) as f64;
		let length = match seconds {
			Some(s) => (s * fps_f).round().max(1.0) as i64,
			None => (10.0 * fps_f).round().max(1.0) as i64,
		};
		let in_ts = time.0.max(0);
		let result = graphops::place_footage_clip(
			&project,
			seq,
			footage,
			kind,
			track_index_facade,
			in_ts,
			in_ts + length,
			0,
		)
		.map(|_| ());
		// The placement pushes ONE undoable "Add Clip" entry; the refresh
		// also invalidates the cached rendered frames.
		self.apply_edit(result, "drop footage", cx);
	}

	/// The footage's length in sequence frames (the drop ghost's extent):
	/// the probed duration times the frame rate; `None` when the entry is
	/// not footage or was never probed.
	fn footage_length_frames(&self, id: u64) -> Option<i64> {
		let project = self.project_ref()?;
		let node = graphops::id_of(id)?;
		let seconds = {
			let guard = graphops::lock(project);
			graphops::footage_duration_seconds(&guard.graph, node)
		}?;
		let fps = self.frame_rate();
		let fps_f = fps.num as f64 / fps.den.max(1) as f64;
		Some((seconds * fps_f).round().max(1.0) as i64)
	}

	// --- project library (M13 D4) --------------------------------------

	fn storage_bound(&self) -> bool {
		self.storage
			.as_ref()
			.map(|h| graphops::storage_bound(&h.0))
			.unwrap_or(false)
	}

	fn storage_last_error(&self) -> Option<String> {
		graphops::storage_last_error(&self.storage.as_ref()?.0)
	}

	fn library_projects(&self) -> Result<Vec<LibraryProject>, String> {
		graphops::library_list()
	}

	fn library_create_project(&mut self, name: &str, cx: &mut Context<Self>) -> Result<(), String> {
		let uuid = graphops::library_create(name)?;
		self.open_library_project(&uuid, cx)
	}

	fn library_open_project(&mut self, uuid: &str, cx: &mut Context<Self>) -> Result<(), String> {
		self.open_library_project(uuid, cx)
	}

	fn library_delete_project(&mut self, uuid: &str) -> Result<(), String> {
		graphops::library_delete(uuid)
			.map_err(|e| format!("failed to delete the project: {e}"))
	}

	fn library_rename_project(&mut self, uuid: &str, name: &str) -> Result<(), String> {
		graphops::library_rename(uuid, name)
			.map_err(|e| format!("failed to rename the project: {e}"))
	}

	fn library_duplicate_project(&mut self, uuid: &str) -> Result<(), String> {
		graphops::library_duplicate(uuid)
			.map(|_| ())
			.map_err(|e| format!("failed to duplicate the project: {e}"))
	}

	fn library_import_project(&mut self, path: PathBuf) -> Result<String, String> {
		graphops::library_import(&path)
			.map_err(|e| format!("failed to import \"{}\": {e}", path.display()))
	}

	fn library_export_project(&mut self, uuid: &str, path: PathBuf) -> Result<(), String> {
		graphops::library_export(uuid, &path)
			.map_err(|e| format!("failed to export the project to \"{}\": {e}", path.display()))
	}

	fn set_use_proxy_media(&mut self, enabled: bool, cx: &mut Context<Self>) {
		oak_common::configstore::ConfigStore::instance().set(
			None,
			CONFIG_KEY_USE_PROXY,
			if enabled { "true" } else { "false" },
		);
		// Every footage's preview media may change: drop the rendered
		// frames so the next pull re-resolves original/proxy.
		self.invalidate_preview_frames(cx);
	}

	/// Sets the playback resolution divider (the viewer `Playback
	/// Resolution ▸` menu): the preview geometry changes, so every cached
	/// and in-flight preview frame is stale.
	fn set_playback_divider(&mut self, divider: i64, cx: &mut Context<Self>) {
		oak_common::configstore::ConfigStore::instance().set(
			None,
			"PlaybackDivider",
			&divider.clamp(1, 8).to_string(),
		);
		self.invalidate_preview_frames(cx);
	}

	/// The project's OCIO config override (the 项目属性 color tab; "" = the
	/// app default config).
	fn project_ocio_config(&self) -> String {
		self.project_ref()
			.map(|p| {
				graphops::lock(p)
					.settings
					.get(PROJECT_SETTING_OCIO_CONFIG)
					.cloned()
					.unwrap_or_default()
			})
			.unwrap_or_default()
	}

	fn set_project_ocio_config(&mut self, path: String, cx: &mut Context<Self>) -> Result<(), String> {
		let Some(project) = self.project.clone() else {
			return Err("no project open".to_string());
		};
		let trimmed = path.trim().to_string();
		// Validate first — the dialog stays open on Err (the C++ accept()
		// refuses an invalid config the same way). Applying is the
		// process-wide color config reload plus a full frame invalidation.
		if trimmed.is_empty() {
			oak_render::color::set_up_default_config().map_err(|e| e.to_string())?;
		} else {
			oak_render::color::set_up_default_config_from(Some(&trimmed))
				.map_err(|e| e.to_string())?;
		}
		{
			let mut guard = graphops::lock(&project);
			if trimmed.is_empty() {
				guard.settings.remove(PROJECT_SETTING_OCIO_CONFIG);
			} else {
				guard
					.settings
					.insert(PROJECT_SETTING_OCIO_CONFIG.to_string(), trimmed);
			}
			guard.modified = true;
		}
		super::displaycolor::invalidate();
		self.invalidate_rendered_frames();
		cx.notify();
		Ok(())
	}

	fn project_cache_location(&self) -> (i32, String) {
		self.project_ref()
			.map(|p| {
				let g = graphops::lock(p);
				// Clamp: the dialog's combo indexes by this value.
				(g.cache_location_setting.clamp(0, 2), g.custom_cache_path.clone())
			})
			.unwrap_or((0, String::new()))
	}

	fn set_project_cache_location(&mut self, setting: i32, custom_path: String, cx: &mut Context<Self>) {
		let Some(project) = self.project.clone() else {
			return;
		};
		{
			let mut guard = graphops::lock(&project);
			guard.cache_location_setting = setting.clamp(0, 2);
			guard.custom_cache_path = if guard.cache_location_setting == 2 {
				custom_path.trim().to_string()
			} else {
				String::new()
			};
			guard.modified = true;
		}
		cx.notify();
	}

	fn proxy_rows(&self) -> Vec<super::engine::ProxyFootageRow> {
		let Some(project) = self.project.as_ref() else {
			return Vec::new();
		};
		let guard = graphops::lock(project);
		graphops::footage_ids(&guard)
			.into_iter()
			.filter_map(|node| {
				let f = graphops::footage_behavior(&guard.graph, node)?;
				let name = graphops::node_label(&guard.graph, node);
				Some(super::engine::ProxyFootageRow {
					id: node.identity(),
					name,
					state: self.proxy_state_of(f, node),
					enabled: f.proxy_enabled,
					has_custom: f.has_custom_proxy_params(),
					can_generate: f.valid && f.streams.iter().any(|s| s.is_video),
					has_proxy: !f.proxy.is_empty(),
				})
			})
			.collect()
	}

	fn proxy_state(&self, id: u64) -> Option<super::engine::ProxyMediaState> {
		let project = self.project.as_ref()?;
		let node = graphops::id_of(id)?;
		let guard = graphops::lock(project);
		let f = graphops::footage_behavior(&guard.graph, node)?;
		Some(self.proxy_state_of(f, node))
	}

	fn proxy_row(&self, id: u64) -> Option<super::engine::ProxyFootageRow> {
		self.proxy_rows().into_iter().find(|row| row.id == id)
	}

	fn proxy_generate(&mut self, id: u64, cx: &mut Context<Self>) -> Result<(), String> {
		let footage = self.footage_of(id).ok_or("entry is not footage")?;
		if self.proxy_runs.iter().any(|run| run.footage == footage) {
			return Err("a proxy is already generating for this footage".into());
		}
		let (filename, stream_index, params) = {
			let project = self.project.as_ref().ok_or("no project open")?;
			let guard = graphops::lock(project);
			let f = graphops::footage_behavior(&guard.graph, footage)
				.ok_or("entry is not footage")?;
			if f.filename.is_empty() {
				return Err("the footage has no media file".into());
			}
			let stream = f
				.streams
				.iter()
				.find(|s| s.is_video)
				.map(|s| s.index)
				.ok_or("the footage has no video stream")?;
			(f.filename.clone(), stream, f.effective_proxy_params())
		};

		let proxy_path = oak_codec::proxymanager::ProxyManager::get_proxy_filename(
			&Self::proxy_cache_path(),
			&filename,
			stream_index,
			&params,
		)
		.map_err(|e| format!("failed to build the proxy filename: {e}"))?;

		// Absolute targets only apply in custom-size mode; divider mode
		// scales from the source (the request mirrors the C++ submission).
		let (proxy_width, proxy_height) = if params.divider <= 1 {
			(params.width, params.height)
		} else {
			(0, 0)
		};
		let request = oak_codec::task::TaskRequest {
			kind: oak_codec::task::TaskKind::Proxy,
			input_filename: &filename,
			output_filename: &proxy_path,
			stream_index,
			sample_rate: 0,
			channel_layout: 0,
			sample_format: 0,
			proxy_width,
			proxy_height,
		};
		let task_params = oak_task::proxy::ProxyParams {
			width: params.width,
			height: params.height,
			divider: params.divider,
			version: params.version,
			crf: params.crf,
			include_audio: params.include_audio != 0,
			extension: {
				let end = params
					.extension
					.iter()
					.position(|&b| b == 0)
					.unwrap_or(params.extension.len());
				String::from_utf8_lossy(&params.extension[..end]).into_owned()
			},
			preset: {
				let end = params
					.preset
					.iter()
					.position(|&b| b == 0)
					.unwrap_or(params.preset.len());
				String::from_utf8_lossy(&params.preset[..end]).into_owned()
			},
		};

		let label = format!("Generating Proxy {}", filename);
		let (tx, rx) = mpsc::channel::<super::engine::ExportEvent>();
		let mut driver = oak_task::task::Task::new(&label, None);
		{
			let tx = tx.clone();
			driver.set_event_listener(Box::new(move |event| {
				let event = match event {
					oak_task::task::TaskEvent::Started => super::engine::ExportEvent::Started,
					oak_task::task::TaskEvent::Progress(value) => {
						super::engine::ExportEvent::Progress(value)
					}
					oak_task::task::TaskEvent::Finished => return,
				};
				let _ = tx.send(event);
			}));
		}
		driver.set_behavior(Box::new(oak_task::proxy::ProxyTask::new(
			&request,
			task_params,
		)));
		std::thread::spawn(move || {
			let result = driver.start();
			let error = if result.is_ok() {
				String::new()
			} else {
				driver
					.error()
					.map(|s| s.to_string())
					.unwrap_or_else(|| "proxy generation failed".to_string())
			};
			let _ = tx.send(super::engine::ExportEvent::Finished(result.is_ok(), error));
		});

		// Mark the footage generating immediately (state 1), exactly like
		// the C++ dialog's set_proxy(..., proxy.state, ...) after
		// get_or_start; the tick drain finalizes it.
		if let Some(project) = self.project.as_ref() {
			let mut guard = graphops::lock(project);
			if let Some(f) = guard
				.graph
				.get_mut(footage)
				.and_then(|e| e.behavior.as_any_mut())
				.and_then(|a| a.downcast_mut::<oak_node::footage::FootageBehavior>())
			{
				f.set_proxy(&proxy_path, 1, stream_index, params.version, true);
			}
		}
		self.proxy_runs.push(ProxyRun {
			footage,
			label,
			progress: 0.0,
			events: rx,
		});
		cx.notify();
		Ok(())
	}

	fn proxy_task_progress(&self) -> Option<(String, f64)> {
		self.proxy_runs
			.first()
			.map(|run| (run.label.clone(), run.progress))
	}

	fn proxy_delete(&mut self, id: u64, cx: &mut Context<Self>) {
		let Some(footage) = self.footage_of(id) else {
			return;
		};
		let proxy_path = {
			let Some(project) = self.project.as_ref() else {
				return;
			};
			let guard = graphops::lock(project);
			match graphops::footage_behavior(&guard.graph, footage) {
				Some(f) if !f.proxy.is_empty() => f.proxy.clone(),
				_ => return,
			}
		};
		let _ = std::fs::remove_file(&proxy_path);
		if let Ok(working) = oak_codec::proxymanager::ProxyManager::get_working_filename(&proxy_path)
		{
			let _ = std::fs::remove_file(&working);
		}
		if let Some(project) = self.project.as_ref() {
			let mut guard = graphops::lock(project);
			if let Some(f) = guard
				.graph
				.get_mut(footage)
				.and_then(|e| e.behavior.as_any_mut())
				.and_then(|a| a.downcast_mut::<oak_node::footage::FootageBehavior>())
			{
				f.clear_proxy();
			}
		}
		self.invalidate_preview_frames(cx);
	}

	fn proxy_set_enabled(&mut self, id: u64, enabled: bool, cx: &mut Context<Self>) {
		let Some(footage) = self.footage_of(id) else {
			return;
		};
		if let Some(project) = self.project.as_ref() {
			let mut guard = graphops::lock(project);
			if let Some(f) = guard
				.graph
				.get_mut(footage)
				.and_then(|e| e.behavior.as_any_mut())
				.and_then(|a| a.downcast_mut::<oak_node::footage::FootageBehavior>())
			{
				f.proxy_enabled = enabled;
			}
		}
		self.invalidate_preview_frames(cx);
	}

	fn proxy_reveal(&self, id: u64) {
		let Some(footage) = self.footage_of(id) else {
			return;
		};
		let proxy_path = {
			let Some(project) = self.project.as_ref() else {
				return;
			};
			let guard = graphops::lock(project);
			match graphops::footage_behavior(&guard.graph, footage) {
				Some(f) if !f.proxy.is_empty() => f.proxy.clone(),
				_ => return,
			}
		};
		let path = std::path::Path::new(&proxy_path);
		if path.exists() {
			let _ = std::process::Command::new("open")
				.arg("-R")
				.arg(path)
				.spawn();
		}
	}

	fn proxy_set_custom_params(
		&mut self,
		id: u64,
		params: super::engine::ProxyParamsUi,
		cx: &mut Context<Self>,
	) {
		let Some(footage) = self.footage_of(id) else {
			return;
		};
		if let Some(project) = self.project.as_ref() {
			let mut guard = graphops::lock(project);
			if let Some(f) = guard
				.graph
				.get_mut(footage)
				.and_then(|e| e.behavior.as_any_mut())
				.and_then(|a| a.downcast_mut::<oak_node::footage::FootageBehavior>())
			{
				f.set_custom_proxy_params(params.to_codec());
			}
		}
		cx.notify();
	}

	fn proxy_clear_custom_params(&mut self, id: u64, cx: &mut Context<Self>) {
		let Some(footage) = self.footage_of(id) else {
			return;
		};
		if let Some(project) = self.project.as_ref() {
			let mut guard = graphops::lock(project);
			if let Some(f) = guard
				.graph
				.get_mut(footage)
				.and_then(|e| e.behavior.as_any_mut())
				.and_then(|a| a.downcast_mut::<oak_node::footage::FootageBehavior>())
			{
				f.clear_custom_proxy_params();
			}
		}
		cx.notify();
	}

	fn proxy_custom_params(&self, id: u64) -> Option<super::engine::ProxyParamsUi> {
		let project = self.project.as_ref()?;
		let node = graphops::id_of(id)?;
		let guard = graphops::lock(project);
		let f = graphops::footage_behavior(&guard.graph, node)?;
		f.custom_proxy_params
			.as_ref()
			.map(super::engine::ProxyParamsUi::from_codec)
	}

	fn proxy_effective_params(&self, id: u64) -> super::engine::ProxyParamsUi {
		let Some(project) = self.project.as_ref() else {
			return super::engine::proxy_params_from_config();
		};
		let Some(node) = graphops::id_of(id) else {
			return super::engine::proxy_params_from_config();
		};
		let guard = graphops::lock(project);
		match graphops::footage_behavior(&guard.graph, node) {
			Some(f) => super::engine::ProxyParamsUi::from_codec(&f.effective_proxy_params()),
			None => super::engine::proxy_params_from_config(),
		}
	}

	fn clip_footage_entries(&self, clips: &[ClipId]) -> Vec<super::engine::ProxyFootageRow> {
		let Some(project) = self.project.as_ref() else {
			return Vec::new();
		};
		let guard = graphops::lock(project);
		let mut seen: Vec<NodeId> = Vec::new();
		let mut rows: Vec<super::engine::ProxyFootageRow> = Vec::new();
		for clip in clips {
			let Some(node) = graphops::id_of(clip.0) else {
				continue;
			};
			let Some(footage) = graphops::find_input_footage(&guard.graph, node) else {
				continue;
			};
			if seen.contains(&footage) {
				continue;
			}
			let Some(f) = graphops::footage_behavior(&guard.graph, footage) else {
				continue;
			};
			seen.push(footage);
			rows.push(super::engine::ProxyFootageRow {
				id: footage.identity(),
				name: graphops::node_label(&guard.graph, footage),
				state: self.proxy_state_of(f, footage),
				enabled: f.proxy_enabled,
				has_custom: f.has_custom_proxy_params(),
				can_generate: f.valid && f.streams.iter().any(|s| s.is_video),
				has_proxy: !f.proxy.is_empty(),
			});
		}
		rows
	}

	fn sync_eligibility(&self, clips: &[ClipId]) -> super::engine::SyncEligibility {
		let mut eligibility = super::engine::SyncEligibility::default();
		let (Some(project), Some(cache)) = (self.project.as_ref(), self.waveform_cache()) else {
			return eligibility;
		};
		let guard = graphops::lock(project);
		for clip in clips {
			let Some(node) = graphops::id_of(clip.0) else {
				continue;
			};
			if graphops::clip_range(&guard.graph, node).is_none() {
				continue;
			}
			if let Some(footage) = graphops::find_input_footage(&guard.graph, node)
				.and_then(|f| graphops::footage_behavior(&guard.graph, f))
			{
				if footage.has_source_start_time {
					eligibility.source_time += 1;
				}
			}
			if let Some(waveform) = cache.get(clip.0) {
				let media_len = clip_media_length_seconds(&guard.graph, node);
				if super::waveformsync::waveform_sync_eligible(&waveform, media_len) {
					eligibility.waveform += 1;
				}
			}
		}
		eligibility
	}

	fn sync_clips_by_source_time(&mut self, clips: Vec<ClipId>, cx: &mut Context<Self>) {
		self.sync_clips_by_source_time_internal(&clips);
		cx.notify();
	}

	fn sync_clips_by_waveform(
		&mut self,
		clips: Vec<ClipId>,
		adjust_speed: bool,
		cx: &mut Context<Self>,
	) {
		self.sync_clips_by_waveform_internal(&clips, adjust_speed);
		cx.notify();
	}

	fn start_export(&mut self, format: i32, path: PathBuf) -> Result<ExportSession, String> {
		let (Some(project), Some(seq)) = (self.project.clone(), self.sequence) else {
			return Err("no sequence open".into());
		};
		let workarea = self.workarea().map(|(s, e)| (s.0, e.0));
		let params = super::renderops::encoding_params(
			&project,
			seq,
			format,
			&path,
			workarea,
			self.sequence_length().0,
		)?;
		Ok(super::renderops::spawn_export(&project, seq, params))
	}

	fn multicam_state(&self) -> Option<MulticamState> {
		self.multicam_state_internal()
	}

	fn multicam_angle_frame(&mut self, source: i32, _cx: &mut Context<Self>) -> Option<Arc<RenderImage>> {
		self.multicam_angle_frame_internal(source)
	}

	fn multicam_eligible(&self, clips: &[ClipId]) -> bool {
		let Some(project) = self.project_ref() else {
			return false;
		};
		let g = graphops::lock(project);
		for id in clips {
			let Some(block) = graphops::id_of(id.0) else {
				continue;
			};
			if graphops::clip_behavior(&g.graph, block).is_none() {
				continue;
			}
			if super::multicam::clip_connected_sequence(&g.graph, block).is_some() {
				return true;
			}
		}
		false
	}

	fn multicam_enabled_on_selection(&self, clips: &[ClipId]) -> bool {
		let Some(project) = self.project_ref() else {
			return false;
		};
		for id in clips {
			let Some(block) = graphops::id_of(id.0) else {
				continue;
			};
			if graphops::clip_behavior(&graphops::lock(project).graph, block).is_none() {
				continue;
			}
			let clip_ref = NodeRef::new(project.clone(), block);
			if oak_timeline::multicam::clip_find_multicam(&clip_ref).is_some() {
				return true;
			}
		}
		false
	}

	fn multicam_enable_selected(&mut self, clips: Vec<ClipId>, enabled: bool, cx: &mut Context<Self>) {
		let Some(project) = self.project.clone() else {
			return;
		};
		// Resolve the selected clips' block nodes. Enable additionally needs
		// each clip's connected sequence (the clip's source must be a
		// sequence — the C++ `connected_viewer()` check); disable just walks
		// every selected clip (`multicam_disable` skips clips without a
		// multicam itself).
		let mut all_clips: Vec<NodeId> = Vec::new();
		let mut eligible: Vec<(NodeId, NodeId)> = Vec::new();
		{
			let g = graphops::lock(&project);
			for id in &clips {
				let Some(block) = graphops::id_of(id.0) else {
					continue;
				};
				if graphops::clip_behavior(&g.graph, block).is_none() {
					continue;
				}
				all_clips.push(block);
				if let Some(seq) = super::multicam::clip_connected_sequence(&g.graph, block) {
					eligible.push((block, seq));
				}
			}
		}
		let result = if enabled {
			if eligible.is_empty() {
				return;
			}
			// Group the clips by their connected sequence (one enable
			// command per sequence; the common case is a single sequence).
			let mut by_seq: HashMap<NodeId, Vec<NodeRef>> = HashMap::new();
			for (block, seq) in &eligible {
				by_seq
					.entry(*seq)
					.or_default()
					.push(NodeRef::new(project.clone(), *block));
			}
			let children: Vec<_> = by_seq
				.into_iter()
				.map(|(seq, clips)| {
					oak_timeline::multicam::multicam_enable(
						clips,
						NodeRef::new(project.clone(), seq),
					)
				})
				.collect();
			let label = oak_timeline::multicam::enable_label(eligible.len());
			graphops::push_multi_command(children, &label)
		} else {
			let clip_refs: Vec<NodeRef> = all_clips
				.iter()
				.map(|block| NodeRef::new(project.clone(), *block))
				.collect();
			let label = oak_timeline::multicam::disable_label(all_clips.len());
			graphops::push_command(oak_timeline::multicam::multicam_disable(clip_refs), &label)
		};
		self.apply_edit(result, "multicam enable/disable", cx);
	}

	fn multicam_switch_to(&mut self, source: i32, split_clip: bool, cx: &mut Context<Self>) {
		let Some(project) = self.project.clone() else {
			return;
		};
		let Some(seq) = self.sequence else {
			return;
		};
		let Some(state) = self.multicam_state_internal() else {
			return;
		};
		if source < 0 || source >= state.source_count {
			return;
		}
		let Some(clip) = graphops::id_of(state.clip_id) else {
			return;
		};
		let playhead = graphops::sequence_playhead(&graphops::lock(&project).graph, seq);
		let cmd = oak_timeline::multicam::multicam_switch(
			NodeRef::new(project.clone(), clip),
			source,
			split_clip,
			playhead,
		);
		let result =
			graphops::push_command(cmd, oak_timeline::multicam::SWITCH_LABEL);
		self.apply_edit(result, "multicam switch", cx);
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
	/// Opens a `.ove` / `.ovexml` project through the module serializer.
	fn open_ove(&mut self, path: &PathBuf, cx: &mut Context<Self>) -> Result<(), String> {
		let project = graphops::load_ove(path)
			.map_err(|e| format!("failed to load \"{}\": {e}", path.display()))?;
		// The module serializer cannot parse every legacy document (e.g. the
		// `<olive>`-rooted format skips its nested `<project>` body), which
		// loads "successfully" with no content; surface it instead of
		// pretending the project opened.
		if graphops::lock(&project).graph.node_count() == 0 {
			println!(
				"[real engine] warning: \"{}\" loaded but contained no parseable content; starting from an empty project",
				path.display()
			);
		}
		self.adopt_project(project, cx);
		Ok(())
	}

	/// Opens an `.otio` / `.fcpxml` project through the oaktask interchange
	/// loader and adopts the loaded project.
	fn open_interchange(&mut self, path: &PathBuf, cx: &mut Context<Self>) -> Result<(), String> {
		let title = format!("Loading '{}'", path.display());
		let result: Arc<Mutex<Option<ProjectRef>>> = Arc::new(Mutex::new(None));
		let mut driver = oak_task::task::Task::new(&title, None);
		driver.set_behavior(Box::new(OtioLoadBehavior {
			inner: oak_task::project::loadotio::LoadOTIOTask::new(
				oak_task::project::load::ProjectLoadBaseTask::new(
					oak_task::task::Task::new(&title, None),
					path.to_string_lossy().into_owned(),
				),
			),
			result: result.clone(),
		}));
		let run = driver.start();
		let loaded = result.lock().unwrap_or_else(|e| e.into_inner()).take();
		if run.is_err() {
			let error = driver
				.error()
				.map(|s| s.to_string())
				.unwrap_or_else(|| "the task failed".to_string());
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
		let Some(project) = self.project.clone() else {
			return Err("no project open".into());
		};
		graphops::save_ove(&project, path)
			.map_err(|e| format!("failed to export the project: {e}"))?;
		// The save records the target filename (legacy save side effect);
		// refresh the display name to match.
		let (name, filename) = {
			let guard = graphops::lock(&project);
			(guard.name(), guard.filename().to_string())
		};
		if !name.is_empty() && name != "(untitled)" {
			self.project_info.name = name;
		}
		if !filename.is_empty() {
			self.project_info.path = PathBuf::from(filename);
		}
		Ok(())
	}

	/// Exports as `.otio` / `.fcpxml` through the oaktask save task.
	fn export_interchange(&mut self, path: &PathBuf, _cx: &mut Context<Self>) -> Result<(), String> {
		let Some(project) = self.project.clone() else {
			return Err("no project open".into());
		};
		let filename = path.to_string_lossy().into_owned();
		let mut driver = oak_task::task::Task::new("Saving project...", None);
		driver.set_behavior(Box::new(oak_task::project::saveotio::SaveOTIOTask {
			base: oak_task::task::Task::new("Saving project...", None),
			project,
			filename,
		}));
		if driver.start().is_err() {
			let error = driver
				.error()
				.map(|s| s.to_string())
				.unwrap_or_else(|| "the task failed".to_string());
			return Err(format!("failed to export \"{}\": {error}", path.display()));
		}
		self.project_info.path = path.clone();
		Ok(())
	}

	/// Opens the library row `uuid` through the oakstorage database backend
	/// and adopts it (the adopt binds the project to the write-through
	/// session, continuing the row's journal from its head seq).
	fn open_library_project(&mut self, uuid: &str, cx: &mut Context<Self>) -> Result<(), String> {
		let project = graphops::library_open(uuid)
			.map_err(|e| format!("failed to open the library project: {e}"))?;
		self.adopt_project(project, cx);
		// The project's module name is filename-derived ("(untitled)" for a
		// library row); display the library row name instead.
		if let Ok(rows) = graphops::library_list() {
			if let Some(row) = rows.iter().find(|row| row.uuid == uuid) {
				self.project_info.name = row.name.clone();
			}
		}
		Ok(())
	}
}

/// The OTIO load driver behavior: runs the module load task and stashes
/// the loaded project for the caller (the facade's `OtioLoadTaskBehavior`
/// pattern).
struct OtioLoadBehavior {
	/// The module load task.
	inner: oak_task::project::loadotio::LoadOTIOTask,
	/// The result slot (the loaded project).
	result: Arc<Mutex<Option<ProjectRef>>>,
}

impl oak_task::task::TaskBehavior for OtioLoadBehavior {
	fn run(&mut self, task: &mut oak_task::task::Task) -> oak_task::error::Result<()> {
		self.inner.run(task)?;
		if let Ok(project) = self.inner.base.take_project() {
			*self.result.lock().unwrap_or_else(|e| e.into_inner()) = Some(project);
		}
		Ok(())
	}
}

/// Builds the export-format list: (format id, display name, extension) from
/// the oakcodec encoding enumeration.
///
/// Pure helper so the export dialog can be unit tested.
pub fn encoding_formats() -> Vec<(i32, String, String)> {
	let mut out = Vec::new();
	for i in 0..(oak_codec::exportformat::Format::Count as i32) {
		let Some(format) = oak_codec::exportformat::Format::from_i32(i) else {
			continue;
		};
		out.push((
			i,
			oak_codec::exportformat::Format::get_name(format),
			oak_codec::exportformat::Format::get_extension(format),
		));
	}
	out
}

/// The format id of the default export container: MPEG-4 Video (`.mp4`).
pub const EXPORT_FORMAT_MP4: i32 = 2;

// ---------------------------------------------------------------------------
// Config (preferences) — the oakcommon config store directly
// ---------------------------------------------------------------------------

/// The config key selecting the renderer backend (worker `create_renderer`
/// backend id).
pub const CONFIG_KEY_RENDERER_BACKEND: &str = "GraphicsBackend";
/// The config key holding the UI theme (`"dark"` / `"light"`; the app
/// defaults to dark when the key is absent).
pub const CONFIG_KEY_THEME: &str = "Theme";
/// The config key overriding the disk cache directory (empty = the
/// platform default `<config dir>/mediacache`; honored by oakcommon's
/// `default_disk_cache_path`, so oakrender/oaknode caches follow it).
pub const CONFIG_KEY_DISK_CACHE_PATH: &str = "DiskCachePath";
/// The config key toggling proxy media use (`UseProxyMedia`, bool).
pub const CONFIG_KEY_USE_PROXY: &str = "UseProxyMedia";
/// The config key holding the explicit ffmpeg executable path the proxy
/// transcode should use (`FFmpegPath`; empty = auto-detect on PATH and
/// the common install locations).
pub const CONFIG_KEY_FFMPEG_PATH: &str = "FFmpegPath";
/// The config key holding the proxy resolution divider (`ProxyDivider`,
/// int; 1 = full resolution, 2/4/8/16 = 1/2 … 1/16). oakcodec's
/// `ProxyManager::proxy_params_from_config` reads it for generation.
pub const CONFIG_KEY_PROXY_DIVIDER: &str = "ProxyDivider";
/// The config key holding the project snapshot interval in seconds
/// (`Storage/SnapshotIntervalSec`; the write-through era's "auto-save
/// interval" — oakstorage's snapshot thread reads it every pass, default
/// 600, ≤ 0 snapshots every dirty save).
pub const CONFIG_KEY_SNAPSHOT_INTERVAL_SEC: &str = "Storage/SnapshotIntervalSec";
/// The config key holding the default transition length in seconds
/// (`DefaultTransitionLength`, decimal string; consumed by the engine's
/// add-default-transition command — currently a module stub).
pub const CONFIG_KEY_DEFAULT_TRANSITION_SEC: &str = "DefaultTransitionLength";
/// The config key holding the audio output device NAME (empty = system
/// default; C++ parity `AudioOutput`).
pub const CONFIG_KEY_AUDIO_OUTPUT: &str = "AudioOutput";
/// The config key holding the audio input device NAME (`AudioInput`).
pub const CONFIG_KEY_AUDIO_INPUT: &str = "AudioInput";

/// The default snapshot interval (seconds), mirroring oakstorage's
/// compiled-in default.
pub const DEFAULT_SNAPSHOT_INTERVAL_SEC: i64 = 600;
/// The default transition length (seconds).
pub const DEFAULT_TRANSITION_SEC: &str = "0.5";

/// The process-wide config store.
fn config_store() -> &'static oak_common::configstore::ConfigStore {
	oak_common::configstore::ConfigStore::instance()
}

/// Loads the persisted configuration from disk (once at startup, before
/// any preference is read).
pub fn config_load() {
	config_store().load().ok();
}

/// Persists the configuration to disk (the app calls it on exit).
pub fn config_save() {
	config_store().save().ok();
}

/// Reads a config string from the store (empty when missing).
pub fn config_get_string(key: &str) -> String {
	config_store().get(None, key).unwrap_or_default()
}

/// Writes a config string to the store.
pub fn config_set_string(key: &str, value: &str) {
	config_store().set(None, key, value);
}

/// Reads a config integer from the store (`default` when the key is
/// missing or not an integer).
pub fn config_get_int(key: &str, default: i64) -> i64 {
	config_store().get_int64(None, key, default)
}

/// Writes a config integer to the store.
pub fn config_set_int(key: &str, value: i64) {
	config_store().set_int64(None, key, value);
}

/// Reads a config boolean through the string accessor (the store parses
/// `"true"`/`"false"` for registered bool keys).
pub fn config_get_bool(key: &str, default: bool) -> bool {
	match config_get_string(key).as_str() {
		"true" => true,
		"false" => false,
		_ => default,
	}
}

/// Writes a config boolean (see [`config_get_bool`]).
pub fn config_set_bool(key: &str, value: bool) {
	config_set_string(key, if value { "true" } else { "false" });
}

/// The renderer backends offered in the preferences dialog, in display
/// order. The first entry is the built-in default.
pub fn renderer_backends() -> Vec<&'static str> {
	vec!["opengl", "metal", "vulkan", "none"]
}

/// The proxy resolution dividers offered in the preferences dialog, in
/// display order (1 = full resolution).
pub fn proxy_dividers() -> Vec<i64> {
	vec![1, 2, 4, 8, 16]
}

/// Whether the persisted theme is dark (the default).
pub fn theme_is_dark() -> bool {
	config_get_string(CONFIG_KEY_THEME) != "light"
}

/// Persists the theme choice (applied live by the caller).
pub fn set_theme_dark(dark: bool) {
	config_set_string(CONFIG_KEY_THEME, if dark { "dark" } else { "light" });
}

// ---------------------------------------------------------------------------
// Audio devices (preferences + startup wiring) — oakaudio's manager directly
// ---------------------------------------------------------------------------

/// The host's audio output device names in enumeration order (the index is
/// what the manager's `set_output_device` takes).
pub fn audio_output_devices() -> Vec<String> {
	oak_audio::manager::output_device_names()
}

/// The host's audio input device names (see [`audio_output_devices`]).
pub fn audio_input_devices() -> Vec<String> {
	oak_audio::manager::input_device_names()
}

/// Selects the output device by NAME (empty = system default), persists the
/// choice to `AudioOutput`, and applies it live: the output stream reopens
/// on the device with the next pushed samples.
pub fn set_audio_output_device(name: &str) {
	config_set_string(CONFIG_KEY_AUDIO_OUTPUT, name);
	let index = if name.is_empty() {
		-1
	} else {
		oak_audio::manager::device_index_by_name(name, true).unwrap_or(-1)
	};
	if let Some(mut manager) = oak_audio::manager::instance() {
		manager.set_output_device(index).ok();
	}
}

/// Selects the input device by NAME (empty = system default) and persists
/// the choice to `AudioInput` (used by recording).
pub fn set_audio_input_device(name: &str) {
	config_set_string(CONFIG_KEY_AUDIO_INPUT, name);
	let index = if name.is_empty() {
		-1
	} else {
		oak_audio::manager::device_index_by_name(name, false).unwrap_or(-1)
	};
	if let Some(mut manager) = oak_audio::manager::instance() {
		manager.set_input_device(index).ok();
	}
}

/// The configured output device name, validated against the live
/// enumeration (empty when the configured device is gone).
pub fn audio_output_device() -> String {
	let name = config_get_string(CONFIG_KEY_AUDIO_OUTPUT);
	if name.is_empty() || audio_output_devices().iter().any(|n| *n == name) {
		name
	} else {
		String::new()
	}
}

/// The configured input device name (see [`audio_output_device`]).
pub fn audio_input_device() -> String {
	let name = config_get_string(CONFIG_KEY_AUDIO_INPUT);
	if name.is_empty() || audio_input_devices().iter().any(|n| *n == name) {
		name
	} else {
		String::new()
	}
}

/// Brings up the AudioManager singleton and applies the persisted device
/// choices. Called once at startup; without an instance `push_to_output`
/// fails silently and playback stays video-only.
pub fn audio_init_from_config() {
	oak_audio::manager::ManagerInner::create_instance().ok();
	let output = config_get_string(CONFIG_KEY_AUDIO_OUTPUT);
	if !output.is_empty() {
		set_audio_output_device(&output);
	}
	let input = config_get_string(CONFIG_KEY_AUDIO_INPUT);
	if !input.is_empty() {
		set_audio_input_device(&input);
	}
}

// ---------------------------------------------------------------------------
// Project library (M13 D4: the write-through database the manager browses)
// ---------------------------------------------------------------------------

/// The config key selecting the storage backend (see
/// `crates/oakstorage/src/writethrough.rs`).
pub const CONFIG_KEY_STORAGE_BACKEND: &str = "Storage/Backend";

/// Enables the SQLite write-through library unless the user configured the
/// backend explicitly (any existing value — including "off" — wins over the
/// app's default). The library path defaults to `<system data
/// directory>/library.db`.
pub fn configure_storage() {
	if config_get_string(CONFIG_KEY_STORAGE_BACKEND).is_empty() {
		config_set_string(CONFIG_KEY_STORAGE_BACKEND, "sqlite");
	}
}

/// Flushes every bound project (write-through + snapshot) and stops the
/// snapshot thread. The app calls this on exit.
pub fn storage_flush() {
	graphops::storage_flush();
}

/// The library rows, most recently modified first (the project manager's
/// data source).
pub fn library_list() -> Result<Vec<LibraryProject>, String> {
	graphops::library_list()
}

#[cfg(test)]
mod tests {
	use super::*;
	use std::sync::mpsc as std_mpsc;
	use std::time::Duration;

	/// Serializes the media/FFmpeg-heavy tests (the codec library is not
	/// thread-safe against concurrent decode sessions) and shares the
	/// process-global undo stack with the other app test modules.
	fn media_lock() -> std::sync::MutexGuard<'static, ()> {
		crate::oakui::graphops::test_lock()
	}

	/// Points the render manager's worker resolution at the built
	/// oak-worker binary (dev layout `target/debug/oak-worker`) for tests
	/// that render through the process backend. The default resolver looks
	/// next to the *test* binary (`target/debug/deps`), which holds no
	/// oak-worker. Restored on drop; only used inside [`media_lock`]
	/// sections so the process env stays serialized.
	struct WorkerBinGuard {
		prev: Option<String>,
	}
	impl WorkerBinGuard {
		fn set() -> Self {
			let prev = std::env::var("OAK_WORKER_BIN").ok();
			// The worker binary lives in the workspace-root target dir (the
			// app crate is at crates/oak-app, not the repo root).
			let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
				.join("../../target/debug/oak-worker");
			std::env::set_var("OAK_WORKER_BIN", path);
			Self { prev }
		}
	}
	impl Drop for WorkerBinGuard {
		fn drop(&mut self) {
			match &self.prev {
				Some(p) => std::env::set_var("OAK_WORKER_BIN", p),
				None => std::env::remove_var("OAK_WORKER_BIN"),
			}
		}
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

	/// Serializes the config round-trip tests: the config store is a
	/// process-global store, so tests mutating the same keys must not run
	/// concurrently (shared with the other app test modules).
	fn config_lock() -> std::sync::MutexGuard<'static, ()> {
		crate::oakui::graphops::test_lock()
	}

	/// Restores `key`'s original value when dropped, so a round-trip test
	/// never leaks a preference into the rest of the test process.
	struct ConfigRestore(&'static str, String);

	impl ConfigRestore {
		fn of(key: &'static str) -> Self {
			ConfigRestore(key, config_get_string(key))
		}
	}

	impl Drop for ConfigRestore {
		fn drop(&mut self) {
			config_set_string(self.0, &self.1);
		}
	}

	/// Every preferences-dialog key round-trips through the config store:
	/// the value written is the value read back.
	#[test]
	fn preferences_keys_round_trip_through_the_config() {
		let _guard = config_lock();

		// String-valued keys (theme, cache dir, transition seconds, audio
		// device names, renderer backend).
		let _theme = ConfigRestore::of(CONFIG_KEY_THEME);
		config_set_string(CONFIG_KEY_THEME, "light");
		assert_eq!(config_get_string(CONFIG_KEY_THEME), "light");
		assert!(!theme_is_dark());
		config_set_string(CONFIG_KEY_THEME, "dark");
		assert!(theme_is_dark());

		let _cache = ConfigRestore::of(CONFIG_KEY_DISK_CACHE_PATH);
		config_set_string(CONFIG_KEY_DISK_CACHE_PATH, "/tmp/oak-test-cache");
		assert_eq!(
			config_get_string(CONFIG_KEY_DISK_CACHE_PATH),
			"/tmp/oak-test-cache"
		);

		let _transition = ConfigRestore::of(CONFIG_KEY_DEFAULT_TRANSITION_SEC);
		config_set_string(CONFIG_KEY_DEFAULT_TRANSITION_SEC, "1.5");
		assert_eq!(
			config_get_string(CONFIG_KEY_DEFAULT_TRANSITION_SEC),
			"1.5"
		);

		let _output = ConfigRestore::of(CONFIG_KEY_AUDIO_OUTPUT);
		config_set_string(CONFIG_KEY_AUDIO_OUTPUT, "Test Speakers");
		assert_eq!(config_get_string(CONFIG_KEY_AUDIO_OUTPUT), "Test Speakers");

		let _backend = ConfigRestore::of(CONFIG_KEY_RENDERER_BACKEND);
		config_set_string(CONFIG_KEY_RENDERER_BACKEND, "metal");
		assert_eq!(config_get_string(CONFIG_KEY_RENDERER_BACKEND), "metal");

		// The bool key parses through the string accessor (the store's
		// registered bool entry accepts "true"/"false").
		let _proxy = ConfigRestore::of(CONFIG_KEY_USE_PROXY);
		config_set_bool(CONFIG_KEY_USE_PROXY, false);
		assert!(!config_get_bool(CONFIG_KEY_USE_PROXY, true));
		config_set_bool(CONFIG_KEY_USE_PROXY, true);
		assert!(config_get_bool(CONFIG_KEY_USE_PROXY, false));

		// The int keys round-trip through the int accessors (and read back
		// as strings too — the store serializes typed values).
		config_set_int(CONFIG_KEY_PROXY_DIVIDER, 4);
		assert_eq!(config_get_int(CONFIG_KEY_PROXY_DIVIDER, 1), 4);
		assert_eq!(config_get_string(CONFIG_KEY_PROXY_DIVIDER), "4");
		config_set_int(CONFIG_KEY_PROXY_DIVIDER, 1);

		config_set_int(CONFIG_KEY_SNAPSHOT_INTERVAL_SEC, 120);
		assert_eq!(config_get_int(CONFIG_KEY_SNAPSHOT_INTERVAL_SEC, 600), 120);
		config_set_int(
			CONFIG_KEY_SNAPSHOT_INTERVAL_SEC,
			DEFAULT_SNAPSHOT_INTERVAL_SEC,
		);
	}

	/// The audio device enumeration crosses oakaudio's manager without
	/// crashing; the output and input lists are independent (either may be
	/// empty on a headless box).
	#[test]
	fn audio_device_enumeration_is_stable() {
		let outputs = audio_output_devices();
		let inputs = audio_input_devices();
		assert!(outputs.iter().all(|n| !n.is_empty()));
		assert!(inputs.iter().all(|n| !n.is_empty()));
		// An unknown device name validates to "system default" (empty).
		let _guard = config_lock();
		let _output = ConfigRestore::of(CONFIG_KEY_AUDIO_OUTPUT);
		config_set_string(CONFIG_KEY_AUDIO_OUTPUT, "No Such Device");
		assert_eq!(audio_output_device(), "");
	}

	/// The snapshot-interval key is the one oakstorage's snapshot thread
	/// reads (`Storage/SnapshotIntervalSec`, see crates/oakstorage/src/
	/// writethrough.rs) — a rename here would silently disconnect the dialog.
	#[test]
	fn snapshot_interval_key_matches_the_storage_module() {
		assert_eq!(CONFIG_KEY_SNAPSHOT_INTERVAL_SEC, "Storage/SnapshotIntervalSec");
	}

	/// End-to-end through the module crates: a project the engine itself
	/// writes (save → load round-trip) keeps its identity, and the
	/// in-memory sequence the app drives carries real tracks.
	///
	/// NOTE: the direct-rlib app keeps the sequence IN the project's graph
	/// (the facade kept it in a scratch project), so the saved file now
	/// carries the sequence and its tracks.
	#[test]
	fn real_project_save_load_round_trip() {
		let _media = media_lock();
		let project = graphops::create_project();
		let seq = graphops::create_sequence(&project, "Round Trip");
		let v = graphops::add_track(&project, seq, TrackType::Video).expect("add a video track");
		let a = graphops::add_track(&project, seq, TrackType::Audio).expect("add an audio track");
		// add_track returns the NEW track's index: with the default 2V+2A
		// layout those are the third video and third audio track.
		assert_eq!((v, a), (2, 2), "in-memory tracks");
		oak_undo::global::clear().unwrap();

		// Save as uncompressed `.ovexml` (the module serializer reads plain
		// XML).
		let save_path =
			std::env::temp_dir().join(format!("oakapp_roundtrip_{}.ovexml", std::process::id()));
		graphops::save_ove(&project, &save_path).expect("save");
		assert!(save_path.exists());

		// Load it back through the same path the app uses: the file loads
		// and the project identity round-trips.
		let loaded = graphops::load_ove(&save_path).expect("load");
		let (loaded_name, sequences) = {
			let guard = graphops::lock(&loaded);
			(graphops::project_name(&guard), graphops::sequence_ids(&guard))
		};
		assert!(!loaded_name.is_empty(), "the loaded project has a name");
		assert_eq!(sequences.len(), 1, "the sequence survives the round-trip");
		let (video, audio) = {
			let guard = graphops::lock(&loaded);
			(
				graphops::track_ids(&guard.graph, sequences[0], TrackType::Video).len(),
				graphops::track_ids(&guard.graph, sequences[0], TrackType::Audio).len(),
			)
		};
		assert_eq!(
			(video, audio),
			(3, 3),
			"the default 2V+2A layout plus the two added tracks survive the round-trip"
		);

		let _ = std::fs::remove_file(&save_path);
	}

	/// Legacy-project regression: the C++ fixture
	/// `tests/project_with_footage.ove` has no `<streams>` segment and no
	/// `<filename>` element (the media path lives only in the footage's
	/// `file_in` input, relative to the project directory). Loading it
	/// used to leave the footage unprobed, so `source_length()` was 0 and
	/// the source monitor pinned its playhead at frame 0 (the
	/// `RealClock::tick` `length=0` report): the `load_custom` file_in
	/// fallback plus `reprobe_unprobed_footage` (the adopt_project probe
	/// cascade) restore the stream inventory and the duration.
	#[test]
	fn legacy_footage_is_reprobed_after_load() {
		let _media = media_lock();
		let fixture = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
			.join("tests/project_with_footage.ove");
		let project = graphops::load_ove(&fixture).expect("the C++ fixture loads");

		let ids = graphops::footage_ids(&graphops::lock(&project));
		assert!(!ids.is_empty(), "the fixture carries footage");
		{
			let guard = graphops::lock(&project);
			for &id in &ids {
				assert_eq!(
					graphops::footage_duration_seconds(&guard.graph, id),
					None,
					"legacy footage loads without a probed duration"
				);
			}
		}

		graphops::reprobe_unprobed_footage(&project);

		let guard = graphops::lock(&project);
		for &id in &ids {
			let secs = graphops::footage_duration_seconds(&guard.graph, id)
				.expect("the reprobe restores a duration");
			assert!(secs > 0.5, "the fixture's demo.mp4 has a real duration: {secs}");
		}
	}

	/// End-to-end CPU render through the same path
	/// [`RealEngine::render_program_frame`] uses: with the render manager
	/// up (the default M15 S2 process backend), rendering an in-memory
	/// sequence produces a real BGRA8 frame in a shared-memory slot at the
	/// requested proxy geometry, the display image builds straight from
	/// the slot bytes (zero copy), and the picture is well-formed. The
	/// sequence starts empty, so the picture is black; with a clip of real
	/// media on the video track the same render produces the decoded
	/// footage (known content, non-black).
	#[test]
	fn real_render_frame_e2e() {
		let _media = media_lock();
		let _worker = WorkerBinGuard::set();
		if !crate::oakui::renderops::ensure_render_manager() {
			panic!("the render manager failed to start");
		}

		let project = graphops::create_project();
		let seq = graphops::create_sequence(&project, "Render E2E");
		let tb = graphops::sequence_time_base(&graphops::lock(&project).graph, seq)
			.expect("the sequence has a frame rate");

		// The app's proxy size: sequence aspect (default 1920x1080) scaled
		// to a 480px long edge.
		let frame = crate::oakui::renderops::render_sequence_frame(&project, seq, 0, tb, 480, 270)
			.expect("render_frame must produce a frame");
		assert_eq!((frame.width(), frame.height()), (480, 270));
		assert!(
			frame.is_shm(),
			"the default process backend delivers shm slots (got format {})",
			frame.format()
		);
		let (image, _scope) = frame.to_display().expect("display image from the slot");
		let bytes = image.as_bytes(0).expect("one frame");
		assert_eq!(bytes.len(), 480 * 270 * 4, "BGRA8 proxy geometry");
		assert!(
			bytes.iter().all(|&b| b == 0),
			"an empty sequence renders transparent black"
		);
		release_rendered_frame(&frame);

		// M12 P0: with a clip of real media on the video track, the same
		// render must produce the decoded footage (known content, non
		// black). The media is program-generated.
		let media = std::env::temp_dir().join(format!(
			"oakapp_e2e_media_{}.mp4",
			std::process::id()
		));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10)
			.expect("generate e2e test media");
		let footage = graphops::import_footage(&project, &media).expect("import_footage");
		graphops::add_track(&project, seq, TrackType::Video).expect("add a video track");
		// Clip covering [0, 10) frames at the sequence's rate.
		graphops::place_footage_clip(&project, seq, footage, TrackType::Video, 0, 0, 10, 0)
			.expect("clip placement");
		let frame = crate::oakui::renderops::render_sequence_frame(&project, seq, 0, tb, 480, 270)
			.expect("render_frame with a clip must produce a frame");
		let (image, _scope) = frame.to_display().expect("display image from the slot");
		let bytes = image.as_bytes(0).expect("one frame");
		let nonzero = bytes.chunks(4).filter(|px| px[..3].iter().any(|&c| c != 0)).count();
		assert!(
			nonzero > 0,
			"the sequence with a footage clip must render non-black pixels"
		);
		// Known content: the test clip's left half is red on frame 0 —
		// the center-left pixel must be red-dominant (BGRA bytes: R at 2).
		let px = |x: usize, y: usize| &bytes[(y * 480 + x) * 4..][..4];
		let center_left = px(120, 135);
		assert!(
			center_left[2] > 127 && center_left[0] < 102 && center_left[1] < 102,
			"center-left pixel stays red from the decoded clip: {center_left:?}"
		);
		release_rendered_frame(&frame);

		// A second frame at a later timestamp renders too.
		assert!(crate::oakui::renderops::render_sequence_frame(&project, seq, 30, tb, 480, 270).is_ok());

		// Invalid geometry is rejected.
		assert!(crate::oakui::renderops::render_sequence_frame(&project, seq, 0, tb, 0, 270).is_err());

		oak_undo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}

	/// M15 S2 acceptance: the app's onscreen path reads the process
	/// backend's shm slots without the counted main-process copy. The
	/// preview path (`RenderedFrame::to_display`) wraps the slot bytes
	/// into the display buffer — the GPU-upload staging copy — and must
	/// leave `main_heap_frame_copies == 0`; the long-lived full-res path
	/// (`rendered_to_owned_image`) is the one counted copy
	/// (`slot_to_vec`), released right after.
	#[test]
	fn process_backend_preview_path_is_zero_copy() {
		let _media = media_lock();
		let _worker = WorkerBinGuard::set();
		use oak_render::manager::{RenderBackendChoice, RenderManager};
		use oak_render::procpool::{
			main_heap_frame_copies, reset_main_heap_frame_copies, DispatcherConfig,
		};
		RenderManager::shutdown();
		let config = DispatcherConfig {
			worker_bin: Some(
				std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
					.join("../../target/debug/oak-worker"),
			),
			workers: 1,
			slots_per_worker: 2,
			width: 64,
			height: 64,
			batch_size: 2,
			..Default::default()
		};
		RenderManager::init_with_backend(RenderBackendChoice::Processes(config))
			.expect("process backend init");

		let project = graphops::create_project();
		let seq = graphops::create_sequence(&project, "Zero Copy");
		let tb = graphops::sequence_time_base(&graphops::lock(&project).graph, seq).unwrap();

		reset_main_heap_frame_copies();
		let frame = crate::oakui::renderops::render_sequence_frame(&project, seq, 0, tb, 64, 64)
			.expect("render_frame must produce a frame");
		let crate::oakui::renderops::RenderedFrame::Shm(slot) = &frame else {
			panic!("the process backend must deliver a shm slot");
		};
		assert_eq!(slot.meta.format, crate::oakui::renderops::SLOT_FORMAT_BGRA8);
		let (image, _scope) = frame.to_display().expect("display image from the slot");
		assert_eq!(image.as_bytes(0).expect("one frame").len(), 64 * 64 * 4);
		assert_eq!(
			main_heap_frame_copies(),
			0,
			"the preview path must not copy through slot_to_vec"
		);
		release_rendered_frame(&frame);
		assert_eq!(main_heap_frame_copies(), 0);

		// The long-lived full-res path is the one counted copy.
		let frame = crate::oakui::renderops::render_sequence_frame(&project, seq, 0, tb, 64, 64)
			.expect("render_frame must produce a frame");
		let image = rendered_to_owned_image(&frame).expect("owned full-res image");
		assert_eq!(image.as_bytes(0).expect("one frame").len(), 64 * 64 * 4);
		assert_eq!(
			main_heap_frame_copies(),
			1,
			"the long-lived cache path is the counted copy"
		);
		release_rendered_frame(&frame);

		RenderManager::shutdown();
		oak_undo::global::clear().unwrap();
	}

	/// M12 P3 acceptance: importing a media file makes it appear in the
	/// project browser's real folder tree.
	#[test]
	fn real_project_browser_lists_imported_footage() {
		let _media = media_lock();
		let project = graphops::create_project();

		// Generate a real media file, import it.
		let media = std::env::temp_dir().join(format!(
			"oakapp_browser_{}.mp4",
			std::process::id()
		));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10)
			.expect("generate test media");
		graphops::import_footage(&project, &media).expect("import must succeed");

		// The project browser (ProjectDataSource) must list it.
		let roots = crate::oakui::projectbrowser::roots(&project);
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
		let node = crate::oakui::projectbrowser::find_by_identity(&project, entry.id);
		assert!(node.is_some(), "selection resolves to a node");

		oak_undo::global::clear().unwrap();
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
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10)
			.expect("generate e2e test media");
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

	/// The material-bin thumbnail pipeline end to end: importing a real media
	/// file (tests/demo.mp4) lists a footage entry whose icon-view thumbnail
	/// is rendered on a background worker, drained on the tick, and cached as
	/// a PNG on disk keyed by the media filename — the entry eventually
	/// carries a `thumbnail` path that resolves to an existing file (the
	/// engine half of the icon-view img chain; the widget half is covered by
	/// `icon_view_renders_thumbnail_img_or_placeholder`).
	#[gpui::test]
	async fn real_engine_thumbnail_pipeline_produces_png(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let _worker = WorkerBinGuard::set();
		if !crate::oakui::renderops::ensure_render_manager() {
			panic!("the render manager failed to start");
		}
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));

		let media = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/demo.mp4");
		let imported = cx
			.update(|app| engine.update(app, |engine, cx| engine.import_footage(media.clone(), cx)));
		assert!(imported.is_ok(), "import tests/demo.mp4: {imported:?}");
		let name = media.file_name().unwrap().to_string_lossy().into_owned();

		// The core render step runs synchronously: the first frame decodes to
		// a PNG in the shared thumbnail directory.
		let entry_id = cx
			.read(|app| {
				engine
					.read(app)
					.roots()
					.into_iter()
					.find(|e| e.name.as_ref() == name)
					.expect("the imported footage is listed")
					.id
			});
		let project = cx.read(|app| engine.read(app).project_ref().cloned().unwrap());
		let rendered = RealEngine::render_thumbnail(&project, entry_id);
		let rendered = rendered
			.expect("render_thumbnail produces a PNG path for the real media");
		assert!(
			rendered.exists(),
			"the rendered PNG exists: {}",
			rendered.display()
		);

		// The async path installs it: a fresh engine (no cached done entries)
		// spawns the worker on `roots()`, and the drain installs the completed
		// path into the cache so the entry re-reads with the thumbnail.
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.import_footage(media.clone(), cx)
			})
		})
		.expect("re-import the footage");
		// Progress criterion, not wall time: the worker's async thumbnail
		// installs after a bounded number of engine pumps (a wall-clock
		// cap would fail on slow machines for machine speed).
		let mut pumps = 0usize;
		loop {
			pumps += 1;
			let thumbnail = cx.read(|app| {
				engine
					.read(app)
					.roots()
					.into_iter()
					.find(|e| e.name.as_ref() == name)
					.and_then(|e| e.thumbnail.clone())
			});
			if let Some(thumbnail) = thumbnail {
				assert!(
					std::path::PathBuf::from(thumbnail.as_ref()).exists(),
					"the attached thumbnail resolves to a file on disk"
				);
				break;
			}
			assert!(
				pumps < 5000,
				"the entry eventually carries a thumbnail path (after {pumps} pumps)"
			);
			cx.update(|app| engine.update(app, |engine, _cx| engine.drain_thumbnails()));
			std::thread::sleep(Duration::from_millis(10));
		}
		// Leave the shared cache clean for the next run.
		let _ = std::fs::remove_file(&rendered);
	}

	/// Track header toggles through the app seam: a `TrackToggleRequested`
	/// event lands as ONE undoable engine command, the timeline snapshot
	/// reflects the new flag, and undo restores it. Visibility maps onto the
	/// track's muted flag (Olive parity).
	#[gpui::test]
	async fn real_engine_track_toggles_are_undoable(cx: &mut gpui::TestAppContext) {
		use gpui::timeline::TrackHeaderEvent;
		let _media = media_lock();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.add_track(TrackKind::Video, cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.add_track(TrackKind::Audio, cx)));

		// Display order: video tracks first (index 0), then audio (index 1).
		assert!(cx.read(|app| engine.read(app).track(0).expect("V1").is_visible()));
		assert!(!cx.read(|app| engine.read(app).track(1).expect("A1").is_muted()));

		let toggle = |cx: &mut gpui::TestAppContext, track: usize, t: TrackHeaderEvent| {
			cx.update(|app| {
				engine.update(app, |engine, cx| {
					engine.apply_timeline_event(
						&TimelineEvent::TrackToggleRequested { track, toggle: t },
						cx,
					);
				})
			});
		};
		toggle(cx, 0, TrackHeaderEvent::ToggleVisibility);
		assert!(
			!cx.read(|app| engine.read(app).track(0).expect("V1").is_visible()),
			"the visibility toggle hides the video track"
		);
		toggle(cx, 0, TrackHeaderEvent::ToggleLock);
		assert!(cx.read(|app| engine.read(app).track(0).expect("V1").is_locked()));
		toggle(cx, 1, TrackHeaderEvent::ToggleMute);
		assert!(cx.read(|app| engine.read(app).track(1).expect("A1").is_muted()));

		// Three toggle commands, three undos.
		cx.update(|app| engine.update(app, |engine, cx| engine.undo(cx)));
		assert!(!cx.read(|app| engine.read(app).track(1).expect("A1").is_muted()));
		cx.update(|app| engine.update(app, |engine, cx| engine.undo(cx)));
		assert!(!cx.read(|app| engine.read(app).track(0).expect("V1").is_locked()));
		cx.update(|app| engine.update(app, |engine, cx| engine.undo(cx)));
		assert!(cx.read(|app| engine.read(app).track(0).expect("V1").is_visible()));
	}

	/// The history panel's engine surface mirrors the C++ `HistoryWidget`
	/// over the real stack: rows track every pushed command, `done` flags
	/// gray the redoable tail, and `jump_history` walks the stack both
	/// ways (a row click's `row + 1` target).
	#[gpui::test]
	async fn real_engine_history_tracks_the_undo_stack(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));

		// A fresh project leaves the bottom "New/Open Project" command on
		// the stack: one row, the pointer at 1.
		let base = cx.read(|app| engine.read(app).history_entries().len());
		assert!(base >= 1, "the stack always lists the bottom command");
		assert_eq!(cx.read(|app| engine.read(app).history_index()), base as i64);

		// Two undoable edits add two labeled done rows.
		cx.update(|app| engine.update(app, |engine, cx| engine.add_track(TrackKind::Video, cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.add_track(TrackKind::Audio, cx)));
		let entries = cx.read(|app| engine.read(app).history_entries());
		assert_eq!(entries.len(), base + 2);
		assert!(entries.iter().all(|e| e.done), "fresh rows are done");
		assert!(
			entries[base].name.is_empty() == false && entries[base + 1].name.is_empty() == false,
			"edit rows carry their command labels"
		);
		assert_eq!(cx.read(|app| engine.read(app).history_index()), (base + 2) as i64);

		// Undo grays the newest row (it joins the redoable tail).
		cx.update(|app| engine.update(app, |engine, cx| engine.undo(cx)));
		let entries = cx.read(|app| engine.read(app).history_entries());
		assert!(!entries.last().unwrap().done, "undone row stays listed");
		assert_eq!(cx.read(|app| engine.read(app).history_index()), (base + 1) as i64);

		// A jump to the bottom undoes everything below the base command;
		// the rows stay listed (gray), matching the C++ jump semantics.
		cx.update(|app| engine.update(app, |engine, cx| engine.jump_history(base as i64, cx)));
		assert_eq!(cx.read(|app| engine.read(app).history_index()), base as i64);
		assert!(
			cx.read(|app| !engine.read(app).can_undo()),
			"nothing to undo at the bottom command"
		);

		// Jumping forward redoes both edits in order.
		cx.update(|app| {
			engine.update(app, |engine, cx| engine.jump_history((base + 2) as i64, cx))
		});
		assert_eq!(cx.read(|app| engine.read(app).history_index()), (base + 2) as i64);
		let entries = cx.read(|app| engine.read(app).history_entries());
		assert!(entries.iter().all(|e| e.done), "the redo restored every row");

		oak_undo::global::clear().unwrap();
	}

	/// The multicam switch through the UI path (`multicam_switch_to`): it
	/// lands on the global undo stack as ONE entry and the engine's
	/// undo/redo round-trip it — the digit keys, the `⌘` variants and the
	/// grid clicks all run this exact path. Also covers the timeline menu's
	/// eligibility/checked state and the enable/disable detection.
	#[gpui::test]
	async fn real_engine_multicam_switch_round_trips_through_undo(
		cx: &mut gpui::TestAppContext,
	) {
		use oak_node::block::clip_input::TEXTURE_INPUT;
		let _media = media_lock();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));

		// A project whose clip is fed by a sequence (the multicam host).
		let clip_id = cx.update(|app| {
			engine.update(app, |engine, cx| {
				let project = graphops::create_project();
				let seq = graphops::create_sequence(&project, "Multicam Test");
				graphops::add_track(&project, seq, TrackType::Video).unwrap();
				graphops::add_track(&project, seq, TrackType::Video).unwrap();
				let clip = oak_timeline::util::block_clip_create(&project);
				{
					let mut g = graphops::lock(&project);
					let c = g
						.graph
						.get_mut(clip.id)
						.unwrap()
						.behavior
						.as_any_mut()
						.unwrap()
						.downcast_mut::<oak_node::block::ClipBlockBehavior>()
						.unwrap();
					c.core.range = oak_core::TimeRange::new(
						oak_core::Rational::new(0, 1),
						oak_core::Rational::new(100, 1),
					);
					c.core.media_in = oak_core::Rational::new(0, 1);
				}
				let track0 = {
					let g = graphops::lock(&project);
					graphops::track_ids(&g.graph, seq, TrackType::Video)[0]
				};
				oak_timeline::util::track_append_block(
					&oak_timeline::util::NodeRef::new(project.clone(), track0),
					&clip,
				);
				{
					let mut g = graphops::lock(&project);
					g.graph.connect(seq, clip.id, TEXTURE_INPUT, -1).unwrap();
				}
				let clip_id = ClipId(clip.id.identity());
				engine.adopt_project(project, cx);
				clip_id
			})
		});

		// Select the clip and enable multicam through the UI path.
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.set_selected_clips(vec![clip_id], cx)
			})
		});
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.multicam_enable_selected(vec![clip_id], true, cx)
			})
		});

		// The timeline menu's enable + checked state reflect the clip.
		assert!(cx.read(|app| engine.read(app).multicam_eligible(&[clip_id])));
		assert!(cx.read(|app| engine.read(app).multicam_enabled_on_selection(&[clip_id])));

		// The detection (selection → clip → find_multicam) resolves the
		// source count from the source sequence's video tracks (the two
		// default video tracks plus the two added for this test).
		let state = cx
			.read(|app| engine.read(app).multicam_state())
			.expect("a selected multicam clip is detected");
		assert_eq!(state.source_count, 4, "default 2 video tracks + 2 added = four angles");
		assert_eq!(state.current_source, 0);

		// Switch through the UI path (no split: the playhead sits at the
		// clip's in point, so the switch is a plain current_in write).
		cx.update(|app| {
			engine.update(app, |engine, cx| engine.multicam_switch_to(1, false, cx))
		});
		let state = cx
			.read(|app| engine.read(app).multicam_state())
			.expect("still detected after the switch");
		assert_eq!(state.current_source, 1);

		// ONE undo entry restores the previous source; redo re-applies it.
		cx.update(|app| engine.update(app, |engine, cx| engine.undo(cx)));
		assert_eq!(
			cx.read(|app| engine.read(app).multicam_state()).unwrap().current_source,
			0,
			"undo restores the pre-switch source"
		);
		cx.update(|app| engine.update(app, |engine, cx| engine.redo(cx)));
		assert_eq!(
			cx.read(|app| engine.read(app).multicam_state()).unwrap().current_source,
			1,
			"redo re-applies the switched source"
		);

		// Disabling through the UI path clears the detection (the panel
		// falls back to its empty state).
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.multicam_enable_selected(vec![clip_id], false, cx)
			})
		});
		assert!(
			cx.read(|app| engine.read(app).multicam_state()).is_none(),
			"disabling multicam clears the detection"
		);

		oak_undo::global::clear().unwrap();
	}

	/// M12 P2 acceptance: a real project with a sequence + footage clip
	/// builds a NON-EMPTY node graph with the wires the node editor shows:
	/// the footage feeds the clip's `tex_in` (a real edge), and every clip
	/// connects to the sequence output through the synthesized wire. Runs
	/// through the same builder `RealEngine::nodes()`/`edges()` use.
	#[test]
	fn real_node_graph_enumerates_sequence() {
		let _media = media_lock();
		let project = graphops::create_project();
		let seq = graphops::create_sequence(&project, "Node Editor");
		graphops::add_track(&project, seq, TrackType::Video).expect("add a video track");

		let media = std::env::temp_dir().join(format!(
			"oakapp_nodegraph_{}.mp4",
			std::process::id()
		));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10)
			.expect("generate test media");
		let footage = graphops::import_footage(&project, &media).expect("import must succeed");
		graphops::place_footage_clip(&project, seq, footage, TrackType::Video, 0, 0, 10, 0)
			.expect("clip placement");

		// The graph through the same builder `RealEngine::nodes()` /
		// `edges()` use.
		let (nodes, edges) = crate::oakui::nodegraph::build_graph(&project, seq);
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
		let output_id = gpui::node_graph::NodeId(seq.identity());
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

		// The footage→clip media edge is a REAL graph edge, so its wire is
		// not the synthesized kind.
		let real_edges = edges
			.iter()
			.filter(|e| !crate::oakui::nodegraph::is_output_wire(e.id))
			.count();
		assert!(
			real_edges >= 1,
			"the footage→clip media edge is real (got {real_edges} real edges)"
		);

		oak_undo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}

	/// The selection linkage: selecting a timeline clip narrows the node
	/// graph to that clip's context chain and mirrors its block node as the
	/// graph selection; selecting the effect's node in the graph updates the
	/// inspector's stack target and the highlighted effect card; and a card
	/// click selects the effect node again (the reverse direction).
	#[gpui::test]
	async fn selection_links_timeline_graph_and_inspector(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));

		let (clip_id, effect_ident, footage_ident) = cx.update(|app| {
			engine.update(app, |engine, cx| {
				let project = graphops::create_project();
				let seq = graphops::create_sequence(&project, "Selection Link");
				graphops::add_track(&project, seq, TrackType::Video).expect("a video track");
				// The clip: a bare block whose chain gets the effect first
				// (the effect-chain insert on an empty chain rewires nothing
				// and connects the effect to the clip's `tex_in`), then the
				// footage feeds the effect's media input.
				let clip = oak_timeline::util::block_clip_create(&project);
				let ty = crate::oakui::effectchain::addable_effects()
					.into_iter()
					.next()
					.expect("an addable effect")
					.type_id;
				let effect =
					crate::oakui::effectchain::insert(&project, clip.id, 0, &ty).expect("chain it");
				let media = std::env::temp_dir().join(format!(
					"oakapp_sel_link_{}.mp4",
					std::process::id()
				));
				oak_codec::testmedia::write_test_clip(&media, 32, 32, 10, 10)
					.expect("generate test media");
				let footage = graphops::import_footage(&project, &media).expect("import the media");
				let effect_input = graphops::lock(&project)
					.graph
					.get(effect)
					.map(|e| e.core.effect_input.clone())
					.unwrap_or_default();
				{
					let mut g = graphops::lock(&project);
					g.graph
						.connect(footage, effect, &effect_input, -1)
						.expect("wire the footage into the effect");
				}
				// Place the clip on the track so the engine's timeline
				// snapshot includes it (the inspector walks the snapshot).
				let track0 = {
					let g = graphops::lock(&project);
					graphops::track_ids(&g.graph, seq, TrackType::Video)[0]
				};
				oak_timeline::util::track_append_block(
					&oak_timeline::util::NodeRef::new(project.clone(), track0),
					&clip,
				);
				let _ = std::fs::remove_file(&media);
				engine.adopt_project(project, cx);
				(ClipId(clip.id.identity()), effect.identity(), footage.identity())
			})
		});

		// A single clip selection narrows the node graph to the chain and
		// mirrors the block node as the graph selection.
		cx.update(|app| {
			engine.update(app, |engine, cx| engine.set_selected_clips(vec![clip_id], cx))
		});
		assert_eq!(
			cx.read(|app| engine.read(app).selected_graph_node()),
			Some(clip_id.0),
			"a clip selection highlights its block node"
		);
		let ids: Vec<u64> = cx
			.read(|app| engine.read(app).nodes().into_iter().map(|n| n.id.0).collect());
		assert_eq!(ids.len(), 3, "only the clip's context chain is shown (got {ids:?})");
		assert!(ids.contains(&clip_id.0), "the clip node is part of the chain");
		assert!(ids.contains(&effect_ident), "the effect is part of the chain");
		assert!(ids.contains(&footage_ident), "the footage is part of the chain");

		// Selecting the effect node in the graph retargets the inspector to
		// the owning clip and highlights the matching card.
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.apply_node_graph_event(
					&NodeGraphEvent::SelectionChanged {
						nodes: BTreeSet::from([gpui::node_graph::NodeId(effect_ident)]),
					},
					cx,
				);
			})
		});
		assert_eq!(
			cx.read(|app| engine.read(app).selected_graph_node()),
			Some(effect_ident),
			"the graph selection mirrors the clicked node"
		);
		let cards = cx.read(|app| engine.read(app).effects());
		assert_eq!(
			cards.len(),
			2,
			"the inspector shows the owning clip's chain (media source + effect)"
		);
		assert_eq!(
			cx.read(|app| engine.read(app).selected_effect()),
			Some(EffectId(effect_ident)),
			"the selected node highlights its effect card"
		);
		assert!(
			cx.read(|app| engine.read(app).target_label()).is_some(),
			"the stack keeps its target label"
		);

		// An inspector card click selects the effect node again (the reverse
		// direction of the bidirectional link).
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.apply_effect_event(
					&EffectStackEvent::CardSelected {
						effect: EffectId(effect_ident),
					},
					cx,
				);
			})
		});
		assert_eq!(
			cx.read(|app| engine.read(app).selected_graph_node()),
			Some(effect_ident),
			"a card click re-selects the effect's node"
		);

		oak_undo::global::clear().unwrap();
	}

	/// Regression: the source monitor's full-res job renders the selected
	/// footage while holding its own project `Arc` — dropping the engine's
	/// project reference while the job is in flight leaves the job's copy
	/// alive, so the render completes and frees cleanly.
	#[test]
	fn full_res_worker_outlives_a_dropped_project() {
		let _media = media_lock();
		let _worker = WorkerBinGuard::set();
		if !crate::oakui::renderops::ensure_render_manager() {
			panic!("the render manager failed to start");
		}

		let project = graphops::create_project();
		let seq = graphops::create_sequence(&project, "Full Res Source");
		let media = std::env::temp_dir().join(format!(
			"oakapp_fullres_src_{}.mp4",
			std::process::id()
		));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10)
			.expect("generate test media");
		let footage = graphops::import_footage(&project, &media).expect("import must succeed");
		let tb = graphops::sequence_time_base(&graphops::lock(&project).graph, seq).unwrap();

		// The worker's own project copy (what build_full_res_request hands
		// it); the caller's reference goes away BEFORE the worker runs —
		// the pre-Arc crash window.
		let worker_project = project.clone();
		drop(project);

		let (tx, rx) = std_mpsc::channel();
		let request = FullResRequest {
			monitor: Monitor::Source,
			frame: 0,
			generation: 1,
			project: worker_project,
			target: FullResTarget::Footage(footage),
			width: 64,
			height: 64,
			tb,
		};
		std::thread::spawn(move || RealEngine::full_res_worker(request, tx));

		let event = rx
			.recv_timeout(Duration::from_secs(60))
			.expect("the worker delivers the frame after the project drop");
		let bytes = event.image.as_bytes(0).expect("one frame");
		assert_eq!(bytes.len(), 64 * 64 * 4, "full-res geometry");
		oak_undo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}

	// -----------------------------------------------------------------------
	// M12 P5a: the full-resolution fill — scheduling logic (pure)
	// -----------------------------------------------------------------------

	/// A tiny 2x2 viewer image for cache tests.
	fn test_image() -> Arc<RenderImage> {
		let samples = [
			1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
		];
		Arc::new(f32_rgba_to_bgra_image(2, 2, &samples))
	}

	/// Scope samples matching the test image's pixel count.
	fn test_scope() -> ScopeData {
		ScopeData {
			luma: Arc::new(vec![0.5; 4]),
			chroma: Arc::new(vec![(0.5, 0.5); 4]),
		}
	}

	#[test]
	fn full_res_fill_replaces_the_proxy_on_display() {
		let mut cache = MonitorFrameCache::default();
		cache.proxy = Some(ProxyEntry {
			frame: 5,
			image: test_image(),
			scope: test_scope(),
		});
		// The proxy is displayed until the fill lands...
		let proxy = cache.proxy.as_ref().unwrap().image.clone();
		assert!(Arc::ptr_eq(cache.image_for(5).unwrap(), &proxy));
		// ...and the fill wins once it does (the same playhead frame).
		let fill = test_image();
		cache.pending = Some((5, 1));
		assert!(cache.install_full_res(5, 1, fill.clone()));
		assert!(Arc::ptr_eq(cache.image_for(5).unwrap(), &fill));
		// The proxy stays cached (the scopes keep reading it).
		assert!(cache.proxy.is_some());
	}

	#[test]
	fn full_res_fill_does_not_cover_other_frames() {
		let mut cache = MonitorFrameCache::default();
		cache.proxy = Some(ProxyEntry {
			frame: 5,
			image: test_image(),
			scope: test_scope(),
		});
		let fill = test_image();
		cache.full = Some(FullResEntry {
			frame: 5,
			image: fill.clone(),
		});
		// The fill covers exactly its own frame.
		assert!(Arc::ptr_eq(cache.image_for(5).unwrap(), &fill));
		assert!(cache.image_for(6).is_none(), "other frames are unrendered");
		// A later proxy render for another frame displays alongside the fill.
		let proxy6 = test_image();
		cache.proxy = Some(ProxyEntry {
			frame: 6,
			image: proxy6.clone(),
			scope: test_scope(),
		});
		assert!(Arc::ptr_eq(cache.image_for(6).unwrap(), &proxy6));
		// The scopes always read the proxy pass (the fill carries none).
		assert_eq!(cache.scope_for(6).unwrap().luma.len(), 4);
		assert!(cache.scope_for(5).is_none());
	}

	#[test]
	fn full_res_policy_skips_playing_cached_and_in_flight_frames() {
		let cache = MonitorFrameCache::default();
		assert!(cache.needs_full_res(0, false), "resting playhead schedules");
		assert!(
			!cache.needs_full_res(0, true),
			"playback keeps the proxy path (smoothness)"
		);

		let mut cached = MonitorFrameCache::default();
		cached.full = Some(FullResEntry {
			frame: 5,
			image: test_image(),
		});
		assert!(!cached.needs_full_res(5, false), "already filled");

		let mut in_flight = MonitorFrameCache::default();
		in_flight.pending = Some((5, 1));
		assert!(
			!in_flight.needs_full_res(5, false),
			"one job in flight per monitor"
		);
		assert!(
			!in_flight.needs_full_res(7, false),
			"the drain re-schedules the moved playhead when the job lands"
		);
	}

	#[test]
	fn full_res_install_accepts_only_the_pending_job() {
		// A job that outlived an invalidation must be discarded: the cache
		// entry was recreated with a fresh pending marker (new frame and
		// generation), so the old completion does not install.
		let mut stale = MonitorFrameCache::default();
		stale.pending = Some((7, 2));
		assert!(
			!stale.install_full_res(5, 1, test_image()),
			"stale generation is discarded"
		);
		assert_eq!(stale.pending, Some((7, 2)), "the new job stays pending");
		assert!(stale.full.is_none(), "nothing is installed");

		// The matching job installs, clears the pending marker and lets the
		// moved playhead schedule again.
		let mut current = MonitorFrameCache::default();
		current.pending = Some((5, 1));
		assert!(current.install_full_res(5, 1, test_image()));
		assert!(current.pending.is_none());
		assert!(current.full.as_ref().is_some_and(|f| f.frame == 5));
		assert!(current.needs_full_res(7, false));
	}

	/// M12 P5a end-to-end: a full-res job (the exact request
	/// [`RealEngine::build_full_res_request`] builds) renders a real frame
	/// on a background thread — the footage clip is decoded and the frame
	/// is delivered through the completion channel.
	#[test]
	fn full_res_worker_renders_real_frame() {
		let _media = media_lock();
		let _worker = WorkerBinGuard::set();
		if !crate::oakui::renderops::ensure_render_manager() {
			panic!("the render manager failed to start");
		}

		let project = graphops::create_project();
		let seq = graphops::create_sequence(&project, "Full Res E2E");
		graphops::add_track(&project, seq, TrackType::Video).expect("add a video track");

		let media = std::env::temp_dir().join(format!(
			"oakapp_fullres_{}.mp4",
			std::process::id()
		));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10)
			.expect("generate test media");
		let footage = graphops::import_footage(&project, &media).expect("import must succeed");
		graphops::place_footage_clip(&project, seq, footage, TrackType::Video, 0, 0, 10, 0)
			.expect("clip placement");
		let tb = graphops::sequence_time_base(&graphops::lock(&project).graph, seq).unwrap();

		let (tx, rx) = std_mpsc::channel();
		let request = FullResRequest {
			monitor: Monitor::Program,
			frame: 0,
			generation: 1,
			project: project.clone(),
			target: FullResTarget::Sequence(seq),
			width: 320,
			height: 180,
			tb,
		};
		std::thread::spawn(move || RealEngine::full_res_worker(request, tx));

		let event = rx
			.recv_timeout(Duration::from_secs(60))
			.expect("the worker delivers the full-res frame");
		assert_eq!(event.monitor, Monitor::Program);
		assert_eq!(event.frame, 0);
		assert_eq!(event.generation, 1);
		let bytes = event.image.as_bytes(0).expect("one frame");
		assert_eq!(bytes.len(), 320 * 180 * 4, "full-res geometry");
		// The test clip's left half is red on frame 0: the decoded footage
		// must be visible (non-black), like the proxy e2e test asserts.
		let nonzero = bytes
			.chunks(4)
			.filter(|px| px[..3].iter().any(|&c| c != 0))
			.count();
		assert!(nonzero > 0, "the footage clip renders non-black pixels");

		oak_undo::global::clear().unwrap();
		let _ = std::fs::remove_file(&media);
	}

	/// M12 P5a acceptance through the app seam: a `RealEngine` built exactly
	/// as the app builds it renders the program frame at the proxy size on
	/// demand, and the background full-res job fills the sequence's native
	/// size once the playhead rests (the tick loop schedules, the worker
	/// renders, the drain installs, `cpu_frame` hands the fill to the
	/// viewer).
	#[gpui::test]
	async fn real_engine_fills_full_res_behind_the_proxy(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let _worker = WorkerBinGuard::set();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));

		// The sequence's native size is the fill geometry.
		let (width, height) = cx.read(|app| {
			let format = engine.read(app).current_sequence().unwrap().format;
			(format.width, format.height)
		});

		// The immediate path is the proxy: the first read renders the small
		// proxy synchronously (no fill has landed yet).
		let proxy_len = cx.read(|app| {
			engine
				.read(app)
				.cpu_frame(Monitor::Program, app)
				.as_bytes(0)
				.unwrap()
				.len()
		});
		assert!(
			proxy_len < (width * height * 4) as usize,
			"the immediate display is the proxy (got {proxy_len} bytes)"
		);

		// Drive the tick loop until the background fill lands and replaces
		// the proxy in the display path.
		// Progress criterion, not wall time: the fill lands after a bounded
		// number of engine pumps (machine-speed independent).
		let full_len = (width * height * 4) as usize;
		let mut pumps = 0usize;
		loop {
			pumps += 1;
			cx.update(|app| engine.update(app, |engine, cx| engine.tick(cx)));
			let len = cx.read(|app| {
				engine
					.read(app)
					.cpu_frame(Monitor::Program, app)
					.as_bytes(0)
					.unwrap()
					.len()
			});
			if len == full_len {
				break;
			}
			assert!(
				pumps < 5000,
				"the full-res fill lands after a bounded number of pumps (got {len} bytes after {pumps})"
			);
			std::thread::sleep(Duration::from_millis(10));
		}
	}

	/// A fresh sequence starts with the default 2 video + 2 audio track
	/// layout (user-mandated: V1, V2 on top, A1, A2 below) — and the
	/// default layout is NOT an undoable edit (the undo stack stays
	//  empty for the sequence's birth).
	#[gpui::test]
	async fn new_sequence_has_default_two_video_two_audio_tracks(cx: &mut gpui::TestAppContext) {
		// Serializes with the other engine tests: `new_project` clears the
		// GLOBAL undo stack, and this test asserts on it — running lock-free
		// raced a parallel test's undo history.
		let _media = media_lock();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));
		let kinds: Vec<TrackKind> =
			cx.read(|app| engine.read(app).tracks.iter().map(|t| t.kind).collect());
		assert_eq!(
			kinds,
			vec![
				TrackKind::Video,
				TrackKind::Video,
				TrackKind::Audio,
				TrackKind::Audio
			],
			"a new sequence starts with 2 video + 2 audio tracks"
		);
	}

	/// Cmd+= regression: set_track_height used to lock the project graph
	/// and then call graphops::set_track_height (which locks it again)
	/// inside the same scope — an instant self-deadlock. Completing this
	/// call at all is the assertion.
	#[gpui::test]
	async fn set_track_height_does_not_self_deadlock(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));
		cx.update(|app| {
			engine.update(app, |engine, cx| engine.set_track_height(px(96.0), cx))
		});
		let height = cx.read(|app| engine.read(app).tracks[0].height());
		assert_eq!(height, px(96.0), "the new height is applied");
	}

	/// Dropping a video-with-audio footage places BOTH a video clip and a
	/// linked audio clip at the same range in ONE undoable entry (the NLE
	/// A/V drop): one undo removes both, and the clips are linked.
	#[gpui::test]
	async fn drop_av_footage_places_linked_video_and_audio_clips(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));

		// demo.mp4: 1080p H.264 video + AAC audio (+ timecode stream).
		let media = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/demo.mp4");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.import_footage(media.clone(), cx).expect("import")
			})
		});
		let name = media.file_name().unwrap().to_string_lossy().into_owned();
		let entry = cx.read(|app| {
			engine
				.read(app)
				.roots()
				.into_iter()
				.find(|e| e.name.as_ref() == name)
		})
		.expect("imported footage is listed");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				// Drop at a non-zero frame: the placement must land where
				// the cursor was (the "always lands at zero" regression).
				engine.drop_footage(entry.id, TrackKind::Video, 0, Frame(40), cx)
			})
		});
		// The clip landed at the drop frame (not the timeline zero).
		let video_in = cx.read(|app| {
			let engine = engine.read(app);
			engine
				.tracks
				.iter()
				.find(|t| t.kind == TrackKind::Video && !t.clips.is_empty())
				.map(|t| t.clips[0].range.start.0)
		});
		assert_eq!(video_in, Some(40), "the clip lands at the drop frame");

		let clip_count = |engine: &RealEngine, kind: TrackKind| -> usize {
			engine
				.tracks
				.iter()
				.filter(|t| t.kind == kind)
				.map(|t| t.clips.len())
				.sum()
		};
		let (video_clips, audio_clips) = cx.read(|app| {
			let engine = engine.read(app);
			(clip_count(engine, TrackKind::Video), clip_count(engine, TrackKind::Audio))
		});
		assert_eq!((video_clips, audio_clips), (1, 1), "one video clip + one audio clip");

		// The two clips are linked (grouped edits apply to both).
		let (video_block, audio_block) = cx.read(|app| {
			let engine = engine.read(app);
			let v = engine
				.tracks
				.iter()
				.find(|t| t.kind == TrackKind::Video && !t.clips.is_empty())
				.map(|t| t.clips[0].block);
			let a = engine
				.tracks
				.iter()
				.find(|t| t.kind == TrackKind::Audio && !t.clips.is_empty())
				.map(|t| t.clips[0].block);
			(v.expect("video clip"), a.expect("audio clip"))
		});
		let project = cx.read(|app| engine.read(app).project.clone().expect("project"));
		let guard = graphops::lock(&project);
		assert!(
			guard.graph.links_of(video_block).contains(&audio_block),
			"the video clip links to its audio clip"
		);
		assert!(
			guard.graph.links_of(audio_block).contains(&video_block),
			"the audio clip links back to its video clip"
		);
		drop(guard);

		// ONE undo removes both clips (a single "Add Clip" entry).
		cx.update(|app| engine.update(app, |engine, cx| engine.undo(cx)));
		let (video_clips, audio_clips) = cx.read(|app| {
			let engine = engine.read(app);
			(clip_count(engine, TrackKind::Video), clip_count(engine, TrackKind::Audio))
		});
		assert_eq!((video_clips, audio_clips), (0, 0), "one undo removes both clips");
		cx.update(|app| engine.update(app, |engine, cx| engine.redo(cx)));
		let (video_clips, audio_clips) = cx.read(|app| {
			let engine = engine.read(app);
			(clip_count(engine, TrackKind::Video), clip_count(engine, TrackKind::Audio))
		});
		assert_eq!((video_clips, audio_clips), (1, 1), "one redo restores both clips");
	}

	/// Dragging a clip of a linked A/V pair drags its partner in lockstep:
	/// the linked clip stays on its own track and shifts by the same frame
	/// offset, both for a same-track drag and a cross-track drag, and ONE
	/// undo restores the whole group.
	#[gpui::test]
	async fn moving_a_linked_clip_drags_its_partner(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));

		let media = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/demo.mp4");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.import_footage(media.clone(), cx).expect("import")
			})
		});
		let name = media.file_name().unwrap().to_string_lossy().into_owned();
		let entry = cx.read(|app| {
			engine
				.read(app)
				.roots()
				.into_iter()
				.find(|e| e.name.as_ref() == name)
		})
		.expect("imported footage is listed");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.drop_footage(entry.id, TrackKind::Video, 0, Frame(40), cx)
			})
		});

		// The video clip id, the audio clip id, and their display-track
		// indices.
		let (video_id, audio_id, video_idx, audio_idx, other_video_idx) = cx.read(|app| {
			let engine = engine.read(app);
			let video_idx = engine
				.tracks
				.iter()
				.position(|t| t.kind == TrackKind::Video && !t.clips.is_empty())
				.expect("a video track with the clip");
			let audio_idx = engine
				.tracks
				.iter()
				.position(|t| t.kind == TrackKind::Audio && !t.clips.is_empty())
				.expect("an audio track with the clip");
			let other_video_idx = engine
				.tracks
				.iter()
				.enumerate()
				.find(|(i, t)| t.kind == TrackKind::Video && *i != video_idx)
				.map(|(i, _)| i)
				.expect("a second video track");
			(
				engine.tracks[video_idx].clips[0].id,
				engine.tracks[audio_idx].clips[0].id,
				video_idx,
				audio_idx,
				other_video_idx,
			)
		});

		let in_points = |cx: &mut gpui::TestAppContext, video_idx: usize, audio_idx: usize| {
			cx.read(|app| {
				let engine = engine.read(app);
				let video = engine.tracks[video_idx]
					.clips
					.iter()
					.find(|c| c.id == video_id)
					.map(|c| c.range.start.0);
				let audio = engine.tracks[audio_idx]
					.clips
					.iter()
					.find(|c| c.id == audio_id)
					.map(|c| c.range.start.0);
				(video, audio)
			})
		};

		let move_video = |cx: &mut gpui::TestAppContext, track: usize, start: i64| {
			cx.update(|app| {
				engine.update(app, |engine, cx| {
					engine.apply_timeline_event(
						&TimelineEvent::ClipMoveRequested {
							clip: video_id,
							new_track: track,
							new_start: Frame(start),
						},
						cx,
					);
				})
			});
		};

		// Same-track drag: the video clip +40 frames; the audio clip follows
		// on its own track.
		move_video(cx, video_idx, 80);
		assert_eq!(in_points(cx, video_idx, audio_idx), (Some(80), Some(80)));

		// Cross-track drag: the video clip moves to the other video track at
		// 120; the audio clip stays on its audio track but shifts to 120 too.
		move_video(cx, other_video_idx, 120);
		assert_eq!(
			in_points(cx, other_video_idx, audio_idx),
			(Some(120), Some(120)),
			"the audio clip follows the cross-track drag while keeping its track"
		);
		assert!(
			!cx.read(|app| engine.read(app).tracks[video_idx]
				.clips
				.iter()
				.any(|c| c.id == video_id)),
			"the video clip left its original track"
		);

		// ONE undo restores the whole group (the cross-track move was a
		// single "Move Clip" entry).
		cx.update(|app| engine.update(app, |engine, cx| engine.undo(cx)));
		assert_eq!(
			in_points(cx, video_idx, audio_idx),
			(Some(80), Some(80)),
			"one undo restores both clips to the same-track position"
		);
		cx.update(|app| engine.update(app, |engine, cx| engine.undo(cx)));
		assert_eq!(
			in_points(cx, video_idx, audio_idx),
			(Some(40), Some(40)),
			"a second undo restores the pre-drag position"
		);
	}

	/// Playback pre-render window (M15 S2): during playback the playhead
	/// frame must come from the worker-rendered shm slot cache, NOT the
	/// synchronous render path (the main thread blocking in
	/// `TicketArena::wait` on every painted frame is the "playback is
	/// unusably choppy" regression — the UI must never sync-wait during
	/// playback once the window has warmed up).
	#[gpui::test]
	async fn playback_window_supplies_playhead_frames(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let _worker = WorkerBinGuard::set();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));

		let media = std::env::temp_dir().join(format!(
			"oakapp_playback_window_{}.mp4",
			std::process::id()
		));
		oak_codec::testmedia::write_test_clip(&media, 64, 64, 250, 25)
			.expect("generate playback test media");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.import_footage(media.clone(), cx).expect("import")
			})
		});
		let name = media.file_name().unwrap().to_string_lossy().into_owned();
		let entry = cx.read(|app| {
			engine
				.read(app)
				.roots()
				.into_iter()
				.find(|e| e.name.as_ref() == name)
		})
		.expect("imported footage is listed");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.drop_footage(entry.id, TrackKind::Video, 0, Frame(0), cx)
			})
		});

		// Start playback and drive the tick loop: the window must fill.
		cx.update(|app| engine.update(app, |engine, cx| engine.play(Monitor::Program, cx)));
		let mut filled = 0usize;
		let mut hit = false;
		let mut pumps = 0usize;
		loop {
			pumps += 1;
			cx.update(|app| engine.update(app, |engine, cx| engine.tick(cx)));
			let (slots, submitted) = cx.read(|app| {
				let engine = engine.read(app);
				let windows = engine.preview_windows.lock().unwrap();
				let window = windows.get(&Monitor::Program);
				(
					window.map(|w| w.slots.len()).unwrap_or(0),
					window.map(|w| w.submitted.len()).unwrap_or(0),
				)
			});
			filled = filled.max(slots);
			let playhead = cx.read(|app| engine.read(app).clock_frame(Monitor::Program, app));
			hit = hit
				|| cx
					.update(|app| engine.update(app, |engine, _cx| engine.preview_slot_frame(Monitor::Program, playhead)))
					.is_some();
			if hit {
				break;
			}
			assert!(
				pumps < 5000,
				"the playback window must supply playhead frames (peak cached {filled}, submitted {submitted}, after {pumps} pumps)"
			);
			std::thread::sleep(Duration::from_millis(10));
		}
		assert!(filled > 0, "the window cached worker frames");
		let _ = std::fs::remove_file(&media);
	}

	/// Probe: a single interactive seek must render the seeked frame
	/// without hanging the UI path (the ruler mouse-down path goes through
	/// request_frame + a synchronous cpu_frame render).
	#[gpui::test]
	async fn interactive_seek_renders_without_hanging(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let _worker = WorkerBinGuard::set();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));
		let media = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/demo.mp4");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.import_footage(media.clone(), cx).expect("import")
			})
		});
		let name = media.file_name().unwrap().to_string_lossy().into_owned();
		let entry = cx.read(|app| {
			engine.read(app).roots().into_iter().find(|e| e.name.as_ref() == name)
		}).expect("imported footage is listed");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.drop_footage(entry.id, TrackKind::Video, 0, Frame(0), cx)
			})
		});
		// Interactive seek (not playing): a single synchronous render of the
		// target frame. If this hangs, the seek render path deadlocks.
		cx.update(|app| {
			engine.update(app, |engine, cx| engine.request_frame(Monitor::Program, Frame(24), cx))
		});
		let img = cx.read(|app| engine.read(app).cpu_frame(Monitor::Program, app));
		let bytes = img.as_bytes(0).expect("frame bytes");
		assert!(!bytes.is_empty(), "seeked frame has content");

		// The reported freeze: play (the preview window fills and holds shm
		// slots), THEN an interactive seek — the synchronous render must not
		// deadlock against the window's held slots.
		cx.update(|app| engine.update(app, |engine, cx| engine.play(Monitor::Program, cx)));
		for _ in 0..30 {
			cx.update(|app| engine.update(app, |engine, cx| engine.tick(cx)));
			std::thread::sleep(Duration::from_millis(10));
		}
		cx.update(|app| engine.update(app, |engine, cx| engine.pause(Monitor::Program, cx)));
		cx.update(|app| {
			engine.update(app, |engine, cx| engine.request_frame(Monitor::Program, Frame(48), cx))
		});
		let img = cx.read(|app| engine.read(app).cpu_frame(Monitor::Program, app));
		let bytes = img.as_bytes(0).expect("seek-after-play frame bytes");
		assert!(!bytes.is_empty(), "seek after playback has content");
	}

	/// A project with a real clip on the timeline survives a save/load
	/// roundtrip (also the fixture generator for viewer debugging: the
	/// saved file lands at a stable temp path).
	#[gpui::test]
	async fn save_load_roundtrips_a_timeline_clip(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));
		let media = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/demo.mp4");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.import_footage(media.clone(), cx).expect("import")
			})
		});
		let name = media.file_name().unwrap().to_string_lossy().into_owned();
		let entry = cx.read(|app| {
			engine.read(app).roots().into_iter().find(|e| e.name.as_ref() == name)
		}).expect("imported footage is listed");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.drop_footage(entry.id, TrackKind::Video, 0, Frame(0), cx)
			})
		});
		let path = std::env::temp_dir().join("oakapp_save_roundtrip.ove");
		cx.update(|app| {
			engine.update(app, |engine, _cx| {
				let project = engine.project_ref().expect("project").clone();
				crate::oakui::graphops::save_ove(&project, &path).expect("save")
			})
		});
		// Load it back: the footage and the timeline clip are restored.
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.open_project_path(path.clone(), cx).expect("load")
			})
		});
		let (roots, tracks) = cx.read(|app| {
			let engine = engine.read(app);
			(
				engine.roots().iter().map(|e| e.name.to_string()).collect::<Vec<_>>(),
				engine.tracks.iter().map(|t| t.clips.len()).sum::<usize>(),
			)
		});
		assert!(roots.iter().any(|n| n == &name), "footage survives: {roots:?}");
		assert!(tracks > 0, "the timeline clip survives the roundtrip");
	}

	/// The production scenario: real 1080p media on the timeline, driving
	/// the actual `cpu_frame` display path the viewer paints with (not
	/// just the window internals). The displayed frame must track the
	/// playhead during playback — a permanently frozen picture means the
	/// window never serves the display path.
	#[gpui::test]
	async fn playback_display_tracks_the_playhead(cx: &mut gpui::TestAppContext) {
		let _media = media_lock();
		let _worker = WorkerBinGuard::set();
		let engine = cx.update(|cx| cx.new(|cx| RealEngine::create(cx)));
		cx.update(|app| engine.update(app, |engine, cx| engine.new_project(cx)));

		let media = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/demo.mp4");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.import_footage(media.clone(), cx).expect("import")
			})
		});
		let name = media.file_name().unwrap().to_string_lossy().into_owned();
		let entry = cx.read(|app| {
			engine
				.read(app)
				.roots()
				.into_iter()
				.find(|e| e.name.as_ref() == name)
		})
		.expect("imported footage is listed");
		cx.update(|app| {
			engine.update(app, |engine, cx| {
				engine.drop_footage(entry.id, TrackKind::Video, 0, Frame(0), cx)
			})
		});

		cx.update(|app| engine.update(app, |engine, cx| engine.play(Monitor::Program, cx)));
		// Track-the-playhead smoke test. The loop's progress criterion is
		// the playback clock itself, NOT wall time: the transport advances
		// independently of render speed, so the loop always terminates
		// after ~5 s of playback regardless of how fast the machine is.
		// The assertion is the only judgment: once playback has progressed,
		// the displayed frame must have tracked the playhead. A wall-clock
		// deadline here would fail the test for machine slowness rather
		// than for a broken pipeline — exactly the spurious failures we
		// are avoiding.
		let mut last_displayed = -1i64;
		loop {
			cx.update(|app| engine.update(app, |engine, cx| engine.tick(cx)));
			let (playhead, displayed, slots) = cx.read(|app| {
				let engine = engine.read(app);
				let playhead = engine.clock_frame(Monitor::Program, app).0;
				let displayed = engine
					.cpu_frame_cache
					.lock()
					.unwrap()
					.get(&Monitor::Program)
					.and_then(|e| e.proxy.as_ref().map(|p| p.frame))
					.unwrap_or(-1);
				let slots = engine
					.preview_windows
					.lock()
					.unwrap()
					.get(&Monitor::Program)
					.map(|w| w.slots.len())
					.unwrap_or(0);
				(playhead, displayed, slots)
			});
			last_displayed = last_displayed.max(displayed);
			// 120 frames ≈ 5 s at 24 fps: enough for the pre-render window
			// to warm up on any machine. The playhead always gets here.
			if playhead >= 120 {
				assert!(
					displayed >= 3 && playhead - displayed < 4,
					"the displayed frame must track the playhead (playhead {playhead}, displayed {displayed}, peak displayed {last_displayed}, window slots {slots})"
				);
				break;
			}
			// The viewer paints at ~60 Hz.
			std::thread::sleep(Duration::from_millis(16));
			cx.update(|app| {
				engine.read(app).cpu_frame(Monitor::Program, app);
			});
		}
		assert!(last_displayed >= 3);
	}

	// ---- M15 S3 audio prefetch ------------------------------------------

	/// A lightweight `RenderedAudio` stand-in (the prefetch logic only
	/// reads the fields, never renders).
	fn audio_chunk(start: i64) -> (i64, crate::oakui::renderops::RenderedAudio) {
		(
			start,
			crate::oakui::renderops::RenderedAudio {
				data: vec![0.0; 800],
				sample_rate: 48000,
				channel_count: 2,
			},
		)
	}

	#[test]
	fn audio_prefetch_orders_and_serves_chunks() {
		let mut st = AudioPrefetch::new();
		assert!(!st.covers(0), "uninitialized prefetch covers nothing");
		st.reset(0, 10);
		assert!(!st.covers(0), "nothing submitted yet");
		st.next_submit = 40; // pretend 4 chunks were submitted (0..40)

		// Out-of-order arrivals (different workers) are reordered.
		st.insert(20, audio_chunk(20).1);
		st.insert(0, audio_chunk(0).1);
		st.insert(30, audio_chunk(30).1);
		assert_eq!(
			st.buffered.iter().map(|(t, _)| *t).collect::<Vec<_>>(),
			vec![0, 20, 30],
			"sorted by start ts"
		);
		// The chunk at the playhead is served.
		let got = st.pop_at(0).expect("chunk 0 buffered");
		assert_eq!(got.sample_rate, 48000);
		assert_eq!(st.buffered.len(), 2);
		// A not-yet-rendered chunk reports nothing.
		assert!(st.pop_at(10).is_none());
		// Stale arrivals (a seek raced the render) are dropped.
		st.insert(-10, audio_chunk(-10).1);
		st.insert(100, audio_chunk(100).1);
		assert_eq!(st.buffered.len(), 2, "stale chunks dropped");
	}

	#[test]
	fn audio_prefetch_resets_on_seek() {
		let mut st = AudioPrefetch::new();
		st.reset(0, 10);
		st.next_submit = 40;
		st.insert(10, audio_chunk(10).1);
		// A seek far ahead: the playhead is outside [front_ts, next_submit).
		assert!(!st.covers(200));
		st.reset(200, 10);
		assert_eq!(st.buffered.len(), 0, "old chunks dropped");
		assert!(st.pop_at(200).is_none());
		// A backward seek (the playhead behind the buffer front after it
		// advanced) is a reset too.
		st.reset(0, 10);
		st.next_submit = 40;
		st.insert(10, audio_chunk(10).1);
		let _ = st.pop_at(10); // front_ts now 20
		assert!(!st.covers(5), "chunk 5 is behind the front");
		st.reset(5, 10);
		assert_eq!(st.buffered.len(), 0);
	}

	#[test]
	fn audio_prefetch_drops_duplicate_arrivals() {
		let mut st = AudioPrefetch::new();
		st.reset(0, 10);
		st.next_submit = 30;
		st.insert(10, audio_chunk(10).1);
		st.insert(10, audio_chunk(10).1);
		assert_eq!(st.buffered.len(), 1, "duplicates dropped");
	}
}

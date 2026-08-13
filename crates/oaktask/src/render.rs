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

//! `RenderTask` abstract base, mirroring `src/task/src/render/render.h`.
//!
//! The abstract parent of [`crate::export::ExportTask`] and
//! [`crate::precache::PreCacheTask`]. It renders frames/audio through
//! oakrender tickets (`bridge::render`) and hands each result to a virtual
//! hook (frame/audio downloaded, subtitle encoded) that the subclass
//! implements via the [`RenderTaskBehavior`] trait.
//!
//! ## Concurrent render loop
//!
//! Like the C++ original (`render.cpp`), the loop keeps up to
//! `max_inflight` tickets running at once (default
//! `std::thread::available_parallelism()`, matching the C++
//! `std::max(1, hardware_concurrency)`). Tickets report completion through
//! the `oakrender_ticket_finished_fn` callback (fired on the ticket's own
//! finishing thread), which pushes the finished ticket into a shared queue
//! and wakes the render thread (queue + condvar, exactly the C++ design).
//! Tickets may complete out of order; a **reorder buffer** delivers the
//! results to the hooks in timestamp order — the audio range first (the C++
//! queue-audio-first order), then every frame in ascending time — so the
//! observable per-frame contract (`frame_downloaded`/`audio_downloaded` in
//! order, progress updates, cancellation between frames) is unchanged.
//!
//! The ticket's finished callback hands the loop a **borrowed copy** of the
//! submitter's handle (`include/render/ticket.h`: "the submitter keeps
//! ownership and releases it"). The loop therefore releases exactly the
//! handle value each submit function returns, once, and never releases the
//! queue's borrowed copies. Cancellation (or a hook error) cancels and
//! waits every in-flight ticket before returning, so every submitted
//! ticket's completion still fires exactly once.
//!
//! CPP-PARITY: src/task/src/render/render.h

use std::collections::{HashMap, VecDeque};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Condvar, Mutex};

use crate::bridge;
use crate::bridge::common::OakColorTransform;
use crate::bridge::node::{cstr, OAKNODE_SEQUENCE_SAMPLES_INPUT, OAKNODE_SEQUENCE_TEXTURE_INPUT};
use crate::bridge::render::{OakRenderTicket, OakRenderVideoTicketParams};
use crate::error::{Error, Result};
use crate::handle::CHandle;
use crate::task::Task;
use oakcore_rs::{Rational, TimeRange};

/// `OAKRENDER_TICKET_VIDEO` (`include/render/ticket.h`).
const TICKET_VIDEO: i32 = 0;
/// `OAKRENDER_TICKET_AUDIO` (`include/render/ticket.h`).
const TICKET_AUDIO: i32 = 1;

/// Overrides applied to a render ticket, mirroring the C++ `ForceParams`
/// struct in render.h (fields map onto `oakrender_video_ticket_params`).
///
/// CPP-PARITY: src/task/src/render/render.h (ForceParams)
#[derive(Clone, Debug, Default)]
pub struct ForceParams {
	/// Forced output width (0 = off).
	pub force_width: i32,
	/// Forced output height (0 = off).
	pub force_height: i32,
	/// Forced color matrix (row-major 4x4); only used when
	/// `has_force_matrix` is set.
	pub force_matrix: [f64; 16],
	/// Whether `force_matrix` is in effect.
	pub has_force_matrix: bool,
	/// Forced pixel format (`oakcore_rs::PixelFormat` as int; -1 = off).
	pub force_format: i32,
	/// Forced channel count (0 = off).
	pub force_channel_count: i32,
	/// Forced color output (borrowed `OakColorProcessor`; empty = none).
	pub force_color_output: CHandle,
}

/// Subclass hooks, standing in for the C++ protected virtuals
/// `download_frame`/`frame_downloaded`/`audio_downloaded`/`encode_subtitle`.
pub trait RenderTaskBehavior {
	/// Called for each rendered frame.
	fn frame_downloaded(&mut self, task: &mut Task, frame: CHandle) -> Result<()>;
	/// Called for each rendered audio buffer.
	fn audio_downloaded(&mut self, task: &mut Task, buffer: CHandle) -> Result<()>;
	/// Called to encode a subtitle.
	fn encode_subtitle(&mut self, task: &mut Task, text: &str) -> Result<()>;
}

/// The abstract render task base. Holds the shared [`Task`], the sequence
/// output params, the viewer node, and the subclass behavior.
pub struct RenderTask {
	/// The shared task base.
	pub base: Task,
	/// Output video params (borrowed `OakVideoParams`).
	pub video_params: CHandle,
	/// Output audio params (borrowed `OakAudioParams`; may be empty).
	pub audio_params: CHandle,
	/// The viewer node being rendered (borrowed `OakNodeNode`).
	pub viewer: CHandle,
	/// Force params applied to every ticket.
	pub force_params: ForceParams,
	/// The subclass behavior.
	pub behavior: Option<Box<dyn RenderTaskBehavior + Send>>,
	// --- private render inputs (set by the subclass before render()) ---
	/// Borrowed color manager.
	color_manager: CHandle,
	/// Borrowed frame cache.
	cache: CHandle,
	/// `olive::RenderMode::Mode` as int.
	mode: i32,
	/// Whether audio ranges are rendered.
	audio_enabled: bool,
	/// The range rendered by [`RenderTask::render`].
	export_range: TimeRange,
	/// Whether the render loop emits progress itself (the export task
	/// disables it and reports progress per written frame).
	native_progress_signalling: bool,
	/// Total number of frames in `export_range` (computed by `render`).
	total_frames: i64,
	/// Maximum render tickets kept in flight at once (the C++
	/// `maximum_rendered_frames`; defaults to `available_parallelism`).
	max_inflight: usize,
}

impl RenderTask {
	/// Create a render task with default (empty) render inputs.
	pub fn new(
		base: Task,
		video_params: CHandle,
		audio_params: CHandle,
		viewer: CHandle,
		force_params: ForceParams,
		behavior: Option<Box<dyn RenderTaskBehavior + Send>>,
	) -> RenderTask {
		RenderTask {
			base,
			video_params,
			audio_params,
			viewer,
			force_params,
			behavior,
			color_manager: CHandle::null(),
			cache: CHandle::null(),
			mode: 0,
			audio_enabled: false,
			export_range: TimeRange::new(Rational::new(0, 1), Rational::new(0, 1)),
			native_progress_signalling: true,
			total_frames: 0,
			max_inflight: std::thread::available_parallelism()
				.map(|n| n.get())
				.unwrap_or(1)
				.max(1),
		}
	}

	/// Configure the render inputs before [`RenderTask::render`] (called by
	/// the concrete subclasses' `run`).
	pub fn set_render_inputs(
		&mut self,
		color_manager: CHandle,
		cache: CHandle,
		mode: i32,
		audio_enabled: bool,
		export_range: TimeRange,
	) {
		self.color_manager = color_manager;
		self.cache = cache;
		self.mode = mode;
		self.audio_enabled = audio_enabled;
		self.export_range = export_range;
	}

	/// Enable/disable the render loop's own progress signalling.
	pub fn set_native_progress_signalling(&mut self, enabled: bool) {
		self.native_progress_signalling = enabled;
	}

	/// Override the number of render tickets kept in flight at once.
	///
	/// Defaults to `std::thread::available_parallelism()`, mirroring the
	/// C++ `std::max(1, int(std::thread::hardware_concurrency()))`. Primarily
	/// a test hook: a smaller window makes concurrent-completion tests
	/// deterministic regardless of the host core count.
	pub fn set_max_inflight(&mut self, max: usize) {
		self.max_inflight = max.max(1);
	}

	/// The number of frames computed by the last [`RenderTask::render`] run.
	pub fn total_frames(&self) -> i64 {
		self.total_frames
	}

	/// Frame duration rational from the video params (falls back to 1/1
	/// when the params are empty/invalid).
	fn timebase(&self) -> Rational {
		let mut num = 0;
		let mut den = 1;
		unsafe {
			bridge::common::oakcommon_videoparams_frame_rate_as_time_base(
				self.video_params,
				&mut num,
				&mut den,
			);
		}
		if num <= 0 || den <= 0 {
			Rational::new(1, 1)
		} else {
			Rational::new(num as i64, den as i64)
		}
	}

	/// Submit one video frame ticket at `time` (mirrors the C++
	/// `start_video_ticket`). `dispatch` is the shared completion channel
	/// handed to the ticket's finished callback.
	fn submit_video_ticket(
		&self,
		time: Rational,
		dispatch: *mut RenderDispatch,
	) -> Result<OakRenderTicket> {
		let mut output_node = CHandle::null();
		unsafe {
			bridge::node::oaknode_node_input_get_connected_node(
				self.viewer,
				cstr(OAKNODE_SEQUENCE_TEXTURE_INPUT),
				&mut output_node,
			);
		}
		if output_node.ctx.is_null() {
			return Err(Error::Failed(
				"No node connected to the viewer output".to_string(),
			));
		}

		let audio_params_ptr = if self.audio_params.ctx.is_null() {
			std::ptr::null()
		} else {
			&self.audio_params as *const CHandle
		};
		let force = &self.force_params;
		let params = OakRenderVideoTicketParams {
			output_node,
			video_params: self.video_params,
			audio_params: audio_params_ptr,
			time_num: time.numerator(),
			time_den: time.denominator(),
			color_manager: self.color_manager,
			mode: self.mode,
			force_width: force.force_width,
			force_height: force.force_height,
			force_matrix: force.force_matrix,
			has_force_matrix: if force.has_force_matrix { 1 } else { 0 },
			force_format: force.force_format,
			force_channel_count: force.force_channel_count,
			force_color_output: force.force_color_output,
			force_color_transform: OakColorTransform {
				ctx: std::ptr::null_mut(),
				addref: None,
				release: None,
				abi_version: 0,
			},
			cache: self.cache,
			footage_filename: std::ptr::null(),
			footage_stream: 0,
			montage: std::ptr::null(),
			montage_count: 0,
		};

		let ticket = unsafe {
			bridge::render::oakrender_ticket_render_frame(
				&params,
				Some(ticket_finished),
				dispatch as *mut std::ffi::c_void,
			)
		};
		// Per-frame call: release the borrowed handle box (the ticket keeps
		// the native node, not the handle).
		unsafe {
			bridge::node::oaknode_node_free(&mut output_node);
		}
		if ticket.ctx.is_null() {
			return Err(Error::Failed(
				"Failed to start frame render ticket".to_string(),
			));
		}
		Ok(ticket)
	}

	/// Submit one audio ticket for `range` (the Rust API renders a single
	/// range; the C++ submits one ticket per audio range).
	fn submit_audio_ticket(
		&self,
		range: TimeRange,
		dispatch: *mut RenderDispatch,
	) -> Result<OakRenderTicket> {
		let mut output_node = CHandle::null();
		unsafe {
			bridge::node::oaknode_node_input_get_connected_node(
				self.viewer,
				cstr(OAKNODE_SEQUENCE_SAMPLES_INPUT),
				&mut output_node,
			);
		}
		if output_node.ctx.is_null() {
			return Err(Error::Failed(
				"No node connected to the viewer samples input".to_string(),
			));
		}

		let audio_params_ptr = if self.audio_params.ctx.is_null() {
			std::ptr::null()
		} else {
			&self.audio_params as *const CHandle
		};
		let ticket = unsafe {
			bridge::render::oakrender_ticket_render_audio(
				output_node,
				range.in_().numerator(),
				range.in_().denominator(),
				range.out().numerator(),
				range.out().denominator(),
				audio_params_ptr,
				self.mode,
				Some(ticket_finished),
				dispatch as *mut std::ffi::c_void,
			)
		};
		unsafe {
			bridge::node::oaknode_node_free(&mut output_node);
		}
		if ticket.ctx.is_null() {
			return Err(Error::Failed(
				"Failed to start audio render ticket".to_string(),
			));
		}
		Ok(ticket)
	}

	/// Submit a video ticket and account it as in-flight.
	///
	/// The in-flight count is bumped **before** the submit: a ticket can
	/// complete synchronously (its callback fires before the submit returns),
	/// so the callback's decrement must always see its increment. A failed
	/// submit (null ticket) never fires a callback, so the bump is rolled
	/// back.
	fn start_video_ticket(
		&self,
		time: Rational,
		dispatch: *mut RenderDispatch,
		in_flight: &mut Vec<OakRenderTicket>,
	) -> Result<()> {
		unsafe {
			(&*dispatch).running.fetch_add(1, Ordering::SeqCst);
		}
		match self.submit_video_ticket(time, dispatch) {
			Ok(ticket) => {
				in_flight.push(ticket);
				Ok(())
			}
			Err(e) => {
				unsafe {
					(&*dispatch).running.fetch_sub(1, Ordering::SeqCst);
				}
				Err(e)
			}
		}
	}

	/// Submit the audio ticket and account it as in-flight (see
	/// [`RenderTask::start_video_ticket`] for the counting contract).
	fn start_audio_ticket(
		&self,
		range: TimeRange,
		dispatch: *mut RenderDispatch,
		in_flight: &mut Vec<OakRenderTicket>,
	) -> Result<()> {
		unsafe {
			(&*dispatch).running.fetch_add(1, Ordering::SeqCst);
		}
		match self.submit_audio_ticket(range, dispatch) {
			Ok(ticket) => {
				in_flight.push(ticket);
				Ok(())
			}
			Err(e) => {
				unsafe {
					(&*dispatch).running.fetch_sub(1, Ordering::SeqCst);
				}
				Err(e)
			}
		}
	}

	/// Map a finished ticket back to its delivery slot: the audio slot for
	/// audio tickets, the matching frame slot for video tickets.
	fn classify_ticket(
		&self,
		ticket: &OakRenderTicket,
		slot_by_key: &HashMap<(i32, i64, i64), usize>,
	) -> Option<usize> {
		let kind = unsafe { bridge::render::oakrender_ticket_get_type(*ticket) };
		if kind == TICKET_AUDIO {
			let mut a = 0i64;
			let mut b = 1i64;
			let mut c = 0i64;
			let mut d = 1i64;
			unsafe {
				bridge::render::oakrender_ticket_get_range(*ticket, &mut a, &mut b, &mut c, &mut d);
			}
			slot_by_key.get(&(TICKET_AUDIO, a, b)).copied()
		} else if kind == TICKET_VIDEO {
			let mut n = 0i64;
			let mut d = 1i64;
			unsafe {
				bridge::render::oakrender_ticket_get_time(*ticket, &mut n, &mut d);
			}
			slot_by_key.get(&(TICKET_VIDEO, n, d)).copied()
		} else {
			None
		}
	}

	/// Drive the whole render: keep up to `max_inflight` frame tickets in
	/// flight (audio first, then frames in timestamp order), deliver each
	/// finished ticket's result to the behavior hooks in timestamp order via
	/// the reorder buffer, and stop on cancellation or a hook error —
	/// cancelling and waiting every still-running ticket so their
	/// completions fire exactly once.
	///
	/// `task` is the live driving task (cancellation/progress/error
	/// reporting); `behavior` is the concrete subclass receiving the
	/// `frame_downloaded`/`audio_downloaded` hooks.
	pub fn render(&mut self, task: &mut Task, behavior: &mut dyn RenderTaskBehavior) -> Result<()> {
		let timebase = self.timebase();

		// Compute the frame timestamps (progress denominator mirrors the
		// C++ `total_length <= 0 -> 1` guard).
		let mut frame_times: Vec<Rational> = Vec::new();
		let mut t = self.export_range.in_();
		while t < self.export_range.out() {
			frame_times.push(t);
			t = t + timebase;
		}
		self.total_frames = frame_times.len() as i64;
		let total_length = if frame_times.is_empty() {
			1.0
		} else {
			frame_times.len() as f64
		};

		// Delivery order: the audio range first (mirrors the C++ queue
		// order), then every frame in ascending timestamp order. The reorder
		// buffer delivers to the hooks in exactly this order no matter the
		// completion order.
		let mut slots: Vec<DeliverySlot> = Vec::new();
		if self.audio_enabled {
			slots.push(DeliverySlot {
				kind: TICKET_AUDIO,
				time: self.export_range.in_(),
			});
		}
		for &ft in &frame_times {
			slots.push(DeliverySlot {
				kind: TICKET_VIDEO,
				time: ft,
			});
		}
		let mut slot_by_key: HashMap<(i32, i64, i64), usize> = HashMap::with_capacity(slots.len());
		for (i, slot) in slots.iter().enumerate() {
			slot_by_key.insert(
				(slot.kind, slot.time.numerator(), slot.time.denominator()),
				i,
			);
		}
		let total_slots = slots.len();

		// The shared completion channel. Heap-allocated: the C callback
		// reaches it through its `userdata` pointer. Freed only once every
		// in-flight ticket has fired its callback (`running == 0`).
		let dispatch = Box::into_raw(Box::new(RenderDispatch::new()));
		// Shared view of the same state for the render thread (the raw
		// pointer stays valid until the box is freed below).
		let dispatch_ref = unsafe { &*dispatch };
		let window = self.max_inflight.max(1);

		// Tickets we hold (the submitter's copy of each handle, released
		// exactly once at the end); `consumed` counts tickets whose finished
		// queue copy has been popped, so `in_flight.len() - consumed` is the
		// live window.
		let mut in_flight: Vec<OakRenderTicket> = Vec::new();
		let mut consumed = 0usize;
		// Next frame timestamp to submit and next delivery slot.
		let mut next_frame_index = 0usize;
		let mut next_slot = 0usize;
		// Reorder buffer: finished tickets not yet deliverable.
		let mut pending: HashMap<usize, OakRenderTicket> = HashMap::new();
		let mut progress_counter = 0.0;
		let mut result: Result<()> = Ok(());

		// Queue audio first (mirrors the C++ order).
		if self.audio_enabled && result.is_ok() {
			if let Err(e) = self.start_audio_ticket(self.export_range, dispatch, &mut in_flight) {
				result = Err(e);
			}
		}

		// Start the initial frame window.
		if result.is_ok() {
			while in_flight.len() < window && next_frame_index < frame_times.len() {
				if let Err(e) =
					self.start_video_ticket(frame_times[next_frame_index], dispatch, &mut in_flight)
				{
					result = Err(e);
					break;
				}
				next_frame_index += 1;
			}
		}

		while result.is_ok() && !task.is_cancelled() && next_slot < total_slots {
			// Drain the completion queue into the reorder buffer.
			while let Some(ticket) = dispatch_ref.pop_finished() {
				consumed += 1;
				match self.classify_ticket(&ticket, &slot_by_key) {
					Some(slot_index) => {
						pending.insert(slot_index, ticket);
					}
					None => {
						result = Err(Error::Failed(
							"Render ticket reported an unexpected timestamp".to_string(),
						));
						break;
					}
				}
			}
			if result.is_err() {
				break;
			}

			// Deliver contiguous results in the observable (timestamp) order.
			while let Some(ticket) = pending.remove(&next_slot) {
				let slot = &slots[next_slot];
				if slot.kind == TICKET_AUDIO {
					unsafe {
						bridge::render::oakrender_ticket_get_samples(ticket, std::ptr::null_mut());
					}
					if let Err(e) = behavior.audio_downloaded(task, CHandle::null()) {
						result = Err(e);
						break;
					}
				} else {
					let mut frame = CHandle::null();
					unsafe {
						bridge::render::oakrender_ticket_get_frame(ticket, &mut frame);
					}
					if let Err(e) = behavior.frame_downloaded(task, frame) {
						result = Err(e);
						break;
					}
					if !frame.ctx.is_null() {
						unsafe {
							bridge::render::oakrender_codec_frame_free(&mut frame);
						}
					}
					if self.native_progress_signalling {
						progress_counter += 1.0;
						task.emit_progress(progress_counter / total_length);
					}
				}
				next_slot += 1;
			}
			if result.is_err() || task.is_cancelled() {
				break;
			}

			// Refill the window: one new ticket per delivered frame.
			while in_flight.len().saturating_sub(consumed) < window
				&& next_frame_index < frame_times.len()
			{
				if let Err(e) =
					self.start_video_ticket(frame_times[next_frame_index], dispatch, &mut in_flight)
				{
					result = Err(e);
					break;
				}
				next_frame_index += 1;
			}
			if result.is_err() {
				break;
			}
			if next_slot >= total_slots {
				break;
			}

			// Wait for the next completion (a cancellation or a hook error
			// aborts the wait; in-flight tickets then finish, waking us).
			let mut guard = dispatch_ref
				.finished
				.lock()
				.unwrap_or_else(|e| e.into_inner());
			while guard.is_empty()
				&& dispatch_ref.running.load(Ordering::SeqCst) > 0
				&& !task.is_cancelled()
			{
				guard = dispatch_ref
					.cv
					.wait(guard)
					.unwrap_or_else(|e| e.into_inner());
			}
			drop(guard);
			if dispatch_ref
				.finished
				.lock()
				.unwrap_or_else(|e| e.into_inner())
				.is_empty() && dispatch_ref.running.load(Ordering::SeqCst) == 0
			{
				// Every ticket finished and its queue copy was consumed.
				break;
			}
		}

		// Cancellation that aborted the loop before every slot was delivered
		// maps to the cancellation error (the sync loop returned
		// `Error::Cancelled` from its between-frames check; a render that
		// finished every frame despite a late cancel stays successful).
		if result.is_ok() && task.is_cancelled() && next_slot < total_slots {
			result = Err(Error::Cancelled);
		}

		// Tear down. On cancellation or error, cancel and wait every ticket
		// still in flight (the C++ abort path), so their completions still
		// fire exactly once. Then wait until every callback has returned,
		// release the submitter's handles, and free the completion channel.
		if result.is_err() || task.is_cancelled() {
			for &ticket in &in_flight {
				unsafe {
					bridge::render::oakrender_ticket_cancel(ticket);
					bridge::render::oakrender_ticket_wait(ticket);
				}
			}
		}
		dispatch_ref.wait_idle();
		for ticket in in_flight.iter_mut() {
			unsafe {
				bridge::render::oakrender_ticket_free(ticket);
			}
		}
		unsafe {
			drop(Box::from_raw(dispatch));
		}

		result
	}

	/// A detached render state used while a concrete task temporarily moves
	/// its render out of itself to drive it with itself as the behavior
	/// (avoids a self-referential borrow). Never used for actual rendering.
	pub(crate) fn placeholder() -> RenderTask {
		RenderTask::new(
			Task::new("", CHandle::null()),
			CHandle::null(),
			CHandle::null(),
			CHandle::null(),
			ForceParams::default(),
			None,
		)
	}
}

/// One deliverable unit of the render: an audio range or a frame time, in
/// the observable delivery order (audio first, then frames in time order).
struct DeliverySlot {
	/// `TICKET_AUDIO` or `TICKET_VIDEO`.
	kind: i32,
	/// Frame time (video) or range start (audio).
	time: Rational,
}

/// Shared state between the render thread and the ticket-finished
/// callbacks (the ticket's async return channel, mirroring the C++
/// `finished_mutex_`/`finished_tickets_`/`finished_wait_cond_`). One
/// instance per [`RenderTask::render`] run, reached from the C callback
/// through its `userdata` pointer; heap-allocated for the run and freed
/// only after every in-flight ticket has fired (`running == 0`).
struct RenderDispatch {
	/// Finished tickets in completion order (callbacks push here).
	finished: Mutex<VecDeque<OakRenderTicket>>,
	/// Tickets whose completion callback has not fired yet.
	running: AtomicUsize,
	/// Wakes the render thread when a ticket finishes.
	cv: Condvar,
}

impl RenderDispatch {
	fn new() -> RenderDispatch {
		RenderDispatch {
			finished: Mutex::new(VecDeque::new()),
			running: AtomicUsize::new(0),
			cv: Condvar::new(),
		}
	}

	/// Pop the next finished ticket (the callback's borrowed copy; `None`
	/// when the queue is empty).
	fn pop_finished(&self) -> Option<OakRenderTicket> {
		self.finished
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.pop_front()
	}

	/// Block until every submitted ticket has fired its callback. Called
	/// before freeing `self`, so no callback can touch the state afterwards.
	fn wait_idle(&self) {
		let mut guard = self.finished.lock().unwrap_or_else(|e| e.into_inner());
		while self.running.load(Ordering::SeqCst) > 0 {
			guard = self.cv.wait(guard).unwrap_or_else(|e| e.into_inner());
		}
	}
}

/// Ticket-finished callback, mirroring the C++ `on_ticket_finished`. Fires
/// on the ticket's finishing thread; pushes the ticket's borrowed handle
/// copy into the completion queue and wakes the render thread. The push
/// precedes the in-flight decrement, so a waiter never observes
/// `running == 0` with a missing queue entry.
///
/// # Safety
///
/// `userdata` must point to a live `RenderDispatch` for the duration of
/// every in-flight ticket (guaranteed by [`RenderTask::render`]'s
/// `wait_idle` before freeing it).
unsafe extern "C" fn ticket_finished(ticket: OakRenderTicket, userdata: *mut std::ffi::c_void) {
	let dispatch = unsafe { &*(userdata as *const RenderDispatch) };
	let mut queue = dispatch.finished.lock().unwrap_or_else(|e| e.into_inner());
	queue.push_back(ticket);
	dispatch.running.fetch_sub(1, Ordering::SeqCst);
	dispatch.cv.notify_all();
}

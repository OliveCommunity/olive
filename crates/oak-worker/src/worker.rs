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

//! The render worker runtime — the Rust port of
//! `engine/src/capi/worker.cpp`, owned by the oak-worker binary since
//! M14 R2 (the facade keeps its own copy for the frozen
//! `oakengine_worker_*` C ABI).
//!
//!   - **Backend selection.** [`Renderer::create`] initializes the render
//!     backend through the oakrender crate's direct Rust API
//!     ([`oakrender::backend::DisplayRenderer`]), falling back to the
//!     direct OpenGL renderer exactly like the C++ `create_renderer()`
//!     chain.
//!   - **The session.** [`WorkerSession`] holds the renderer, the
//!     shared-memory frame-slot pools ([`crate::ipc::FrameSlotPool`]) and
//!     the shutdown flag, and answers one NDJSON control message at a time.
//!   - **The main loop.** [`worker_main`] creates the session, loads the
//!     runtime config, writes the startup handshake, and serves the
//!     stdin/stdout NDJSON loop until a `shutdown` message or EOF.
//!
//! The control-plane protocol is the same NDJSON the C++ worker speaks
//! (`engine/render/ipc/ipcmessage.cpp`): one compact JSON object per line,
//! `"type"`-dispatched ([`crate::ipc`]), with `handshake` carrying the
//! shared-memory geometry the worker attaches to via the real
//! [`crate::ipc`] transport. `load_graph`/`render_frame` reproduce the
//! C++ validation and then answer with the documented "not yet available"
//! errors (the oaknode graph crate is still a skeleton).

use std::io::{self, BufRead, Write};

use serde_json::Value;

use oakrender::backend::{BackendKind, DisplayRenderer};

use crate::ipc::{
	error_message, write_message, FrameSlotPool, HandshakeMsg, LoadGraphMsg, RenderFrameMsg,
	SharedMemoryRegion, ShmMode, TYPE_CANCEL, TYPE_HANDSHAKE, TYPE_LOAD_GRAPH, TYPE_RENDER_FRAME,
	TYPE_SHUTDOWN,
};
use crate::{log_error, PROTOCOL_VERSION};

/// Why `load_graph` answers "not yet available" (after the real file checks).
const GRAPH_STUB: &str = "load_graph: node-graph deserialization is not yet available in the \
     Rust worker (the oaknode crate is a todo!() skeleton; see worker/rust/README.md)";

/// Why `render_frame` answers "not yet available".
const RENDER_STUB: &str = "render_frame: frame rendering is not yet available in the Rust \
     worker (no node-graph or render-pipeline backing; the shm frame-slot transport is \
     attached but there is no graph to render; see worker/rust/README.md)";

// ---------------------------------------------------------------------------
// Renderer (backend selection)
// ---------------------------------------------------------------------------

/// Whether `backend` requests no renderer (worker.cpp
/// `backend_requests_no_renderer()`: NULL, "" and "none").
pub fn is_no_backend(backend: &str) -> bool {
	backend.is_empty() || backend.eq_ignore_ascii_case("none")
}

/// A live, initialized oakrender display renderer (destroyed on drop).
pub struct Renderer {
	/// The oakrender crate's value-typed display renderer (single-lib
	/// unification; the CHandle-based C ABI is deleted).
	inner: DisplayRenderer,
}

impl Renderer {
	/// Create and initialize a renderer through the oakrender crate's
	/// direct Rust API, trying the named dynamic backend first and falling
	/// back to the direct OpenGL renderer — the exact fallback chain of
	/// worker.cpp `create_renderer()`.
	pub fn create(backend: &str) -> Result<Renderer, String> {
		match Self::create_dynamic(backend) {
			Ok(r) => Ok(r),
			Err(first) => {
				log_error(&format!(
					"failed to initialize dynamic {backend} backend: {first}; falling back to direct OpenGL renderer"
				));
				Self::create_opengl().map_err(|second| {
					format!("{first}; direct OpenGL fallback also failed: {second}")
				})
			}
		}
	}

	/// Try the named dynamic backend (`DisplayRenderer::new` +
	/// `init`, the single-lib equivalent of
	/// `oakrender_display_renderer_create_dynamic` + `_init`).
	fn create_dynamic(backend: &str) -> Result<Renderer, String> {
		let renderer = DisplayRenderer::new(BackendKind::from_config_string(backend));
		Self::init_inner(renderer, &format!("dynamic {backend}"))
	}

	/// Fall back to the direct OpenGL renderer.
	fn create_opengl() -> Result<Renderer, String> {
		let renderer = DisplayRenderer::new(BackendKind::Gl);
		Self::init_inner(renderer, "direct OpenGL")
	}

	/// Initialize a freshly created renderer.
	fn init_inner(mut renderer: DisplayRenderer, what: &str) -> Result<Renderer, String> {
		// NULL gl_context makes the backend use its default device/context
		// path.
		if let Err(e) = renderer.init(std::ptr::null_mut()) {
			return Err(format!("failed to initialize {what} renderer ({e})"));
		}
		Ok(Renderer { inner: renderer })
	}

	/// 1 when the renderer is OpenGL-based (the C++ worker uses the GL
	/// context to announce the negotiated GL version in the handshake).
	///
	/// Not called yet: the oakrender module exposes no GL context
	/// version, so the startup handshake omits `gl_major`/`gl_minor`.
	#[allow(dead_code)]
	pub fn is_open_gl(&self) -> bool {
		self.inner.is_open_gl()
	}
}

// ---------------------------------------------------------------------------
// WorkerSession
// ---------------------------------------------------------------------------

/// The worker-side session state machine — the Rust mirror of
/// `OakWorkerSession` in worker.cpp. Holds the renderer, the attached
/// shared-memory frame-slot pools and the shutdown flag, and answers one
/// NDJSON control message at a time.
pub struct WorkerSession {
	renderer: Option<Renderer>,
	shutdown_requested: bool,
	runtime_initialized: bool,
	output_region: Option<SharedMemoryRegion>,
	output_pool: Option<FrameSlotPool>,
	input_region: Option<SharedMemoryRegion>,
	input_pool: Option<FrameSlotPool>,
}

impl WorkerSession {
	/// Create a session for `backend`, mirroring
	/// `oakengine_worker_session_create()`: "none"/"" skips renderer
	/// creation, anything else initializes the render backend through the
	/// oakrender crate's direct Rust API (dynamic -> OpenGL fallback).
	pub fn create(backend: &str) -> Result<WorkerSession, String> {
		let renderer = if is_no_backend(backend) {
			None
		} else {
			Some(Renderer::create(backend)?)
		};
		Ok(WorkerSession {
			renderer,
			shutdown_requested: false,
			runtime_initialized: false,
			output_region: None,
			output_pool: None,
			input_region: None,
			input_pool: None,
		})
	}

	/// 1 when the session holds a successfully initialized render backend.
	pub fn has_renderer(&self) -> bool {
		self.renderer.is_some()
	}

	/// 1 once a shutdown control message has been received.
	pub fn shutdown_requested(&self) -> bool {
		self.shutdown_requested
	}

	/// Load the runtime services the session depends on — the Rust analog
	/// of the C++ `initialize_runtime()`. Of the C++ list (config, node
	/// factory, color manager, frame/disk managers, project serializer)
	/// only the color-manager default config has a Rust backing linked into
	/// the worker binary; the rest are logged and skipped. Always returns
	/// true (the C++ returns true unconditionally).
	pub fn initialize_runtime(&mut self) -> bool {
		if self.runtime_initialized {
			return true;
		}
		log_error("runtime: loading color-manager default config");
		if let Err(e) = oakrender::color::set_up_default_config() {
			log_error(&format!(
				"runtime: color-manager default config failed ({e}); continuing"
			));
		}
		log_error(
			"runtime: config / node factory / frame manager / disk manager / project \
			 serializer have no Rust backing in the worker binary; skipped",
		);
		self.runtime_initialized = true;
		true
	}

	/// The startup handshake the worker sends to its parent
	/// (`worker.cpp startup_handshake()`): protocol version 1 and empty
	/// shared-memory geometry — the parent creates the segments and
	/// announces their geometry in its handshake reply.
	///
	/// Deviation from the C++: `gl_major`/`gl_minor` are omitted because
	/// the oakrender module exposes no GL context version.
	pub fn startup_handshake(&self) -> Value {
		HandshakeMsg {
			protocol_version: PROTOCOL_VERSION,
			shm_key: String::new(),
			input_shm_key: String::new(),
			input_slots: 0,
			output_slots: 0,
			slot_data_bytes: 0,
			input_slot_data_bytes: 0,
		}
		.to_json()
	}

	/// Handle one complete NDJSON control line and produce the response, if
	/// any — the port of worker.cpp `handle()`. A malformed line yields an
	/// error response (the loop continues), never a failure.
	pub fn handle_line(&mut self, line: &str) -> Option<Value> {
		let msg: Value = match serde_json::from_str::<Value>(line) {
			Ok(v) if v.is_object() => v,
			_ => return Some(error_message("malformed control message", None)),
		};
		let typ = msg.get("type").and_then(Value::as_str).unwrap_or("");
		match typ {
			TYPE_HANDSHAKE => self.handle_handshake(&msg),
			TYPE_LOAD_GRAPH => self.handle_load_graph(&msg),
			TYPE_RENDER_FRAME => self.handle_render_frame(&msg),
			// cancel: the worker does synchronous single-frame work
			// (nothing in flight), so a cancel produces no response.
			TYPE_CANCEL => None,
			TYPE_SHUTDOWN => {
				self.shutdown_requested = true;
				None
			}
			other => Some(error_message(
				&format!("unknown message type: {other}"),
				None,
			)),
		}
	}

	/// `handshake`: validate and attach the shared-memory frame-slot pools
	/// — the real port of worker.cpp `attach_output_pool()`.
	fn handle_handshake(&mut self, msg: &Value) -> Option<Value> {
		let hs: HandshakeMsg = match serde_json::from_value(msg.clone()) {
			Ok(hs) => hs,
			Err(_) => return Some(error_message("invalid handshake message", None)),
		};
		if hs.protocol_version != PROTOCOL_VERSION {
			return Some(error_message(
				&format!("unsupported protocol version {}", hs.protocol_version),
				None,
			));
		}
		if hs.shm_key.is_empty() || hs.output_slots <= 0 || hs.slot_data_bytes <= 0 {
			return Some(error_message(
				"handshake missing output shared-memory geometry",
				None,
			));
		}

		// A re-handshake replaces the pools (worker.cpp resets the input
		// pool before attaching the output).
		self.input_pool = None;
		self.input_region = None;
		self.output_pool = None;
		self.output_region = None;

		let bytes =
			FrameSlotPool::bytes_needed(hs.output_slots as u32, hs.slot_data_bytes as usize);
		let mut output_region = SharedMemoryRegion::new();
		if !output_region.open(&hs.shm_key, bytes, ShmMode::Attach) {
			return Some(error_message(
				&format!("failed to attach shared memory: {}", output_region.error()),
				None,
			));
		}
		// SAFETY: `output_region` is a live mapping of at least `bytes`
		// bytes (checked above).
		let output_pool = unsafe { FrameSlotPool::attach(output_region.data()) };
		if !output_pool.is_valid() {
			return Some(error_message(
				"shared memory does not contain a frame slot pool",
				None,
			));
		}
		self.output_region = Some(output_region);
		self.output_pool = Some(output_pool);

		if hs.input_slots > 0 {
			if hs.input_shm_key.is_empty() || hs.input_slot_data_bytes <= 0 {
				return Some(error_message(
					"handshake missing input shared-memory geometry",
					None,
				));
			}
			let input_bytes = FrameSlotPool::bytes_needed(
				hs.input_slots as u32,
				hs.input_slot_data_bytes as usize,
			);
			let mut input_region = SharedMemoryRegion::new();
			if !input_region.open(&hs.input_shm_key, input_bytes, ShmMode::Attach) {
				return Some(error_message(
					&format!(
						"failed to attach input shared memory: {}",
						input_region.error()
					),
					None,
				));
			}
			// SAFETY: `input_region` is a live mapping of at least
			// `input_bytes` bytes (checked above).
			let input_pool = unsafe { FrameSlotPool::attach(input_region.data()) };
			if !input_pool.is_valid() {
				return Some(error_message(
					"input shared memory does not contain a frame slot pool",
					None,
				));
			}
			self.input_region = Some(input_region);
			self.input_pool = Some(input_pool);
		}

		// Success: no response (worker.cpp leaves `response` untouched).
		None
	}

	/// `load_graph`: the file checks are real (mirror worker.cpp
	/// `load_graph()`); the deserialization is the documented stub.
	fn handle_load_graph(&mut self, msg: &Value) -> Option<Value> {
		let load: LoadGraphMsg = match serde_json::from_value(msg.clone()) {
			Ok(l) => l,
			Err(_) => return Some(error_message("invalid load_graph message", None)),
		};
		match std::fs::metadata(&load.path) {
			Err(_) => Some(error_message(
				&format!("graph file does not exist: {}", load.path),
				None,
			)),
			Ok(md) if md.len() == 0 => Some(error_message(
				&format!("graph file is empty: {}", load.path),
				None,
			)),
			Ok(md) => {
				log_error(&format!(
					"LoadGraph: loading {} ({} bytes)",
					load.path,
					md.len()
				));
				Some(error_message(GRAPH_STUB, None))
			}
		}
	}

	/// `render_frame`: the graph/render pipeline has no Rust backing, so a
	/// render request is answered with a clear error carrying the ticket.
	fn handle_render_frame(&mut self, msg: &Value) -> Option<Value> {
		let render: RenderFrameMsg = match serde_json::from_value(msg.clone()) {
			Ok(r) => r,
			Err(_) => return Some(error_message("invalid render_frame message", None)),
		};
		Some(error_message(RENDER_STUB, Some(render.ticket)))
	}
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

/// Full render-worker main, transport-agnostic in the backend name.
///
/// Mirrors `oakengine_worker_main()` in worker.cpp: create the session
/// (which initializes the render backend), load the runtime config, write
/// the startup handshake, then serve the NDJSON control loop on
/// stdin/stdout until a `shutdown` message or EOF. Returns the process
/// exit code.
pub fn worker_main(backend: &str) -> i32 {
	// 1. Session creation initializes the render backend through the
	//    oakrender crate's direct Rust API
	//    (oakengine_worker_session_create()).
	let mut session = match WorkerSession::create(backend) {
		Ok(s) => s,
		Err(msg) => {
			log_error(&msg);
			return 1;
		}
	};
	if !session.has_renderer() {
		// Mirrors oakengine_worker_main(): without a renderer the worker
		// cannot do anything, so it exits 1. ("--backend none" lands here.)
		log_error("no renderer initialized");
		return 1;
	}

	// 2. Runtime services (config load etc.).
	if !session.initialize_runtime() {
		return 1;
	}

	// 3. Startup handshake before the loop (mirrors worker.cpp main).
	let handshake = session.startup_handshake();
	let stdout = io::stdout();
	let mut out = io::BufWriter::new(stdout.lock());
	if let Err(e) = write_message(&mut out, &handshake) {
		log_error(&format!("failed to write startup handshake: {e}"));
		return 1;
	}
	if let Err(e) = out.flush() {
		log_error(&format!("failed to flush startup handshake: {e}"));
		return 1;
	}

	// 4. NDJSON control loop until a shutdown message or EOF.
	let stdin = io::stdin();
	let mut reader = stdin.lock();
	let mut line = String::new();
	let mut exit_code = 0;
	while !session.shutdown_requested() {
		line.clear();
		match reader.read_line(&mut line) {
			Ok(0) => break, // EOF: the parent closed the control pipe.
			Ok(_) => {}
			Err(e) => {
				log_error(&format!("failed to read control line: {e}"));
				break;
			}
		}
		if line.trim().is_empty() {
			// Blank lines are skipped silently (read_message() semantics).
			continue;
		}
		if let Some(response) = session.handle_line(&line) {
			if let Err(e) = write_message(&mut out, &response) {
				log_error(&format!("failed to write response: {e}"));
				exit_code = 1;
				break;
			}
			if let Err(e) = out.flush() {
				log_error(&format!("failed to flush response: {e}"));
				exit_code = 1;
				break;
			}
		}
	}
	exit_code
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::ipc::{FrameSlotPool, SharedMemoryRegion, ShmMode};
	use serde_json::json;
	use std::ptr;

	fn test_key(name: &str) -> String {
		static COUNTER: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
		let n = COUNTER.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
		SharedMemoryRegion::make_key(i64::from(std::process::id()), (n & 0x7FFF) as i32)
			+ &format!("-w-{name}")
	}

	/// The "parent" side of a handshake: create an output segment holding a
	/// pool, optionally an input segment, and return the handshake message
	/// plus the owner regions (kept alive by the caller).
	fn parent_side(
		slots: i32,
		slot_bytes: i64,
		input: bool,
	) -> (Value, SharedMemoryRegion, Option<SharedMemoryRegion>) {
		let out_key = test_key("out");
		let out_bytes = FrameSlotPool::bytes_needed(slots as u32, slot_bytes as usize);
		let mut out_region = SharedMemoryRegion::new();
		assert!(
			out_region.open(&out_key, out_bytes, ShmMode::Create),
			"{}",
			out_region.error()
		);
		// SAFETY: live mapping sized by bytes_needed.
		let _pool =
			unsafe { FrameSlotPool::create(out_region.data(), slots as u32, slot_bytes as usize) };

		let (in_key, in_bytes, in_region) = if input {
			let in_key = test_key("in");
			let in_bytes = FrameSlotPool::bytes_needed(slots as u32, slot_bytes as usize);
			let mut in_region = SharedMemoryRegion::new();
			assert!(in_region.open(&in_key, in_bytes, ShmMode::Create));
			// SAFETY: live mapping.
			let _ = unsafe {
				FrameSlotPool::create(in_region.data(), slots as u32, slot_bytes as usize)
			};
			(Some(in_key), Some(in_bytes), Some(in_region))
		} else {
			(None, None, None)
		};

		let hs = json!({
			"type": "handshake",
			"protocol_version": PROTOCOL_VERSION,
			"shm_key": out_key,
			"input_shm_key": in_key.unwrap_or_default(),
			"input_slots": if input { slots } else { 0 },
			"output_slots": slots,
			"slot_data_bytes": slot_bytes,
			"input_slot_data_bytes": in_bytes.unwrap_or(0),
		});
		(hs, out_region, in_region)
	}

	#[test]
	fn no_backend_detection_matches_cpp() {
		assert!(is_no_backend(""));
		assert!(is_no_backend("none"));
		assert!(is_no_backend("NONE"));
		assert!(!is_no_backend("opengl"));
		assert!(!is_no_backend("vulkan"));
	}

	#[test]
	fn none_backend_session_has_no_renderer_but_serves_messages() {
		let mut s = WorkerSession::create("none").unwrap();
		assert!(!s.has_renderer());
		let resp = s.handle_line(r#"{"type":"shutdown"}"#);
		assert!(resp.is_none());
		assert!(s.shutdown_requested());
	}

	#[test]
	fn startup_handshake_is_protocol_version_1_with_empty_geometry() {
		let s = WorkerSession::create("none").unwrap();
		let hs = s.startup_handshake();
		assert_eq!(
			hs,
			json!({
				"type": "handshake",
				"protocol_version": 1,
				"shm_key": "",
				"input_shm_key": "",
				"input_slots": 0,
				"output_slots": 0,
				"slot_data_bytes": 0,
				"input_slot_data_bytes": 0,
			})
		);
	}

	#[test]
	fn malformed_line_yields_error_response() {
		let mut s = WorkerSession::create("none").unwrap();
		let resp = s.handle_line("this is not json").unwrap();
		assert_eq!(resp["type"], "error");
		assert_eq!(resp["message"], "malformed control message");
	}

	#[test]
	fn unknown_message_type_yields_error_response() {
		let mut s = WorkerSession::create("none").unwrap();
		let resp = s.handle_line(r#"{"type":"frobnicate"}"#).unwrap();
		assert_eq!(resp["message"], "unknown message type: frobnicate");
	}

	#[test]
	fn cancel_and_shutdown_produce_no_response() {
		let mut s = WorkerSession::create("none").unwrap();
		assert!(s.handle_line(r#"{"type":"cancel","ticket":5}"#).is_none());
		assert!(s.handle_line(r#"{"type":"shutdown"}"#).is_none());
		assert!(s.shutdown_requested());
	}

	#[test]
	fn handshake_wrong_protocol_version() {
		let mut s = WorkerSession::create("none").unwrap();
		let resp = s
			.handle_line(
				r#"{"type":"handshake","protocol_version":99,"shm_key":"k","output_slots":1,"slot_data_bytes":16}"#,
			)
			.unwrap();
		assert_eq!(resp["message"], "unsupported protocol version 99");
	}

	#[test]
	fn handshake_missing_geometry() {
		let mut s = WorkerSession::create("none").unwrap();
		let resp = s
			.handle_line(r#"{"type":"handshake","protocol_version":1}"#)
			.unwrap();
		assert_eq!(
			resp["message"],
			"handshake missing output shared-memory geometry"
		);
	}

	#[test]
	fn handshake_attaches_real_output_pool() {
		let mut s = WorkerSession::create("none").unwrap();
		let (hs, out_region, _in) = parent_side(4, 4096, false);
		let resp = s.handle_line(&hs.to_string());
		assert!(resp.is_none(), "unexpected error: {resp:?}");
		// The session now holds a real attached pool with the parent's
		// geometry.
		let out_pool = s.output_pool.as_ref().unwrap();
		assert_eq!(out_pool.slot_count(), 4);
		assert_eq!(out_pool.slot_data_bytes(), 4096);

		// The two views share the same rings, not copies: the parent pops a
		// free slot and the worker's pool sees the ring cursor move; the
		// parent's publish lands in the worker's ready ring.
		// SAFETY: `out_region` is a live mapping containing the pool the
		// session attached to.
		let parent_pool = unsafe { FrameSlotPool::attach(out_region.data()) };
		let mut parent_slot = 0;
		assert!(unsafe { parent_pool.acquire(&mut parent_slot) });
		assert_eq!(parent_slot, 0);
		let mut worker_slot = 0;
		assert!(unsafe { out_pool.acquire(&mut worker_slot) });
		assert_eq!(worker_slot, 1, "worker must see the parent's free-ring pop");

		// SAFETY: `parent_slot` was acquired by the parent; slot_bytes
		// writable.
		unsafe {
			ptr::write_bytes(parent_pool.slot_data(parent_slot), 0xAB, 64);
		}
		assert!(unsafe { parent_pool.publish(parent_slot) });
		let mut consumed = 0;
		assert!(unsafe { out_pool.consume(&mut consumed) });
		assert_eq!(consumed, parent_slot);
		// SAFETY: `consumed` was consumed by the worker's pool.
		assert_eq!(unsafe { *out_pool.slot_data_const(consumed) }, 0xAB);
		// Clean up so the region drop at test end unlinks cleanly.
		unsafe { out_pool.release(consumed) };
		unsafe { out_pool.release(worker_slot) };
	}

	#[test]
	fn handshake_attaches_input_pool_too() {
		let mut s = WorkerSession::create("none").unwrap();
		let (hs, _out, _in) = parent_side(2, 256, true);
		let resp = s.handle_line(&hs.to_string());
		assert!(resp.is_none(), "unexpected error: {resp:?}");
		assert!(s.input_pool.is_some());
		let in_pool = s.input_pool.as_ref().unwrap();
		assert_eq!(in_pool.slot_count(), 2);
		assert_eq!(in_pool.slot_data_bytes(), 256);
	}

	#[test]
	fn handshake_attach_failure_reports_error() {
		let mut s = WorkerSession::create("none").unwrap();
		// A key that was never created.
		let resp = s
			.handle_line(
				&json!({
					"type": "handshake",
					"protocol_version": 1,
					"shm_key": format!("olive-rw-{}-missing", std::process::id()),
					"output_slots": 4,
					"slot_data_bytes": 4096,
				})
				.to_string(),
			)
			.unwrap();
		assert_eq!(resp["type"], "error");
		assert!(resp["message"]
			.as_str()
			.unwrap()
			.starts_with("failed to attach shared memory: "));
		assert!(s.output_pool.is_none());
	}

	#[test]
	fn handshake_rejects_non_pool_segment() {
		let mut s = WorkerSession::create("none").unwrap();
		// A real segment of the right size that does not contain a pool
		// (zeroed memory → wrong magic). Sized so the attach size check
		// passes and the magic check fires.
		let key = test_key("nopool");
		let bytes = FrameSlotPool::bytes_needed(4, 4096);
		let mut region = SharedMemoryRegion::new();
		assert!(region.open(&key, bytes, ShmMode::Create));
		let resp = s
			.handle_line(
				&json!({
					"type": "handshake",
					"protocol_version": 1,
					"shm_key": key,
					"output_slots": 4,
					"slot_data_bytes": 4096,
				})
				.to_string(),
			)
			.unwrap();
		assert_eq!(
			resp["message"],
			"shared memory does not contain a frame slot pool"
		);
	}

	#[test]
	fn handshake_missing_input_geometry_is_an_error() {
		let mut s = WorkerSession::create("none").unwrap();
		let (mut hs, _out, _in) = parent_side(2, 256, false);
		// Ask for input slots without announcing their geometry.
		hs["input_slots"] = json!(2);
		let resp = s.handle_line(&hs.to_string()).unwrap();
		assert_eq!(
			resp["message"],
			"handshake missing input shared-memory geometry"
		);
	}

	#[test]
	fn load_graph_checks_are_real_then_stub() {
		let mut s = WorkerSession::create("none").unwrap();

		let missing = "/definitely/not/a/real/graph.ove";
		let resp = s
			.handle_line(&json!({ "type": "load_graph", "path": missing }).to_string())
			.unwrap();
		assert_eq!(
			resp["message"],
			format!("graph file does not exist: {missing}")
		);

		let empty = std::env::temp_dir().join("oak_worker_main_test_empty.ove");
		std::fs::write(&empty, b"").unwrap();
		let resp = s
			.handle_line(
				&json!({ "type": "load_graph", "path": empty.display().to_string() }).to_string(),
			)
			.unwrap();
		assert_eq!(
			resp["message"],
			format!("graph file is empty: {}", empty.display())
		);
		let _ = std::fs::remove_file(&empty);

		let real = std::env::temp_dir().join("oak_worker_main_test_graph.ove");
		std::fs::write(&real, b"<root/>").unwrap();
		let resp = s
			.handle_line(
				&json!({ "type": "load_graph", "path": real.display().to_string() }).to_string(),
			)
			.unwrap();
		assert!(resp["message"]
			.as_str()
			.unwrap()
			.contains("node-graph deserialization is not yet available"));
		let _ = std::fs::remove_file(&real);
	}

	#[test]
	fn render_frame_reports_stub_with_ticket() {
		let mut s = WorkerSession::create("none").unwrap();
		let resp = s
			.handle_line(r#"{"type":"render_frame","ticket":123,"node":"abc"}"#)
			.unwrap();
		assert_eq!(resp["type"], "error");
		assert_eq!(resp["ticket"], 123);
		assert!(resp["message"]
			.as_str()
			.unwrap()
			.contains("frame rendering is not yet available"));
	}

	// ---- oak-worker's in-process session tests (M14 R2: folded from the
	// ---- former src/session.rs mirror; the facade's production session
	// ---- tests above cover the rest) -------------------------------------

	#[test]
	fn session_starts_without_pools() {
		let s = WorkerSession::create("none").unwrap();
		assert!(s.output_pool.is_none());
		assert!(s.input_pool.is_none());
		assert!(!s.shutdown_requested());
	}

	#[test]
	fn non_object_json_yields_error_response() {
		let mut s = WorkerSession::create("none").unwrap();
		let resp = s.handle_line("[1,2,3]").unwrap();
		assert_eq!(resp["message"], "malformed control message");
	}

	#[test]
	fn missing_type_field_yields_unknown_error() {
		let mut s = WorkerSession::create("none").unwrap();
		let resp = s.handle_line(r#"{"hello":1}"#).unwrap();
		assert_eq!(resp["message"], "unknown message type: ");
	}

	#[test]
	fn handshake_bad_json_shape_is_invalid_handshake() {
		let mut s = WorkerSession::create("none").unwrap();
		let resp = s
			.handle_line(r#"{"type":"handshake","protocol_version":"x"}"#)
			.unwrap();
		assert_eq!(resp["message"], "invalid handshake message");
	}
}

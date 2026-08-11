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

//! The render worker — the Rust port of `engine/src/capi/worker.cpp`
//! behind the frozen C ABI in `engine/include/oakengine/worker.h`.
//!
//! Like the C++ side, this is where the worker's whole runtime lives:
//!
//!   - **Backend selection.** [`create_renderer`] initializes the render
//!     backend through the oakrender module C ABI
//!     (`oakrender_display_renderer_create_dynamic` + `_init`), falling
//!     back to the direct OpenGL renderer exactly like the C++
//!     `create_renderer()` chain. The worker executable itself never
//!     touches a renderer — it is a thin shell calling
//!     [`oakengine_worker_main`].
//!   - **The session.** [`WorkerSession`] holds the renderer, the
//!     shared-memory frame-slot pools ([`crate::ipc::FrameSlotPool`]) and
//!     the shutdown flag, and answers one NDJSON control message at a time.
//!   - **The main loop.** [`worker_main`] (and its C ABI wrapper
//!     [`oakengine_worker_main`]) creates the session, loads the runtime
//!     config, writes the startup handshake, and serves the stdin/stdout
//!     NDJSON loop until a `shutdown` message or EOF.
//!
//! The control-plane protocol is the same NDJSON the C++ worker speaks
//! (`engine/render/ipc/ipcmessage.cpp`): one compact JSON object per line,
//! `"type"`-dispatched, with `handshake` carrying the shared-memory
//! geometry the worker attaches to via the real [`crate::ipc`] transport.
//! `load_graph`/`render_frame` reproduce the C++ validation and then
//! answer with the documented "not yet available" errors (the oaknode
//! graph crate is still a skeleton).

use std::ffi::{c_char, c_int};
use std::io::{self, BufRead, Write};

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use crate::bridge::render as render_ffi;
use crate::handle::CHandle;
use crate::ipc::{FrameSlotPool, SharedMemoryRegion, ShmMode};

/// Protocol version announced in the startup handshake (`k_protocol_version`).
pub const PROTOCOL_VERSION: i32 = 1;

/// `"handshake"`.
const TYPE_HANDSHAKE: &str = "handshake";
/// `"load_graph"`.
const TYPE_LOAD_GRAPH: &str = "load_graph";
/// `"render_frame"`.
const TYPE_RENDER_FRAME: &str = "render_frame";
/// `"cancel"`.
const TYPE_CANCEL: &str = "cancel";
/// `"shutdown"`.
const TYPE_SHUTDOWN: &str = "shutdown";
/// `"error"`.
const TYPE_ERROR: &str = "error";

/// Why `load_graph` answers "not yet available" after the real file checks.
const GRAPH_STUB: &str = "load_graph: node-graph deserialization is not yet available in the \
     Rust worker (the oaknode crate is a todo!() skeleton; see worker/rust/README.md)";

/// Why `render_frame` answers "not yet available".
const RENDER_STUB: &str = "render_frame: frame rendering is not yet available in the Rust \
     worker (no node-graph or render-pipeline backing; the shm frame-slot transport is \
     attached but there is no graph to render; see worker/rust/README.md)";

/// `handshake` — field-for-field equivalent of `oak_ipc_handshake` (ipc.h);
/// wire field names match the C++ serializer.
#[derive(Serialize, Deserialize, Default, Debug, Clone)]
#[serde(default)]
pub struct HandshakeMsg {
	/// Protocol version.
	pub protocol_version: i32,
	/// Worker->main output shared-memory segment key.
	pub shm_key: String,
	/// Main->worker input shared-memory segment key (optional).
	pub input_shm_key: String,
	/// Number of main->worker input frame slots.
	pub input_slots: i32,
	/// Number of worker->main output frame slots.
	pub output_slots: i32,
	/// Per-output-slot pixel block size.
	pub slot_data_bytes: i64,
	/// Per-input-slot pixel block size.
	pub input_slot_data_bytes: i64,
}

/// `load_graph` — path to a temporary file holding the serialized graph.
#[derive(Serialize, Deserialize, Default, Debug, Clone)]
#[serde(default)]
pub struct LoadGraphMsg {
	/// Temporary file holding the serialized node graph.
	pub path: String,
}

/// `render_frame` — request a frame render (wire names per ipcmessage.cpp).
#[derive(Serialize, Deserialize, Default, Debug, Clone)]
#[serde(default)]
pub struct RenderFrameMsg {
	/// Correlates with the eventual frame_ready.
	pub ticket: i64,
	/// Viewer node stable uuid in the loaded graph.
	pub node: String,
	/// Frame timestamp numerator.
	pub time_num: i64,
	/// Frame timestamp denominator.
	pub time_den: i64,
	/// Forced output width (0 = graph default).
	pub width: i32,
	/// Forced output height (0 = graph default).
	pub height: i32,
	/// Forced `PixelFormat::Format` (-1 = default).
	pub format: i32,
	/// Channel count (0 = default).
	pub channels: i32,
	/// RenderMode::Mode.
	pub mode: i32,
	/// Optional decoded input slot (-1 = none).
	pub input_slot: i32,
	/// Ordered decoded input slots.
	pub input_slots: Vec<i32>,
	/// Output color transform present?
	pub has_color_transform: bool,
	/// 1 when the transform is a display transform.
	pub color_is_display: bool,
	/// Output colorspace or display name.
	pub color_output: String,
	/// Display view.
	pub color_view: String,
	/// Display look.
	pub color_look: String,
}

/// Build a worker-side error report, mirroring `error_message()` in
/// worker.cpp: `{"type":"error","message":...}` plus `"ticket"` when
/// non-zero.
fn error_message(message: &str, ticket: Option<i64>) -> Value {
	match ticket.filter(|t| *t != 0) {
		Some(t) => json!({ "type": TYPE_ERROR, "message": message, "ticket": t }),
		None => json!({ "type": TYPE_ERROR, "message": message }),
	}
}

/// Log a worker-side message to stderr, mirroring worker.cpp `log_error()`
/// (the `worker: ` prefix).
pub fn log_error(message: &str) {
	eprintln!("worker: {message}");
}

/// Whether `backend` requests no renderer (worker.cpp
/// `backend_requests_no_renderer()`: NULL, "" and "none").
pub fn is_no_backend(backend: &str) -> bool {
	backend.is_empty() || backend.eq_ignore_ascii_case("none")
}

// ---------------------------------------------------------------------------
// Renderer (backend selection)
// ---------------------------------------------------------------------------

/// A live, initialized oakrender display renderer handle (destroyed on
/// drop).
pub struct Renderer {
	handle: CHandle,
}

impl Renderer {
	/// Create and initialize a renderer through the oakrender module C ABI,
	/// trying the named dynamic backend first and falling back to the
	/// direct OpenGL renderer — the exact fallback chain of worker.cpp
	/// `create_renderer()`.
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

	/// Try the named dynamic backend through the module C ABI
	/// (`oakrender_display_renderer_create_dynamic` + `_init`).
	fn create_dynamic(backend: &str) -> Result<Renderer, String> {
		let c = std::ffi::CString::new(backend)
			.map_err(|_| format!("invalid backend id {backend:?}"))?;
		// SAFETY: `c` is a valid NUL-terminated string the function only
		// reads during the call.
		let handle = unsafe { render_ffi::oakrender_display_renderer_create_dynamic(c.as_ptr()) };
		Self::init_handle(handle, &format!("dynamic {backend}"))
	}

	/// Fall back to the direct OpenGL renderer.
	fn create_opengl() -> Result<Renderer, String> {
		// SAFETY: no arguments; the function returns an owned handle.
		let handle = unsafe { render_ffi::oakrender_display_renderer_create_opengl() };
		Self::init_handle(handle, "direct OpenGL")
	}

	/// Initialize a freshly created renderer handle.
	fn init_handle(mut handle: CHandle, what: &str) -> Result<Renderer, String> {
		if handle.is_null() {
			return Err(format!("failed to create {what} renderer"));
		}
		// SAFETY: `handle` is live and owned by us; NULL gl_context makes
		// the backend use its default device/context path.
		let rc =
			unsafe { render_ffi::oakrender_display_renderer_init(handle, std::ptr::null_mut()) };
		if rc != 0 {
			// SAFETY: the handle is still owned by us (init failed).
			unsafe { render_ffi::oakrender_display_renderer_destroy(&mut handle) };
			return Err(format!("failed to initialize {what} renderer (rc={rc})"));
		}
		Ok(Renderer { handle })
	}

	/// 1 when the renderer is OpenGL-based (the C++ worker uses the GL
	/// context to announce the negotiated GL version in the handshake).
	///
	/// Not called yet: the oakrender module C ABI exposes no GL context
	/// version, so the startup handshake omits `gl_major`/`gl_minor`.
	#[allow(dead_code)]
	pub fn is_open_gl(&self) -> bool {
		// SAFETY: `self.handle` is the live handle from init_handle().
		unsafe { render_ffi::oakrender_display_renderer_is_open_gl(self.handle) == 1 }
	}
}

impl Drop for Renderer {
	fn drop(&mut self) {
		// SAFETY: `self.handle` is the owned handle from init_handle() and
		// is not used after this.
		unsafe { render_ffi::oakrender_display_renderer_destroy(&mut self.handle) };
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
	/// oakrender module C ABI (dynamic -> OpenGL fallback).
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
		// SAFETY: no arguments; the function initializes process-wide state.
		let rc = unsafe { render_ffi::oakrender_color_manager_set_up_default_config() };
		if rc != 0 {
			log_error(&format!(
				"runtime: color-manager default config failed (rc={rc}); continuing"
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
	/// the oakrender module C ABI exposes no GL context version.
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

impl HandshakeMsg {
	/// Serialize to the wire `handshake` object.
	pub fn to_json(&self) -> Value {
		json!({
			"type": TYPE_HANDSHAKE,
			"protocol_version": self.protocol_version,
			"shm_key": self.shm_key,
			"input_shm_key": self.input_shm_key,
			"input_slots": self.input_slots,
			"output_slots": self.output_slots,
			"slot_data_bytes": self.slot_data_bytes,
			"input_slot_data_bytes": self.input_slot_data_bytes,
		})
	}
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

/// Write one NDJSON message line (compact JSON + `\n`), the Rust port of
/// `ipcmessage.cpp write_message()`.
fn write_message(w: &mut impl Write, msg: &Value) -> io::Result<()> {
	let line =
		serde_json::to_string(msg).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
	w.write_all(line.as_bytes())?;
	w.write_all(b"\n")
}

/// Scan argv for `--backend <name>` (worker.cpp `oakengine_worker_main`).
/// The default is `"opengl"`; the value is lowercased; the last flag wins.
fn parse_backend(argc: c_int, argv: *mut *mut c_char) -> String {
	let mut backend = "opengl".to_string();
	if argc > 0 && !argv.is_null() {
		// SAFETY: `argv` points to `argc` NUL-terminated C strings (the C
		// runtime's argv), and we only read the entries.
		let args = unsafe { std::slice::from_raw_parts(argv, argc as usize) };
		let mut i = 1usize;
		while i < args.len() {
			// SAFETY: `args[i]` is a valid NUL-terminated C string.
			let arg = unsafe { crate::handle::read_cstr(args[i]) };
			if arg == "--backend" && i + 1 < args.len() {
				// SAFETY: `args[i + 1]` is a valid NUL-terminated C string.
				backend = unsafe { crate::handle::read_cstr(args[i + 1]) }.to_ascii_lowercase();
				i += 2;
			} else {
				i += 1;
			}
		}
	}
	backend
}

/// Full render-worker main, transport-agnostic in the backend name.
///
/// Mirrors `oakengine_worker_main()` in worker.cpp: create the session
/// (which initializes the render backend), load the runtime config, write
/// the startup handshake, then serve the NDJSON control loop on
/// stdin/stdout until a `shutdown` message or EOF. Returns the process
/// exit code.
pub fn worker_main(backend: &str) -> i32 {
	// 1. Session creation initializes the render backend through the
	//    oakrender module C ABI (oakengine_worker_session_create()).
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

// ---------------------------------------------------------------------------
// C ABI exports (engine/include/oakengine/worker.h)
// ---------------------------------------------------------------------------

/// Opaque `OakWorkerSession` handle (worker.h). A facade-owned box around a
/// [`WorkerSession`]; the C caller only ever sees the pointer.
#[repr(C)]
pub struct OakWorkerSession {
	_opaque: [u8; 0],
}

/// `oakengine_worker_session_create` — create a session for the given
/// render backend. NULL/""/"none" skips renderer creation.
#[no_mangle]
pub unsafe extern "C" fn oakengine_worker_session_create(
	backend: *const c_char,
) -> *mut OakWorkerSession {
	crate::handle::guard_ptr(|| unsafe {
		let backend = crate::handle::read_cstr(backend);
		let session =
			WorkerSession::create(&backend).map_err(|e| crate::error::Error::Failed(e))?;
		Ok(Box::into_raw(Box::new(session)) as *mut OakWorkerSession)
	})
}

/// `oakengine_worker_session_free` — NULL no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_worker_session_free(self_: *mut OakWorkerSession) {
	if self_.is_null() {
		return;
	}
	// SAFETY: `self_` was produced by `oakengine_worker_session_create`
	// and is not used after this.
	unsafe { drop(Box::from_raw(self_ as *mut WorkerSession)) };
}

/// `oakengine_worker_session_has_renderer` — 1/0.
#[no_mangle]
pub unsafe extern "C" fn oakengine_worker_session_has_renderer(
	self_: *const OakWorkerSession,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok((&*(self_ as *const WorkerSession)).has_renderer() as c_int)
	})
}

/// `oakengine_worker_session_initialize_runtime` — 1 on success, 0 for a
/// NULL session.
#[no_mangle]
pub unsafe extern "C" fn oakengine_worker_session_initialize_runtime(
	self_: *mut OakWorkerSession,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let session = &mut *(self_ as *mut WorkerSession);
		Ok(session.initialize_runtime() as c_int)
	})
}

/// `oakengine_worker_session_startup_handshake` — build the startup
/// handshake (buf/size convention). Returns the required size, or -1 on
/// failure.
#[no_mangle]
pub unsafe extern "C" fn oakengine_worker_session_startup_handshake(
	self_: *const OakWorkerSession,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if self_.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let session = &*(self_ as *const WorkerSession);
		let json = session.startup_handshake();
		let line =
			serde_json::to_string(&json).map_err(|e| crate::error::Error::Failed(e.to_string()))?;
		Ok(crate::handle::write_string(&line, buf, buf_size))
	})
}

/// `oakengine_worker_session_handle_json` — handle one NDJSON control line
/// and produce the response, if any (buf/size convention). Returns 0 for
/// "no response", the response length for a response, -1 on a fatal
/// handler failure. A malformed line yields an error response, not -1.
#[no_mangle]
pub unsafe extern "C" fn oakengine_worker_session_handle_json(
	self_: *mut OakWorkerSession,
	line: *const c_char,
	response_buf: *mut c_char,
	response_buf_size: c_int,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if self_.is_null() || line.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let line = crate::handle::read_cstr(line);
		let session = &mut *(self_ as *mut WorkerSession);
		match session.handle_line(&line) {
			Some(response) => {
				let json = serde_json::to_string(&response)
					.map_err(|e| crate::error::Error::Failed(e.to_string()))?;
				Ok(crate::handle::write_string(
					&json,
					response_buf,
					response_buf_size,
				))
			}
			None => Ok(0),
		}
	})
}

/// `oakengine_worker_session_shutdown_requested` — 1/0.
#[no_mangle]
pub unsafe extern "C" fn oakengine_worker_session_shutdown_requested(
	self_: *const OakWorkerSession,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok((&*(self_ as *const WorkerSession)).shutdown_requested() as c_int)
	})
}

/// `oakengine_worker_main` — full render-worker main. Parses `--backend`,
/// initializes the renderer, sends the startup handshake and runs the
/// stdin/stdout NDJSON loop until a shutdown message or EOF. Returns the
/// process exit code.
#[no_mangle]
pub unsafe extern "C" fn oakengine_worker_main(argc: c_int, argv: *mut *mut c_char) -> c_int {
	crate::handle::guard_int(|| {
		let backend = parse_backend(argc, argv);
		Ok(worker_main(&backend))
	})
}

// ---------------------------------------------------------------------------
// Unit tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
	use super::*;
	use crate::ipc::{self, FrameSlotPool, SharedMemoryRegion, ShmMode};
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
	fn parse_backend_scans_argv() {
		// Build a tiny fake argv the way the C runtime would: an array of
		// NUL-terminated strings.
		let make = |args: &[&str]| -> (Vec<std::ffi::CString>, Vec<*mut c_char>) {
			let cstrings: Vec<std::ffi::CString> = args
				.iter()
				.map(|s| std::ffi::CString::new(*s).unwrap())
				.collect();
			let mut ptrs: Vec<*mut c_char> =
				cstrings.iter().map(|c| c.as_ptr() as *mut c_char).collect();
			(cstrings, ptrs)
		};
		let (_keep, mut argv) = make(&["oak-worker"]);
		assert_eq!(parse_backend(1, argv.as_mut_ptr()), "opengl");

		let (_keep, mut argv) = make(&["oak-worker", "--backend", "Vulkan"]);
		assert_eq!(parse_backend(3, argv.as_mut_ptr()), "vulkan");

		let (_keep, mut argv) = make(&["oak-worker", "--backend", "none"]);
		assert_eq!(parse_backend(3, argv.as_mut_ptr()), "none");

		// Last flag wins (the C++ loop keeps scanning).
		let (_keep, mut argv) = make(&["oak-worker", "--backend", "vulkan", "--backend", "opengl"]);
		assert_eq!(parse_backend(5, argv.as_mut_ptr()), "opengl");

		// A missing value is ignored (the C++ only consumes it when
		// i + 1 < size).
		let (_keep, mut argv) = make(&["oak-worker", "--backend"]);
		assert_eq!(parse_backend(2, argv.as_mut_ptr()), "opengl");
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

		let empty = std::env::temp_dir().join("oak_facade_worker_test_empty.ove");
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

		let real = std::env::temp_dir().join("oak_facade_worker_test_graph.ove");
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

	#[test]
	fn c_abi_session_lifecycle_and_attach() {
		// SAFETY: worker_session_create returns an owned handle.
		let s = unsafe { oakengine_worker_session_create(c"none".as_ptr()) };
		assert!(!s.is_null());
		assert_eq!(unsafe { oakengine_worker_session_has_renderer(s) }, 0);

		// Startup handshake via the C ABI (buf/size convention).
		let mut buf = [0 as c_char; 512];
		let n = unsafe { oakengine_worker_session_startup_handshake(s, buf.as_mut_ptr(), 512) };
		assert!(n > 0);
		let hs: Value = serde_json::from_str(
			unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
				.to_str()
				.unwrap(),
		)
		.unwrap();
		assert_eq!(hs["type"], "handshake");
		assert_eq!(hs["protocol_version"], 1);

		// Attach a real pool through the C ABI handle_json.
		let (parent_hs, out_region, _in) = parent_side(3, 512, false);
		let line = parent_hs.to_string();
		let line_c = std::ffi::CString::new(line.clone()).unwrap();
		// SAFETY: line_c is a valid C string; s is live.
		let n = unsafe {
			oakengine_worker_session_handle_json(s, line_c.as_ptr(), buf.as_mut_ptr(), 512)
		};
		assert_eq!(n, 0, "expected no response on a successful handshake");
		// SAFETY: out_region is a live mapping of the pool; the session
		// holds the attached peer view — the free ring is shared.
		let parent_pool = unsafe { FrameSlotPool::attach(out_region.data()) };
		// SAFETY: `s` is a live session box; the output pool was attached
		// above.
		let session = unsafe { &mut *(s as *mut WorkerSession) };
		let out_pool = session.output_pool.as_ref().unwrap();
		// Drain the free ring through the worker's pool...
		let mut drained = Vec::new();
		for _ in 0..3 {
			let mut slot = 0;
			assert!(unsafe { out_pool.acquire(&mut slot) });
			drained.push(slot);
		}
		drained.sort_unstable();
		assert_eq!(drained, vec![0, 1, 2]);
		// ...so the parent's release lands in the shared free ring the
		// worker pops from.
		assert!(unsafe { parent_pool.release(1) });
		let mut slot = 0;
		assert!(unsafe { out_pool.acquire(&mut slot) });
		assert_eq!(slot, 1);
		unsafe { out_pool.release(slot) };

		// Shutdown through the C ABI.
		let shutdown = std::ffi::CString::new(r#"{"type":"shutdown"}"#).unwrap();
		// SAFETY: valid C string; s is live.
		let n = unsafe {
			oakengine_worker_session_handle_json(s, shutdown.as_ptr(), buf.as_mut_ptr(), 512)
		};
		assert_eq!(n, 0);
		assert_eq!(unsafe { oakengine_worker_session_shutdown_requested(s) }, 1);

		// SAFETY: s is still live (owned by this test).
		unsafe { oakengine_worker_session_free(s) };
	}

	#[test]
	fn ipc_layout_matches_c_abi_exports() {
		// The Rust bytes_needed and the exported C ABI must agree (they are
		// the same function, but this guards the linkage surface).
		assert_eq!(
			unsafe { crate::ipc::oakengine_ipc_framepool_bytes_needed(4, 4096) },
			FrameSlotPool::bytes_needed(4, 4096)
		);
		assert_eq!(
			ipc::SpscRingBuffer::bytes_needed(5),
			ipc::SpscRingBuffer::HEADER_BYTES + 5 * 4
		);
	}
}

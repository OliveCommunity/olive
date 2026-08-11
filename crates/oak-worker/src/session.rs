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

//! The worker-side session state machine — the in-process mirror of
//! `OakWorkerSession` in `engine/src/capi/worker.cpp` (whose production
//! Rust port lives in `oakengine::worker`).
//!
//! The session holds the attached shared-memory frame-slot pools
//! ([`crate::transport::AttachedPools`]) and the shutdown flag, and
//! answers one NDJSON control message at a time. Renderer creation and the
//! main loop are the facade's job (see `crate::main`); this module keeps
//! the message handling testable in-process against the facade's real
//! shared-memory transport. Where the C++ session has machinery the Rust
//! mirror lacks, the handler reproduces the *validation* faithfully and
//! then reports the documented stub ([`crate::transport`]) — it never
//! fakes a result.

use serde_json::Value;

use crate::ipc::{self, HandshakeMsg, LoadGraphMsg, RenderFrameMsg};
use crate::transport::{self, AttachedPools};

/// Worker-side session: attached frame-slot pools + message-handling state.
pub struct WorkerSession {
	/// Attached shared-memory frame-slot pools (output + optional input).
	pools: Option<AttachedPools>,
	shutdown_requested: bool,
}

impl WorkerSession {
	/// A fresh session with no attached pools.
	pub fn new() -> WorkerSession {
		WorkerSession {
			pools: None,
			shutdown_requested: false,
		}
	}

	/// 1 once the handshake has attached the shared-memory frame-slot
	/// pools.
	pub fn has_pools(&self) -> bool {
		self.pools.is_some()
	}

	/// 1 once a shutdown control message has been received.
	pub fn shutdown_requested(&self) -> bool {
		self.shutdown_requested
	}

	/// The attached output pool (the worker->main frame-slot pool).
	pub fn output_pool(&self) -> Option<&oakengine::ipc::FrameSlotPool> {
		self.pools.as_ref().map(|p| &p.output_pool)
	}

	/// The startup handshake the worker sends to its parent
	/// (`worker.cpp startup_handshake()`): protocol version 1 and empty
	/// shared-memory geometry — the parent creates the segments and
	/// announces their geometry in its handshake reply.
	pub fn startup_handshake(&self) -> Value {
		HandshakeMsg {
			protocol_version: crate::PROTOCOL_VERSION,
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
			_ => return Some(ipc::error_message("malformed control message", None)),
		};
		let typ = msg.get("type").and_then(Value::as_str).unwrap_or("");
		match typ {
			ipc::TYPE_HANDSHAKE => self.handle_handshake(&msg),
			ipc::TYPE_LOAD_GRAPH => self.handle_load_graph(&msg),
			ipc::TYPE_RENDER_FRAME => self.handle_render_frame(&msg),
			// cancel: the C++ worker does synchronous single-frame work
			// (nothing in flight), so a cancel produces no response.
			ipc::TYPE_CANCEL => None,
			ipc::TYPE_SHUTDOWN => {
				self.shutdown_requested = true;
				None
			}
			other => Some(ipc::error_message(
				&format!("unknown message type: {other}"),
				None,
			)),
		}
	}

	/// `handshake`: validate and attach the shared-memory frame-slot pools.
	/// Validation mirrors worker.cpp `attach_output_pool()`; the attachment
	/// itself goes through the real [`crate::transport`].
	fn handle_handshake(&mut self, msg: &Value) -> Option<Value> {
		let hs: HandshakeMsg = match serde_json::from_value(msg.clone()) {
			Ok(hs) => hs,
			Err(_) => return Some(ipc::error_message("invalid handshake message", None)),
		};
		if hs.protocol_version != crate::PROTOCOL_VERSION {
			return Some(ipc::error_message(
				&format!("unsupported protocol version {}", hs.protocol_version),
				None,
			));
		}
		if hs.shm_key.is_empty() || hs.output_slots <= 0 || hs.slot_data_bytes <= 0 {
			return Some(ipc::error_message(
				"handshake missing output shared-memory geometry",
				None,
			));
		}
		if hs.input_slots > 0 && (hs.input_shm_key.is_empty() || hs.input_slot_data_bytes <= 0) {
			return Some(ipc::error_message(
				"handshake missing input shared-memory geometry",
				None,
			));
		}
		match transport::attach_pools(&hs) {
			Ok(pools) => {
				self.pools = Some(pools);
				None
			}
			Err(msg) => Some(ipc::error_message(&msg, None)),
		}
	}

	/// `load_graph`: the file checks are real (mirror worker.cpp
	/// `load_graph()`); the deserialization is the documented stub.
	fn handle_load_graph(&mut self, msg: &Value) -> Option<Value> {
		let load: LoadGraphMsg = match serde_json::from_value(msg.clone()) {
			Ok(l) => l,
			Err(_) => return Some(ipc::error_message("invalid load_graph message", None)),
		};
		match std::fs::metadata(&load.path) {
			Err(_) => Some(ipc::error_message(
				&format!("graph file does not exist: {}", load.path),
				None,
			)),
			Ok(md) if md.len() == 0 => Some(ipc::error_message(
				&format!("graph file is empty: {}", load.path),
				None,
			)),
			Ok(md) => {
				crate::log_error(&format!(
					"LoadGraph: loading {} ({} bytes)",
					load.path,
					md.len()
				));
				Some(ipc::error_message(transport::GRAPH_STUB, None))
			}
		}
	}

	/// `render_frame`: the graph/render pipeline has no Rust backing, so a
	/// render request is answered with a clear error carrying the ticket —
	/// the same `error_message()` shape the C++ worker uses for its own
	/// failures.
	fn handle_render_frame(&mut self, msg: &Value) -> Option<Value> {
		let render: RenderFrameMsg = match serde_json::from_value(msg.clone()) {
			Ok(r) => r,
			Err(_) => return Some(ipc::error_message("invalid render_frame message", None)),
		};
		Some(ipc::error_message(
			transport::RENDER_STUB,
			Some(render.ticket),
		))
	}
}

impl Default for WorkerSession {
	fn default() -> Self {
		WorkerSession::new()
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use oakengine::ipc::{FrameSlotPool, SharedMemoryRegion, ShmMode};
	use serde_json::json;

	/// A unique, temporary POSIX segment key for a test.
	fn test_key(name: &str) -> String {
		static COUNTER: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
		let n = COUNTER.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
		SharedMemoryRegion::make_key(i64::from(std::process::id()), (n & 0x7FFF) as i32)
			+ &format!("-t-{name}")
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
		let _ =
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
			"protocol_version": crate::PROTOCOL_VERSION,
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
	fn session_starts_without_pools() {
		let s = WorkerSession::new();
		assert!(!s.has_pools());
		assert!(!s.shutdown_requested());
	}

	#[test]
	fn startup_handshake_is_protocol_version_1_with_empty_geometry() {
		let s = WorkerSession::new();
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
		let mut s = WorkerSession::new();
		let resp = s.handle_line("this is not json").unwrap();
		assert_eq!(resp["type"], "error");
		assert_eq!(resp["message"], "malformed control message");
	}

	#[test]
	fn non_object_json_yields_error_response() {
		let mut s = WorkerSession::new();
		let resp = s.handle_line("[1,2,3]").unwrap();
		assert_eq!(resp["message"], "malformed control message");
	}

	#[test]
	fn unknown_message_type_yields_error_response() {
		let mut s = WorkerSession::new();
		let resp = s.handle_line(r#"{"type":"frobnicate"}"#).unwrap();
		assert_eq!(resp["type"], "error");
		assert_eq!(resp["message"], "unknown message type: frobnicate");
	}

	#[test]
	fn missing_type_field_yields_unknown_error() {
		let mut s = WorkerSession::new();
		let resp = s.handle_line(r#"{"hello":1}"#).unwrap();
		assert_eq!(resp["message"], "unknown message type: ");
	}

	#[test]
	fn cancel_produces_no_response() {
		let mut s = WorkerSession::new();
		assert!(s.handle_line(r#"{"type":"cancel","ticket":5}"#).is_none());
		assert!(!s.shutdown_requested());
	}

	#[test]
	fn shutdown_sets_flag_and_has_no_response() {
		let mut s = WorkerSession::new();
		assert!(s.handle_line(r#"{"type":"shutdown"}"#).is_none());
		assert!(s.shutdown_requested());
	}

	#[test]
	fn handshake_wrong_protocol_version() {
		let mut s = WorkerSession::new();
		let resp = s
			.handle_line(r#"{"type":"handshake","protocol_version":99,"shm_key":"k","output_slots":1,"slot_data_bytes":16}"#)
			.unwrap();
		assert_eq!(resp["message"], "unsupported protocol version 99");
	}

	#[test]
	fn handshake_missing_geometry() {
		let mut s = WorkerSession::new();
		let resp = s
			.handle_line(r#"{"type":"handshake","protocol_version":1}"#)
			.unwrap();
		assert_eq!(
			resp["message"],
			"handshake missing output shared-memory geometry"
		);
	}

	#[test]
	fn handshake_missing_input_geometry_is_an_error() {
		let mut s = WorkerSession::new();
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
	fn handshake_attaches_real_output_pool() {
		let mut s = WorkerSession::new();
		let (hs, out_region, _in) = parent_side(4, 4096, false);
		let resp = s.handle_line(&hs.to_string());
		assert!(resp.is_none(), "unexpected error: {resp:?}");
		assert!(s.has_pools());
		let out_pool = s.output_pool().unwrap();
		assert_eq!(out_pool.slot_count(), 4);
		assert_eq!(out_pool.slot_data_bytes(), 4096);
		// The pool is real shared state: the parent's publish lands in the
		// worker's ready ring.
		// SAFETY: `out_region` is a live mapping of the pool the session
		// attached to.
		let parent_pool = unsafe { FrameSlotPool::attach(out_region.data()) };
		let mut slot = 0;
		assert!(unsafe { parent_pool.acquire(&mut slot) });
		assert_eq!(slot, 0);
		// SAFETY: acquired slot.
		let meta = unsafe { &mut *parent_pool.meta(slot) };
		meta.id = 7;
		assert!(unsafe { parent_pool.publish(slot) });
		let mut consumed = 0;
		assert!(unsafe { out_pool.consume(&mut consumed) });
		assert_eq!(consumed, 0);
		// SAFETY: consumed slot.
		assert_eq!(unsafe { (*out_pool.meta_const(consumed)).id }, 7);
		unsafe { out_pool.release(consumed) };
	}

	#[test]
	fn handshake_attaches_input_pool_too() {
		let mut s = WorkerSession::new();
		let (hs, _out, _in) = parent_side(2, 256, true);
		let resp = s.handle_line(&hs.to_string());
		assert!(resp.is_none(), "unexpected error: {resp:?}");
		assert!(s.has_pools());
		let pools = s.pools.as_ref().unwrap();
		assert!(pools.input_pool.is_some());
		let in_pool = pools.input_pool.as_ref().unwrap();
		assert_eq!(in_pool.slot_count(), 2);
		assert_eq!(in_pool.slot_data_bytes(), 256);
	}

	#[test]
	fn handshake_attach_failure_reports_error() {
		let mut s = WorkerSession::new();
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
		assert!(!s.has_pools());
	}

	#[test]
	fn handshake_rejects_non_pool_segment() {
		let mut s = WorkerSession::new();
		// A real segment of the right size that does not contain a pool
		// (zeroed memory -> wrong magic).
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
		assert!(!s.has_pools());
	}

	#[test]
	fn handshake_bad_json_shape_is_invalid_handshake() {
		let mut s = WorkerSession::new();
		let resp = s
			.handle_line(r#"{"type":"handshake","protocol_version":"x"}"#)
			.unwrap();
		assert_eq!(resp["message"], "invalid handshake message");
	}

	#[test]
	fn load_graph_file_checks_are_real_then_stub() {
		let mut s = WorkerSession::new();

		let missing = "/definitely/not/a/real/graph.ove";
		let resp = s
			.handle_line(&json!({ "type": "load_graph", "path": missing }).to_string())
			.unwrap();
		assert_eq!(
			resp["message"],
			format!("graph file does not exist: {missing}")
		);

		let empty = std::env::temp_dir().join("oak_worker_test_empty_graph.ove");
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

		let real = std::env::temp_dir().join("oak_worker_test_graph.ove");
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
		let mut s = WorkerSession::new();
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
}

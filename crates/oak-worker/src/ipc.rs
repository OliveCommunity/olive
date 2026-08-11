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

//! Control-plane NDJSON protocol (contract: `engine/include/oakengine/ipc.h`
//! and `engine/include/oakengine/worker.h`).
//!
//! The wire format is **one compact JSON object per line** on the stdio
//! pipes (worker.cpp / ipcmessage.cpp `write_message`/`read_message`).
//! Every message carries a `"type"` string; the field names below are the
//! ones the C++ serializers actually emit (`engine/render/ipc/ipcmessage.cpp`):
//! note `ticket` / `node` / `channels` / `slot` — the longer names
//! (`ticket_id`, `node_uuid`, `channel_count`, `output_slot`) exist only on
//! the C POD structs in `ipc.h`.
//!
//! Message types (M = main/editor, W = worker):
//!   handshake     M<->W  negotiate protocol version + announce shm geometry
//!   load_graph    M ->W  path to a temp file holding the serialized graph
//!   render_frame  M ->W  request a frame render (ticket, node, time, params)
//!   frame_ready   W ->M  a rendered frame is published (slot + ticket)
//!   cancel        M ->W  abandon an in-flight ticket
//!   graph_update  M ->W  reserved (no payload struct yet)
//!   shutdown      M ->W  finish current work and exit cleanly
//!   error         W ->M  worker-side failure report ("message" field)
//!
//! Items the worker does not emit yet (frame_ready, graph_update,
//! `FrameReadyMsg`) and message ids it ignores (`cancel`) are kept as the
//! documented protocol surface; `dead_code` until the frame-slot transport
//! lands (see crate::transport).

#![allow(dead_code)]

use std::io::{self, Write};

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

/// `"handshake"`.
pub const TYPE_HANDSHAKE: &str = "handshake";
/// `"load_graph"`.
pub const TYPE_LOAD_GRAPH: &str = "load_graph";
/// `"render_frame"`.
pub const TYPE_RENDER_FRAME: &str = "render_frame";
/// `"frame_ready"`.
pub const TYPE_FRAME_READY: &str = "frame_ready";
/// `"cancel"`.
pub const TYPE_CANCEL: &str = "cancel";
/// `"graph_update"`.
pub const TYPE_GRAPH_UPDATE: &str = "graph_update";
/// `"shutdown"`.
pub const TYPE_SHUTDOWN: &str = "shutdown";
/// `"error"`.
pub const TYPE_ERROR: &str = "error";

/// `handshake` — field-for-field equivalent of `oak_ipc_handshake`
/// (ipc.h). Wire field names match the C++ serializer.
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

impl HandshakeMsg {
	/// The worker's startup handshake (`worker.cpp startup_handshake()`).
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

/// `render_frame` — request a frame render. Wire names per ipcmessage.cpp:
/// `ticket`, `node`, `channels` (not the ipc.h POD names).
#[derive(Serialize, Deserialize, Default, Debug, Clone)]
#[serde(default)]
pub struct RenderFrameMsg {
	/// Correlates with the eventual frame_ready.
	pub ticket: i64,
	/// Viewer node stable uuid in the loaded graph.
	pub node: String,
	pub time_num: i64,
	pub time_den: i64,
	/// Forced output size (0 = graph default).
	pub width: i32,
	pub height: i32,
	/// Forced PixelFormat (-1 = default).
	pub format: i32,
	/// Channel count (0 = default).
	pub channels: i32,
	/// RenderMode.
	pub mode: i32,
	/// Optional decoded input slot (-1 = none).
	pub input_slot: i32,
	/// Ordered decoded input slots.
	pub input_slots: Vec<i32>,
	/// Output color transform present?
	pub has_color_transform: bool,
	pub color_is_display: bool,
	pub color_output: String,
	pub color_view: String,
	pub color_look: String,
}

/// `frame_ready` — a rendered frame is published (wire names `ticket`/
/// `slot`).
#[derive(Serialize, Deserialize, Default, Debug, Clone)]
#[serde(default)]
pub struct FrameReadyMsg {
	pub ticket: i64,
	/// Index into the worker->main output FrameSlotPool.
	pub slot: i32,
}

/// `cancel` — abandon an in-flight ticket by id.
#[derive(Serialize, Deserialize, Default, Debug, Clone)]
#[serde(default)]
pub struct CancelMsg {
	pub ticket: i64,
}

/// `load_graph` — path to a temporary file holding the serialized graph.
#[derive(Serialize, Deserialize, Default, Debug, Clone)]
#[serde(default)]
pub struct LoadGraphMsg {
	pub path: String,
}

/// Build a worker-side error report, mirroring `error_message()` in
/// worker.cpp: `{"type":"error","message":...}` plus `"ticket"` when
/// non-zero.
pub fn error_message(message: &str, ticket: Option<i64>) -> Value {
	match ticket.filter(|t| *t != 0) {
		Some(t) => json!({ "type": TYPE_ERROR, "message": message, "ticket": t }),
		None => json!({ "type": TYPE_ERROR, "message": message }),
	}
}

/// Write one NDJSON message line (compact JSON + `\n`), the Rust port of
/// `ipcmessage.cpp write_message()`.
pub fn write_message(w: &mut impl Write, msg: &Value) -> io::Result<()> {
	let line =
		serde_json::to_string(msg).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
	w.write_all(line.as_bytes())?;
	w.write_all(b"\n")
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn handshake_wire_format_matches_cpp_field_names() {
		let hs = HandshakeMsg {
			protocol_version: 1,
			shm_key: "olive-rw-1234-0-out".into(),
			input_shm_key: "".into(),
			input_slots: 0,
			output_slots: 6,
			slot_data_bytes: 4096,
			input_slot_data_bytes: 0,
		};
		let value = hs.to_json();
		// Key order is not part of the contract (JSON objects; the C++
		// QJsonObject is hash-ordered too), but the names must match the
		// C++ serializer exactly.
		assert_eq!(value["type"], "handshake");
		assert_eq!(value["protocol_version"], 1);
		assert_eq!(value["shm_key"], "olive-rw-1234-0-out");
		assert_eq!(value["input_shm_key"], "");
		assert_eq!(value["input_slots"], 0);
		assert_eq!(value["output_slots"], 6);
		assert_eq!(value["slot_data_bytes"], 4096);
		assert_eq!(value["input_slot_data_bytes"], 0);
		// And the serialized line must parse back to the same object.
		let round: serde_json::Value =
			serde_json::from_str(&serde_json::to_string(&value).unwrap()).unwrap();
		assert_eq!(round, value);
	}

	#[test]
	fn render_frame_parse_accepts_cpp_field_names() {
		let json = r#"{"type":"render_frame","ticket":42,"node":"abcd","time_num":1,"time_den":24,"width":1920,"height":1080,"format":-1,"channels":0,"mode":0,"input_slot":-1,"input_slots":[],"has_color_transform":false,"color_output":"","color_view":"","color_look":""}"#;
		let m: RenderFrameMsg = serde_json::from_str(json).unwrap();
		assert_eq!(m.ticket, 42);
		assert_eq!(m.node, "abcd");
		assert_eq!(m.time_num, 1);
		assert_eq!(m.time_den, 24);
		assert_eq!(m.width, 1920);
		assert_eq!(m.input_slot, -1);
	}

	#[test]
	fn render_frame_defaults_on_missing_fields() {
		// The C++ parser defaults missing fields (QJsonValue defaults);
		// serde(default) mirrors that.
		let m: RenderFrameMsg =
			serde_json::from_str(r#"{"type":"render_frame","ticket":7}"#).unwrap();
		assert_eq!(m.ticket, 7);
		assert_eq!(m.time_den, 0);
		assert!(m.node.is_empty());
		assert!(!m.has_color_transform);
	}

	#[test]
	fn error_message_carries_ticket_only_when_nonzero() {
		assert_eq!(
			error_message("boom", None),
			json!({ "type": "error", "message": "boom" })
		);
		assert_eq!(
			error_message("boom", Some(0)),
			json!({ "type": "error", "message": "boom" })
		);
		assert_eq!(
			error_message("boom", Some(9)),
			json!({ "type": "error", "message": "boom", "ticket": 9 })
		);
	}

	#[test]
	fn write_message_emits_one_json_line() {
		let mut buf = Vec::new();
		write_message(&mut buf, &json!({ "type": "shutdown" })).unwrap();
		assert_eq!(String::from_utf8(buf).unwrap(), "{\"type\":\"shutdown\"}\n");
	}
}

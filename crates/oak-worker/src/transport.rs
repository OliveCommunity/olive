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

//! Shared-memory frame-slot transport.
//!
//! The C++ worker exchanges bulk pixel data with the editor through named
//! shared-memory segments holding an `olive::ipc::FrameSlotPool` — a fixed
//! pool of frame slots whose free/ready queues are synchronized by the
//! lock-free single-producer/single-consumer `SpscRingBuffer`. That
//! machinery is implemented in the facade crate (`oakengine::ipc`, the
//! Rust port of `engine/render/ipc/` behind
//! `engine/include/oakengine/ipc.h`) — this module is the worker-side
//! transport over it.
//!
//! [`attach_pools`] mirrors the C++ `attach_output_pool()`: attach the
//! output segment in [`ShmMode::Attach`], map the [`FrameSlotPool`] it
//! contains (rejecting segments without the pool magic), and attach the
//! input pool when the handshake announces one. The returned
//! [`AttachedPools`] is owned by the session and dropped (unmapped) with
//! it.
//!
//! The node-graph and render-pipeline stubs below carry the same rationale
//! as before: `oaknode` is a `todo!()` skeleton and the oakrender crate
//! does not yet evaluate an arbitrary loaded graph to a frame.

use oakengine::ipc::{FrameSlotPool, SharedMemoryRegion, ShmMode};

use crate::ipc::HandshakeMsg;

/// Why `load_graph` answers "not yet available" (after the real file checks).
pub const GRAPH_STUB: &str = "load_graph: node-graph deserialization is not yet available in the \
     Rust worker (the oaknode crate is a todo!() skeleton; see worker/rust/README.md)";

/// Why `render_frame` answers "not yet available".
pub const RENDER_STUB: &str = "render_frame: frame rendering is not yet available in the Rust \
     worker (no node-graph or render-pipeline backing; the shm frame-slot transport is \
     attached but there is no graph to render; see worker/rust/README.md)";

/// The shared-memory frame-slot pools attached by a successful handshake,
/// kept alive for the session's lifetime.
pub struct AttachedPools {
	/// Worker->main output segment (unmapped on drop).
	pub output_region: SharedMemoryRegion,
	/// Output frame-slot pool view.
	pub output_pool: FrameSlotPool,
	/// Main->worker input segment, when the handshake announced one.
	pub input_region: Option<SharedMemoryRegion>,
	/// Input frame-slot pool view.
	pub input_pool: Option<FrameSlotPool>,
}

/// Attach the handshake's shared-memory frame-slot pools — the real port of
/// worker.cpp `attach_output_pool()`.
///
/// The output segment must exist and contain a valid [`FrameSlotPool`]
/// (magic check); the input pool is attached when `input_slots > 0`.
/// Returns `Err(message)` describing the failure, matching the C++ error
/// strings.
pub fn attach_pools(hs: &HandshakeMsg) -> Result<AttachedPools, String> {
	// Attach the worker->main output pool.
	let bytes = FrameSlotPool::bytes_needed(hs.output_slots as u32, hs.slot_data_bytes as usize);
	let mut output_region = SharedMemoryRegion::new();
	if !output_region.open(&hs.shm_key, bytes, ShmMode::Attach) {
		return Err(format!(
			"failed to attach shared memory: {}",
			output_region.error()
		));
	}
	// SAFETY: `output_region` is a live mapping of at least `bytes` bytes
	// (checked inside `open`).
	let output_pool = unsafe { FrameSlotPool::attach(output_region.data()) };
	if !output_pool.is_valid() {
		return Err("shared memory does not contain a frame slot pool".to_string());
	}

	// Attach the main->worker input pool when the handshake announced one.
	let mut input_region = None;
	let mut input_pool = None;
	if hs.input_slots > 0 {
		let input_bytes = FrameSlotPool::bytes_needed(
			hs.input_slots as u32,
			hs.input_slot_data_bytes as usize,
		);
		let mut region = SharedMemoryRegion::new();
		if !region.open(&hs.input_shm_key, input_bytes, ShmMode::Attach) {
			return Err(format!(
				"failed to attach input shared memory: {}",
				region.error()
			));
		}
		// SAFETY: `region` is a live mapping of at least `input_bytes`
		// bytes (checked inside `open`).
		let pool = unsafe { FrameSlotPool::attach(region.data()) };
		if !pool.is_valid() {
			return Err("input shared memory does not contain a frame slot pool".to_string());
		}
		input_region = Some(region);
		input_pool = Some(pool);
	}

	Ok(AttachedPools {
		output_region,
		output_pool,
		input_region,
		input_pool,
	})
}

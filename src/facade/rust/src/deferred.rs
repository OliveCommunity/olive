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

//! Deferred `oakengine_*` families and the reasons.
//!
//! This module exists purely as documentation: the areas below are in the
//! facade's scope (module-backed or assembly-layer) but are **not wrapped
//! yet**. Nothing here is exported.
//!
//! ## Genuinely facade-only areas (out of scope, per M9 §4)
//!
//! viewer/playback/preview/display/gizmo/app/events/exporter/disk/proxy/
//! serializer — the liboakengine assembly layer. No files for them in this
//! crate.
//!
//! worker and ipc were in this list too until the render-worker port
//! landed: [`worker`] (`engine/include/oakengine/worker.h`) and the
//! shared-memory frame-slot transport (`engine/include/oakengine/ipc.h`,
//! the shm/framepool half) now live in this crate — see `src/worker.rs`
//! and `src/ipc.rs`.
//!
//! The node/timeline/task families were deferred while the oaknode crate
//! was a `todo!()` skeleton and oaktimeline's test-stub mocks collided
//! with the real oakundo crate in one test binary. Both blockers are
//! cleared: oaknode now implements the module C ABI, and the facade links
//! oaknode/oaktimeline/oaktask WITHOUT their `test-stubs` features (see
//! README.md "Testing"), so the real exports resolve against the
//! dev-dependency rlibs. The families now live in [`node`]
//! (`engine/include/oakengine/{node,project,footage}.h`), [`timeline`]
//! (`engine/include/oakengine/timeline.h`) and [`task`]
//! (`engine/include/oakengine/task.h`).
//!
//! ## Partial coverage within wrapped families (documented stubs)
//!
//! The wrapped families still carry documented stubs where the module
//! crates lack the C ABI surface — each stub returns its header's
//! documented failure value:
//!
//! - **codec** (encoding.h, 81/85 wrapped): the preset path/count/name,
//!   preset load/save and the sequence-bound export/last-used entry
//!   points (`oakengine_encoding_preset_*`,
//!   `oakengine_encoding_params_load_file/save_file`,
//!   `oakengine_export_render_with_params`,
//!   `oakengine_encoding_params_get/set_last_used`) are stubs — the
//!   oakcodec crate has no preset API and those entry points need the
//!   exporter/sequence families.
//! - **render color** (color.h, 19/31 wrapped): the color-manager list
//!   queries (colorspace/display/view/look/compliant/luma), the
//!   standalone config handle and `color_processor_id` /
//!   `transform_job_set_processor` are stubs — the oakrender crate
//!   exposes only `color_manager_get_config`/`set_up_default_config` and
//!   the processor create/convert surface.
//! - **render lut** (lut.h, 0/5 wrapped): the directory/file library is
//!   facade-level over FileFunctions; the crate only enumerates supported
//!   LUT extensions.
//! - **render audio buffer** (renderer.h): the buffer accessors are
//!   stubs because the crate's `ticket_get_samples` path is
//!   unimplemented.
//! - **node** (node.h+project.h+footage.h, 226/327 wrapped): gizmo
//!   accessors, plugin messages, the QBrush getter, input properties,
//!   thumbnail/waveform caches, shape/subtitle blocks, keyframe
//!   enumeration (count/at/easing/remove/batch/handles-on-track — the
//!   oaknode keyframe C ABI is handle-only), input flags/array/data-type
//!   introspection, category/flags metadata, effect-input lookup,
//!   exclusive dependencies, `node_get_data`, transform-time, dependency
//!   copy, project color reference space / alongside cache path, footage
//!   audio-stream info, colorspace candidates, custom proxy params,
//!   source start time, stream-enabled, proxy generate. Each stub body
//!   carries the one-line reason.
//! - **timeline** (timeline.h, 126/139 wrapped): the ripple-tracks
//!   command, default transitions, move-track/move-clip, standalone
//!   marker creation, auto-cache accessors, clip cache invalidation and
//!   the multicam find/switch helpers are stubs — the oaktimeline/oaknode
//!   module surfaces for them do not exist (see the stub bodies).
//! - **task** (task.h, 27/27 wrapped): `oakengine_task_create_proxy` is
//!   stubbed (the oaktask crate exposes no proxy-task C creator);
//!   `oakengine_task_start_time`/`is_cancelled` are facade-approximated
//!   (the module has no getters).

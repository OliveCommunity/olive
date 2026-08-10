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

//! `engine/include/oakengine/task.h` — the engine background-task system
//! (the C++ `olive::Task` / `olive::TaskManager`) over the oaktask module.
//!
//! Task ownership follows the header: `oakengine_task_create_*` returns an
//! OWNED task; `oakengine_task_manager_add` hands it to the manager (which
//! deletes it when done, so the handle becomes borrowed);
//! `oakengine_task_free` deletes a task that never reached the manager.
//! A task run with `oakengine_task_start_sync` stays owned by the caller.
//!
//! The facade owns the global task manager (module-00 analogue of the C++
//! app-startup `TaskManager`): it is initialized lazily on the first
//! manager-family call, mirroring the undo family's process-wide stack.
//!
//! The oaktask module exposes no getters for the C++ `Task::get_start_time`
//! / `Task::is_cancelled` / `ProjectSaveTask::get_project`; those three are
//! answered from facade-side state recorded at creation/cancel
//! ([`TaskMeta`], see the per-export notes).
//!
//! String output follows the engine buf/size convention: the return value
//! is the would-be length **excluding** the NUL. The module reports the
//! size **including** the NUL, converted with
//! [`crate::handle::string_result`]; module error codes (-80001..) pass
//! through untranslated.

use std::collections::HashMap;
use std::ffi::{c_char, c_int, c_void};
use std::sync::{Mutex, OnceLock};
use std::time::{SystemTime, UNIX_EPOCH};

use crate::bridge::codec::EncodingParamsPOD;
use crate::bridge::node as n;
use crate::bridge::task as t;
use crate::codec::OakEngineEncodingParams;
use crate::common::OakVideoParamsPod;
use crate::error::{Error, Result};
use crate::handle::{
	box_handle, free_box, guard, guard_i64, guard_int, guard_ptr, string_result, unbox, CHandle,
	OakEngineClipboard, OakEngineNode, OakEngineProject, OakEngineSequence, OakEngineTask,
};

// ---------------------------------------------------------------------------
// Facade-side task state
// ---------------------------------------------------------------------------

/// Facade-side sidecars for tasks created through this module, keyed by the
/// module task handle's `ctx` (the stable identity of the underlying task;
/// see [`crate::handle::CHandle`]). Entries are dropped by
/// [`oakengine_task_free`]; a task handed to the manager keeps its entry
/// until free — the header forbids touching a borrowed handle after the
/// task is removed, so an entry left behind by a manager-run task is an
/// intentional, documented process-lifetime leak.
#[derive(Clone)]
struct TaskMeta {
	/// Epoch-millisecond creation stamp, returned by
	/// [`oakengine_task_start_time`] once the task has been started through
	/// the facade (the module has no start-time getter; the C++ reports the
	/// real `Task::get_start_time`).
	created_at_ms: u64,
	/// Whether the task was started through the facade
	/// (`oakengine_task_start_sync` / `oakengine_task_manager_add` /
	/// `oakengine_cli_task_dialog_run`).
	started: bool,
	/// Facade-initiated cancel flag (the module has no `is_cancelled`
	/// getter; only cancels made through this facade are visible).
	cancelled: bool,
	/// The project a save task writes (addref'd at creation, released at
	/// free) — the module has no save-project getter.
	save_project: Option<CHandle>,
	/// The encoding-params box an export task owns, dropped at free
	/// (mirrors the C++ `FacadeExportTask` destructor; stored as `usize` so
	/// the map stays `Send`).
	export_params: Option<usize>,
	/// The color manager an export task owns, released at free.
	export_color_manager: Option<CHandle>,
}

impl TaskMeta {
	fn new() -> Self {
		TaskMeta {
			created_at_ms: now_millis(),
			started: false,
			cancelled: false,
			save_project: None,
			export_params: None,
			export_color_manager: None,
		}
	}
}

/// Epoch milliseconds (0 when the clock is before the epoch; never in
/// practice).
fn now_millis() -> u64 {
	SystemTime::now()
		.duration_since(UNIX_EPOCH)
		.map(|d| d.as_millis() as u64)
		.unwrap_or(0)
}

static META: OnceLock<Mutex<HashMap<usize, TaskMeta>>> = OnceLock::new();

fn meta_lock() -> std::sync::MutexGuard<'static, HashMap<usize, TaskMeta>> {
	META.get_or_init(|| Mutex::new(HashMap::new()))
		.lock()
		.unwrap_or_else(|e| e.into_inner())
}

fn meta_insert(key: usize, meta: TaskMeta) {
	meta_lock().insert(key, meta);
}

fn meta_get(key: usize) -> Option<TaskMeta> {
	meta_lock().get(&key).cloned()
}

fn meta_set_started(key: usize) {
	if let Some(m) = meta_lock().get_mut(&key) {
		m.started = true;
	}
}

fn meta_set_cancelled(key: usize) {
	if let Some(m) = meta_lock().get_mut(&key) {
		m.cancelled = true;
	}
}

/// Release every facade-side sidecar of a task (called by
/// [`oakengine_task_free`]): the addref'd save project, the owned
/// encoding-params box and the derived color manager of an export task.
fn drop_task_meta(key: usize) {
	if let Some(meta) = meta_lock().remove(&key) {
		if let Some(mut project) = meta.save_project {
			unsafe { n::oaknode_project_free(&mut project) };
		}
		if let Some(ptr) = meta.export_params {
			unsafe {
				crate::codec::oakengine_encoding_params_destroy(ptr as *mut OakEngineEncodingParams)
			};
		}
		if let Some(mut manager) = meta.export_color_manager {
			unsafe { n::oaknode_colormanager_free(&mut manager) };
		}
	}
}

/// Box an owned module task handle as an engine handle, registering its
/// facade sidecars. NULL/empty handles stay NULL.
fn box_task(h: CHandle) -> *mut OakEngineTask {
	if h.is_null() {
		return std::ptr::null_mut();
	}
	meta_insert(h.ctx as usize, TaskMeta::new());
	box_handle::<OakEngineTask>(h)
}

// ---------------------------------------------------------------------------
// Global task manager
// ---------------------------------------------------------------------------

/// Lazily initialize the global task manager on first facade use
/// (module-00 analogue of the C++ app-startup `TaskManager` creation; the
/// same pattern as the undo family's `global_stack`). The manager lives for
/// the process. `oaktask_manager_init` only fails when already initialized,
/// which the `OnceLock` prevents, so this always succeeds.
fn manager_ensure() -> Result<()> {
	static INIT: OnceLock<()> = OnceLock::new();
	let _ = INIT.get_or_init(|| unsafe {
		t::oaktask_manager_init();
	});
	Ok(())
}

/// Stable opaque token for `oakengine_task_manager_handle`: a boxed
/// [`CHandle`] whose `ctx` is the address of a facade static (never
/// dereferenced). The oaktask module exposes no manager handle, so the
/// token exists purely to give the (out-of-scope, per README)
/// `OAKENGINE_EVENT_TASK_MANAGER_*` subscription an ABI-ready handle. The
/// box is leaked for the process, like the C++ `TaskManager::instance()`.
fn manager_token() -> *mut c_void {
	static TOKEN: OnceLock<usize> = OnceLock::new();
	// Stored as `usize` so the `OnceLock` stays `Sync`.
	let boxed = TOKEN.get_or_init(|| {
		box_handle::<OakEngineTask>(CHandle {
			ctx: &MANAGER_TOKEN as *const u8 as *mut c_void,
			addref: None,
			release: None,
			abi_version: 0,
		}) as usize
	});
	*boxed as *mut OakEngineTask as *mut c_void
}

static MANAGER_TOKEN: u8 = 0;

/// `oakengine_task_manager_handle` — borrowed token of the global task
/// manager (NULL never: the facade initializes the manager lazily on first
/// use, see [`manager_ensure`]; the C++ engine creates it at app startup).
#[no_mangle]
pub extern "C" fn oakengine_task_manager_handle() -> *mut c_void {
	guard_ptr(|| {
		manager_ensure()?;
		Ok(manager_token())
	})
}

/// `oakengine_task_manager_count` — number of tasks known to the manager
/// (running plus failed-but-kept). The manager is created on first use, so
/// the header's "no manager exists" state is unreachable (0 when empty).
#[no_mangle]
pub extern "C" fn oakengine_task_manager_count() -> c_int {
	guard_int(|| {
		manager_ensure()?;
		Ok(unsafe { t::oaktask_manager_count() })
	})
}

/// `oakengine_task_manager_first` — borrowed handle of the manager's first
/// task (NULL when the queue is empty).
#[no_mangle]
pub extern "C" fn oakengine_task_manager_first() -> *mut OakEngineTask {
	guard_ptr(|| {
		manager_ensure()?;
		let h = unsafe { t::oaktask_manager_at(0) };
		if h.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineTask>(h))
	})
}

/// `oakengine_task_manager_add` — hand `task` to the manager queue
/// (transfers ownership; the manager deletes the task when done). The
/// module's `oaktask_task_start` performs the transfer; a task already
/// running on the manager reports the module's `OAKTASK_E_STATE`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_manager_add(task: *mut OakEngineTask) -> c_int {
	guard(|| unsafe {
		let h = unbox(task)?;
		manager_ensure()?;
		Error::from_module(t::oaktask_task_start(h))?;
		meta_set_started(h.ctx as usize);
		Ok(())
	})
}

/// `oakengine_task_manager_cancel` — ask the manager to cancel `task`. The
/// module's `oaktask_task_cancel` signals the running task's cancellation
/// atom; the "failed-but-kept task is removed and deleted" half is managed
/// by the module's own bookkeeping (`oaktask_manager_delete_finished`) and
/// has no engine export, so it is not mirrored here.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_manager_cancel(task: *mut OakEngineTask) -> c_int {
	guard(|| unsafe {
		let h = unbox(task)?;
		manager_ensure()?;
		Error::from_module(t::oaktask_task_cancel(h))?;
		meta_set_cancelled(h.ctx as usize);
		Ok(())
	})
}

// ---------------------------------------------------------------------------
// Task accessors
// ---------------------------------------------------------------------------

/// `oakengine_task_title` (buf/size; E_INVALID for NULL).
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_title(
	task: *mut OakEngineTask,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(task)?;
		let rc = t::oaktask_task_title(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_task_error` (buf/size; E_INVALID for NULL). Meaningful after
/// a failed run.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_error(
	task: *mut OakEngineTask,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(task)?;
		let rc = t::oaktask_task_error(h, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_task_start_time` — start timestamp in epoch milliseconds.
///
/// The module has no start-time getter, so the facade reports the
/// **creation** stamp (see [`TaskMeta`]) once the task has been started
/// through the facade; 0 before then (matching "0 when the task never
/// started"). Deviation from the C++ `Task::get_start_time`, which records
/// the actual start instant.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_start_time(task: *mut OakEngineTask) -> i64 {
	guard_i64(|| unsafe {
		let h = unbox(task)?;
		Ok(match meta_get(h.ctx as usize) {
			Some(m) if m.started => m.created_at_ms as i64,
			_ => 0,
		})
	})
}

/// `oakengine_task_is_cancelled` — 1 when the task was asked to cancel.
///
/// The module has no `is_cancelled` getter, so only cancels issued through
/// [`oakengine_task_cancel`] / [`oakengine_task_manager_cancel`] on this
/// facade are visible (0 otherwise). Deviation from the C++
/// `Task::is_cancelled`, which reflects the task's own cancellation atom.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_is_cancelled(task: *mut OakEngineTask) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(task)?;
		Ok(if meta_get(h.ctx as usize).map(|m| m.cancelled).unwrap_or(false) {
			1
		} else {
			0
		})
	})
}

/// `oakengine_task_cancel` — signal the task to cancel as soon as possible
/// (module `Task::cancel`, the `Task::Cancel` analogue).
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_cancel(task: *mut OakEngineTask) -> c_int {
	guard(|| unsafe {
		let h = unbox(task)?;
		Error::from_module(t::oaktask_task_cancel(h))?;
		meta_set_cancelled(h.ctx as usize);
		Ok(())
	})
}

/// `oakengine_task_start_sync` — run on the calling thread; 1 = succeeded,
/// 0 = failed or cancelled, E_INVALID for NULL. Ownership stays with the
/// caller.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_start_sync(task: *mut OakEngineTask) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(task)?;
		let rc = t::oaktask_task_start_sync(h);
		meta_set_started(h.ctx as usize);
		Ok(rc)
	})
}

/// `oakengine_task_free` — delete a task that was never added to the
/// manager (releases the module task handle, which drops an owned task).
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_free(task: *mut OakEngineTask) -> c_int {
	guard(|| unsafe {
		if task.is_null() {
			return Err(Error::Invalid);
		}
		let h = (*task).handle;
		if h.is_null() {
			return Err(Error::Invalid);
		}
		drop_task_meta(h.ctx as usize);
		free_box::<OakEngineTask>(task);
		Ok(())
	})
}

/// `oakengine_cli_task_dialog_run` — run `task` through the engine's CLI
/// modal progress dialog; 1 on success, 0 on failure/cancellation.
///
/// The C++ `CLITaskDialog` renders a terminal progress dialog around a
/// synchronous run; the facade ports the observable behavior (sync run,
/// 1/0 result) with the dialog chrome itself stubbed. `parent` is unused.
/// The capi returns 0 (not E_INVALID) for a NULL task, so this mirrors it.
#[no_mangle]
pub unsafe extern "C" fn oakengine_cli_task_dialog_run(
	task: *mut OakEngineTask,
	_parent_or_null: *mut c_void,
) -> c_int {
	guard_int(|| unsafe {
		if task.is_null() {
			return Ok(0);
		}
		let h = unbox(task)?;
		let rc = t::oaktask_task_start_sync(h);
		meta_set_started(h.ctx as usize);
		Ok(rc)
	})
}

// ---------------------------------------------------------------------------
// Task creators (all return OWNED tasks, NULL on invalid input)
// ---------------------------------------------------------------------------

/// `oakengine_task_create_project_load` — task that loads an OVE project
/// from `filename`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_create_project_load(
	filename: *const c_char,
) -> *mut OakEngineTask {
	guard_ptr(|| unsafe {
		if filename.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_task(t::oaktask_create_project_load(filename)))
	})
}

/// `oakengine_task_create_project_load_otio` — task that loads an
/// OpenTimelineIO project. The module always supports OTIO (the interchange
/// format is inferred from the filename extension), so valid input never
/// yields the header's "built without OTIO support" NULL.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_create_project_load_otio(
	filename: *const c_char,
) -> *mut OakEngineTask {
	guard_ptr(|| unsafe {
		if filename.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_task(t::oaktask_create_project_load_otio(filename)))
	})
}

/// `oakengine_task_create_project_save` — task that saves `project`.
///
/// `use_compression` selects the compressed `.ove` writer; `override_filename`
/// may be NULL to save to the project's own filename. `layout` (an opaque
/// `SerializedLayoutInfo *` in the engine) is **ignored**: the module's
/// `ProjectSaveTask` has no layout slot, so a non-NULL layout is accepted
/// but not copied into the file.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_create_project_save(
	project: *mut OakEngineProject,
	use_compression: c_int,
	override_filename: *const c_char,
	_layout: *const c_void,
) -> *mut OakEngineTask {
	guard_ptr(|| unsafe {
		let ph = unbox(project)?;
		let h = t::oaktask_create_project_save(ph, override_filename, use_compression);
		if h.is_null() {
			return Ok(std::ptr::null_mut());
		}
		// Keep the project borrowed for the task's lifetime so
		// `oakengine_task_save_get_project` can answer from facade state
		// (the module has no save-project getter).
		let mut meta = TaskMeta::new();
		meta.save_project = Some(ph.addref());
		meta_insert(h.ctx as usize, meta);
		Ok(box_handle::<OakEngineTask>(h))
	})
}

/// `oakengine_task_create_project_save_otio` — task that saves `project` in
/// OpenTimelineIO format.
///
/// The engine header passes only the project, but the module's creator
/// requires the output filename; the facade derives it from the project's
/// own filename (the OTIO save of the current project file) and returns
/// NULL when the project has no filename. The module always supports OTIO,
/// so a valid input never yields the "built without OTIO support" NULL.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_create_project_save_otio(
	project: *mut OakEngineProject,
) -> *mut OakEngineTask {
	guard_ptr(|| unsafe {
		let ph = unbox(project)?;
		let filename = project_filename_of(ph)?;
		if filename.is_empty() {
			return Ok(std::ptr::null_mut());
		}
		let c_filename =
			std::ffi::CString::new(filename).map_err(|_| Error::Failed("invalid filename".into()))?;
		Ok(box_task(t::oaktask_create_project_save_otio(ph, c_filename.as_ptr())))
	})
}

/// Two-stage read of the project's filename (empty when unset).
fn project_filename_of(project: CHandle) -> Result<String> {
	let needed = unsafe { n::oaknode_project_filename(project, std::ptr::null_mut(), 0) };
	if needed <= 0 {
		return Ok(String::new());
	}
	let mut buf = vec![0 as c_char; needed as usize];
	let rc = unsafe { n::oaknode_project_filename(project, buf.as_mut_ptr(), needed) };
	if rc < 0 {
		return Err(Error::Module(rc));
	}
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	Ok(String::from_utf8_lossy(unsafe {
		std::slice::from_raw_parts(buf.as_ptr() as *const u8, len)
	})
	.into_owned())
}

/// `oakengine_task_create_project_import` — task that imports `url_count`
/// media files into `folder` (a folder node of the target project).
///
/// The URL array is copied by the module during the call. The engine header
/// passes only the folder; the module creator needs the owning project,
/// derived here via `oaknode_node_get_project`. Unlike the capi (which
/// rejects `url_count <= 0`), a zero-count task IS created — the header's
/// `oakengine_task_import_file_count` documents 0 as "nothing to import,
/// free instead of run". `url_count < 0`, a NULL URL inside the array, or a
/// folder with no project yield NULL.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_create_project_import(
	folder: *mut OakEngineNode,
	urls: *const *const c_char,
	url_count: c_int,
) -> *mut OakEngineTask {
	guard_ptr(|| unsafe {
		let fh = unbox(folder)?;
		if url_count < 0 || (urls.is_null() && url_count > 0) {
			return Ok(std::ptr::null_mut());
		}
		let mut project = CHandle::null();
		Error::from_module(n::oaknode_node_get_project(fh, &mut project))?;
		if project.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let h = t::oaktask_create_project_import(fh, project, urls, url_count);
		// Release the transient borrowed project handle (the import task
		// keeps its own copy).
		n::oaknode_project_free(&mut project);
		Ok(box_task(h))
	})
}

/// `oakengine_task_create_proxy` — **not backed** (stub, always NULL).
///
/// The oaktask crate's `ProxyTask` is driven by a codec task request and no
/// proxy-task creator exists on the module C ABI (`oaktask_create_precache`
/// is a different task). The engine's `FacadeProxyTask` would need
/// `oakengine_footage_proxy_generate`, which lives in the deferred exporter
/// family (see `deferred.rs`). Returns NULL per the creators' "NULL on
/// invalid input" contract.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_create_proxy(
	_footage: *mut OakEngineNode,
) -> *mut OakEngineTask {
	std::ptr::null_mut()
}

/// `oakengine_task_create_export` — task that renders an export of
/// `sequence` with `params`.
///
/// Takes ownership of `params` (destroyed with the task, mirroring the C++
/// `FacadeExportTask` destructor; the module copies the POD it needs at
/// creation, so the retained box is a lifetime guarantee for C callers).
/// The color manager is derived from the sequence's owning project
/// (`oaknode_colormanager_init`), mirroring how the C++ exporter obtains
/// its manager; a sequence without a project exports with an empty manager.
/// The module creator requires a POD pointer, so the facade's opaque params
/// handle is copied out through the public `oakengine_encoding_params_*`
/// getters ([`export_params_pod`]) — its backing `ParamsBox` (POD + option
/// map) is private to `codec.rs` and cannot be read here.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_create_export(
	sequence: *mut OakEngineSequence,
	params: *mut OakEngineEncodingParams,
) -> *mut OakEngineTask {
	guard_ptr(|| unsafe {
		let vh = unbox(sequence)?;
		if params.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let pod = export_params_pod(params)?;
		let color_manager = export_color_manager(vh)?;
		let h = t::oaktask_create_export(vh, color_manager, &pod);
		if h.is_null() {
			// Creation failed: release the color manager we derived.
			let mut manager = color_manager;
			n::oaknode_colormanager_free(&mut manager);
			return Ok(std::ptr::null_mut());
		}
		let mut meta = TaskMeta::new();
		meta.export_params = Some(params as usize);
		meta.export_color_manager = Some(color_manager);
		meta_insert(h.ctx as usize, meta);
		Ok(box_handle::<OakEngineTask>(h))
	})
}

/// Copy the encoding-params POD the oaktask export creator reads out of the
/// facade's opaque params handle via its public getters.
///
/// The oaktask crate's `convert_encoding_params` consumes exactly these
/// fields (filename, format, video/audio/subtitle enables, codecs,
/// dimensions, time base, pixel format, export length), so a POD carrying
/// them is behaviorally identical to the original for the export task; all
/// other POD fields stay zeroed.
fn export_params_pod(params: *const OakEngineEncodingParams) -> Result<EncodingParamsPOD> {
	let mut pod = EncodingParamsPOD::zeroed();

	// filename (two-stage; writes NUL-terminated into `buf`)
	let mut buf = [0 as c_char; 1024];
	let rc = unsafe {
		crate::codec::oakengine_encoding_params_filename(params, buf.as_mut_ptr(), buf.len() as c_int)
	};
	if rc < 0 {
		return Err(Error::Invalid);
	}
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	unsafe { std::ptr::copy_nonoverlapping(buf.as_ptr(), pod.filename.as_mut_ptr(), len) };

	pod.format = unsafe { crate::codec::oakengine_encoding_params_format(params) };
	pod.video_enabled = unsafe { crate::codec::oakengine_encoding_params_video_enabled(params) };
	pod.video_codec = unsafe { crate::codec::oakengine_encoding_params_video_codec(params) };
	pod.audio_enabled = unsafe { crate::codec::oakengine_encoding_params_audio_enabled(params) };
	pod.audio_codec = unsafe { crate::codec::oakengine_encoding_params_audio_codec(params) };
	pod.subtitles_enabled = unsafe {
		crate::codec::oakengine_encoding_params_subtitles_enabled(params)
	};
	unsafe {
		crate::codec::oakengine_encoding_params_get_export_length(
			params,
			&mut pod.export_length_num,
			&mut pod.export_length_den,
		);
	}

	if pod.video_enabled != 0 {
		let mut video = std::mem::MaybeUninit::<OakVideoParamsPod>::uninit();
		let rc = unsafe {
			crate::codec::oakengine_encoding_params_get_video_params(params, video.as_mut_ptr())
		};
		if rc == 0 {
			let v = unsafe { video.assume_init() };
			pod.video_width = v.width;
			pod.video_height = v.height;
			pod.video_time_base_num = v.time_base_num;
			pod.video_time_base_den = v.time_base_den;
			pod.video_pixel_format = v.format;
		}
	}
	Ok(pod)
}

/// Derive a color manager for an export task from the sequence's owning
/// project (borrowed project handle released after the manager is created).
/// Empty when the sequence has no project — the module export accepts an
/// empty manager.
fn export_color_manager(sequence: CHandle) -> Result<CHandle> {
	let mut project = CHandle::null();
	Error::from_module(unsafe { n::oaknode_node_get_project(sequence, &mut project) })?;
	if project.is_null() {
		return Ok(CHandle::null());
	}
	let manager = unsafe { n::oaknode_colormanager_init(project) };
	unsafe { n::oaknode_project_free(&mut project) };
	Ok(manager)
}

// ---------------------------------------------------------------------------
// Import task results
// ---------------------------------------------------------------------------

/// `oakengine_task_import_file_count` — number of files the import task
/// will process.
///
/// The module's only import count export is `oaktask_import_footage_count`,
/// which reports the **imported-footage** list length — 0 before the task
/// runs even when files were supplied. Deviation from the C++
/// `get_file_count` (the construction-time count); "0 means nothing to
/// import yet" holds before a run either way.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_import_file_count(task: *mut OakEngineTask) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(task)?;
		let rc = t::oaktask_import_footage_count(h);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(rc)
		}
	})
}

/// `oakengine_task_import_get_command` — the undo command built by a
/// successful import run as an opaque `OakEngineClipboard` (NULL before the
/// run, after a cancelled run, or on a second call). Ownership detaches
/// from the task; push it with `oakengine_undo_push` or free it.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_import_get_command(task: *mut OakEngineTask) -> *mut c_void {
	guard_ptr(|| unsafe {
		let h = unbox(task)?;
		let cmd = t::oaktask_import_take_command(h);
		if cmd.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineClipboard>(cmd).cast())
	})
}

/// `oakengine_task_import_footage_count` — number of footage items a
/// successful import run created (the module's `oaktask_import_footage_count`,
/// the same count reported by `oakengine_task_import_file_count`).
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_import_footage_count(task: *mut OakEngineTask) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(task)?;
		let rc = t::oaktask_import_footage_count(h);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(rc)
		}
	})
}

/// `oakengine_task_import_footage_at` — borrowed node handle of the
/// imported footage at `index` (NULL when out of range or not an import
/// task). The module addrefs the footage handle; the caller releases it
/// with `oakengine_node_free`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_import_footage_at(
	task: *mut OakEngineTask,
	index: c_int,
) -> *mut OakEngineNode {
	guard_ptr(|| unsafe {
		let h = unbox(task)?;
		let fh = t::oaktask_import_footage_at(h, index);
		if fh.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineNode>(fh))
	})
}

/// `oakengine_task_import_invalid_files_count` — number of files the import
/// task rejected.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_import_invalid_files_count(
	task: *mut OakEngineTask,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(task)?;
		let rc = t::oaktask_import_invalid_count(h);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(rc)
		}
	})
}

/// `oakengine_task_import_invalid_file_at` — rejected file path at `index`
/// (buf/size). Out-of-range reports the module's `OAKTASK_E_NOT_FOUND`
/// (-80004) pass-through, the header's "E_INVALID for other tasks" being
/// covered by the NULL-task `OAKENGINE_E_INVALID` path.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_import_invalid_file_at(
	task: *mut OakEngineTask,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = unbox(task)?;
		let rc = t::oaktask_import_invalid_at(h, index, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

// ---------------------------------------------------------------------------
// Save task results
// ---------------------------------------------------------------------------

/// `oakengine_task_save_get_project` — borrowed handle of the project a
/// save task wrote (NULL for other tasks).
///
/// The module has no save-project getter, so the project is kept borrowed
/// from creation in [`TaskMeta`] (mirroring the C++ `ProjectSaveTask::
/// get_project`, which returns the project the task was created with).
/// Each call returns a fresh borrowed handle the caller releases with
/// `oakengine_project_free`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_task_save_get_project(task: *mut OakEngineTask) -> *mut OakEngineProject {
	guard_ptr(|| unsafe {
		let h = unbox(task)?;
		match meta_get(h.ctx as usize).and_then(|m| m.save_project) {
			Some(p) => Ok(box_handle::<OakEngineProject>(p.addref())),
			None => Ok(std::ptr::null_mut()),
		}
	})
}

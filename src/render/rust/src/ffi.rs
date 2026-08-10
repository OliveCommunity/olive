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

//! C ABI export layer: implements `include/render/*.h` verbatim.
//!
//! One submodule per public header; the header is the authoritative
//! function inventory. Every export goes through `handle::guard*`;
//! pointer/struct marshalling follows the C layout exactly.

// Parameter names mirror the C headers verbatim (e.g. `p_or_NULL`).
#![allow(non_snake_case)]
#![allow(clippy::missing_safety_doc)]

use std::ffi::{c_char, c_int, c_void};

use crate::handle::CHandle;

// ---- handle type aliases (all oak render handle structs share the
// neutral {ctx, addref, release, abi_version} layout) ------------------------

/// `OakNodeNode` / `OakNodeProject` / `OakNodeColorManager` (by-value
/// handles from oaknode).
pub type OakNodeNode = CHandle;
/// OakNodeProject (forward-declared opaque).
pub type OakNodeProject = CHandle;
/// OakNodeColorManager.
pub type OakNodeColorManager = CHandle;
/// `OakVideoParams` / `OakColorTransform` / `OakAudioParams` (oakcommon /
/// oakcore value handles).
pub type OakVideoParams = CHandle;
/// OakColorTransform.
pub type OakColorTransform = CHandle;
/// OakAudioParams.
pub type OakAudioParams = CHandle;
/// `OakRenderCache` (render/cache.h).
pub type OakRenderCache = CHandle;
/// `OakRenderTicket` (render/ticket.h).
pub type OakRenderTicket = CHandle;
/// `OakRenderTexture` (render/renderer.h).
pub type OakRenderTexture = CHandle;
/// `OakCodecFrame` (render/renderer.h).
pub type OakCodecFrame = CHandle;
/// `OakColorProcessor` (render/color.h).
pub type OakColorProcessor = CHandle;
/// `OakRenderRenderer` (render/renderer.h).
pub type OakRenderRenderer = CHandle;
/// `OakRenderProjectCopier` (render/copier.h).
pub type OakRenderProjectCopier = CHandle;
/// `OakCancelAtom` (render/cancelatom.h).
pub type OakCancelAtom = CHandle;

/// `oakrender_video_params` POD (render/renderer.h).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct OakRenderVideoParams {
	/// Width.
	pub width: c_int,
	/// Height.
	pub height: c_int,
	/// Frame duration numerator.
	pub time_base_num: c_int,
	/// Frame duration denominator.
	pub time_base_den: c_int,
	/// PixelFormat as int.
	pub format: c_int,
	/// Pixel aspect numerator.
	pub pixel_aspect_num: c_int,
	/// Pixel aspect denominator.
	pub pixel_aspect_den: c_int,
	/// Interlacing as int.
	pub interlacing: c_int,
	/// Color range as int.
	pub color_range: c_int,
	/// Preview divider.
	pub divider: c_int,
	/// Video type.
	pub video_type: c_int,
	/// Premultiplied alpha 0/1.
	pub premultiplied_alpha: c_int,
}

/// `oakrender_video_ticket_params` (render/ticket.h).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakVideoTicketParams {
	/// Connected texture output node (borrowed).
	pub output_node: OakNodeNode,
	/// By-value oakcommon handle.
	pub video_params: OakVideoParams,
	/// Borrowed oakcore handle, may be null.
	pub audio_params: *const OakAudioParams,
	/// Frame timestamp as rational.
	pub time_num: i64,
	/// Frame timestamp as rational.
	pub time_den: i64,
	/// Borrowed, empty ctx = null.
	pub color_manager: OakNodeColorManager,
	/// RenderMode::Mode as int.
	pub mode: c_int,
	/// 0/0 = off.
	pub force_width: c_int,
	/// 0/0 = off.
	pub force_height: c_int,
	/// Used when has_force_matrix != 0.
	pub force_matrix: [f64; 16],
	/// 0/1.
	pub has_force_matrix: c_int,
	/// PixelFormat as int, -1 = off.
	pub force_format: c_int,
	/// 0 = off.
	pub force_channel_count: c_int,
	/// Borrowed; empty ctx = none.
	pub force_color_output: OakColorProcessor,
	/// By value; empty ctx = default.
	pub force_color_transform: OakColorTransform,
	/// Borrowed frame cache; empty ctx = none.
	pub cache: OakRenderCache,
}

/// `oakrender_color_transform_job` (render/renderer.h).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakColorTransformJob {
	/// OakColorProcessor ctx (borrowed), may be null.
	pub processor: *const c_void,
	/// OakRenderTexture ctx (borrowed, not retained).
	pub input_texture: *mut c_void,
	/// 0=none, 1=associated.
	pub input_alpha_association: c_int,
	/// 0/1.
	pub clear_destination: c_int,
	/// 0/1.
	pub force_opaque: c_int,
	/// Column-major 4x4 (all-zero = identity).
	pub matrix: [f32; 16],
	/// Column-major 4x4 (all-zero = identity).
	pub crop_matrix: [f32; 16],
}

// ---- shared marshalling helpers --------------------------------------------

/// Two-stage string getter: returns the required buffer size including
/// NUL; writes only when the buffer is large enough.
fn write_string(s: &str, buf: *mut c_char, n: c_int) -> c_int {
	let required = s.len() + 1;
	if !buf.is_null() && n >= required as c_int {
		unsafe {
			std::ptr::copy_nonoverlapping(s.as_ptr() as *const c_char, buf, s.len());
			*buf.add(s.len()) = 0;
		}
	}
	required as c_int
}

/// Panic-catching wrapper for two-stage string getters: Ok(needed) is
/// returned verbatim (a non-negative size), Err maps to the error code.
fn guard_string<F: FnOnce() -> crate::error::Result<c_int>>(f: F) -> c_int {
	match std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)) {
		Ok(Ok(sz)) => sz,
		Ok(Err(e)) => e.code(),
		Err(_) => crate::error::OAKRENDER_E_FAILED,
	}
}

/// Map an `OakPixelFormat` int to the crate's pixel format.
fn oakcore_pixel_format(v: c_int) -> oakcore_rs::PixelFormat {
	match v {
		0 => oakcore_rs::PixelFormat::U8,
		1 => oakcore_rs::PixelFormat::U10,
		2 => oakcore_rs::PixelFormat::U16,
		3 => oakcore_rs::PixelFormat::F16,
		4 => oakcore_rs::PixelFormat::F32,
		_ => oakcore_rs::PixelFormat::Invalid,
	}
}

/// Read a NUL-terminated C string (None for null / non-UTF8).
unsafe fn cstr(p: *const c_char) -> Option<String> {
	if p.is_null() {
		return None;
	}
	unsafe { std::ffi::CStr::from_ptr(p) }
		.to_str()
		.ok()
		.map(|s| s.to_string())
}

/// A boxed `Frame` as an owned `OakCodecFrame` handle.
fn frame_handle(frame: crate::texture::Frame) -> CHandle {
	crate::handle::make_owned(frame)
}

/// View a borrowed ctx pointer (job structs carry `ctx` pointers by
/// value, see the header's "borrowed, not retained" notes) as a handle.
fn ctx_handle(ctx: *const c_void) -> CHandle {
	CHandle {
		ctx: ctx as *mut c_void,
		addref: None,
		release: None,
		abi_version: crate::handle::OAKRENDER_ABI_VERSION,
	}
}

/// A borrowed opaque identity box (C++ borrowed `make_handle(…, false)`):
/// releasing it only frees the box. Never dereferenced by this crate.
#[derive(Clone, Copy, Debug)]
struct BorrowedOpaque {
	#[allow(dead_code)]
	identity: u64,
}

/// Raw `userdata` pointer carried across threads (C convention: the
/// callback fires on the worker thread with the same userdata).
#[derive(Clone, Copy)]
struct RawSend(*mut c_void);
unsafe impl Send for RawSend {}

impl RawSend {
	fn ptr(self) -> *mut c_void {
		self.0
	}
}

/// Map a `VideoParamsPod` to the public POD.
fn pod_to_ffi(p: &crate::frame::VideoParamsPod) -> OakRenderVideoParams {
	OakRenderVideoParams {
		width: p.width,
		height: p.height,
		time_base_num: p.time_base_num,
		time_base_den: p.time_base_den,
		format: p.format,
		pixel_aspect_num: p.pixel_aspect_num,
		pixel_aspect_den: p.pixel_aspect_den,
		interlacing: p.interlacing,
		color_range: p.color_range,
		divider: p.divider,
		video_type: p.video_type,
		premultiplied_alpha: p.premultiplied_alpha,
	}
}

/// Map the public POD to a `VideoParamsPod` (divider defaults to 1).
fn ffi_to_pod(p: &OakRenderVideoParams) -> crate::frame::VideoParamsPod {
	crate::frame::VideoParamsPod {
		width: p.width,
		height: p.height,
		time_base_num: p.time_base_num,
		time_base_den: p.time_base_den,
		format: p.format,
		pixel_aspect_num: p.pixel_aspect_num,
		pixel_aspect_den: p.pixel_aspect_den,
		interlacing: p.interlacing,
		color_range: p.color_range,
		divider: if p.divider > 0 { p.divider } else { 1 },
		video_type: p.video_type,
		premultiplied_alpha: p.premultiplied_alpha,
	}
}

// ============================================================================
// render/manager.h
// ============================================================================

/// `include/render/manager.h` exports: the manager singleton lifecycle,
/// the async frame request path, the auto-cacher settings, and the disk
/// cache singleton.
pub mod manager {
	use super::*;
	use std::sync::atomic::{AtomicI64, Ordering};
	use std::sync::Mutex;

	/// In-flight frame requests: request id → (arena, ticket id).
	static REQUESTS: std::sync::LazyLock<
		Mutex<std::collections::HashMap<i64, (std::sync::Arc<crate::ticket::TicketArena>, crate::ticket::TicketId)>>,
	> = std::sync::LazyLock::new(|| Mutex::new(std::collections::HashMap::new()));
	static NEXT_REQUEST_ID: AtomicI64 = AtomicI64::new(1);

	/// `oakrender_manager_init`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_manager_init() -> c_int {
		crate::handle::guard(|| {
			crate::manager::RenderManager::init()?;
			Ok(())
		})
	}

	/// `oakrender_manager_shutdown`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_manager_shutdown() {
		crate::handle::guard_void(|| {
			crate::manager::RenderManager::shutdown();
			lock().clear();
		});
	}

	/// `oakrender_manager_available`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_manager_available() -> c_int {
		if crate::manager::RenderManager::global().is_some() {
			1
		} else {
			0
		}
	}

	/// `oakrender_request_frame` — async single-frame render with a
	/// completion callback. `ts` is a frame number; without the viewer's
	/// timebase (node bridge pending) it is treated as whole seconds.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_request_frame(
		viewer: OakNodeNode,
		ts: i64,
		cb: Option<unsafe extern "C" fn(OakCodecFrame, i64, *mut c_void)>,
		userdata: *mut c_void,
	) -> i64 {
		if viewer.is_null() || cb.is_none() {
			return crate::error::OAKRENDER_E_INVALID as i64;
		}
		let Some(manager) = crate::manager::RenderManager::global() else {
			return crate::error::OAKRENDER_E_STATE as i64;
		};
		let time = oakcore_rs::Rational::from_double(ts as f64);
		let rid = NEXT_REQUEST_ID.fetch_add(1, Ordering::Relaxed);
		let arena = manager.tickets.clone();
		let cb = cb.unwrap();
		let userdata = RawSend(userdata);

		let mut cacher = manager.get_cacher();
		let cacher = cacher.as_mut().expect("cacher lazily created");
		cacher.set_viewer_identity(Some(viewer.ctx as u64));
		let ticket = cacher.single_frame_with_completion(time, {
			let _arena = arena.clone();
			Box::new(move |result| {
				let handle = match result {
					Ok(tex) => match tex.to_frame() {
						Ok(frame) => frame_handle(frame),
						Err(_) => CHandle::null(),
					},
					Err(_) => CHandle::null(),
				};
				lock().remove(&rid);
				unsafe { cb(handle, ts, userdata.ptr()) };
			})
		});
		lock().insert(rid, (arena, ticket));
		rid
	}

	/// `oakrender_cancel_request`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cancel_request(request_id: i64) -> c_int {
		let entry = lock().remove(&request_id);
		match entry {
			Some((arena, ticket)) => {
				arena.cancel(ticket);
				crate::error::OAKRENDER_OK
			}
			None => crate::error::OAKRENDER_E_NOT_FOUND,
		}
	}

	/// `oakrender_set_cacher_multicam`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_set_cacher_multicam(multicam_or_NULL: OakNodeNode) -> c_int {
		let Some(manager) = crate::manager::RenderManager::global() else {
			return crate::error::OAKRENDER_E_STATE;
		};
		let mut cacher = manager.get_cacher();
		if let Some(c) = cacher.as_mut() {
			c.multicam_node = if multicam_or_NULL.is_null() {
				None
			} else {
				Some(multicam_or_NULL.ctx as u64)
			};
		}
		crate::error::OAKRENDER_OK
	}

	/// `oakrender_set_display_color_processor`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_set_display_color_processor(
		p_or_NULL: OakColorProcessor,
	) -> c_int {
		let Some(manager) = crate::manager::RenderManager::global() else {
			return crate::error::OAKRENDER_E_STATE;
		};
		let mut cacher = manager.get_cacher();
		if let Some(c) = cacher.as_mut() {
			c.set_display_color_processor(if p_or_NULL.is_null() {
				None
			} else {
				Some(p_or_NULL.ctx as u64)
			});
		}
		crate::error::OAKRENDER_OK
	}

	/// `oakrender_cancel_video_tasks`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cancel_video_tasks(wait_for_done: c_int) {
		crate::handle::guard_void(|| {
			if let Some(manager) = crate::manager::RenderManager::global() {
				let cacher = manager.get_cacher();
				if let Some(c) = cacher.as_ref() {
					c.cancel_video_tasks(wait_for_done != 0);
				}
			}
		});
	}

	/// `oakrender_disk_cache_path`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_disk_cache_path(buf: *mut c_char, n: c_int) -> c_int {
		guard_string(|| {
			let path = crate::manager::disk_cache_path();
			Ok(write_string(&path, buf, n))
		})
	}

	/// `oakrender_disk_cache_size`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_disk_cache_size() -> i64 {
		match crate::manager::disk_cache_size() {
			Ok(n) => n,
			Err(_) => crate::error::OAKRENDER_E_FAILED as i64,
		}
	}

	/// `oakrender_disk_cache_clear`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_disk_cache_clear() -> c_int {
		crate::handle::guard(|| crate::manager::disk_cache_clear())
	}

	fn lock(
	) -> std::sync::MutexGuard<'static, std::collections::HashMap<i64, (std::sync::Arc<crate::ticket::TicketArena>, crate::ticket::TicketId)>>
	{
		REQUESTS.lock().unwrap_or_else(|e| e.into_inner())
	}
}

// ============================================================================
// render/cache.h
// ============================================================================

/// `include/render/cache.h` exports: the playback/frame-hash cache family.
pub mod cache {
	use super::*;
	use std::sync::Mutex;

	/// Held external locks (see `oakrender_cache_lock`): cache box address
	/// → address of a boxed `MutexGuard<'static, ()>` (usize so the static
	/// stays `Send + Sync`).
	static HELD_LOCKS: std::sync::LazyLock<Mutex<std::collections::HashMap<usize, usize>>> =
		std::sync::LazyLock::new(|| Mutex::new(std::collections::HashMap::new()));

	/// `oakrender_cache_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_create() -> OakRenderCache {
		crate::handle::guard_handle(|| {
			Ok(crate::handle::make_owned(Some(crate::cache::PlaybackCache::new(
				crate::cache::CacheKind::VideoFrame,
				crate::cache::next_owner_identity(),
			))))
		})
	}

	/// `oakrender_cache_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_free(cache: *mut OakRenderCache) {
		crate::handle::guard_void(|| free_handle(cache));
	}

	/// `oakrender_cache_wrap_borrowed` — the native cache is a C++ object
	/// this crate cannot dereference; the handle boxes `None` (queries on
	/// borrowed caches return `OAKRENDER_E_INVALID` until the C++ interop
	/// layer lands).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_wrap_borrowed(native_cache: *mut c_void) -> OakRenderCache {
		if native_cache.is_null() {
			return CHandle::null();
		}
		let _ = native_cache;
		crate::handle::make_borrowed_owned(None::<crate::cache::PlaybackCache>)
	}

	/// `oakrender_cache_create_for_node`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_create_for_node(
		parent: OakNodeNode,
		kind: c_int,
	) -> OakRenderCache {
		if parent.is_null() {
			return CHandle::null();
		}
		let kind = match kind {
			0 => crate::cache::CacheKind::VideoFrame,
			1 => crate::cache::CacheKind::Thumbnail,
			2 => crate::cache::CacheKind::AudioPlayback,
			3 => crate::cache::CacheKind::AudioWaveform,
			_ => return CHandle::null(),
		};
		crate::handle::make_owned(Some(crate::cache::PlaybackCache::new(
			kind,
			parent.ctx as u64,
		)))
	}

	/// `oakrender_cache_get_uuid` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_get_uuid(
		cache: OakRenderCache,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		guard_string(|| {
			let c = unsafe { crate::handle::get::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_ref())
				.ok_or(crate::error::Error::Invalid)?;
			Ok(write_string(&c.uuid, buf, buf_size))
		})
	}

	/// `oakrender_cache_request` — the context is validated as a viewer
	/// through the node bridge (pending); any non-empty context is
	/// accepted this pass.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_request(
		cache: OakRenderCache,
		context: OakNodeNode,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
	) -> c_int {
		crate::handle::guard(|| {
			if context.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let c = unsafe { crate::handle::get_mut::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_mut())
				.ok_or(crate::error::Error::Invalid)?;
			let range = oakcore_rs::TimeRange::new(
				oakcore_rs::Rational::new(in_num, in_den),
				oakcore_rs::Rational::new(out_num, out_den),
			);
			c.request(range);
			// Drive the auto-cacher (facade re-emits notifications; the
			// cacher is the consuming side).
			if let Some(manager) = crate::manager::RenderManager::global() {
				let mut cacher = manager.get_cacher();
				if let Some(cacher) = cacher.as_mut() {
					cacher.on_cache_request(context.ctx as u64, range);
				}
			}
			Ok(())
		})
	}

	/// `oakrender_cache_load_state`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_load_state(cache: OakRenderCache) -> c_int {
		crate::handle::guard(|| {
			let c = unsafe { crate::handle::get_mut::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_mut())
				.ok_or(crate::error::Error::Invalid)?;
			let dir = c.disk_dir().to_string();
			c.load_state(std::path::Path::new(&dir))
		})
	}

	/// `oakrender_cache_save_state`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_save_state(cache: OakRenderCache) -> c_int {
		crate::handle::guard(|| {
			let c = unsafe { crate::handle::get::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_ref())
				.ok_or(crate::error::Error::Invalid)?;
			c.save_state(std::path::Path::new(c.disk_dir()))
		})
	}

	/// `oakrender_cache_set_saving_enabled`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_set_saving_enabled(
		cache: OakRenderCache,
		enabled: c_int,
	) -> c_int {
		crate::handle::guard(|| {
			let c = unsafe { crate::handle::get_mut::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_mut())
				.ok_or(crate::error::Error::Invalid)?;
			c.set_saving_enabled(enabled != 0);
			Ok(())
		})
	}

	/// `oakrender_cache_set_passthrough`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_set_passthrough(
		cache: OakRenderCache,
		other: OakRenderCache,
	) -> c_int {
		crate::handle::guard(|| {
			// Snapshot the source's data first: `cache == other` (or any
			// aliasing) must not create overlapping borrows.
			let snapshot = {
				let o = unsafe { crate::handle::get::<Option<crate::cache::PlaybackCache>>(&other) }
					.and_then(|o| o.as_ref())
					.ok_or(crate::error::Error::Invalid)?;
				crate::cache::PassthroughSnapshot {
					validated: o.validated_ranges().clone(),
					passthroughs: o.passthroughs().to_vec(),
					timebase: o.timebase,
					uuid: o.uuid.clone(),
				}
			};
			let c = unsafe { crate::handle::get_mut::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_mut())
				.ok_or(crate::error::Error::Invalid)?;
			c.set_passthrough_snapshot(snapshot);
			Ok(())
		})
	}

	/// `oakrender_cache_get_valid_cache_filename` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_get_valid_cache_filename(
		cache: OakRenderCache,
		time_num: i64,
		time_den: i64,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		guard_string(|| {
			let c = unsafe { crate::handle::get::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_ref())
				.ok_or(crate::error::Error::Invalid)?;
			if !c.kind.is_frame_hash() {
				return Err(crate::error::Error::Invalid);
			}
			let time = oakcore_rs::Rational::new(time_num, time_den);
			match c.frame_filename(time) {
				Some(name) => Ok(write_string(&name, buf, buf_size)),
				None => Err(crate::error::Error::NotFound),
			}
		})
	}

	/// `oakrender_cache_get_passthroughs` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_get_passthroughs(
		cache: OakRenderCache,
		ranges: *mut i64,
		max_ranges: c_int,
	) -> c_int {
		let Some(c) = (unsafe { crate::handle::get::<Option<crate::cache::PlaybackCache>>(&cache) })
			.and_then(|o| o.as_ref())
		else {
			return crate::error::OAKRENDER_E_INVALID;
		};
		let all = c.passthroughs();
		let count = all.len() as c_int;
		if !ranges.is_null() && max_ranges > 0 {
			let n = count.min(max_ranges);
			for i in 0..n {
				let (r, _) = &all[i as usize];
				unsafe {
					*ranges.add(i as usize * 4) = r.in_().numerator();
					*ranges.add(i as usize * 4 + 1) = r.in_().denominator();
					*ranges.add(i as usize * 4 + 2) = r.out().numerator();
					*ranges.add(i as usize * 4 + 3) = r.out().denominator();
				}
			}
		}
		count
	}

	/// `oakrender_cache_get_timebase`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_get_timebase(
		cache: OakRenderCache,
		num: *mut c_int,
		den: *mut c_int,
	) -> c_int {
		crate::handle::guard(|| {
			let c = unsafe { crate::handle::get::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_ref())
				.ok_or(crate::error::Error::Invalid)?;
			if !c.kind.is_frame_hash() {
				return Err(crate::error::Error::Invalid);
			}
			let tb = c.timebase.unwrap_or(oakcore_rs::Rational::new(1, 1));
			if !num.is_null() {
				unsafe { *num = tb.numerator() as c_int };
			}
			if !den.is_null() {
				unsafe { *den = tb.denominator() as c_int };
			}
			Ok(())
		})
	}

	/// `oakrender_cache_lock` — acquires and *holds* the cache's external
	/// mutex until the matching unlock (the guard is boxed in a registry
	/// keyed by the cache box; a leaked lock — e.g. the cache destroyed
	/// while locked — is a documented leak, mirroring a C++ deadlock).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_lock(cache: OakRenderCache) {
		if let Some(c) = unsafe { crate::handle::get::<Option<crate::cache::PlaybackCache>>(&cache) }
			.and_then(|o| o.as_ref())
		{
			// The boxed cache lives for 'static; re-derive a 'static
			// reference from the box pointer to build a 'static guard.
			let c_static: &'static crate::cache::PlaybackCache =
				unsafe { &*(c as *const crate::cache::PlaybackCache) };
			let guard: Box<std::sync::MutexGuard<'static, ()>> =
				Box::new(c_static.lock.lock().unwrap_or_else(|e| e.into_inner()));
			let raw = Box::into_raw(guard) as usize;
			HELD_LOCKS
				.lock()
				.unwrap_or_else(|e| e.into_inner())
				.insert(c as *const _ as usize, raw);
		}
	}

	/// `oakrender_cache_unlock` — releases the lock held by
	/// [`oakrender_cache_lock`].
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_unlock(cache: OakRenderCache) {
		if let Some(c) = unsafe { crate::handle::get::<Option<crate::cache::PlaybackCache>>(&cache) } {
			let key = c as *const _ as usize;
			if let Some(raw) = HELD_LOCKS
				.lock()
				.unwrap_or_else(|e| e.into_inner())
				.remove(&key)
			{
				// SAFETY: the raw address came from Box::into_raw of a
				// MutexGuard in oakrender_cache_lock.
				unsafe { drop(Box::from_raw(raw as *mut std::sync::MutexGuard<'static, ()>)) };
			}
		}
	}

	/// `oakrender_cache_set_timebase`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_set_timebase(
		cache: OakRenderCache,
		num: c_int,
		den: c_int,
	) -> c_int {
		crate::handle::guard(|| {
			if num <= 0 || den <= 0 {
				return Err(crate::error::Error::Invalid);
			}
			let c = unsafe { crate::handle::get_mut::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_mut())
				.ok_or(crate::error::Error::Invalid)?;
			c.set_timebase(oakcore_rs::Rational::new(num as i64, den as i64));
			Ok(())
		})
	}

	/// `oakrender_cache_set_uuid`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_set_uuid(
		cache: OakRenderCache,
		uuid: *const c_char,
	) -> c_int {
		crate::handle::guard(|| {
			let uuid = unsafe { cstr(uuid) }.ok_or(crate::error::Error::Invalid)?;
			let c = unsafe { crate::handle::get_mut::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_mut())
				.ok_or(crate::error::Error::Invalid)?;
			c.set_uuid(&uuid);
			Ok(())
		})
	}

	/// `oakrender_cache_invalidate` (timestamp range).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_invalidate(
		cache: OakRenderCache,
		in_ts: i64,
		out_ts: i64,
	) {
		crate::handle::guard_void(|| {
			if let Some(c) = unsafe { crate::handle::get_mut::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_mut())
			{
				let range = ts_range(c, in_ts, out_ts);
				c.invalidate(range);
			}
		});
	}

	/// `oakrender_cache_invalidate_range`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_invalidate_range(
		cache: OakRenderCache,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
	) {
		crate::handle::guard_void(|| {
			if let Some(c) = unsafe { crate::handle::get_mut::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_mut())
			{
				c.invalidate(oakcore_rs::TimeRange::new(
					oakcore_rs::Rational::new(in_num, in_den),
					oakcore_rs::Rational::new(out_num, out_den),
				));
			}
		});
	}

	/// `oakrender_cache_validate` (timestamp range).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_validate(
		cache: OakRenderCache,
		in_ts: i64,
		out_ts: i64,
	) {
		crate::handle::guard_void(|| {
			if let Some(c) = unsafe { crate::handle::get_mut::<Option<crate::cache::PlaybackCache>>(&cache) }
				.and_then(|o| o.as_mut())
			{
				let range = ts_range(c, in_ts, out_ts);
				c.validate(range);
			}
		});
	}

	/// `oakrender_cache_has_validated_ranges`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_has_validated_ranges(
		cache: OakRenderCache,
	) -> c_int {
		match unsafe { crate::handle::get::<Option<crate::cache::PlaybackCache>>(&cache) }
			.and_then(|o| o.as_ref())
		{
			Some(c) if c.has_validated_ranges() => 1,
			_ => 0,
		}
	}

	/// `oakrender_cache_indicator_height`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_indicator_height() -> c_int {
		4
	}

	/// `oakrender_cache_get_invalidated_ranges` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cache_get_invalidated_ranges(
		c: OakRenderCache,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
		ranges: *mut i64,
		max_ranges: c_int,
	) -> c_int {
		guard_string(|| {
			if max_ranges < 0 {
				return Err(crate::error::Error::Invalid);
			}
			let cache = unsafe { crate::handle::get::<crate::cache::PlaybackCache>(&c) }
				.ok_or(crate::error::Error::Invalid)?;
			let within = oakcore_rs::TimeRange::new(
				oakcore_rs::Rational::new(in_num, in_den),
				oakcore_rs::Rational::new(out_num, out_den),
			);
			let list = cache.invalidated_ranges(within);
			let count = list.ranges().len() as c_int;
			if !ranges.is_null() {
				let written = count.min(max_ranges);
				for i in 0..written {
					let r = &list.ranges()[i as usize];
					unsafe {
						*ranges.add(i as usize * 4) = r.in_().numerator();
						*ranges.add(i as usize * 4 + 1) = r.in_().denominator();
						*ranges.add(i as usize * 4 + 2) = r.out().numerator();
						*ranges.add(i as usize * 4 + 3) = r.out().denominator();
					}
				}
			}
			Ok(count)
		})
	}

	/// `oakrender_frame_cache_load`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_frame_cache_load(
		cache: OakRenderCache,
		path: *const c_char,
		uuid: *const c_char,
		ts: i64,
		out_frame: *mut OakCodecFrame,
	) -> c_int {
		crate::handle::guard(|| {
			if cache.is_null() || path.is_null() || uuid.is_null() || out_frame.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let path = unsafe { cstr(path) }.unwrap_or_default();
			let uuid = unsafe { cstr(uuid) }.unwrap_or_default();
			let filename = std::path::Path::new(&path)
				.join(&uuid)
				.join(ts.to_string())
				.to_string_lossy()
				.into_owned();
			if !std::path::Path::new(&filename).exists() {
				return Err(crate::error::Error::NotFound);
			}
			// Payload decode belongs to the oakcodec crate (EXR/JPEG),
			// deferred; without it the file exists but cannot be decoded.
			Err(crate::error::Error::Failed(
				"oakcodec frame decode bridge pending".into(),
			))
		})
	}

	/// `oakrender_frame_cache_save` — computes the C++-parity filename;
	/// the payload write goes through the oakcodec crate (deferred), so
	/// with Rust-owned frames this is a documented no-op.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_frame_cache_save(
		cache: OakRenderCache,
		path: *const c_char,
		uuid: *const c_char,
		frame: OakCodecFrame,
	) {
		crate::handle::guard_void(|| {
			let (Some(c), Some(path), Some(uuid), Some(f)) = (
				unsafe { crate::handle::get::<Option<crate::cache::PlaybackCache>>(&cache) }
					.and_then(|o| o.as_ref()),
				unsafe { cstr(path) },
				unsafe { cstr(uuid) },
				unsafe { crate::handle::get::<crate::texture::Frame>(&frame) },
			) else {
				return;
			};
			let tb = c.timebase.unwrap_or(oakcore_rs::Rational::new(1, 1));
			let filename = crate::cache::PlaybackCache::frame_cache_path(
				&path,
				&uuid,
				f.timestamp,
				tb,
			);
			eprintln!(
				"oakrender: frame_cache_save to {filename} deferred (oakcodec write bridge pending)"
			);
		});
	}

	/// `oakrender_debug_alive_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_debug_alive_count() -> c_int {
		crate::handle::alive_count()
	}

	/// Timestamp range through the cache's timebase (whole seconds when
	/// unset; C++ `ts_to_time`).
	fn ts_range(c: &crate::cache::PlaybackCache, in_ts: i64, out_ts: i64) -> oakcore_rs::TimeRange {
		let to_time = |ts: i64| match c.timebase {
			Some(tb) => tb.timestamp_to_time(ts),
			None => oakcore_rs::Rational::from_double(ts as f64),
		};
		oakcore_rs::TimeRange::new(to_time(in_ts), to_time(out_ts))
	}

	/// Release a caller-owned handle and null it (C++ `free_handle`).
	fn free_handle(h: *mut OakRenderCache) {
		if h.is_null() {
			return;
		}
		unsafe {
			if (*h).is_null() {
				return;
			}
			if let Some(release) = (*h).release {
				release((*h).ctx);
			}
			(*h).ctx = std::ptr::null_mut();
			(*h).addref = None;
			(*h).release = None;
		}
	}
}

// ============================================================================
// render/ticket.h
// ============================================================================

/// `include/render/ticket.h` exports: async render tickets.
pub mod ticket {
	use super::*;

	/// A ticket handle boxes the arena + ticket id, so queries/cancel/wait
	/// work independently of the manager's lifetime.
	struct TicketBox {
		arena: std::sync::Arc<crate::ticket::TicketArena>,
		id: crate::ticket::TicketId,
	}

	/// `oakrender_ticket_render_frame`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_render_frame(
		params: *const OakVideoTicketParams,
		cb: Option<unsafe extern "C" fn(OakRenderTicket, *mut c_void)>,
		userdata: *mut c_void,
	) -> OakRenderTicket {
		crate::handle::guard_handle(|| {
			let params = if params.is_null() {
				return Err(crate::error::Error::Invalid);
			} else {
				unsafe { &*params }
			};
			if params.output_node.is_null() || params.video_params.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let Some(manager) = crate::manager::RenderManager::global() else {
				return Err(crate::error::Error::State);
			};

			let time = oakcore_rs::Rational::new(params.time_num, params.time_den);
			let force_size = if params.force_width > 0 && params.force_height > 0 {
				Some((params.force_width, params.force_height))
			} else {
				None
			};
			let force_format = if params.force_format >= 0 {
				Some(oakcore_pixel_format(params.force_format))
			} else {
				None
			};
			let (cache_dir, cache_id, cache_timebase) =
				if !params.cache.is_null() {
					if let Some(c) = unsafe { crate::handle::get::<crate::cache::PlaybackCache>(&params.cache) } {
						(
							Some(c.disk_dir().to_string()),
							Some(c.uuid.clone()),
							c.timebase,
						)
					} else {
						(None, None, None)
					}
				} else {
					(None, None, None)
				};

			let arena = manager.tickets.clone();
			// Reserve the id up front so the handle is fully stamped before
			// the job is posted: a fast worker could otherwise complete the
			// ticket (firing `cb`) before the id was written into the box,
			// and the callback's ticket would carry the placeholder id.
			let id = arena.next_id();
			let handle = crate::handle::make_owned(TicketBox {
				arena: arena.clone(),
				id,
			});
			let userdata = RawSend(userdata);
			// The completion needs the final handle to pass to `cb`.
			arena.submit_video_with_id(
				id,
				crate::ticket::VideoTicketParams {
					viewer: params.output_node.ctx as u64,
					time,
					force_size,
					force_format,
					cache: if params.cache.is_null() {
						None
					} else {
						Some(params.cache.ctx as u64)
					},
					cache_dir,
					cache_id,
					cache_timebase,
				},
				match cb {
					Some(cb) => Box::new(move |_result| {
						unsafe { cb(handle, userdata.ptr()) };
					}),
					None => Box::new(|_| {}),
				},
			);
			Ok(handle)
		})
	}

	/// `oakrender_ticket_render_audio`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_render_audio(
		output_node: OakNodeNode,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
		params: *const OakAudioParams,
		mode: c_int,
		cb: Option<unsafe extern "C" fn(OakRenderTicket, *mut c_void)>,
		userdata: *mut c_void,
	) -> OakRenderTicket {
		crate::handle::guard_handle(|| {
			if output_node.is_null() || params.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let Some(manager) = crate::manager::RenderManager::global() else {
				return Err(crate::error::Error::State);
			};
			let _ = mode;
			let range = oakcore_rs::TimeRange::new(
				oakcore_rs::Rational::new(in_num, in_den),
				oakcore_rs::Rational::new(out_num, out_den),
			);
			let arena = manager.tickets.clone();
			let id = arena.next_id();
			let handle = crate::handle::make_owned(TicketBox {
				arena: arena.clone(),
				id,
			});
			let userdata = RawSend(userdata);
			arena.submit_audio_with_id(
				id,
				output_node.ctx as u64,
				range,
				match cb {
					Some(cb) => Box::new(move |_result| {
						unsafe { cb(handle, userdata.ptr()) };
					}),
					None => Box::new(|_| {}),
				},
			);
			Ok(handle)
		})
	}

	/// `oakrender_ticket_is_finished`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_is_finished(ticket: OakRenderTicket) -> c_int {
		match unsafe { crate::handle::get::<TicketBox>(&ticket) } {
			Some(b) if b.arena.is_finished(b.id) => 1,
			Some(_) => 0,
			None => crate::error::OAKRENDER_E_INVALID,
		}
	}

	/// `oakrender_ticket_wait`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_wait(ticket: OakRenderTicket) -> c_int {
		crate::handle::guard(|| {
			let b = unsafe { crate::handle::get::<TicketBox>(&ticket) }
				.ok_or(crate::error::Error::Invalid)?;
			b.arena.wait(b.id)?;
			Ok(())
		})
	}

	/// `oakrender_ticket_cancel`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_cancel(ticket: OakRenderTicket) -> c_int {
		crate::handle::guard(|| {
			let b = unsafe { crate::handle::get::<TicketBox>(&ticket) }
				.ok_or(crate::error::Error::Invalid)?;
			b.arena.cancel(b.id);
			Ok(())
		})
	}

	/// `oakrender_ticket_get_type`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_get_type(ticket: OakRenderTicket) -> c_int {
		match unsafe { crate::handle::get::<TicketBox>(&ticket) } {
			Some(b) => b.arena.kind(b.id).unwrap_or(crate::error::OAKRENDER_E_INVALID),
			None => crate::error::OAKRENDER_E_INVALID,
		}
	}

	/// `oakrender_ticket_get_time`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_get_time(
		ticket: OakRenderTicket,
		out_num: *mut i64,
		out_den: *mut i64,
	) -> c_int {
		crate::handle::guard(|| {
			if out_num.is_null() || out_den.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let b = unsafe { crate::handle::get::<TicketBox>(&ticket) }
				.ok_or(crate::error::Error::Invalid)?;
			let time = b.arena.time(b.id).unwrap_or(oakcore_rs::Rational::NULL);
			unsafe {
				*out_num = time.numerator();
				*out_den = time.denominator();
			}
			Ok(())
		})
	}

	/// `oakrender_ticket_get_range`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_get_range(
		ticket: OakRenderTicket,
		in_num: *mut i64,
		in_den: *mut i64,
		out_num: *mut i64,
		out_den: *mut i64,
	) -> c_int {
		crate::handle::guard(|| {
			if in_num.is_null() || in_den.is_null() || out_num.is_null() || out_den.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let b = unsafe { crate::handle::get::<TicketBox>(&ticket) }
				.ok_or(crate::error::Error::Invalid)?;
			let range = b
				.arena
				.range(b.id)
				.unwrap_or(oakcore_rs::TimeRange::new(
					oakcore_rs::Rational::NULL,
					oakcore_rs::Rational::NULL,
				));
			unsafe {
				*in_num = range.in_().numerator();
				*in_den = range.in_().denominator();
				*out_num = range.out().numerator();
				*out_den = range.out().denominator();
			}
			Ok(())
		})
	}

	/// `oakrender_ticket_get_frame`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_get_frame(
		ticket: OakRenderTicket,
		out: *mut OakCodecFrame,
	) -> c_int {
		crate::handle::guard(|| {
			if out.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			unsafe { *out = CHandle::null() };
			let b = unsafe { crate::handle::get::<TicketBox>(&ticket) }
				.ok_or(crate::error::Error::Invalid)?;
			if !b.arena.is_finished(b.id) {
				return Err(crate::error::Error::State);
			}
			let result = b
				.arena
				.result(b.id)
				.ok_or_else(|| crate::error::Error::Failed("ticket has no result".into()))?;
			let texture =
				result.map_err(|_| crate::error::Error::Failed("ticket failed".into()))?;
			let frame = texture.to_frame()?;
			unsafe { *out = frame_handle(frame) };
			if unsafe { (*out).is_null() } {
				return Err(crate::error::Error::NoMem);
			}
			Ok(())
		})
	}

	/// `oakrender_ticket_get_samples` — audio rendering is not implemented
	/// in this pass; always fails explainably.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_get_samples(
		ticket: OakRenderTicket,
		out: *mut *mut c_void,
	) -> c_int {
		crate::handle::guard(|| {
			if out.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			unsafe { *out = std::ptr::null_mut() };
			let _ = unsafe { crate::handle::get::<TicketBox>(&ticket) }
				.ok_or(crate::error::Error::Invalid)?;
			Err(crate::error::Error::Failed(
				"audio samples deferred: audio rendering not implemented".into(),
			))
		})
	}

	/// `oakrender_ticket_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_ticket_free(ticket: *mut OakRenderTicket) {
		crate::handle::guard_void(|| free_handle(ticket));
	}

	/// `oakrender_manager_set_aggressive_gc` (declared in ticket.h).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_manager_set_aggressive_gc(enabled: c_int) -> c_int {
		let Some(manager) = crate::manager::RenderManager::global() else {
			return crate::error::OAKRENDER_E_STATE;
		};
		manager.set_aggressive_gc(enabled != 0);
		crate::error::OAKRENDER_OK
	}

	fn free_handle(h: *mut OakRenderTicket) {
		if h.is_null() {
			return;
		}
		unsafe {
			if (*h).is_null() {
				return;
			}
			if let Some(release) = (*h).release {
				release((*h).ctx);
			}
			(*h).ctx = std::ptr::null_mut();
			(*h).addref = None;
			(*h).release = None;
		}
	}
}

// ============================================================================
// render/renderer.h
// ============================================================================

/// `include/render/renderer.h` exports: the display renderer, texture and
/// frame families, and backend management.
pub mod renderer {
	use super::*;
	use std::sync::Mutex;

	static REQUESTED_BACKEND: std::sync::LazyLock<Mutex<String>> =
		std::sync::LazyLock::new(|| Mutex::new("opengl".to_string()));

	/// `oakrender_display_renderer_create_dynamic`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_renderer_create_dynamic(
		backend_id: *const c_char,
	) -> OakRenderRenderer {
		crate::handle::guard_handle(|| {
			let id = unsafe { cstr(backend_id) }.ok_or(crate::error::Error::Invalid)?;
			if id.is_empty() {
				return Err(crate::error::Error::Invalid);
			}
			let kind = match id.to_ascii_lowercase().as_str() {
				"opengl" => crate::backend::BackendKind::Gl,
				"vulkan" => crate::backend::BackendKind::Vulkan,
				"metal" => crate::backend::BackendKind::Metal,
				"auto" => crate::backend::BackendKind::Auto,
				"cpu" | "dummy" | "multiprocess" => crate::backend::BackendKind::Cpu,
				_ => return Err(crate::error::Error::Invalid),
			};
			Ok(crate::handle::make_owned(crate::backend::DisplayRenderer::new(
				kind,
			)))
		})
	}

	/// `oakrender_display_renderer_create_opengl`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_renderer_create_opengl() -> OakRenderRenderer {
		crate::handle::make_owned(crate::backend::DisplayRenderer::new(
			crate::backend::BackendKind::Gl,
		))
	}

	/// `oakrender_display_renderer_init`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_renderer_init(
		renderer: OakRenderRenderer,
		gl_context: *mut c_void,
	) -> c_int {
		crate::handle::guard(|| {
			let r = unsafe { crate::handle::get_mut::<crate::backend::DisplayRenderer>(&renderer) }
				.ok_or(crate::error::Error::Invalid)?;
			r.init(gl_context)
		})
	}

	/// `oakrender_display_renderer_destroy`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_renderer_destroy(renderer: *mut OakRenderRenderer) {
		crate::handle::guard_void(|| free_handle(renderer));
	}

	/// `oakrender_display_renderer_is_open_gl`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_renderer_is_open_gl(
		renderer: OakRenderRenderer,
	) -> c_int {
		match unsafe { crate::handle::get::<crate::backend::DisplayRenderer>(&renderer) } {
			Some(r) if r.is_open_gl() => 1,
			_ => 0,
		}
	}

	/// `oakrender_display_renderer_is_vulkan`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_renderer_is_vulkan(
		renderer: OakRenderRenderer,
	) -> c_int {
		match unsafe { crate::handle::get::<crate::backend::DisplayRenderer>(&renderer) } {
			Some(r) if r.is_vulkan() => 1,
			_ => 0,
		}
	}

	/// `oakrender_display_texture_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_texture_create(
		renderer: OakRenderRenderer,
		params: *const OakRenderVideoParams,
		pixels: *const c_void,
		linesize: c_int,
	) -> OakRenderTexture {
		crate::handle::guard_handle(|| {
			if params.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let pod = ffi_to_pod(unsafe { &*params });
			let r = unsafe { crate::handle::get::<crate::backend::DisplayRenderer>(&renderer) }
				.ok_or(crate::error::Error::Invalid)?;
			let pixels = if pixels.is_null() {
				None
			} else {
				Some((pixels as *const u8, linesize as usize))
			};
			let tex = r.create_texture(&pod, pixels)?;
			Ok(crate::handle::make_owned(tex))
		})
	}

	/// `oakrender_display_texture_retain`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_texture_retain(
		texture: OakRenderTexture,
	) -> OakRenderTexture {
		if !texture.is_null() {
			if let Some(addref) = texture.addref {
				unsafe { addref(texture.ctx) };
			}
		}
		texture
	}

	/// `oakrender_display_texture_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_texture_free(texture: *mut OakRenderTexture) {
		crate::handle::guard_void(|| free_handle(texture));
	}

	/// `oakrender_display_texture_upload`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_texture_upload(
		texture: OakRenderTexture,
		pixels: *const c_void,
		linesize: c_int,
	) -> c_int {
		crate::handle::guard(|| {
			if pixels.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let t = unsafe { crate::handle::get_mut::<crate::texture::Texture>(&texture) }
				.ok_or(crate::error::Error::Invalid)?;
			let (w, h) = t.size();
			match t {
				crate::texture::Texture::Gpu { token, ctx, .. } => {
					let frame = crate::backend::frame_from_pixels_for_upload(
						(w, h),
						pixels as *const u8,
						linesize as usize,
					)?;
					ctx.upload(*token, &frame)
				}
				crate::texture::Texture::Cpu(frame) => {
					let stride = frame.linesize_bytes();
					if linesize as usize != stride {
						return Err(crate::error::Error::Invalid);
					}
					let n = stride * frame.height as usize;
					frame.data.copy_from_slice(unsafe {
						std::slice::from_raw_parts(pixels as *const u8, n)
					});
					Ok(())
				}
			}
		})
	}

	/// `oakrender_display_texture_download`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_texture_download(
		texture: OakRenderTexture,
		pixels: *mut c_void,
		linesize: c_int,
	) -> c_int {
		crate::handle::guard(|| {
			if pixels.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let t = unsafe { crate::handle::get::<crate::texture::Texture>(&texture) }
				.ok_or(crate::error::Error::Invalid)?;
			let frame = t.to_frame()?;
			let stride = frame.linesize_bytes();
			if linesize as usize != stride {
				return Err(crate::error::Error::Invalid);
			}
			let n = stride * frame.height as usize;
			unsafe {
				std::ptr::copy_nonoverlapping(frame.data.as_ptr(), pixels as *mut u8, n);
			}
			Ok(())
		})
	}

	/// `oakrender_display_texture_get_params`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_texture_get_params(
		texture: OakRenderTexture,
		out: *mut OakRenderVideoParams,
	) -> c_int {
		crate::handle::guard(|| {
			if out.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let t = unsafe { crate::handle::get::<crate::texture::Texture>(&texture) }
				.ok_or(crate::error::Error::Invalid)?;
			let pod = match t {
				crate::texture::Texture::Cpu(f) => f.video_params(),
				crate::texture::Texture::Gpu {
					width,
					height,
					format,
					..
				} => {
					let mut pod = crate::frame::VideoParamsPod::default();
					pod.width = *width;
					pod.height = *height;
					pod.format = *format as i32;
					pod
				}
			};
			unsafe { *out = pod_to_ffi(&pod) };
			Ok(())
		})
	}

	/// `oakrender_display_texture_id`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_texture_id(texture: OakRenderTexture) -> c_int {
		match unsafe { crate::handle::get::<crate::texture::Texture>(&texture) } {
			Some(crate::texture::Texture::Gpu { token, .. }) => *token as c_int,
			_ => 0,
		}
	}

	/// `oakrender_display_texture_is_dummy`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_texture_is_dummy(
		texture: OakRenderTexture,
	) -> c_int {
		match unsafe { crate::handle::get::<crate::texture::Texture>(&texture) } {
			Some(t) if t.is_dummy() => 1,
			_ => 0,
		}
	}

	/// `oakrender_display_texture_get_frame`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_texture_get_frame(
		texture: OakRenderTexture,
		out: *mut OakCodecFrame,
	) -> c_int {
		crate::handle::guard(|| {
			if out.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			unsafe { *out = CHandle::null() };
			let t = unsafe { crate::handle::get::<crate::texture::Texture>(&texture) }
				.ok_or(crate::error::Error::Invalid)?;
			let frame = t.to_frame()?;
			unsafe { *out = frame_handle(frame) };
			if unsafe { (*out).is_null() } {
				return Err(crate::error::Error::NoMem);
			}
			Ok(())
		})
	}

	/// `oakrender_codec_frame_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_create() -> OakCodecFrame {
		crate::handle::make_owned(crate::texture::Frame::new())
	}

	/// `oakrender_codec_frame_retain`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_retain(frame: OakCodecFrame) -> OakCodecFrame {
		if !frame.is_null() {
			if let Some(addref) = frame.addref {
				unsafe { addref(frame.ctx) };
			}
		}
		frame
	}

	/// `oakrender_codec_frame_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_free(frame: *mut OakCodecFrame) {
		crate::handle::guard_void(|| free_handle(frame));
	}

	/// `oakrender_codec_frame_set_video_params`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_set_video_params(
		frame: OakCodecFrame,
		params: *const OakRenderVideoParams,
	) -> c_int {
		crate::handle::guard(|| {
			if params.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let f = unsafe { crate::handle::get_mut::<crate::texture::Frame>(&frame) }
				.ok_or(crate::error::Error::Invalid)?;
			f.set_video_params(ffi_to_pod(unsafe { &*params }));
			Ok(())
		})
	}

	/// `oakrender_codec_frame_get_params`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_get_params(
		frame: OakCodecFrame,
		out: *mut OakRenderVideoParams,
	) -> c_int {
		crate::handle::guard(|| {
			if out.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let f = unsafe { crate::handle::get::<crate::texture::Frame>(&frame) }
				.ok_or(crate::error::Error::Invalid)?;
			unsafe { *out = pod_to_ffi(&f.video_params()) };
			Ok(())
		})
	}

	/// `oakrender_codec_frame_allocate`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_allocate(frame: OakCodecFrame) -> c_int {
		crate::handle::guard(|| {
			let f = unsafe { crate::handle::get_mut::<crate::texture::Frame>(&frame) }
				.ok_or(crate::error::Error::Invalid)?;
			if f.allocate() {
				Ok(())
			} else {
				Err(crate::error::Error::Failed("frame allocation failed".into()))
			}
		})
	}

	/// `oakrender_codec_frame_data`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_data(frame: OakCodecFrame) -> *mut c_void {
		match unsafe { crate::handle::get::<crate::texture::Frame>(&frame) } {
			Some(f) if f.is_allocated() => f.data.as_ptr() as *mut c_void,
			_ => std::ptr::null_mut(),
		}
	}

	/// `oakrender_codec_frame_const_data`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_const_data(frame: OakCodecFrame) -> *const c_void {
		match unsafe { crate::handle::get::<crate::texture::Frame>(&frame) } {
			Some(f) if f.is_allocated() => f.data.as_ptr() as *const c_void,
			_ => std::ptr::null(),
		}
	}

	/// `oakrender_codec_frame_linesize_bytes`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_linesize_bytes(frame: OakCodecFrame) -> c_int {
		match unsafe { crate::handle::get::<crate::texture::Frame>(&frame) } {
			Some(f) => f.linesize_bytes() as c_int,
			None => 0,
		}
	}

	/// `oakrender_codec_frame_is_allocated`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_is_allocated(frame: OakCodecFrame) -> c_int {
		match unsafe { crate::handle::get::<crate::texture::Frame>(&frame) } {
			Some(f) if f.is_allocated() => 1,
			_ => 0,
		}
	}

	/// `oakrender_codec_frame_width`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_width(frame: OakCodecFrame) -> c_int {
		match unsafe { crate::handle::get::<crate::texture::Frame>(&frame) } {
			Some(f) => f.width,
			None => 0,
		}
	}

	/// `oakrender_codec_frame_height`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_height(frame: OakCodecFrame) -> c_int {
		match unsafe { crate::handle::get::<crate::texture::Frame>(&frame) } {
			Some(f) => f.height,
			None => 0,
		}
	}

	/// `oakrender_codec_frame_fb_format` — the Rust frames are never
	/// AVFrame-wrapped; always -1.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_codec_frame_fb_format(_frame: OakCodecFrame) -> c_int {
		-1
	}

	/// `oakrender_display_renderer_blit_color_managed`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_renderer_blit_color_managed(
		renderer: OakRenderRenderer,
		job: *const OakColorTransformJob,
		dst_texture: OakRenderTexture,
		params: *const OakRenderVideoParams,
	) -> c_int {
		crate::handle::guard(|| {
			if job.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let job = unsafe { &*job };
			let r = unsafe { crate::handle::get::<crate::backend::DisplayRenderer>(&renderer) }
				.ok_or(crate::error::Error::Invalid)?;
			let processor_h = if job.processor.is_null() {
				None
			} else {
				Some(ctx_handle(job.processor))
			};
			let src_h = if job.input_texture.is_null() {
				None
			} else {
				Some(ctx_handle(job.input_texture))
			};
			let processor = processor_h
				.as_ref()
				.and_then(|h| unsafe { crate::handle::get::<crate::color::ColorProcessor>(h) });
			let src = src_h
				.as_ref()
				.and_then(|h| unsafe { crate::handle::get::<crate::texture::Texture>(h) });
			if dst_texture.is_null() {
				// C++ would blit to the renderer's current output target;
				// no output target exists in this pass.
				return Err(crate::error::Error::Invalid);
			}
			let dst = unsafe { crate::handle::get_mut::<crate::texture::Texture>(&dst_texture) }
				.ok_or(crate::error::Error::Invalid)?;
			let _ = (params, job); // matrices applied by the (deferred) GPU shader path
			r.blit_color_managed(src, dst, processor)
		})
	}

	/// `oakrender_display_renderer_download_from_texture`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_display_renderer_download_from_texture(
		renderer: OakRenderRenderer,
		texture_id: c_int,
		params: *const OakRenderVideoParams,
		dst_pixels: *mut c_void,
		linesize: c_int,
	) -> c_int {
		crate::handle::guard(|| {
			if params.is_null() || dst_pixels.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let r = unsafe { crate::handle::get::<crate::backend::DisplayRenderer>(&renderer) }
				.ok_or(crate::error::Error::Invalid)?;
			let pod = ffi_to_pod(unsafe { &*params });
			r.download_from_texture(texture_id, &pod, dst_pixels as *mut u8, linesize as usize)
		})
	}

	/// `oakrender_backend_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_backend_count() -> c_int {
		4
	}

	/// `oakrender_backend_id_at` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_backend_id_at(i: c_int, buf: *mut c_char, n: c_int) -> c_int {
		const IDS: [&str; 4] = ["opengl", "vulkan", "multiprocess", "dummy"];
		if i < 0 || i >= IDS.len() as c_int {
			return crate::error::OAKRENDER_E_NOT_FOUND;
		}
		guard_string(|| Ok(write_string(IDS[i as usize], buf, n)))
	}

	/// `oakrender_set_backend`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_set_backend(backend_id: *const c_char) -> c_int {
		crate::handle::guard(|| {
			let id = unsafe { cstr(backend_id) }.ok_or(crate::error::Error::Invalid)?;
			let lower = id.to_ascii_lowercase();
			if !["opengl", "vulkan", "multiprocess", "dummy"].contains(&lower.as_str()) {
				return Err(crate::error::Error::Invalid);
			}
			*REQUESTED_BACKEND.lock().unwrap_or_else(|e| e.into_inner()) = lower;
			Ok(())
		})
	}

	/// `oakrender_current_backend` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_current_backend(buf: *mut c_char, n: c_int) -> c_int {
		let id = match crate::manager::RenderManager::global() {
			Some(m) => m.backend.to_config_string().to_string(),
			None => REQUESTED_BACKEND
				.lock()
				.unwrap_or_else(|e| e.into_inner())
				.clone(),
		};
		guard_string(|| Ok(write_string(&id, buf, n)))
	}

	fn free_handle(h: *mut OakRenderRenderer) {
		if h.is_null() {
			return;
		}
		unsafe {
			if (*h).is_null() {
				return;
			}
			if let Some(release) = (*h).release {
				release((*h).ctx);
			}
			(*h).ctx = std::ptr::null_mut();
			(*h).addref = None;
			(*h).release = None;
		}
	}

}

// ============================================================================
// render/color.h
// ============================================================================

/// `include/render/color.h` exports: color processors, the LUT library,
/// and the ColorManager statics.
pub mod color {
	use super::*;

	/// `oakrender_color_processor_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_processor_create(
		src_space: *const c_char,
		dst_transform: *const c_char,
		direction: c_int,
	) -> OakColorProcessor {
		crate::handle::guard_handle(|| {
			let src = unsafe { cstr(src_space) }.ok_or(crate::error::Error::Invalid)?;
			let dst = unsafe { cstr(dst_transform) }.ok_or(crate::error::Error::Invalid)?;
			if src.is_empty() || dst.is_empty() {
				return Err(crate::error::Error::Invalid);
			}
			let dir = direction_kind(direction)?;
			let processor =
				crate::color::ColorProcessor::create(&src, &dst, dir).ok_or(crate::error::Error::Invalid)?;
			Ok(crate::handle::make_owned(processor))
		})
	}

	/// `oakrender_color_processor_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_processor_free(processor: *mut OakColorProcessor) {
		crate::handle::guard_void(|| free_handle(processor));
	}

	/// `oakrender_color_processor_is_valid`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_processor_is_valid(
		processor: OakColorProcessor,
	) -> c_int {
		match unsafe { crate::handle::get::<crate::color::ColorProcessor>(&processor) } {
			Some(p) if p.is_valid() => 1,
			_ => 0,
		}
	}

	/// `oakrender_color_processor_create_transform` — the node color
	/// manager and transform decode belong to the oaknode/oakcommon
	/// bridges (pending); the input colorspace is resolved against the
	/// default config instead.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_processor_create_transform(
		_manager: OakNodeColorManager,
		input: *const c_char,
		_dest: OakColorTransform,
		direction: c_int,
	) -> OakColorProcessor {
		crate::handle::guard_handle(|| {
			let input = unsafe { cstr(input) }.ok_or(crate::error::Error::Invalid)?;
			if input.is_empty() {
				return Err(crate::error::Error::Invalid);
			}
			let dir = direction_kind(direction)?;
			// Destination transform decode is a node/common bridge feature;
			// until it lands the output transform defaults to the config's
			// reference role (documented deviation).
			let processor = crate::color::ColorProcessor::create(
				&input,
				"reference",
				dir,
			)
			.ok_or(crate::error::Error::Invalid)?;
			Ok(crate::handle::make_owned(processor))
		})
	}

	/// `oakrender_color_processor_create_lut`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_processor_create_lut(
		_manager: OakNodeColorManager,
		path: *const c_char,
		direction: c_int,
	) -> OakColorProcessor {
		crate::handle::guard_handle(|| {
			let path = unsafe { cstr(path) }.ok_or(crate::error::Error::Invalid)?;
			if path.is_empty() {
				return Err(crate::error::Error::Invalid);
			}
			let dir = direction_kind(direction)?;
			let processor =
				crate::color::ColorProcessor::create_lut(&path, dir).ok_or(crate::error::Error::Invalid)?;
			Ok(crate::handle::make_owned(processor))
		})
	}

	/// `oakrender_color_processor_create_grading_primary`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_processor_create_grading_primary(
		_manager: OakNodeColorManager,
		style: c_int,
	) -> OakColorProcessor {
		crate::handle::guard_handle(|| {
			let style = match style {
				0 => crate::color::GradingStyle::Lin,
				1 => crate::color::GradingStyle::Log,
				_ => return Err(crate::error::Error::Invalid),
			};
			let processor = crate::color::ColorProcessor::create_grading_primary(style)
				.ok_or(crate::error::Error::Invalid)?;
			Ok(crate::handle::make_owned(processor))
		})
	}

	/// `oakrender_lut_is_supported_extension`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_lut_is_supported_extension(extension: *const c_char) -> c_int {
		match unsafe { cstr(extension) } {
			Some(ext) if crate::color::is_supported_lut_extension(&ext) => 1,
			_ => 0,
		}
	}

	/// `oakrender_lut_supported_extensions_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_lut_supported_extensions_count() -> c_int {
		crate::color::SUPPORTED_LUT_EXTENSIONS.len() as c_int
	}

	/// `oakrender_lut_supported_extension_at` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_lut_supported_extension_at(
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if index < 0 || index >= crate::color::SUPPORTED_LUT_EXTENSIONS.len() as c_int {
			return crate::error::OAKRENDER_E_NOT_FOUND;
		}
		guard_string(|| {
			Ok(write_string(
				crate::color::SUPPORTED_LUT_EXTENSIONS[index as usize],
				buf,
				buf_size,
			))
		})
	}

	/// `oakrender_color_processor_convert`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_processor_convert(
		processor: OakColorProcessor,
		ir: f64,
		ig: f64,
		ib: f64,
		ia: f64,
		out_r: *mut f64,
		out_g: *mut f64,
		out_b: *mut f64,
		out_a: *mut f64,
	) -> c_int {
		crate::handle::guard(|| {
			if out_r.is_null() || out_g.is_null() || out_b.is_null() || out_a.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let p = unsafe { crate::handle::get::<crate::color::ColorProcessor>(&processor) }
				.ok_or(crate::error::Error::Invalid)?;
			let out = p.convert_color([ir, ig, ib, ia]);
			unsafe {
				*out_r = out[0];
				*out_g = out[1];
				*out_b = out[2];
				*out_a = out[3];
			}
			Ok(())
		})
	}

	/// `oakrender_color_processor_convert_frame`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_processor_convert_frame(
		processor: OakColorProcessor,
		frame: OakCodecFrame,
	) -> c_int {
		crate::handle::guard(|| {
			let p = unsafe { crate::handle::get::<crate::color::ColorProcessor>(&processor) }
				.ok_or(crate::error::Error::Invalid)?;
			let f = unsafe { crate::handle::get_mut::<crate::texture::Frame>(&frame) }
				.ok_or(crate::error::Error::Invalid)?;
			p.convert_frame(f)
		})
	}

	/// `oakrender_color_manager_set_up_default_config`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_manager_set_up_default_config() -> c_int {
		crate::handle::guard(|| {
			crate::color::set_up_default_config()?;
			Ok(())
		})
	}

	/// `oakrender_color_manager_get_config` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_manager_get_config(buf: *mut c_char, n: c_int) -> c_int {
		match crate::color::config_path() {
			Some(path) => guard_string(|| Ok(write_string(&path, buf, n))),
			None => crate::error::OAKRENDER_E_STATE,
		}
	}

	/// `oakrender_color_manager_display_transform` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_color_manager_display_transform(
		display: *const c_char,
		view: *const c_char,
		buf: *mut c_char,
		n: c_int,
	) -> c_int {
		let (Some(display), Some(view)) = (unsafe { cstr(display) }, unsafe { cstr(view) }) else {
			return crate::error::OAKRENDER_E_INVALID;
		};
		if display.is_empty() || view.is_empty() {
			return crate::error::OAKRENDER_E_INVALID;
		}
		match crate::color::display_transform_result(&display, &view) {
			Ok(Some(id)) => guard_string(|| Ok(write_string(&id, buf, n))),
			Ok(None) => crate::error::OAKRENDER_E_NOT_FOUND,
			Err(e) => e.code(),
		}
	}

	fn direction_kind(direction: c_int) -> crate::error::Result<crate::color::Direction> {
		match direction {
			0 => Ok(crate::color::Direction::Normal),
			1 => Ok(crate::color::Direction::Inverse),
			_ => Err(crate::error::Error::Invalid),
		}
	}

	fn free_handle(h: *mut OakColorProcessor) {
		if h.is_null() {
			return;
		}
		unsafe {
			if (*h).is_null() {
				return;
			}
			if let Some(release) = (*h).release {
				release((*h).ctx);
			}
			(*h).ctx = std::ptr::null_mut();
			(*h).addref = None;
			(*h).release = None;
		}
	}
}

// ============================================================================
// render/copier.h
// ============================================================================

/// `include/render/copier.h` exports: the project copier.
pub mod copier {
	use super::*;

	/// `oakrender_project_copier_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_project_copier_create() -> OakRenderProjectCopier {
		crate::handle::make_owned(crate::copier::ProjectCopy::new())
	}

	/// `oakrender_project_copier_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_project_copier_free(copier: *mut OakRenderProjectCopier) {
		crate::handle::guard_void(|| free_handle(copier));
	}

	/// `oakrender_project_copier_set_project`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_project_copier_set_project(
		copier: OakRenderProjectCopier,
		project: OakNodeProject,
	) -> c_int {
		crate::handle::guard(|| {
			if project.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let c = unsafe { crate::handle::get_mut::<crate::copier::ProjectCopy>(&copier) }
				.ok_or(crate::error::Error::Invalid)?;
			c.set_project(project)
		})
	}

	/// `oakrender_project_copier_get_copy` — the node-map query belongs to
	/// the oaknode bridge (pending); returns an empty handle until then.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_project_copier_get_copy(
		copier: OakRenderProjectCopier,
		original: OakNodeNode,
	) -> OakNodeNode {
		let _ = (copier, original);
		CHandle::null()
	}

	/// `oakrender_project_copier_get_copied_project` — borrowed handle
	/// boxing the copy identity (the actual node handle lives in
	/// oaknode; release only frees the box).
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_project_copier_get_copied_project(
		copier: OakRenderProjectCopier,
	) -> OakNodeProject {
		match unsafe { crate::handle::get::<crate::copier::ProjectCopy>(&copier) } {
			Some(c) => match c.copied_project() {
				Some(h) if !h.is_null() => {
					crate::handle::make_borrowed_owned(BorrowedOpaque {
						identity: h.ctx as u64,
					})
				}
				_ => CHandle::null(),
			},
			None => CHandle::null(),
		}
	}

	fn free_handle(h: *mut OakRenderProjectCopier) {
		if h.is_null() {
			return;
		}
		unsafe {
			if (*h).is_null() {
				return;
			}
			if let Some(release) = (*h).release {
				release((*h).ctx);
			}
			(*h).ctx = std::ptr::null_mut();
			(*h).addref = None;
			(*h).release = None;
		}
	}
}

// ============================================================================
// render/cancelatom.h
// ============================================================================

/// `include/render/cancelatom.h` exports: the cancellation primitive.
pub mod cancelatom {
	use super::*;

	/// `oakrender_cancelatom_init`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cancelatom_init() -> OakCancelAtom {
		crate::handle::make_owned(crate::cancelatom::CancelAtom::new())
	}

	/// `oakrender_cancelatom_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cancelatom_free(atom: *mut OakCancelAtom) {
		crate::handle::guard_void(|| free_handle(atom));
	}

	/// `oakrender_cancelatom_cancel`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cancelatom_cancel(atom: OakCancelAtom) -> c_int {
		crate::handle::guard(|| {
			let a = unsafe { crate::handle::get::<crate::cancelatom::CancelAtom>(&atom) }
				.ok_or(crate::error::Error::Invalid)?;
			a.cancel();
			Ok(())
		})
	}

	/// `oakrender_cancelatom_is_cancelled`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cancelatom_is_cancelled(
		atom: OakCancelAtom,
		cancelled: *mut c_int,
	) -> c_int {
		crate::handle::guard(|| {
			if cancelled.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let a = unsafe { crate::handle::get::<crate::cancelatom::CancelAtom>(&atom) }
				.ok_or(crate::error::Error::Invalid)?;
			unsafe { *cancelled = a.is_cancelled() as c_int };
			Ok(())
		})
	}

	/// `oakrender_cancelatom_heard_cancel`.
	#[no_mangle]
	pub unsafe extern "C" fn oakrender_cancelatom_heard_cancel(
		atom: OakCancelAtom,
		heard: *mut c_int,
	) -> c_int {
		crate::handle::guard(|| {
			if heard.is_null() {
				return Err(crate::error::Error::Invalid);
			}
			let a = unsafe { crate::handle::get::<crate::cancelatom::CancelAtom>(&atom) }
				.ok_or(crate::error::Error::Invalid)?;
			unsafe { *heard = a.heard_cancel() as c_int };
			Ok(())
		})
	}

	fn free_handle(h: *mut OakCancelAtom) {
		if h.is_null() {
			return;
		}
		unsafe {
			if (*h).is_null() {
				return;
			}
			if let Some(release) = (*h).release {
				release((*h).ctx);
			}
			(*h).ctx = std::ptr::null_mut();
			(*h).addref = None;
			(*h).release = None;
		}
	}
}

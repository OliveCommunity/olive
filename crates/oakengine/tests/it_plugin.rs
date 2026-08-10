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

//! Integration tests for the plugin family: the facade exports
//! `oakengine_plugin_*` (src/plugin.rs; module C contract
//! `include/plugin/{host,instance,error}.h`), exercised end to end
//! against the REAL `oakplugin` crate — no mocks anywhere.
//!
//! `oakengine_plugin_load_plugins` drives the real OFX host scan
//! (dlopen of real plugin bundles); the family's only destroy surface
//! lives in the backend module (`oakplugin_instance_create/free`), which
//! is verified here against the module's debug alive counter
//! (`oakplugin_debug_alive_count`, the leak assertion for this family).
//! The two provider setters are pure facade state (module 00 analogues of
//! the C++ capi statics): their result IS the return code, asserted below.
//!
//! The host singleton is process-global and only internally locked, so
//! every test that touches it serializes on [`with_host`] (same
//! convention as the module crate's own tests).
//!
//! The end-to-end bundle test needs the minimal test plugin that the
//! oakplugin crate's build.rs compiles (cbits/oak_test_plugin.c). When
//! it is unavailable the test prints SKIP and returns (never fails).

#[path = "common/mod.rs"]
mod common;

use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use oakengine::handle::{CHandle, OakEngineNode};
use oakengine::plugin::{
	oakengine_plugin_load_plugins, oakengine_plugin_node_push_button_clicked,
	oakengine_plugin_set_active_viewer_provider, oakengine_plugin_set_progress_reporter_factory,
};
use oakplugin::ffi::{
	oakplugin_debug_alive_count, oakplugin_host_plugin_count, oakplugin_host_plugin_id_at,
	oakplugin_host_plugin_label, oakplugin_instance_create, oakplugin_instance_free,
};

/// `OAKENGINE_E_INVALID` (src/error.rs).
const E_INVALID: c_int = -1;
/// `OAKENGINE_E_FAILED` (src/error.rs).
const E_FAILED: c_int = -3;
/// `OAKPLUGIN_E_INVALID` (module error.h) — module codes pass through the
/// facade untranslated.
const PLUGIN_E_INVALID: c_int = -90001;

/// Identifier of the minimal OFX test plugin (cbits/oak_test_plugin.c).
const TEST_PLUGIN_ID: &str = "org.oak.test-plugin";
/// Build-system injected bundle path (set by the CMake test runner).
const TEST_PLUGIN_ENV: &str = "OAK_TEST_PLUGIN_DIR";

// ---------------------------------------------------------------------------
// Host serialization + fixtures
// ---------------------------------------------------------------------------

/// Serialize host-touching tests: the oakplugin host is a process
/// singleton without a top-level lock (each internal list is mutexed, but
/// init/scan/shutdown interleavings would make count assertions flaky).
fn with_host(f: impl FnOnce()) {
	static LOCK: Mutex<()> = Mutex::new(());
	let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	f();
}

/// Fresh directory under the system temp dir (removed before creation).
fn fresh_temp_dir(name: &str) -> PathBuf {
	let p = std::env::temp_dir().join(format!("oak-it-plugin-{}-{name}", std::process::id()));
	let _ = std::fs::remove_dir_all(&p);
	std::fs::create_dir_all(&p).expect("create temp dir");
	p
}

/// Current number of live backend objects (host instance registry).
fn alive() -> c_int {
	unsafe { oakplugin_debug_alive_count() }
}

/// Number of plugins discovered by the real host cache.
fn plugin_count() -> c_int {
	unsafe { oakplugin_host_plugin_count() }
}

/// Run the facade scan through the real host, returning its exit code.
fn scan_facade(dir: &Path) -> c_int {
	let cs = CString::new(dir.as_os_str().as_encoded_bytes()).expect("NUL-free path");
	unsafe { oakengine_plugin_load_plugins(cs.as_ptr()) }
}

/// Locate the real test plugin shared library: oakplugin's build.rs
/// compiles cbits/oak_test_plugin.c to `$OUT_DIR/oak_test_plugin.{dylib,so}`
/// inside its own `target/*/build/oakplugin-*/out/` directory.
fn find_test_plugin_lib() -> Option<PathBuf> {
	let ext = if cfg!(target_os = "macos") {
		"dylib"
	} else {
		"so"
	};
	let mut roots: Vec<PathBuf> = Vec::new();
	if let Some(t) = std::env::var_os("CARGO_TARGET_DIR") {
		roots.push(PathBuf::from(t));
	}
	roots.push(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../target"));
	for root in roots {
		for profile in ["debug", "release"] {
			let build = root.join(profile).join("build");
			let Ok(entries) = std::fs::read_dir(&build) else {
				continue;
			};
			let mut hits: Vec<PathBuf> = entries
				.flatten()
				.map(|e| e.path())
				.filter(|p| {
					p.file_name()
						.and_then(|n| n.to_str())
						.is_some_and(|n| n.starts_with("oakplugin-"))
				})
				.collect();
			hits.sort();
			for dir in hits {
				let lib = dir.join("out").join(format!("oak_test_plugin.{ext}"));
				if lib.is_file() {
					return Some(lib);
				}
			}
		}
	}
	None
}

/// Copy a test-plugin shared library into a `.bundle` directory layout
/// under `root` (Contents/MacOS or Contents/Linux-x86-64, matching the
/// host's `find_binary_in_bundle`).
fn install_bundle(lib: &Path, root: &Path) -> Option<()> {
	let platform = if cfg!(target_os = "macos") {
		"MacOS"
	} else {
		"Linux-x86-64"
	};
	let bin_dir = root
		.join("oak-test-plugin.ofx.bundle")
		.join("Contents")
		.join(platform);
	std::fs::create_dir_all(&bin_dir).ok()?;
	std::fs::copy(lib, bin_dir.join("plugin")).ok()?;
	Some(())
}

/// A scan directory containing the real test plugin bundle, if available:
/// either the parent of the build-system-injected bundle
/// (`OAK_TEST_PLUGIN_DIR`) or a bundle assembled in the temp dir from the
/// shared library oakplugin's build.rs produced. `None` → caller skips.
fn test_plugin_scan_dir() -> Option<PathBuf> {
	if let Some(bundle) = std::env::var_os(TEST_PLUGIN_ENV) {
		return PathBuf::from(bundle).parent().map(|p| p.to_path_buf());
	}
	let lib = find_test_plugin_lib()?;
	let root = std::env::temp_dir().join(format!("oak-it-plugin-bundle-{}", std::process::id()));
	if !root.join("oak-test-plugin.ofx.bundle").exists() {
		install_bundle(&lib, &root)?;
	}
	Some(root)
}

/// A scan directory that is guaranteed to have been scanned by NO other
/// test in this binary (fresh per call), so "scan registers the plugin"
/// assertions are deterministic. The env-injected bundle has no fresh
/// variant and falls back to the shared one.
fn fresh_test_plugin_scan_dir(tag: &str) -> Option<PathBuf> {
	if std::env::var_os(TEST_PLUGIN_ENV).is_some() {
		return test_plugin_scan_dir();
	}
	let lib = find_test_plugin_lib()?;
	let root = fresh_temp_dir(&format!("bundle-{tag}"));
	install_bundle(&lib, &root)?;
	Some(root)
}

/// All plugin identifiers currently registered in the real host cache
/// (two-stage getter round trip per index).
fn plugin_ids() -> Vec<String> {
	let count = plugin_count();
	let mut out = Vec::new();
	for i in 0..count {
		let len = unsafe { oakplugin_host_plugin_id_at(i, std::ptr::null_mut(), 0) };
		if len <= 0 {
			continue;
		}
		let mut buf = vec![0u8; len as usize];
		let rc = unsafe { oakplugin_host_plugin_id_at(i, buf.as_mut_ptr() as *mut c_char, len) };
		if rc == 0 {
			out.push(
				unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) }
					.to_string_lossy()
					.into_owned(),
			);
		}
	}
	out
}

// ---------------------------------------------------------------------------
// oakengine_plugin_set_active_viewer_provider
// ---------------------------------------------------------------------------

/// Legal matrix for the active-viewer provider: fn Some/None × userdata
/// ptr/NULL all register (or clear) with `OAKENGINE_OK`.
#[test]
fn active_viewer_provider_register_clear_matrix() {
	common::force_link();

	unsafe extern "C" fn viewer(_userdata: *mut c_void) -> *mut OakEngineNode {
		std::ptr::null_mut()
	}
	let mut userdata = 42i32;
	let ud = &mut userdata as *mut i32 as *mut c_void;

	// Some(fn) + userdata.
	assert_eq!(
		oakengine_plugin_set_active_viewer_provider(Some(viewer), ud),
		0
	);
	// Some(fn) + NULL userdata (userdata is opaque, NULL is legal).
	assert_eq!(
		oakengine_plugin_set_active_viewer_provider(Some(viewer), std::ptr::null_mut()),
		0
	);
	// None clears (NULL fn), userdata is then ignored but still legal.
	assert_eq!(oakengine_plugin_set_active_viewer_provider(None, ud), 0);
	assert_eq!(
		oakengine_plugin_set_active_viewer_provider(None, std::ptr::null_mut()),
		0
	);
	// Register again and clear, so the process-global state ends neutral.
	assert_eq!(
		oakengine_plugin_set_active_viewer_provider(Some(viewer), std::ptr::null_mut()),
		0
	);
	assert_eq!(
		oakengine_plugin_set_active_viewer_provider(None, std::ptr::null_mut()),
		0
	);
}

// ---------------------------------------------------------------------------
// oakengine_plugin_set_progress_reporter_factory
// ---------------------------------------------------------------------------

/// Legal matrix for the progress-reporter factory: full factory, clear
/// (all NULL), partial registrations and NULL userdata all return
/// `OAKENGINE_OK`.
#[test]
fn progress_reporter_factory_register_clear_matrix() {
	common::force_link();

	unsafe extern "C" fn create(
		_m: *const c_char,
		_t: *const c_char,
		_u: *mut c_void,
	) -> *mut c_void {
		std::ptr::null_mut()
	}
	unsafe extern "C" fn destroy(_r: *mut c_void, _u: *mut c_void) {}
	unsafe extern "C" fn is_cancelled(_r: *mut c_void, _u: *mut c_void) -> c_int {
		0
	}
	unsafe extern "C" fn set_progress(_r: *mut c_void, _p: f64, _u: *mut c_void) {}
	let mut userdata = 7i64;
	let ud = &mut userdata as *mut i64 as *mut c_void;

	// Full factory + userdata.
	assert_eq!(
		oakengine_plugin_set_progress_reporter_factory(
			Some(create),
			Some(destroy),
			Some(is_cancelled),
			Some(set_progress),
			ud,
		),
		0
	);
	// All-NULL clears (NULL userdata too).
	assert_eq!(
		oakengine_plugin_set_progress_reporter_factory(
			None,
			None,
			None,
			None,
			std::ptr::null_mut()
		),
		0
	);
	// Partial registrations are accepted (the facade stores what it gets).
	assert_eq!(
		oakengine_plugin_set_progress_reporter_factory(
			Some(create),
			None,
			None,
			None,
			std::ptr::null_mut()
		),
		0
	);
	assert_eq!(
		oakengine_plugin_set_progress_reporter_factory(None, Some(destroy), None, None, ud),
		0
	);
	assert_eq!(
		oakengine_plugin_set_progress_reporter_factory(
			None,
			None,
			Some(is_cancelled),
			None,
			std::ptr::null_mut()
		),
		0
	);
	assert_eq!(
		oakengine_plugin_set_progress_reporter_factory(None, None, None, Some(set_progress), ud),
		0
	);
	// Back to cleared.
	assert_eq!(
		oakengine_plugin_set_progress_reporter_factory(
			None,
			None,
			None,
			None,
			std::ptr::null_mut()
		),
		0
	);
}

// ---------------------------------------------------------------------------
// oakengine_plugin_load_plugins
// ---------------------------------------------------------------------------

/// NULL path → facade `E_INVALID`, never a crash.
#[test]
fn load_plugins_null_path() {
	with_host(|| {
		common::force_link();
		let before = alive();
		assert_eq!(
			unsafe { oakengine_plugin_load_plugins(std::ptr::null()) },
			E_INVALID
		);
		assert_eq!(
			alive(),
			before,
			"failed scan must not touch the host registry"
		);
	});
}

/// Empty string path is a documented no-op (canonicalize fails, not a
/// directory → host returns OK; the C++ host never errors on a missing
/// path).
#[test]
fn load_plugins_empty_string_path() {
	with_host(|| {
		common::force_link();
		let cs = CString::new("").unwrap();
		let before = alive();
		assert_eq!(unsafe { oakengine_plugin_load_plugins(cs.as_ptr()) }, 0);
		assert_eq!(alive(), before);
	});
}

/// Nonexistent path: silently skipped with OK (olivehost.cpp add_plugin_path
/// semantics), no crash.
#[test]
fn load_plugins_nonexistent_path() {
	with_host(|| {
		common::force_link();
		let dir =
			std::env::temp_dir().join(format!("oak-it-plugin-missing-{}", std::process::id()));
		let _ = std::fs::remove_dir_all(&dir); // guarantee absence
		let before = alive();
		assert_eq!(scan_facade(&dir), 0);
		assert_eq!(alive(), before);
	});
}

/// A path that points at a regular file (not a directory) is a documented
/// no-op returning OK.
#[test]
fn load_plugins_path_is_a_file() {
	with_host(|| {
		common::force_link();
		let dir = fresh_temp_dir("file-scan");
		let file = dir.join("not-a-dir");
		std::fs::write(&file, b"hi").unwrap();
		let before = alive();
		assert_eq!(scan_facade(&file), 0);
		assert_eq!(alive(), before);
	});
}

/// Empty directory scans cleanly (OK), changes nothing in the plugin
/// cache, and a repeat scan of the same path is deduplicated (also OK) —
/// the meaningful size=0 / index-range ground state.
#[test]
fn load_plugins_empty_dir_and_dedup() {
	with_host(|| {
		common::force_link();
		let dir = fresh_temp_dir("empty");
		let count_before = plugin_count();
		let before = alive();
		assert_eq!(scan_facade(&dir), 0);
		assert_eq!(
			plugin_count(),
			count_before,
			"empty dir must not register plugins"
		);
		// Same path again → dedup no-op, still OK.
		assert_eq!(scan_facade(&dir), 0);
		assert_eq!(plugin_count(), count_before);
		assert_eq!(alive(), before);
	});
}

/// Unicode path (non-ASCII directory name) scans cleanly.
#[test]
fn load_plugins_unicode_path() {
	with_host(|| {
		common::force_link();
		let dir = fresh_temp_dir("unicode");
		let unicode = dir.join("插件-目录-β");
		std::fs::create_dir_all(&unicode).unwrap();
		let before = alive();
		assert_eq!(scan_facade(&unicode), 0);
		assert_eq!(alive(), before);
	});
}

/// Non-UTF-8 path bytes: the module rejects them with `OAKPLUGIN_E_INVALID`
/// which passes through the facade untranslated (-90001), never a crash.
#[test]
fn load_plugins_non_utf8_path() {
	with_host(|| {
		common::force_link();
		let cs = CString::new(&b"/tmp/oak-it-plugin-\xff\xfe"[..]).unwrap();
		let before = alive();
		assert_eq!(
			unsafe { oakengine_plugin_load_plugins(cs.as_ptr()) },
			PLUGIN_E_INVALID
		);
		assert_eq!(alive(), before);
	});
}

/// End-to-end legal path: `oakengine_plugin_load_plugins` against a real
/// directory containing the real OFX test plugin bundle registers the
/// plugin in the real host cache (dlopen + setHost + load + describe all
/// run). Verified through the module's own introspection, plus a repeat
/// scan (dedup) and the alive counter.
///
/// Skips when the test plugin was not built (see module docs).
#[test]
fn load_plugins_real_bundle_end_to_end() {
	with_host(|| {
		common::force_link();
		// A fresh directory so "scan registers the plugin" is deterministic;
		// the env-injected mode falls back to the shared dir.
		let Some(dir) = fresh_test_plugin_scan_dir("e2e") else {
			println!("SKIP: test plugin bundle unavailable (oakplugin build.rs output missing)");
			return;
		};
		let before = alive();
		let ids_before = plugin_ids();
		let had_test_plugin = ids_before.iter().any(|id| id == TEST_PLUGIN_ID);
		let count_before = plugin_count();

		// The facade scan returns OK and — unless the test plugin ids were
		// already registered by an earlier scan in this binary (the host
		// dedups globally by identifier, so a second scan of the same
		// bundle binary is a no-op) — registers the test plugin.
		assert_eq!(scan_facade(&dir), 0);
		let ids_after = plugin_ids();
		assert!(
			ids_after.iter().any(|id| id == TEST_PLUGIN_ID),
			"test plugin id must be discoverable after scan (ids: {ids_after:?})"
		);
		if !had_test_plugin {
			assert!(
				plugin_count() > count_before,
				"a first scan of the real bundle must register the plugin ({} -> {})",
				count_before,
				plugin_count()
			);
		}

		// Label lookup for a known id resolves (phase 1: the id itself).
		let len = unsafe {
			oakplugin_host_plugin_label(c"org.oak.test-plugin".as_ptr(), std::ptr::null_mut(), 0)
		};
		assert!(len > 0);
		let mut lbuf = vec![0u8; len as usize];
		assert_eq!(
			unsafe {
				oakplugin_host_plugin_label(
					c"org.oak.test-plugin".as_ptr(),
					lbuf.as_mut_ptr() as *mut c_char,
					len,
				)
			},
			0
		);

		// Repeat scan of the same path is deduplicated: still OK, cache
		// unchanged, alive counter untouched.
		let count_after_first = plugin_count();
		assert_eq!(scan_facade(&dir), 0);
		assert_eq!(plugin_count(), count_after_first);
		assert_eq!(alive(), before, "scan must not leak host instances");
	});
}

// ---------------------------------------------------------------------------
// oakengine_plugin_node_push_button_clicked
// ---------------------------------------------------------------------------

/// Documented stub: the oakplugin crate has no push-button API (the OFX
/// button-param trigger is C++-only), so every input combination returns
/// `OAKENGINE_E_FAILED` and never reads its arguments.
#[test]
fn push_button_clicked_documented_stub() {
	common::force_link();
	// NULL node + NULL button.
	assert_eq!(
		unsafe {
			oakengine_plugin_node_push_button_clicked(std::ptr::null_mut(), std::ptr::null())
		},
		E_FAILED
	);
	// NULL node + button id.
	assert_eq!(
		unsafe { oakengine_plugin_node_push_button_clicked(std::ptr::null_mut(), c"btn".as_ptr()) },
		E_FAILED
	);
	// Non-NULL node (empty handle) + NULL button.
	let mut node = OakEngineNode {
		handle: CHandle::null(),
	};
	assert_eq!(
		unsafe { oakengine_plugin_node_push_button_clicked(&mut node, std::ptr::null()) },
		E_FAILED
	);
	// Non-NULL node + button id.
	assert_eq!(
		unsafe { oakengine_plugin_node_push_button_clicked(&mut node, c"btn".as_ptr()) },
		E_FAILED
	);
}

// ---------------------------------------------------------------------------
// Backend destroy contract (the family's only free surface)
// ---------------------------------------------------------------------------

/// The facade plugin family exports no free/destroy function; its only
/// destroy surface is the backend `oakplugin_instance_free`. Contracts
/// verified against the real host: free(NULL)/free(empty)/double-free are
/// no-ops, unknown ids yield an empty handle (documented), and a real
/// instance create → free round trip restores the module's alive counter
/// to baseline (the leak assertion for this family).
#[test]
fn backend_instance_free_contracts() {
	with_host(|| {
		common::force_link();
		let base = alive();

		// free(NULL) and free(empty handle) are no-ops.
		unsafe { oakplugin_instance_free(std::ptr::null_mut()) };
		let mut empty = CHandle::null();
		unsafe { oakplugin_instance_free(&mut empty) };
		assert!(empty.is_null(), "free must leave the handle emptied");

		// Unknown plugin id → empty handle, not a crash.
		let mut h = unsafe { oakplugin_instance_create(c"org.oak.not-a-plugin".as_ptr()) };
		assert!(h.is_null());
		unsafe { oakplugin_instance_free(&mut h) };
		assert_eq!(alive(), base);

		// Real plugin: create +1, free back to baseline, double-free safe.
		let Some(dir) = test_plugin_scan_dir() else {
			println!("SKIP: test plugin bundle unavailable (oakplugin build.rs output missing)");
			return;
		};
		assert_eq!(scan_facade(&dir), 0);
		let mut inst = unsafe { oakplugin_instance_create(c"org.oak.test-plugin".as_ptr()) };
		assert!(
			!inst.is_null(),
			"scanned test plugin must create an instance"
		);
		assert_eq!(alive(), base + 1, "one live instance must be registered");
		unsafe { oakplugin_instance_free(&mut inst) };
		assert!(inst.is_null());
		assert_eq!(
			alive(),
			base,
			"free must return the alive counter to baseline"
		);
		// Double free of the already-emptied handle is a no-op.
		unsafe { oakplugin_instance_free(&mut inst) };
		assert_eq!(alive(), base);
	});
}

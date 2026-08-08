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

//! C ABI surface, one `#[no_mangle]` export per function declared in
//! `include/common/*.h`.
//!
//! Every handle typedef in these headers (`OakColorTransform`,
//! `OakVideoParams`, `OakSubtitleParams`, `OakXmlReader`, `OakXmlWriter`,
//! `OakFileFunctions`, `OakCurrent`, `OakCommandLineParser`,
//! `OakCommandLineOption`, `OakCommandLinePositionalArgument`,
//! `OakOCIOUtils`, `OakOIIOUtils`) has the identical layout
//! `{void *ctx; void (*addref)(void*); void (*release)(void*);
//! uint32_t abi_version;}`, so [`crate::handle::CHandle`] is used for all
//! of them (same precedent as the oakundo/oaknode/oakcodec crates).
//!
//! # Coverage inventory
//!
//! | Header | Domain module | Notes |
//! |---|---|---|
//! | error.h | `crate::error` | constants only, no functions |
//! | handle.h | `crate::handle` | documentation only |
//! | miscutils.h | `crate::miscutils` | decibel/lerp |
//! | loopmode.h | `crate::miscutils` | enum only |
//! | dropworkflowbehavior.h | `crate::miscutils` | enum + name/is_valid |
//! | power.h | `crate::miscutils` | power helpers |
//! | current.h | `crate::miscutils` | `Current` singleton |
//! | config.h | `crate::configstore` | singleton, no handle |
//! | debug.h | `crate::debug` | `oakcommon_log` is C-variadic, omitted |
//! | ffmpegutils.h | `crate::ffmpegutils` | |
//! | colortransform.h | `crate::colortransform` | native fns are C++-only |
//! | commandlineparser.h | `crate::commandlineparser` | |
//! | videoparams.h | `crate::videoparams` | native fns are C++-only |
//! | subtitleparams.h | `crate::subtitleparams` | native fn is C++-only |
//! | xmlutils.h | `crate::xmlutils` | native fns are C++-only |
//! | filefunctions.h | `crate::filefunctions` | |
//! | ocioutils.h | `crate::ocioutils` | |
//! | oiioutils.h | `crate::oiioutils` | |
//! | qtutils.h | `crate::qtutils` | |

use std::ffi::c_char;
use std::ffi::c_void;

use std::ffi::CStr;

use crate::handle::{guard_void, CHandle};
use crate::miscutils::DestroyFn;

/// NUL-terminated C string as `&str` (null / invalid UTF-8 -> empty string).
fn cstr<'a>(p: *const c_char) -> &'a str {
	if p.is_null() {
		return "";
	}
	unsafe { CStr::from_ptr(p) }.to_str().unwrap_or("")
}

/// Standard two-stage getter copy: copy only when the buffer is large
/// enough (never truncates); always return the required size incl. NUL.
fn copy_string(value: &str, buf: *mut c_char, buf_size: i32) -> i32 {
	let required = (value.len() + 1) as i32;
	if !buf.is_null() && buf_size >= required {
		unsafe {
			std::ptr::copy_nonoverlapping(value.as_ptr() as *const c_char, buf, value.len());
			*buf.add(value.len()) = 0;
		}
	}
	required
}

/// Truncating copy for `commandlineoption_get_setting` / positionals.
fn copy_setting(value: &str, buf: *mut c_char, buf_size: i32) -> i32 {
	let required = (value.len() + 1) as i32;
	if !buf.is_null() && buf_size > 0 {
		let n = value.len().min(buf_size as usize - 1);
		unsafe {
			std::ptr::copy_nonoverlapping(value.as_ptr() as *const c_char, buf, n);
			*buf.add(n) = 0;
		}
	}
	required
}

/// Whether `(buf, buf_size)` is a valid two-stage getter output.
fn is_valid_string_out(buf: *mut c_char, buf_size: i32) -> bool {
	buf_size >= 0 && (buf_size == 0 || !buf.is_null())
}

/// group pointer -> `Option<&str>` (null -> `None`).
fn group_opt<'a>(group: *const c_char) -> Option<&'a str> {
	if group.is_null() {
		None
	} else {
		Some(cstr(group))
	}
}

/// Release a `CHandle` in place: call its release callback, then write
/// null back.
fn free_handle(p: *mut CHandle) {
	if p.is_null() {
		return;
	}
	guard_void(|| unsafe {
		if let Some(h) = p.as_ref() {
			if let Some(rel) = h.release {
				rel(h.ctx);
			}
		}
		p.write(CHandle::null());
	})
}

/// Handle-returning exports return an empty handle; use [`CHandle::null`].
pub mod colortransform {
	//! `include/common/colortransform.h`.
	//!
	//! The C++-only `oakcommon_colortransform_init_from_native` /
	//! `oakcommon_colortransform_get_native` deal with
	//! `olive::ColorTransform` and are served by the C++ adapter layer.

	use super::{cstr, CHandle};
	use crate::colortransform::ColorTransform;
	use crate::handle::{guard_handle, make_owned};

	/// Create a transform targeting an output color space.
	#[no_mangle]
	pub extern "C" fn oakcommon_colortransform_init_output(output: *const super::c_char) -> CHandle {
		if output.is_null() {
			return CHandle::null();
		}
		guard_handle(|| Ok(make_owned(ColorTransform::new_output(cstr(output)))))
	}

	/// Create a transform targeting a display/view/look.
	#[no_mangle]
	pub extern "C" fn oakcommon_colortransform_init_display(
		display: *const super::c_char,
		view: *const super::c_char,
		look: *const super::c_char,
	) -> CHandle {
		if display.is_null() || view.is_null() || look.is_null() {
			return CHandle::null();
		}
		guard_handle(|| {
			Ok(make_owned(ColorTransform::new_display(
				cstr(display),
				cstr(view),
				cstr(look),
			)))
		})
	}

	/// Release one reference (NULL/empty no-op).
	#[no_mangle]
	pub extern "C" fn oakcommon_colortransform_free(transform: *mut CHandle) {
		super::free_handle(transform);
	}

	/// 1 when the transform targets display/view/look, 0 for output
	/// transforms, negative on empty handle.
	#[no_mangle]
	pub extern "C" fn oakcommon_colortransform_is_display(transform: CHandle) -> i32 {
		match unsafe { crate::handle::get::<ColorTransform>(&transform) } {
			Some(ct) => {
				if ct.is_display() {
					1
				} else {
					0
				}
			}
			None => crate::error::OAKCOMMON_E_INVALID,
		}
	}

	/// Display name (two-stage string).
	#[no_mangle]
	pub extern "C" fn oakcommon_colortransform_get_display(
		transform: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		match unsafe { crate::handle::get::<ColorTransform>(&transform) } {
			Some(ct) => super::copy_string(ct.display(), buf, buf_size),
			None => crate::error::OAKCOMMON_E_INVALID,
		}
	}

	/// Output color space (two-stage string).
	#[no_mangle]
	pub extern "C" fn oakcommon_colortransform_get_output(
		transform: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		match unsafe { crate::handle::get::<ColorTransform>(&transform) } {
			Some(ct) => super::copy_string(ct.output(), buf, buf_size),
			None => crate::error::OAKCOMMON_E_INVALID,
		}
	}

	/// View name (two-stage string).
	#[no_mangle]
	pub extern "C" fn oakcommon_colortransform_get_view(
		transform: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		match unsafe { crate::handle::get::<ColorTransform>(&transform) } {
			Some(ct) => super::copy_string(ct.view(), buf, buf_size),
			None => crate::error::OAKCOMMON_E_INVALID,
		}
	}

	/// Look name (two-stage string).
	#[no_mangle]
	pub extern "C" fn oakcommon_colortransform_get_look(
		transform: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		match unsafe { crate::handle::get::<ColorTransform>(&transform) } {
			Some(ct) => super::copy_string(ct.look(), buf, buf_size),
			None => crate::error::OAKCOMMON_E_INVALID,
		}
	}
}

/// Command-line parser and its option/argument handles.
pub mod commandlineparser {
	//! `include/common/commandlineparser.h`.

	use std::ffi::{CStr, CString};

	use super::{copy_setting, cstr, is_valid_string_out, CHandle};
	use crate::commandlineparser::{CommandLineOption, CommandLineParser, CommandLinePositionalArgument};
	use crate::error::{Error, OAKCOMMON_E_INVALID, OAKCOMMON_OK};
	use crate::handle::{get, guard, guard_handle, make_borrowed, make_owned};

	/// Create a new parser.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineparser_init() -> CHandle {
		guard_handle(|| Ok(make_owned(CommandLineParser::new())))
	}

	/// Release one reference to a parser.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineparser_free(parser: *mut CHandle) {
		super::free_handle(parser);
	}

	/// Set the application name/version shown by `print_help`.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineparser_set_app_info(
		parser: CHandle,
		name: *const super::c_char,
		version: *const super::c_char,
	) -> i32 {
		if parser.is_null() || name.is_null() || version.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(parser.ctx as *mut CommandLineParser) };
			p.set_app_info(cstr(name), cstr(version));
			Ok(())
		})
	}

	/// Register a named option; writes the option handle to `out_option`.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineparser_add_option(
		parser: CHandle,
		names: *const *const super::c_char,
		name_count: i32,
		description: *const super::c_char,
		takes_arg: i32,
		arg_placeholder: *const super::c_char,
		hidden: i32,
		out_option: *mut CHandle,
	) -> i32 {
		if parser.is_null() || names.is_null() || name_count <= 0 || description.is_null() || arg_placeholder.is_null() || out_option.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(parser.ctx as *mut CommandLineParser) };
			let mut ns: Vec<CString> = Vec::with_capacity(name_count as usize);
			for i in 0..name_count {
				let s = unsafe { CStr::from_ptr(*names.add(i as usize)) };
				let s = s.to_str().map_err(|_| Error::Invalid)?;
				ns.push(CString::new(s).map_err(|_| Error::Invalid)?);
			}
			p.add_option(&ns, cstr(description), takes_arg != 0, cstr(arg_placeholder), hidden != 0)?;
			let idx = p.option_count() - 1;
			let opt = p.option(idx).ok_or(Error::State)?;
			unsafe {
				*out_option = make_borrowed(opt as *const CommandLineOption as *mut CommandLineOption);
			}
			Ok(())
		})
	}

	/// Register a positional argument; writes the argument handle to
	/// `out_argument`.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineparser_add_positional_argument(
		parser: CHandle,
		name: *const super::c_char,
		description: *const super::c_char,
		required: i32,
		out_argument: *mut CHandle,
	) -> i32 {
		if parser.is_null() || name.is_null() || description.is_null() || out_argument.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(parser.ctx as *mut CommandLineParser) };
			p.add_positional_argument(cstr(name), cstr(description), required != 0)?;
			let idx = p.positional_count() - 1;
			let arg = p.positional(idx).ok_or(Error::State)?;
			unsafe {
				*out_argument =
					make_borrowed(arg as *const CommandLinePositionalArgument as *mut CommandLinePositionalArgument);
			}
			Ok(())
		})
	}

	/// Parse an argv-style argument list.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineparser_process(
		parser: CHandle,
		argv: *const *const super::c_char,
		argc: i32,
	) -> i32 {
		if parser.is_null() || argv.is_null() || argc < 0 {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(parser.ctx as *mut CommandLineParser) };
			let mut av: Vec<CString> = Vec::with_capacity(argc as usize);
			for i in 0..argc {
				av.push(unsafe { CStr::from_ptr(*argv.add(i as usize)) }.to_owned());
			}
			p.process(&av)?;
			Ok(())
		})
	}

	/// Print usage/help text.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineparser_print_help(
		parser: CHandle,
		filename: *const super::c_char,
	) -> i32 {
		if parser.is_null() || filename.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(parser.ctx as *const CommandLineParser) };
			p.print_help(cstr(filename))?;
			Ok(())
		})
	}

	/// Whether the option was present on the command line.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineoption_is_set(
		option: CHandle,
		is_set: *mut bool,
	) -> i32 {
		if option.is_null() || is_set.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let o = match unsafe { get::<CommandLineOption>(&option) } {
			Some(o) => o,
			None => return OAKCOMMON_E_INVALID,
		};
		unsafe { *is_set = o.is_set(); }
		OAKCOMMON_OK
	}

	/// Release one reference to an option.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineoption_free(option: *mut CHandle) {
		super::free_handle(option);
	}

	/// The option's argument value (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineoption_get_setting(
		option: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if option.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let s = match unsafe { get::<CommandLineOption>(&option) } {
			Some(o) => o.get_setting().unwrap_or_default(),
			None => return OAKCOMMON_E_INVALID,
		};
		copy_setting(s, buf, buf_size)
	}

	/// Set the option's argument value.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlineoption_set_setting(
		option: CHandle,
		value: *const super::c_char,
	) -> i32 {
		if option.is_null() || value.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let o = unsafe { &mut *(option.ctx as *mut CommandLineOption) };
		o.set_setting(cstr(value))
			.map(|_| OAKCOMMON_OK)
			.unwrap_or_else(|e| e.code())
	}

	/// The positional argument's value (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlinepositionalargument_get_setting(
		argument: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if argument.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let s = match unsafe { get::<CommandLinePositionalArgument>(&argument) } {
			Some(a) => a.get_setting().unwrap_or_default(),
			None => return OAKCOMMON_E_INVALID,
		};
		copy_setting(s, buf, buf_size)
	}

	/// Set the positional argument's value.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlinepositionalargument_set_setting(
		argument: CHandle,
		value: *const super::c_char,
	) -> i32 {
		if argument.is_null() || value.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let a = unsafe { &mut *(argument.ctx as *mut CommandLinePositionalArgument) };
		a.set_setting(cstr(value))
			.map(|_| OAKCOMMON_OK)
			.unwrap_or_else(|e| e.code())
	}

	/// Release one reference to a positional argument.
	#[no_mangle]
	pub extern "C" fn oakcommon_commandlinepositionalargument_free(argument: *mut CHandle) {
		super::free_handle(argument);
	}
}

/// Process-wide configuration store (no handle).
pub mod config {
	//! `include/common/config.h`.

	use super::{copy_string, cstr, group_opt, is_valid_string_out};
	use crate::configstore::{ConfigStore, EntryType, ErrorHandler};
	use crate::error::{OAKCOMMON_E_INVALID, OAKCOMMON_OK};

	fn store() -> &'static ConfigStore {
		ConfigStore::instance()
	}

	/// Load `config.ini` (a missing file is not an error).
	#[no_mangle]
	pub extern "C" fn oakcommon_config_load() -> i32 {
		match store().load() {
			Ok(()) => OAKCOMMON_OK,
			Err(e) => e.code(),
		}
	}

	/// Persist the current store.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_save() -> i32 {
		match store().save() {
			Ok(()) => OAKCOMMON_OK,
			Err(e) => e.code(),
		}
	}

	/// Reset to compiled-in defaults.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_reset_defaults() -> i32 {
		match store().reset_defaults() {
			Ok(()) => OAKCOMMON_OK,
			Err(e) => e.code(),
		}
	}

	/// Set a string entry.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_set(
		group: *const super::c_char,
		key: *const super::c_char,
		value_utf8: *const super::c_char,
	) {
		if key.is_null() || value_utf8.is_null() {
			return;
		}
		store().set(group_opt(group), cstr(key), cstr(value_utf8));
	}

	/// Read an entry as a string (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_config_get(
		group: *const super::c_char,
		key: *const super::c_char,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if key.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		match store().get(group_opt(group), cstr(key)) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Read an INT entry; `fallback` when absent or a different type.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_get_int(
		group: *const super::c_char,
		key: *const super::c_char,
		fallback: i32,
	) -> i32 {
		if key.is_null() {
			return fallback;
		}
		store().get_int(group_opt(group), cstr(key), fallback)
	}

	/// Read a DOUBLE entry; `fallback` when absent or a different type.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_get_double(
		group: *const super::c_char,
		key: *const super::c_char,
		fallback: f64,
	) -> f64 {
		if key.is_null() {
			return fallback;
		}
		store().get_double(group_opt(group), cstr(key), fallback)
	}

	/// Set an INT entry.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_set_int(
		group: *const super::c_char,
		key: *const super::c_char,
		v: i32,
	) {
		if key.is_null() {
			return;
		}
		store().set_int(group_opt(group), cstr(key), v);
	}

	/// Read an INT entry as int64; `fallback` when absent or a different
	/// type.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_get_int64(
		group: *const super::c_char,
		key: *const super::c_char,
		fallback: i64,
	) -> i64 {
		if key.is_null() {
			return fallback;
		}
		store().get_int64(group_opt(group), cstr(key), fallback)
	}

	/// Set an INT entry as int64.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_set_int64(
		group: *const super::c_char,
		key: *const super::c_char,
		v: i64,
	) {
		if key.is_null() {
			return;
		}
		store().set_int64(group_opt(group), cstr(key), v);
	}

	/// Read a BOOL entry as 0/1; `fallback` when absent or a different type.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_get_bool(
		group: *const super::c_char,
		key: *const super::c_char,
		fallback: i32,
	) -> i32 {
		if key.is_null() {
			return fallback;
		}
		store().get_bool(group_opt(group), cstr(key), fallback)
	}

	/// Set a BOOL entry.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_set_bool(
		group: *const super::c_char,
		key: *const super::c_char,
		v: i32,
	) {
		if key.is_null() {
			return;
		}
		store().set_bool(group_opt(group), cstr(key), v);
	}

	/// Set a DOUBLE entry.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_set_double(
		group: *const super::c_char,
		key: *const super::c_char,
		v: f64,
	) {
		if key.is_null() {
			return;
		}
		store().set_double(group_opt(group), cstr(key), v);
	}

	/// Entry type code of a key (`OAKCOMMON_CONFIG_ENTRY_*`), or a negative
	/// `OAKCOMMON_E_*` error code.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_entry_type(
		group: *const super::c_char,
		key: *const super::c_char,
	) -> i32 {
		if key.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		match store().entry_type(group_opt(group), cstr(key)) {
			Ok(EntryType::None) => 0,
			Ok(EntryType::String) => 1,
			Ok(EntryType::Int) => 2,
			Ok(EntryType::Double) => 3,
			Ok(EntryType::Bool) => 4,
			Err(e) => e.code(),
		}
	}

	/// Register (or clear, with a null handler) the error handler.
	#[no_mangle]
	pub extern "C" fn oakcommon_config_set_error_handler(
		handler: ErrorHandler,
		userdata: *mut super::c_void,
	) -> i32 {
		match store().set_error_handler(handler, userdata) {
			Ok(()) => OAKCOMMON_OK,
			Err(e) => e.code(),
		}
	}
}

/// Debug/logging helpers.
pub mod debug {
	//! `include/common/debug.h`.
	//!
	//! `oakcommon_log` is a C variadic (`(int level, const char *fmt, ...)`)
	//! which Rust cannot export; it is served by the C++ adapter layer and
	//! is intentionally absent here. Domain callers use
	//! `crate::debug::log(level, msg)` instead.

	use super::{copy_string, cstr};
	use crate::debug::{log_get_level, log_raw, log_set_level, Level};
	use crate::error::{OAKCOMMON_E_INVALID, OAKCOMMON_OK};

	/// Log a message at a given level.
	#[no_mangle]
	pub extern "C" fn oakcommon_debug_log(level: i32, msg: *const super::c_char) -> i32 {
		if msg.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		match log_raw(level, cstr(msg)) {
			Ok(()) => OAKCOMMON_OK,
			Err(e) => e.code(),
		}
	}

	/// Human-readable name for a debug level (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_debug_level_name(
		level: i32,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		let name = Level::from_code(level).map(Level::name).unwrap_or("UNKNOWN");
		copy_string(name, buf, buf_size)
	}

	/// Set the minimum logging level.
	#[no_mangle]
	pub extern "C" fn oakcommon_log_set_level(level: i32) -> i32 {
		match Level::from_code(level) {
			Some(l) => {
				log_set_level(l);
				OAKCOMMON_OK
			}
			None => OAKCOMMON_E_INVALID,
		}
	}

	/// Get the current minimum logging level.
	#[no_mangle]
	pub extern "C" fn oakcommon_log_get_level(out_level: *mut i32) -> i32 {
		if out_level.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		unsafe { *out_level = log_get_level() as i32; }
		OAKCOMMON_OK
	}
}

/// Placeholder module for constants-only headers.
pub mod error {
	//! `include/common/error.h` exposes no functions; the `OAKCOMMON_OK` /
	//! `OAKCOMMON_E_*` constants live in `crate::error`.
}

/// FFmpeg format helpers.
pub mod ffmpegutils {
	//! `include/common/ffmpegutils.h`. `OAKCOMMON_RGB_CHANNEL_COUNT` /
	//! `OAKCOMMON_RGBA_CHANNEL_COUNT` live in `crate::ffmpegutils`.

	use crate::error::{OAKCOMMON_E_INVALID, OAKCOMMON_OK};

	/// Write a value into a non-null out-param, returning `OK`.
	fn fill(value: i32, out: *mut i32) -> i32 {
		if out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		unsafe { *out = value; }
		OAKCOMMON_OK
	}

	/// Find the nearest compatible bridge pixel format.
	#[no_mangle]
	pub extern "C" fn oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(
		pix_fmt: i32,
		maximum_pix_fmt: i32,
		out: *mut i32,
	) -> i32 {
		fill(crate::ffmpegutils::get_compatible_bridge_pixel_format(pix_fmt, maximum_pix_fmt), out)
	}

	/// Find the nearest compatible pixel format.
	#[no_mangle]
	pub extern "C" fn oakcommon_ffmpegutils_get_compatible_pixel_format(
		pix_fmt: i32,
		out: *mut i32,
	) -> i32 {
		fill(crate::ffmpegutils::get_compatible_pixel_format(pix_fmt), out)
	}

	/// Map a native format + channel count to an FFmpeg pixel format.
	#[no_mangle]
	pub extern "C" fn oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
		pix_fmt: i32,
		channel_count: i32,
		out: *mut i32,
	) -> i32 {
		fill(crate::ffmpegutils::get_ffmpeg_pixel_format(pix_fmt, channel_count), out)
	}

	/// Map an FFmpeg sample format to a native sample format.
	#[no_mangle]
	pub extern "C" fn oakcommon_ffmpegutils_get_native_sample_format(
		smp_fmt: i32,
		out: *mut i32,
	) -> i32 {
		fill(crate::ffmpegutils::get_native_sample_format(smp_fmt), out)
	}

	/// Map a native sample format to an FFmpeg sample format.
	#[no_mangle]
	pub extern "C" fn oakcommon_ffmpegutils_get_ffmpeg_sample_format(
		smp_fmt: i32,
		out: *mut i32,
	) -> i32 {
		fill(crate::ffmpegutils::get_ffmpeg_sample_format(smp_fmt), out)
	}

	/// Convert a JPEG-range pixel format to its regular-space equivalent.
	#[no_mangle]
	pub extern "C" fn oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(
		pix_fmt: i32,
		out: *mut i32,
	) -> i32 {
		fill(crate::ffmpegutils::convert_jpeg_space_to_regular_space(pix_fmt), out)
	}
}

/// Filesystem helper handle.
pub mod filefunctions {
	//! `include/common/filefunctions.h`.

	use super::{copy_string, cstr, is_valid_string_out, CHandle};
	use crate::error::{OAKCOMMON_E_FAILED, OAKCOMMON_E_INVALID, OAKCOMMON_OK};
	use crate::filefunctions::FileFunctions;
	use crate::handle::{get, make_owned};

	/// Create a FileFunctions handle.
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_init() -> CHandle {
		make_owned(FileFunctions::new())
	}

	/// Release one reference.
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_free(self_: *mut CHandle) {
		super::free_handle(self_);
	}

	/// Unique identifier for a file (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_get_unique_file_identifier(
		self_: CHandle,
		filename: *const super::c_char,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if self_.is_null() || filename.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		match ff.get_unique_file_identifier(cstr(filename)) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Configuration directory (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_get_configuration_location(
		self_: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if self_.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		match ff.get_configuration_location() {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Application directory (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_get_application_path(
		self_: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if self_.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		match ff.get_application_path() {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Temp file directory (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_get_temp_file_path(
		self_: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if self_.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		match ff.get_temp_file_path() {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Auto-recovery root (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_get_auto_recovery_root(
		self_: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if self_.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		match ff.get_auto_recovery_root() {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Whether a directory can be copied without overwriting anything.
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_can_copy_directory_without_overwriting(
		self_: CHandle,
		source: *const super::c_char,
		dest: *const super::c_char,
		out: *mut i32,
	) -> i32 {
		if self_.is_null() || source.is_null() || dest.is_null() || out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		unsafe { *out = ff.can_copy_directory_without_overwriting(cstr(source), cstr(dest)) as i32; }
		OAKCOMMON_OK
	}

	/// Recursively copy a directory.
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_copy_directory(
		self_: CHandle,
		source: *const super::c_char,
		dest: *const super::c_char,
		overwrite: i32,
	) -> i32 {
		if self_.is_null() || source.is_null() || dest.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		match ff.copy_directory(cstr(source), cstr(dest), overwrite != 0) {
			Ok(()) => OAKCOMMON_OK,
			Err(_) => OAKCOMMON_E_FAILED,
		}
	}

	/// Whether `dir` is a valid directory, optionally creating it.
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_directory_is_valid(
		self_: CHandle,
		dir: *const super::c_char,
		try_to_create_if_not_exists: i32,
		out: *mut i32,
	) -> i32 {
		if self_.is_null() || dir.is_null() || out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		unsafe { *out = ff.directory_is_valid(cstr(dir), try_to_create_if_not_exists != 0) as i32; }
		OAKCOMMON_OK
	}

	/// Ensure a filename carries the given extension (two-stage string
	/// getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_ensure_filename_extension(
		self_: CHandle,
		filename: *const super::c_char,
		extension: *const super::c_char,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if self_.is_null() || filename.is_null() || extension.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		match ff.ensure_filename_extension(cstr(filename), cstr(extension)) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Read a file's contents as a string (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_read_file_as_string(
		self_: CHandle,
		filename: *const super::c_char,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if self_.is_null() || filename.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		match ff.read_file_as_string(cstr(filename)) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Safe temporary filename derived from `original` (two-stage string
	/// getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_get_safe_temporary_filename(
		self_: CHandle,
		original: *const super::c_char,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if self_.is_null() || original.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		match ff.get_safe_temporary_filename(cstr(original)) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Rename a file, allowing overwrite.
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_rename_file_allow_overwrite(
		self_: CHandle,
		from: *const super::c_char,
		to: *const super::c_char,
		out: *mut i32,
	) -> i32 {
		if self_.is_null() || from.is_null() || to.is_null() || out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		unsafe { *out = ff.rename_file_allow_overwrite(cstr(from), cstr(to)) as i32; }
		OAKCOMMON_OK
	}

	/// Add the platform's executable extension to a name (two-stage string
	/// getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_filefunctions_get_formatted_executable_for_platform(
		self_: CHandle,
		unformatted: *const super::c_char,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if self_.is_null() || unformatted.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let ff = match unsafe { get::<FileFunctions>(&self_) } {
			Some(f) => f,
			None => return OAKCOMMON_E_INVALID,
		};
		match ff.get_formatted_executable_for_platform(cstr(unformatted)) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}
}

/// The `Current` singleton (no-op addref/release).
pub mod current {
	//! `include/common/current.h`.

	use super::CHandle;
	use crate::error::{OAKCOMMON_E_INVALID, OAKCOMMON_OK};
	use crate::handle::{guard, guard_handle, make_owned};
	use crate::miscutils::Current;

	/// Fetch the process-wide singleton handle.
	#[no_mangle]
	pub extern "C" fn oakcommon_current_instance() -> CHandle {
		guard_handle(|| Ok(make_owned::<()>(())))
	}

	/// Release a reference (no-op for the singleton).
	#[no_mangle]
	pub extern "C" fn oakcommon_current_free(self_: *mut CHandle) {
		super::free_handle(self_);
	}

	/// Store a pointer in the video-params slot.
	#[no_mangle]
	pub extern "C" fn oakcommon_current_set_video_params(
		self_: CHandle,
		obj: *mut super::c_void,
		destroy: super::DestroyFn,
	) -> i32 {
		if self_.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			Current::instance().set_video_params(obj, destroy)?;
			Ok(())
		})
	}

	/// Store a pointer in the audio-params slot.
	#[no_mangle]
	pub extern "C" fn oakcommon_current_set_audio_params(
		self_: CHandle,
		obj: *mut super::c_void,
		destroy: super::DestroyFn,
	) -> i32 {
		if self_.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			Current::instance().set_audio_params(obj, destroy)?;
			Ok(())
		})
	}

	/// Store a pointer in the plugin-host slot.
	#[no_mangle]
	pub extern "C" fn oakcommon_current_set_plugin_host(
		self_: CHandle,
		obj: *mut super::c_void,
		destroy: super::DestroyFn,
	) -> i32 {
		if self_.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			Current::instance().set_plugin_host(obj, destroy)?;
			Ok(())
		})
	}

	/// Store a pointer in the plugin-cache slot.
	#[no_mangle]
	pub extern "C" fn oakcommon_current_set_plugin_cache(
		self_: CHandle,
		obj: *mut super::c_void,
		destroy: super::DestroyFn,
	) -> i32 {
		if self_.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			Current::instance().set_plugin_cache(obj, destroy)?;
			Ok(())
		})
	}

	/// Fetch the video-params slot.
	#[no_mangle]
	pub extern "C" fn oakcommon_current_get_video_params(
		self_: CHandle,
		out: *mut *mut super::c_void,
	) -> i32 {
		if self_.is_null() || out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let v = Current::instance().get_video_params()?;
			unsafe { *out = v; }
			Ok(())
		})
	}

	/// Fetch the audio-params slot.
	#[no_mangle]
	pub extern "C" fn oakcommon_current_get_audio_params(
		self_: CHandle,
		out: *mut *mut super::c_void,
	) -> i32 {
		if self_.is_null() || out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let v = Current::instance().get_audio_params()?;
			unsafe { *out = v; }
			Ok(())
		})
	}

	/// Fetch the plugin-host slot.
	#[no_mangle]
	pub extern "C" fn oakcommon_current_get_plugin_host(
		self_: CHandle,
		out: *mut *mut super::c_void,
	) -> i32 {
		if self_.is_null() || out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let v = Current::instance().get_plugin_host()?;
			unsafe { *out = v; }
			Ok(())
		})
	}

	/// Fetch the plugin-cache slot.
	#[no_mangle]
	pub extern "C" fn oakcommon_current_get_plugin_cache(
		self_: CHandle,
		out: *mut *mut super::c_void,
	) -> i32 {
		if self_.is_null() || out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let v = Current::instance().get_plugin_cache()?;
			unsafe { *out = v; }
			Ok(())
		})
	}

	/// Whether the session is interactive.
	#[no_mangle]
	pub extern "C" fn oakcommon_current_is_interactive(
		self_: CHandle,
		out: *mut i32,
	) -> i32 {
		if self_.is_null() || out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		unsafe { *out = Current::instance().is_interactive().unwrap_or(false) as i32; }
		OAKCOMMON_OK
	}
}

/// Decibel/lerp, loop-mode, drop-workflow, and power helpers (no handle).
pub mod misc {
	//! Folds `miscutils.h`, `loopmode.h`, `dropworkflowbehavior.h`, and
	//! `power.h`; the enums and `OAKCOMMON_DECIBEL_MINIMUM` live in
	//! `crate::miscutils`.

	use super::copy_string;
	use crate::error::{OAKCOMMON_E_INVALID, OAKCOMMON_OK};
	use crate::miscutils as m;

	/// Write a double into a non-null out-param, returning `OK`.
	fn f64_out(value: f64, out: *mut f64) -> i32 {
		if out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		unsafe { *out = value; }
		OAKCOMMON_OK
	}

	/// Linear amplitude -> decibels.
	#[no_mangle]
	pub extern "C" fn oakcommon_decibel_from_linear(
		linear: f64,
		out_db: *mut f64,
	) -> i32 {
		f64_out(m::decibel_from_linear(linear).unwrap_or(0.0), out_db)
	}

	/// Decibels -> linear amplitude.
	#[no_mangle]
	pub extern "C" fn oakcommon_decibel_to_linear(
		db: f64,
		out_linear: *mut f64,
	) -> i32 {
		f64_out(m::decibel_to_linear(db).unwrap_or(0.0), out_linear)
	}

	/// Logarithmic slider position -> decibels.
	#[no_mangle]
	pub extern "C" fn oakcommon_decibel_from_logarithmic(
		logarithmic: f64,
		out_db: *mut f64,
	) -> i32 {
		f64_out(m::decibel_from_logarithmic(logarithmic).unwrap_or(0.0), out_db)
	}

	/// Decibels -> logarithmic slider position.
	#[no_mangle]
	pub extern "C" fn oakcommon_decibel_to_logarithmic(
		db: f64,
		out_logarithmic: *mut f64,
	) -> i32 {
		f64_out(m::decibel_to_logarithmic(db).unwrap_or(0.0), out_logarithmic)
	}

	/// Linear amplitude -> logarithmic position.
	#[no_mangle]
	pub extern "C" fn oakcommon_decibel_linear_to_logarithmic(
		linear: f64,
		out_logarithmic: *mut f64,
	) -> i32 {
		f64_out(m::decibel_linear_to_logarithmic(linear).unwrap_or(0.0), out_logarithmic)
	}

	/// Logarithmic position -> linear amplitude.
	#[no_mangle]
	pub extern "C" fn oakcommon_decibel_logarithmic_to_linear(
		logarithmic: f64,
		out_linear: *mut f64,
	) -> i32 {
		f64_out(m::decibel_logarithmic_to_linear(logarithmic).unwrap_or(0.0), out_linear)
	}

	/// Linearly interpolate between `a` and `b` with `t`.
	#[no_mangle]
	pub extern "C" fn oakcommon_lerp(a: f64, b: f64, t: f64, out_value: *mut f64) -> i32 {
		f64_out(m::lerp(a, b, t).unwrap_or(0.0), out_value)
	}

	/// Whether `value` is a valid drop-workflow behavior.
	#[no_mangle]
	pub extern "C" fn oakcommon_drop_workflow_behavior_is_valid(value: i32) -> i32 {
		if m::DropWorkflowBehavior::is_valid(value) {
			1
		} else {
			0
		}
	}

	/// Name for a drop-workflow behavior (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_drop_workflow_behavior_name(
		value: i32,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		copy_string(m::DropWorkflowBehavior::name(value), buf, buf_size)
	}

	/// Round `value` up to the next power of two.
	#[no_mangle]
	pub extern "C" fn oakcommon_power_ceil_to_power_of_2(value: u32, out: *mut u32) -> i32 {
		if out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		unsafe { *out = m::power_ceil_to_power_of_2(value).unwrap_or(0); }
		OAKCOMMON_OK
	}

	/// Round `value` down to the nearest power of two.
	#[no_mangle]
	pub extern "C" fn oakcommon_power_floor_to_power_of_2(value: u32, out: *mut u32) -> i32 {
		if out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		unsafe { *out = m::power_floor_to_power_of_2(value).unwrap_or(0); }
		OAKCOMMON_OK
	}
}

/// Placeholder module for the enum-only loop-mode header.
pub mod loopmode {
	//! `include/common/loopmode.h` exposes only `OakLoopMode`; the enum
	//! lives in `crate::miscutils::LoopMode`.
}

/// Placeholder module for the documentation-only handle header.
pub mod handle {
	//! `include/common/handle.h` is documentation only; the scaffolding
	//! lives in `crate::handle`.
}

/// Placeholder module for the constants-only error header.
pub mod error_abi {
	//! `include/common/error.h` exposes no functions; the constants live in
	//! `crate::error`.
}

/// Placeholder module for the enum-only drop-workflow header.
pub mod dropworkflowbehavior {
	//! `include/common/dropworkflowbehavior.h`'s functions are exported in
	//! `crate::ffi::misc`; the enum lives in
	//! `crate::miscutils::DropWorkflowBehavior`.
}

/// Placeholder module for the enum-only power header.
pub mod power {
	//! `include/common/power.h`'s functions are exported in
	//! `crate::ffi::misc`; there is no domain enum.
}

/// OCIO utility queries.
pub mod ocioutils {
	//! `include/common/ocioutils.h`.

	use super::CHandle;
	use crate::error::OAKCOMMON_E_INVALID;
	use crate::handle::{guard, make_owned};
	use crate::ocioutils::{OCIOUtils, PixelFormat};

	/// Create an OCIOUtils handle.
	#[no_mangle]
	pub extern "C" fn oakcommon_ocioutils_init() -> CHandle {
		make_owned(OCIOUtils::new())
	}

	/// Release one reference.
	#[no_mangle]
	pub extern "C" fn oakcommon_ocioutils_free(self_: *mut CHandle) {
		super::free_handle(self_);
	}

	/// Map a native pixel format to an OCIO bit depth code.
	#[no_mangle]
	pub extern "C" fn oakcommon_ocioutils_get_ocio_bit_depth_from_pixel_format(
		self_: CHandle,
		pixel_format: i32,
		out: *mut i32,
	) -> i32 {
		if self_.is_null() || out.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		if pixel_format < -1 || pixel_format >= 5 {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let v = OCIOUtils::new().get_ocio_bit_depth_from_pixel_format(PixelFormat::from_code(pixel_format))?;
			unsafe { *out = v; }
			Ok(())
		})
	}
}

/// OIIO utility queries.
pub mod oiioutils {
	//! `include/common/oiioutils.h`.

	use super::CHandle;
	use crate::error::OAKCOMMON_E_INVALID;
	use crate::handle::{guard, make_owned};
	use crate::ocioutils::PixelFormat;
	use crate::oiioutils::OIIOUtils;

	/// Create an OIIOUtils handle.
	#[no_mangle]
	pub extern "C" fn oakcommon_oiioutils_init() -> CHandle {
		make_owned(OIIOUtils::new())
	}

	/// Release one reference.
	#[no_mangle]
	pub extern "C" fn oakcommon_oiioutils_free(self_: *mut CHandle) {
		super::free_handle(self_);
	}

	/// Map a native pixel format to an OIIO base type.
	#[no_mangle]
	pub extern "C" fn oakcommon_oiioutils_get_oiio_base_type_from_format(
		self_: CHandle,
		pixel_format: i32,
		out_base_type: *mut i32,
	) -> i32 {
		if self_.is_null() || out_base_type.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		if pixel_format < -1 || pixel_format >= 5 {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let v = OIIOUtils::new().get_oiio_base_type_from_format(PixelFormat::from_code(pixel_format))?;
			unsafe { *out_base_type = v; }
			Ok(())
		})
	}

	/// Map an OIIO base type to a native pixel format.
	#[no_mangle]
	pub extern "C" fn oakcommon_oiioutils_get_format_from_oiio_basetype(
		self_: CHandle,
		base_type: i32,
		out_pixel_format: *mut i32,
	) -> i32 {
		if self_.is_null() || out_pixel_format.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		if base_type < 0 {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let v = OIIOUtils::new().get_format_from_oiio_basetype(base_type)?;
			unsafe { *out_pixel_format = v.code(); }
			Ok(())
		})
	}

	/// Convert a pixel-aspect-ratio value to a rational pair.
	#[no_mangle]
	pub extern "C" fn oakcommon_oiioutils_get_pixel_aspect_ratio(
		self_: CHandle,
		pixel_aspect_ratio: f64,
		out_numerator: *mut i32,
		out_denominator: *mut i32,
	) -> i32 {
		if self_.is_null() || out_numerator.is_null() || out_denominator.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let (n, d) = OIIOUtils::new().get_pixel_aspect_ratio(pixel_aspect_ratio)?;
			unsafe {
				*out_numerator = n;
				*out_denominator = d;
			}
			Ok(())
		})
	}
}

/// Qt boundary helpers.
pub mod qtutils {
	//! `include/common/qtutils.h`.

	use super::cstr;
	use crate::error::OAKCOMMON_E_INVALID;
	use crate::handle::guard;
	use crate::qtutils;

	/// Convert an opaque pointer to its numeric representation.
	#[no_mangle]
	pub extern "C" fn oakcommon_qtutils_ptr_to_value(
		ptr: *mut super::c_void,
		out_value: *mut u64,
	) -> i32 {
		if out_value.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let v = qtutils::ptr_to_value(ptr)?;
			unsafe { *out_value = v; }
			Ok(())
		})
	}

	/// Convert a numeric representation back to an opaque pointer.
	#[no_mangle]
	pub extern "C" fn oakcommon_qtutils_value_to_ptr(
		value: u64,
		out_ptr: *mut *mut super::c_void,
	) -> i32 {
		if out_ptr.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = qtutils::value_to_ptr(value)?;
			unsafe { *out_ptr = p; }
			Ok(())
		})
	}

	/// File creation time as seconds since the Unix epoch.
	#[no_mangle]
	pub extern "C" fn oakcommon_qtutils_get_creation_date(
		path: *const super::c_char,
		out_secs: *mut i64,
	) -> i32 {
		if path.is_null() || out_secs.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let secs = qtutils::get_creation_date(cstr(path))?;
			unsafe { *out_secs = secs; }
			Ok(())
		})
	}
}

/// Subtitle parameter set.
pub mod subtitleparams {
	//! `include/common/subtitleparams.h`.
	//!
	//! The C++-only `oakcommon_subtitleparams_init_from_native` deals with
	//! `olive::SubtitleParams` and is served by the C++ adapter layer.

	use super::{copy_string, cstr, is_valid_string_out, CHandle};
	use crate::error::OAKCOMMON_E_INVALID;
	use crate::handle::{get, guard, guard_handle, make_owned};
	use crate::subtitleparams::SubtitleParams;

	/// Create an empty subtitle parameter set.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_init() -> CHandle {
		guard_handle(|| Ok(make_owned(SubtitleParams::new())))
	}

	/// Release one reference.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_free(params: *mut CHandle) {
		super::free_handle(params);
	}

	/// Get the stream index.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_get_stream_index(
		params: CHandle,
		index: *mut i32,
	) -> i32 {
		if params.is_null() || index.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const SubtitleParams) };
			unsafe { *index = p.stream_index(); }
			Ok(())
		})
	}

	/// Set the stream index.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_set_stream_index(
		params: CHandle,
		index: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut SubtitleParams) };
			p.set_stream_index(index);
			Ok(())
		})
	}

	/// Get whether the stream is enabled.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_get_enabled(
		params: CHandle,
		enabled: *mut i32,
	) -> i32 {
		if params.is_null() || enabled.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const SubtitleParams) };
			unsafe { *enabled = p.enabled() as i32; }
			Ok(())
		})
	}

	/// Set whether the stream is enabled.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_set_enabled(
		params: CHandle,
		enabled: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut SubtitleParams) };
			p.set_enabled(enabled != 0);
			Ok(())
		})
	}

	/// Whether the set contains at least one subtitle.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_is_valid(
		params: CHandle,
		is_valid: *mut i32,
	) -> i32 {
		if params.is_null() || is_valid.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const SubtitleParams) };
			unsafe { *is_valid = p.is_valid() as i32; }
			Ok(())
		})
	}

	/// Number of subtitle entries.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_count(
		params: CHandle,
		count: *mut i32,
	) -> i32 {
		if params.is_null() || count.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const SubtitleParams) };
			unsafe { *count = p.count(); }
			Ok(())
		})
	}

	/// Out-time of the last subtitle as a rational pair.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_duration(
		params: CHandle,
		numerator: *mut i32,
		denominator: *mut i32,
	) -> i32 {
		if params.is_null() || numerator.is_null() || denominator.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const SubtitleParams) };
			let (n, d) = p.duration();
			unsafe {
				*numerator = n;
				*denominator = d;
			}
			Ok(())
		})
	}

	/// Append a subtitle entry.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_add_subtitle(
		params: CHandle,
		in_num: i32,
		in_den: i32,
		out_num: i32,
		out_den: i32,
		text: *const super::c_char,
	) -> i32 {
		if params.is_null() || text.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut SubtitleParams) };
			p.add_subtitle(in_num, in_den, out_num, out_den, cstr(text))
		})
	}

	/// Remove all subtitle entries.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_clear(params: CHandle) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut SubtitleParams) };
			p.clear()
		})
	}

	/// Get the time range of the subtitle at `index`.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_get_subtitle(
		params: CHandle,
		index: i32,
		in_num: *mut i32,
		in_den: *mut i32,
		out_num: *mut i32,
		out_den: *mut i32,
	) -> i32 {
		if params.is_null() || in_num.is_null() || in_den.is_null() || out_num.is_null() || out_den.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const SubtitleParams) };
			let ((in_n, in_d), (out_n, out_d)) = p.get_subtitle(index)?;
			unsafe {
				*in_num = in_n;
				*in_den = in_d;
				*out_num = out_n;
				*out_den = out_d;
			}
			Ok(())
		})
	}

	/// Get the text of the subtitle at `index` (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_get_subtitle_text(
		params: CHandle,
		index: i32,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if params.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let p = match unsafe { get::<SubtitleParams>(&params) } {
			Some(p) => p,
			None => return OAKCOMMON_E_INVALID,
		};
		match p.get_subtitle_text(index) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Generate a default ASS header (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_generate_ass_header(
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		match SubtitleParams::generate_ass_header() {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Load subtitles from an XML fragment.
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_load_xml(
		params: CHandle,
		xml: *const super::c_char,
	) -> i32 {
		if params.is_null() || xml.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut SubtitleParams) };
			p.load_xml(cstr(xml))
		})
	}

	/// Save subtitles to an XML fragment (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_subtitleparams_save_xml(
		params: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if params.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let p = match unsafe { get::<SubtitleParams>(&params) } {
			Some(p) => p,
			None => return OAKCOMMON_E_INVALID,
		};
		match p.save_xml() {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}
}

/// Video parameter set.
pub mod videoparams {
	//! `include/common/videoparams.h`.
	//!
	//! The C++-only `oakcommon_videoparams_init_from_native` /
	//! `oakcommon_videoparams_get_native` deal with `olive::VideoParams`
	//! and are served by the C++ adapter layer.

	use super::{copy_string, cstr, is_valid_string_out, CHandle};
	use crate::error::OAKCOMMON_E_INVALID;
	use crate::handle::{guard, guard_handle, make_owned};
	use crate::ocioutils::PixelFormat;
	use crate::videoparams::{ColorRange, Interlacing, VideoParams, VideoType};

	/// Create a default (invalid) parameter set.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_init() -> CHandle {
		guard_handle(|| Ok(make_owned(VideoParams::new())))
	}

	/// Create a parameter set without a time base.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_init_basic(
		width: i32,
		height: i32,
		pixel_format: i32,
		nb_channels: i32,
		pixel_aspect_num: i32,
		pixel_aspect_den: i32,
		interlacing: i32,
		divider: i32,
	) -> CHandle {
		guard_handle(|| {
			Ok(make_owned(VideoParams::new_basic(
				width,
				height,
				PixelFormat::from_code(pixel_format),
				nb_channels,
				pixel_aspect_num,
				pixel_aspect_den,
				interlacing,
				divider,
			)))
		})
	}

	/// Create a parameter set with a time base.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_init_with_time_base(
		width: i32,
		height: i32,
		time_base_num: i32,
		time_base_den: i32,
		pixel_format: i32,
		nb_channels: i32,
		pixel_aspect_num: i32,
		pixel_aspect_den: i32,
		interlacing: i32,
		divider: i32,
	) -> CHandle {
		guard_handle(|| {
			Ok(make_owned(VideoParams::new_with_time_base(
				width,
				height,
				time_base_num,
				time_base_den,
				PixelFormat::from_code(pixel_format),
				nb_channels,
				pixel_aspect_num,
				pixel_aspect_den,
				interlacing,
				divider,
			)))
		})
	}

	/// Release one reference.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_free(params: *mut CHandle) {
		super::free_handle(params);
	}

	/// Get the width.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_width(
		params: CHandle,
		width: *mut i32,
	) -> i32 {
		if params.is_null() || width.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *width = p.width(); }
			Ok(())
		})
	}

	/// Set the width.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_width(params: CHandle, width: i32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_width(width);
			Ok(())
		})
	}

	/// Get the height.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_height(
		params: CHandle,
		height: *mut i32,
	) -> i32 {
		if params.is_null() || height.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *height = p.height(); }
			Ok(())
		})
	}

	/// Set the height.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_height(params: CHandle, height: i32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_height(height);
			Ok(())
		})
	}

	/// Get the depth.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_depth(
		params: CHandle,
		depth: *mut i32,
	) -> i32 {
		if params.is_null() || depth.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *depth = p.depth(); }
			Ok(())
		})
	}

	/// Set the depth.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_depth(params: CHandle, depth: i32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_depth(depth);
			Ok(())
		})
	}

	/// Get whether the frame is 3D.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_is_3d(
		params: CHandle,
		is_3d: *mut i32,
	) -> i32 {
		if params.is_null() || is_3d.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *is_3d = p.is_3d() as i32; }
			Ok(())
		})
	}


	/// Get the time base as a rational pair.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_time_base(
		params: CHandle,
		numerator: *mut i32,
		denominator: *mut i32,
	) -> i32 {
		if params.is_null() || numerator.is_null() || denominator.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			let (n, d) = p.time_base();
			unsafe {
				*numerator = n;
				*denominator = d;
			}
			Ok(())
		})
	}

	/// Set the time base.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_time_base(
		params: CHandle,
		numerator: i32,
		denominator: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_time_base(numerator, denominator);
			Ok(())
		})
	}

	/// Get the frame rate as a rational pair.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_frame_rate(
		params: CHandle,
		numerator: *mut i32,
		denominator: *mut i32,
	) -> i32 {
		if params.is_null() || numerator.is_null() || denominator.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			let (n, d) = p.frame_rate();
			unsafe {
				*numerator = n;
				*denominator = d;
			}
			Ok(())
		})
	}

	/// Set the frame rate.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_frame_rate(
		params: CHandle,
		numerator: i32,
		denominator: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_frame_rate(numerator, denominator);
			Ok(())
		})
	}

	/// Get the frame rate as a (flipped) time base.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_frame_rate_as_time_base(
		params: CHandle,
		numerator: *mut i32,
		denominator: *mut i32,
	) -> i32 {
		if params.is_null() || numerator.is_null() || denominator.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			let (n, d) = p.frame_rate_as_time_base();
			unsafe {
				*numerator = n;
				*denominator = d;
			}
			Ok(())
		})
	}

	/// Get the pixel aspect ratio as a rational pair.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_pixel_aspect_ratio(
		params: CHandle,
		numerator: *mut i32,
		denominator: *mut i32,
	) -> i32 {
		if params.is_null() || numerator.is_null() || denominator.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			let (n, d) = p.pixel_aspect_ratio();
			unsafe {
				*numerator = n;
				*denominator = d;
			}
			Ok(())
		})
	}

	/// Set the pixel aspect ratio.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_pixel_aspect_ratio(
		params: CHandle,
		numerator: i32,
		denominator: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_pixel_aspect_ratio(numerator, denominator);
			Ok(())
		})
	}

	/// Get the pixel format.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_format(
		params: CHandle,
		format: *mut i32,
	) -> i32 {
		if params.is_null() || format.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *format = p.format().code(); }
			Ok(())
		})
	}

	/// Set the pixel format.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_format(params: CHandle, format: i32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_format(PixelFormat::from_code(format));
			Ok(())
		})
	}

	/// Get the channel count.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_channel_count(
		params: CHandle,
		count: *mut i32,
	) -> i32 {
		if params.is_null() || count.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *count = p.channel_count(); }
			Ok(())
		})
	}

	/// Set the channel count.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_channel_count(params: CHandle, count: i32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_channel_count(count);
			Ok(())
		})
	}

	/// Get the interlacing mode.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_interlacing(
		params: CHandle,
		interlacing: *mut i32,
	) -> i32 {
		if params.is_null() || interlacing.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *interlacing = p.interlacing() as i32; }
			Ok(())
		})
	}

	/// Set the interlacing mode.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_interlacing(
		params: CHandle,
		interlacing: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_interlacing(match interlacing {
				1 => Interlacing::TopFirst,
				2 => Interlacing::BottomFirst,
				_ => Interlacing::None,
			});
			Ok(())
		})
	}

	/// Get the divider.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_divider(
		params: CHandle,
		divider: *mut i32,
	) -> i32 {
		if params.is_null() || divider.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *divider = p.divider(); }
			Ok(())
		})
	}

	/// Set the divider.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_divider(params: CHandle, divider: i32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_divider(divider);
			Ok(())
		})
	}

	/// Get whether the stream is enabled.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_enabled(
		params: CHandle,
		enabled: *mut i32,
	) -> i32 {
		if params.is_null() || enabled.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *enabled = p.enabled() as i32; }
			Ok(())
		})
	}

	/// Set whether the stream is enabled.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_enabled(params: CHandle, enabled: i32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_enabled(enabled != 0);
			Ok(())
		})
	}

	/// Get the X offset.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_x(params: CHandle, x: *mut f32) -> i32 {
		if params.is_null() || x.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *x = p.x(); }
			Ok(())
		})
	}

	/// Set the X offset.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_x(params: CHandle, x: f32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_x(x);
			Ok(())
		})
	}

	/// Get the Y offset.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_y(params: CHandle, y: *mut f32) -> i32 {
		if params.is_null() || y.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *y = p.y(); }
			Ok(())
		})
	}

	/// Set the Y offset.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_y(params: CHandle, y: f32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_y(y);
			Ok(())
		})
	}

	/// Get the stream index.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_stream_index(
		params: CHandle,
		index: *mut i32,
	) -> i32 {
		if params.is_null() || index.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *index = p.stream_index(); }
			Ok(())
		})
	}

	/// Set the stream index.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_stream_index(params: CHandle, index: i32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_stream_index(index);
			Ok(())
		})
	}

	/// Get the video type.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_video_type(
		params: CHandle,
		type_: *mut i32,
	) -> i32 {
		if params.is_null() || type_.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *type_ = p.video_type() as i32; }
			Ok(())
		})
	}

	/// Set the video type.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_video_type(params: CHandle, type_: i32) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_video_type(match type_ {
				1 => VideoType::Still,
				2 => VideoType::ImageSequence,
				_ => VideoType::Video,
			});
			Ok(())
		})
	}

	/// Get the start time.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_start_time(
		params: CHandle,
		start_time: *mut i64,
	) -> i32 {
		if params.is_null() || start_time.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *start_time = p.start_time(); }
			Ok(())
		})
	}

	/// Set the start time.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_start_time(params: CHandle, start_time: i64) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_start_time(start_time);
			Ok(())
		})
	}

	/// Get the duration.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_duration(
		params: CHandle,
		duration: *mut i64,
	) -> i32 {
		if params.is_null() || duration.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *duration = p.duration(); }
			Ok(())
		})
	}

	/// Set the duration.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_duration(params: CHandle, duration: i64) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_duration(duration);
			Ok(())
		})
	}

	/// Get whether alpha is premultiplied.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_premultiplied_alpha(
		params: CHandle,
		premultiplied: *mut i32,
	) -> i32 {
		if params.is_null() || premultiplied.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *premultiplied = p.premultiplied_alpha() as i32; }
			Ok(())
		})
	}

	/// Set whether alpha is premultiplied.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_premultiplied_alpha(
		params: CHandle,
		premultiplied: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_premultiplied_alpha(premultiplied != 0);
			Ok(())
		})
	}

	/// Get the color range.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_color_range(
		params: CHandle,
		color_range: *mut i32,
	) -> i32 {
		if params.is_null() || color_range.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *color_range = p.color_range() as i32; }
			Ok(())
		})
	}

	/// Set the color range.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_color_range(
		params: CHandle,
		color_range: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_color_range(match color_range {
				1 => ColorRange::Full,
				_ => ColorRange::Limited,
			});
			Ok(())
		})
	}

	/// Get the color primaries.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_color_primaries(
		params: CHandle,
		primaries: *mut i32,
	) -> i32 {
		if params.is_null() || primaries.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *primaries = p.color_primaries(); }
			Ok(())
		})
	}

	/// Set the color primaries.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_color_primaries(
		params: CHandle,
		primaries: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_color_primaries(primaries);
			Ok(())
		})
	}

	/// Get the color transfer function.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_color_transfer(
		params: CHandle,
		transfer: *mut i32,
	) -> i32 {
		if params.is_null() || transfer.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *transfer = p.color_transfer(); }
			Ok(())
		})
	}

	/// Set the color transfer function.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_color_transfer(
		params: CHandle,
		transfer: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_color_transfer(transfer);
			Ok(())
		})
	}

	/// Get the colorspace name (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_colorspace(
		params: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if params.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let p = unsafe { &*(params.ctx as *const VideoParams) };
		copy_string(p.colorspace(), buf, buf_size)
	}

	/// Set the colorspace name.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_set_colorspace(
		params: CHandle,
		colorspace: *const super::c_char,
	) -> i32 {
		if params.is_null() || colorspace.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.set_colorspace(cstr(colorspace));
			Ok(())
		})
	}

	/// Get the square-pixel width.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_square_pixel_width(
		params: CHandle,
		width: *mut i32,
	) -> i32 {
		if params.is_null() || width.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *width = p.square_pixel_width(); }
			Ok(())
		})
	}

	/// Get the effective width.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_effective_width(
		params: CHandle,
		width: *mut i32,
	) -> i32 {
		if params.is_null() || width.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *width = p.effective_width(); }
			Ok(())
		})
	}

	/// Get the effective height.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_effective_height(
		params: CHandle,
		height: *mut i32,
	) -> i32 {
		if params.is_null() || height.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *height = p.effective_height(); }
			Ok(())
		})
	}

	/// Get the effective depth.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_effective_depth(
		params: CHandle,
		depth: *mut i32,
	) -> i32 {
		if params.is_null() || depth.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *depth = p.effective_depth(); }
			Ok(())
		})
	}

	/// Get whether the set describes a valid stream.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_is_valid(
		params: CHandle,
		is_valid: *mut i32,
	) -> i32 {
		if params.is_null() || is_valid.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *is_valid = p.is_valid() as i32; }
			Ok(())
		})
	}

	/// Get bytes per channel.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_bytes_per_channel(
		params: CHandle,
		bytes: *mut i32,
	) -> i32 {
		if params.is_null() || bytes.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *bytes = p.bytes_per_channel(); }
			Ok(())
		})
	}

	/// Get bytes per pixel.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_bytes_per_pixel(
		params: CHandle,
		bytes: *mut i32,
	) -> i32 {
		if params.is_null() || bytes.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *bytes = p.bytes_per_pixel(); }
			Ok(())
		})
	}

	/// Get the total buffer size.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_buffer_size(
		params: CHandle,
		size: *mut i32,
	) -> i32 {
		if params.is_null() || size.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			unsafe { *size = p.buffer_size(); }
			Ok(())
		})
	}

	/// Convert a time (in seconds) to time-base units.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_time_in_timebase_units(
		params: CHandle,
		time_num: i32,
		time_den: i32,
		timestamp: *mut i64,
	) -> i32 {
		if params.is_null() || timestamp.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			// CPP-PARITY: C++ returns INT64_MIN (AV_NOPTS_VALUE) when no time
			// base is set; the Rust domain returns None in that case.
			match p.time_in_timebase_units(time_num, time_den) {
				Some(ts) => unsafe { *timestamp = ts; },
				None => unsafe { *timestamp = i64::MIN; },
			}
			Ok(())
		})
	}

	/// Compare two parameter sets for equality.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_equals(
		params: CHandle,
		other: CHandle,
		equal: *mut i32,
	) -> i32 {
		if params.is_null() || other.is_null() || equal.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &*(params.ctx as *const VideoParams) };
			let o = unsafe { &*(other.ctx as *const VideoParams) };
			unsafe { *equal = p.equals(o) as i32; }
			Ok(())
		})
	}

	/// Load parameters from an XML fragment.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_load_xml(
		params: CHandle,
		xml: *const super::c_char,
	) -> i32 {
		if params.is_null() || xml.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let p = unsafe { &mut *(params.ctx as *mut VideoParams) };
			p.load_xml(cstr(xml))
		})
	}

	/// Save parameters to an XML fragment (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_save_xml(
		params: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if params.is_null() || !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		let p = unsafe { &*(params.ctx as *const VideoParams) };
		match p.save_xml() {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Static: bytes per channel for a format.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_bytes_per_channel_for_format(
		pixel_format: i32,
	) -> i32 {
		VideoParams::bytes_per_channel_for_format(PixelFormat::from_code(pixel_format))
	}

	/// Static: bytes per pixel for a format + channel count.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_bytes_per_pixel_for_format(
		pixel_format: i32,
		channels: i32,
	) -> i32 {
		VideoParams::bytes_per_pixel_for_format(PixelFormat::from_code(pixel_format), channels)
	}

	/// Static: total buffer size.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_calculate_buffer_size(
		width: i32,
		height: i32,
		pixel_format: i32,
		channels: i32,
	) -> i32 {
		VideoParams::calculate_buffer_size(width, height, PixelFormat::from_code(pixel_format), channels)
	}

	/// Static: whether the format is float.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_format_is_float(pixel_format: i32) -> i32 {
		VideoParams::format_is_float(PixelFormat::from_code(pixel_format)) as i32
	}

	/// Static: auto divider for the given dimensions.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_generate_auto_divider(
		width: i64,
		height: i64,
	) -> i32 {
		VideoParams::generate_auto_divider(width, height)
	}

	/// Static: scale a dimension by a divider.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_scaled_dimension(
		dimension: i32,
		divider: i32,
	) -> i32 {
		VideoParams::get_scaled_dimension(dimension, divider)
	}

	/// Static: divider for a target resolution.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_divider_for_target_resolution(
		src_width: i32,
		src_height: i32,
		dst_width: i32,
		dst_height: i32,
	) -> i32 {
		VideoParams::get_divider_for_target_resolution(src_width, src_height, dst_width, dst_height)
	}

	/// Static: name for a divider (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_name_for_divider(
		divider: i32,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		match VideoParams::name_for_divider(divider) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Static: name for a pixel format (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_get_format_name(
		pixel_format: i32,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		match VideoParams::format_name(PixelFormat::from_code(pixel_format)) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Static: frame-rate string (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_frame_rate_to_string(
		numerator: i32,
		denominator: i32,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if !is_valid_string_out(buf, buf_size) {
			return OAKCOMMON_E_INVALID;
		}
		match VideoParams::frame_rate_to_string(numerator, denominator) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Static: bytes per channel for an OakPixelFormat.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_static_get_bytes_per_channel(
		format: i32,
	) -> i32 {
		VideoParams::bytes_per_channel_for_format(PixelFormat::from_code(format))
	}

	/// Static: bytes per pixel for an OakPixelFormat + channels.
	#[no_mangle]
	pub extern "C" fn oakcommon_videoparams_static_get_bytes_per_pixel(
		format: i32,
		channels: i32,
	) -> i32 {
		VideoParams::bytes_per_pixel_for_format(PixelFormat::from_code(format), channels)
	}
}

/// Streaming XML reader/writer.
pub mod xmlutils {
	//! `include/common/xmlutils.h`.
	//!
	//! The C++-only `get_native` / `wrap_native` entry points deal with
	//! `olive::XmlStreamReader` / `olive::XmlStreamWriter` and are served
	//! by the C++ adapter layer.

	use super::{copy_string, cstr, free_handle, CHandle};
	use crate::error::OAKCOMMON_E_INVALID;
	use crate::handle::{get, get_mut, guard, guard_handle, make_owned};
	use crate::xmlutils::{XmlReader, XmlWriter};

	/// Reader state boxed behind the handle's `ctx`, mirroring the C++
	/// `XmlReaderState` in `c_api/xmlutils.cpp`: the reader plus a cache for
	/// `read_element_text()`. The domain `XmlReader::read_element_text`
	/// consumes the stream on every call, so the C++ two-stage buffer
	/// convention (size query then copy) would otherwise invoke it twice and
	/// lose the text — the cache preserves the first result for the copy.
	struct ReaderState {
		reader: XmlReader,
		cached_text: String,
		has_cached_text: bool,
	}

	impl ReaderState {
		fn new(data: &str) -> Self {
			Self {
				// `XmlReader::new` always succeeds; `has_error` reports parse
				// failures (CPP-PARITY: the C++ constructor never throws).
				reader: XmlReader::new(data).unwrap_or_else(|_| XmlReader::new("").unwrap()),
				cached_text: String::new(),
				has_cached_text: false,
			}
		}
	}

	/// Create a reader over a complete document.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_reader_init(data: *const super::c_char) -> CHandle {
		if data.is_null() {
			return CHandle::null();
		}
		guard_handle(|| Ok(make_owned(ReaderState::new(cstr(data)))))
	}

	/// Release one reference to a reader.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_reader_free(reader: *mut CHandle) {
		free_handle(reader);
	}

	/// Advance to the next start/end element; writes whether a start
	/// element was found.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_reader_read_next_start_element(
		reader: CHandle,
		found: *mut i32,
	) -> i32 {
		if reader.is_null() || found.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let st = unsafe { get_mut::<ReaderState>(&reader) }.expect("reader handle validated non-null");
			st.has_cached_text = false;
			st.reader.read_next_start_element().map(|b| {
				unsafe { *found = b as i32; }
			})
		})
	}

	/// Name of the current element token (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_reader_name(
		reader: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if reader.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let st = match unsafe { get::<ReaderState>(&reader) } {
			Some(st) => st,
			None => return OAKCOMMON_E_INVALID,
		};
		match st.reader.name() {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Read the character data of the current element (two-stage string
	/// getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_reader_read_element_text(
		reader: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if reader.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let st = match unsafe { get_mut::<ReaderState>(&reader) } {
			Some(st) => st,
			None => return OAKCOMMON_E_INVALID,
		};
		// CPP-PARITY: `read_element_text()` consumes the stream, so cache the
		// result to keep the two-stage (size query then copy) convention
		// working across repeated calls.
		if !st.has_cached_text {
			match st.reader.read_element_text() {
				Ok(s) => {
					st.cached_text = s;
					st.has_cached_text = true;
				}
				Err(e) => return e.code(),
			}
		}
		copy_string(&st.cached_text, buf, buf_size)
	}

	/// Skip the current element and its children.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_reader_skip_current_element(reader: CHandle) -> i32 {
		if reader.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let st = unsafe { get_mut::<ReaderState>(&reader) }.expect("reader handle validated non-null");
			st.has_cached_text = false;
			st.reader.skip_current_element()
		})
	}

	/// Number of attributes on the current start element.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_reader_attribute_count(
		reader: CHandle,
		count: *mut i32,
	) -> i32 {
		if reader.is_null() || count.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let st = unsafe { get::<ReaderState>(&reader) }.expect("reader handle validated non-null");
			st.reader.attribute_count().map(|c| {
				unsafe { *count = c; }
			})
		})
	}

	/// Name of the attribute at `index` (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_reader_attribute_name(
		reader: CHandle,
		index: i32,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if reader.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let st = match unsafe { get::<ReaderState>(&reader) } {
			Some(st) => st,
			None => return OAKCOMMON_E_INVALID,
		};
		match st.reader.attribute_name(index) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Value of the attribute at `index` (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_reader_attribute_value(
		reader: CHandle,
		index: i32,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if reader.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let st = match unsafe { get::<ReaderState>(&reader) } {
			Some(st) => st,
			None => return OAKCOMMON_E_INVALID,
		};
		match st.reader.attribute_value(index) {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Whether the document failed to parse.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_reader_has_error(
		reader: CHandle,
		has_error: *mut i32,
	) -> i32 {
		if reader.is_null() || has_error.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let st = unsafe { get::<ReaderState>(&reader) }.expect("reader handle validated non-null");
			st.reader.has_error().map(|b| {
				unsafe { *has_error = b as i32; }
			})
		})
	}

	/// Create a writer.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_writer_init() -> CHandle {
		guard_handle(|| Ok(make_owned(XmlWriter::new())))
	}

	/// Release one reference to a writer.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_writer_free(writer: *mut CHandle) {
		free_handle(writer);
	}

	/// Write a start element.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_writer_write_start_element(
		writer: CHandle,
		name: *const super::c_char,
	) -> i32 {
		if writer.is_null() || name.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let w = unsafe { get_mut::<XmlWriter>(&writer) }.expect("writer handle validated non-null");
			w.write_start_element(cstr(name))
		})
	}

	/// Write an attribute.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_writer_write_attribute(
		writer: CHandle,
		name: *const super::c_char,
		value: *const super::c_char,
	) -> i32 {
		if writer.is_null() || name.is_null() || value.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let w = unsafe { get_mut::<XmlWriter>(&writer) }.expect("writer handle validated non-null");
			w.write_attribute(cstr(name), cstr(value))
		})
	}

	/// Write character data.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_writer_write_characters(
		writer: CHandle,
		text: *const super::c_char,
	) -> i32 {
		if writer.is_null() || text.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let w = unsafe { get_mut::<XmlWriter>(&writer) }.expect("writer handle validated non-null");
			w.write_characters(cstr(text))
		})
	}

	/// Write an empty element with text.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_writer_write_text_element(
		writer: CHandle,
		name: *const super::c_char,
		text: *const super::c_char,
	) -> i32 {
		if writer.is_null() || name.is_null() || text.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let w = unsafe { get_mut::<XmlWriter>(&writer) }.expect("writer handle validated non-null");
			w.write_text_element(cstr(name), cstr(text))
		})
	}

	/// Write an end element.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_writer_write_end_element(writer: CHandle) -> i32 {
		if writer.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let w = unsafe { get_mut::<XmlWriter>(&writer) }.expect("writer handle validated non-null");
			w.write_end_element()
		})
	}

	/// Write the end of the document.
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_writer_write_end_document(writer: CHandle) -> i32 {
		if writer.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		guard(|| {
			let w = unsafe { get_mut::<XmlWriter>(&writer) }.expect("writer handle validated non-null");
			w.write_end_document()
		})
	}

	/// The document written so far (two-stage string getter).
	#[no_mangle]
	pub extern "C" fn oakcommon_xml_writer_output(
		writer: CHandle,
		buf: *mut super::c_char,
		buf_size: i32,
	) -> i32 {
		if writer.is_null() {
			return OAKCOMMON_E_INVALID;
		}
		let w = match unsafe { get::<XmlWriter>(&writer) } {
			Some(w) => w,
			None => return OAKCOMMON_E_INVALID,
		};
		match w.output() {
			Ok(s) => copy_string(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}
}

